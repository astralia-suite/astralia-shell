use std::cell::RefCell;

type Handler<A> = Box<dyn FnMut(&A)>;

/// A list of handlers called in connection order.
pub struct Signal<A: ?Sized = ()> {
    handlers: RefCell<Vec<Handler<A>>>,
}

impl<A: ?Sized> Default for Signal<A> {
    fn default() -> Self {
        Signal { handlers: RefCell::new(Vec::new()) }
    }
}

impl<A: ?Sized> Signal<A> {
    pub fn connect(&self, f: impl FnMut(&A) + 'static) {
        self.handlers.borrow_mut().push(Box::new(f));
    }

    /// Handlers are taken out while they run, so one may connect or emit again;
    /// a nested emit reaches only handlers connected during this one.
    pub fn emit(&self, arg: &A) {
        let mut running = std::mem::take(&mut *self.handlers.borrow_mut());
        for handler in &mut running {
            handler(arg);
        }
        let mut handlers = self.handlers.borrow_mut();
        running.append(&mut handlers);
        *handlers = running;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;

    #[test]
    fn signal_handlers_may_reenter() {
        let s: Rc<Signal<u32>> = Rc::default();
        let seen = Rc::new(Cell::new(0));
        let (s2, seen2) = (s.clone(), seen.clone());
        s.connect(move |&n| {
            seen2.set(seen2.get() + n);
            if n == 1 {
                s2.emit(&10);
            }
        });
        s.emit(&1);
        assert_eq!(seen.get(), 1); // the nested emit finds no handler out; it must not panic
        s.emit(&2);
        assert_eq!(seen.get(), 3);
    }

    #[test]
    fn signal_keeps_handlers_connected_during_emit() {
        let s: Rc<Signal> = Rc::default();
        let count = Rc::new(Cell::new(0));
        let (s2, c2) = (s.clone(), count.clone());
        s.connect(move |_| {
            let c3 = c2.clone();
            s2.connect(move |_| c3.set(c3.get() + 1));
        });
        s.emit(&());
        assert_eq!(count.get(), 0);
        s.emit(&());
        assert_eq!(count.get(), 1);
    }
}
