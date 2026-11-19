use std::cell::RefCell;
use std::future::Future;
use std::io;

use calloop::LoopHandle;
use calloop::futures::{Scheduler, executor};

thread_local! {
    static SCHEDULER: RefCell<Option<Scheduler<()>>> = const { RefCell::new(None) };
}

/// Runs spawned futures on this loop's thread.
pub fn attach<D: 'static>(handle: &LoopHandle<'static, D>) -> io::Result<()> {
    let (exec, scheduler) = executor::<()>()?;
    handle.insert_source(exec, |(), _, _| {}).map_err(|e| io::Error::other(e.error))?;
    SCHEDULER.with(|s| *s.borrow_mut() = Some(scheduler));
    Ok(())
}

pub fn spawn(fut: impl Future<Output = ()> + 'static) {
    SCHEDULER.with(|s| match &*s.borrow() {
        Some(scheduler) => {
            if let Err(e) = scheduler.schedule(fut) {
                ::log::error!("runtime: {e}");
            }
        }
        None => ::log::error!("runtime: spawn before attach, future dropped"),
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use calloop::EventLoop;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::time::Duration;

    #[test]
    fn runtime_runs_spawned_futures() {
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        attach(&event_loop.handle()).unwrap();
        let flag = Rc::new(Cell::new(false));
        let f = flag.clone();
        spawn(async move { f.set(true) });
        event_loop.dispatch(Duration::from_millis(100), &mut ()).unwrap();
        assert!(flag.get());
    }
}
