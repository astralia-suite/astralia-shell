use std::cell::Cell;
use std::ffi::{CStr, CString};
use std::fmt;
use std::fs::{File, OpenOptions};
use std::io;
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::rc::Rc;
use std::os::unix::ffi::OsStringExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use async_io::Async;
use futures_lite::AsyncReadExt;

/// Holds the locked lock file for the life of the process.
pub struct Instance {
    _file: Option<File>,
}

#[derive(Debug)]
pub struct AlreadyRunning;

impl fmt::Display for AlreadyRunning {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("already running")
    }
}

impl Instance {
    /// Any failure other than a held lock runs unguarded.
    pub fn acquire(path: &Path) -> Result<Instance, AlreadyRunning> {
        // std opens with O_CLOEXEC.
        let file = match OpenOptions::new().read(true).write(true).create(true).truncate(false).mode(0o600).open(path) {
            Ok(file) => file,
            Err(e) => {
                ::log::error!("open {}: {e}; running unguarded", path.display());
                return Ok(Instance { _file: None });
            }
        };
        // SAFETY: the fd is valid for the life of `file`.
        if unsafe { libc::flock(file.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } != 0 {
            let e = io::Error::last_os_error();
            if e.raw_os_error() == Some(libc::EWOULDBLOCK) {
                return Err(AlreadyRunning);
            }
            ::log::error!("flock {}: {e}; running unguarded", path.display());
        }
        Ok(Instance { _file: Some(file) })
    }
}

fn redirect(target: libc::c_int, path: &CStr, flags: libc::c_int) {
    // SAFETY: plain fd syscalls on owned descriptors.
    unsafe {
        let mut fd = libc::open(path.as_ptr(), flags | libc::O_CLOEXEC, 0o600);
        if fd < 0 {
            fd = libc::open(c"/dev/null".as_ptr(), libc::O_RDWR | libc::O_CLOEXEC);
        }
        if fd >= 0 {
            libc::dup2(fd, target);
            libc::close(fd);
        }
    }
}

/// Forks into the background. Call before the loop or any thread exists.
pub fn daemonize() {
    // SAFETY: single-threaded at this point, so fork is sound.
    match unsafe { libc::fork() } {
        pid if pid < 0 => {
            ::log::error!("fork: {}", io::Error::last_os_error());
            std::process::exit(1);
        }
        0 => {}
        _ => unsafe { libc::_exit(0) },
    }
    unsafe { libc::setsid() };
    if let Err(e) = std::env::set_current_dir("/") {
        ::log::error!("chdir /: {e}");
    }
    redirect(libc::STDIN_FILENO, c"/dev/null", libc::O_RDONLY);
    redirect(libc::STDOUT_FILENO, c"/dev/null", libc::O_WRONLY);
    redirect(libc::STDERR_FILENO, c"/dev/null", libc::O_WRONLY);
}

const EGL_VENDOR_VAR: &str = "__EGL_VENDOR_LIBRARY_FILENAMES";
const MESA_EGL_VENDOR: &str = "/usr/share/glvnd/egl_vendor.d/50_mesa.json";

/// Set when `prefer_mesa_egl` wrote `EGL_VENDOR_VAR`, so children get the user's environment.
static EGL_VENDOR_OVERRIDDEN: AtomicBool = AtomicBool::new(false);

/// `glvnd` loads every installed EGL vendor (~105 MB for NVIDIA); with the NVIDIA driver not
/// loaded only Mesa can render, so the others are skipped. Must run before any thread starts.
pub fn prefer_mesa_egl() -> bool {
    if std::env::var_os(EGL_VENDOR_VAR).is_some()
        || std::path::Path::new("/dev/nvidiactl").exists()
        || !std::path::Path::new(MESA_EGL_VENDOR).exists()
    {
        return false;
    }
    // SAFETY: called from `main` before any other thread exists.
    unsafe { std::env::set_var(EGL_VENDOR_VAR, MESA_EGL_VENDOR) };
    EGL_VENDOR_OVERRIDDEN.store(true, Ordering::Relaxed);
    true
}

/// The environment children receive: ours, minus the EGL vendor override `prefer_mesa_egl` added.
fn child_environ() -> Option<Vec<CString>> {
    if !EGL_VENDOR_OVERRIDDEN.load(Ordering::Relaxed) {
        return None;
    }
    let prefix = format!("{EGL_VENDOR_VAR}=");
    Some(
        std::env::vars_os()
            .filter_map(|(k, v)| {
                let mut entry = k.into_vec();
                entry.push(b'=');
                entry.extend(v.into_vec());
                (!entry.starts_with(prefix.as_bytes())).then(|| CString::new(entry).ok()).flatten()
            })
            .collect(),
    )
}

pub fn tune_allocator(max_arenas: i32) {
    // SAFETY: mallopt only adjusts glibc allocator parameters.
    unsafe {
        libc::mallopt(libc::M_ARENA_MAX, max_arenas);
        libc::mallopt(libc::M_MMAP_THRESHOLD, 1 << 20);
    }
}

enum Output {
    Inherit,
    Null,
    Fd(RawFd),
}

/// `posix_spawnp` with stdin on `stdin` (else `/dev/null`), an empty signal mask and default `SIGPIPE`;
/// `std::process` keeps the caller's mask, which on the loop thread blocks `SIGINT`/`SIGTERM`.
fn spawn_child(argv: &[&str], stdin: Option<RawFd>, stdout: Output, stderr: Output) -> io::Result<libc::pid_t> {
    let args = argv.iter().map(|a| CString::new(*a)).collect::<Result<Vec<_>, _>>().map_err(io::Error::other)?;
    if args.is_empty() {
        return Err(io::ErrorKind::InvalidInput.into());
    }
    let mut ptrs: Vec<*mut libc::c_char> = args.iter().map(|a| a.as_ptr().cast_mut()).collect();
    ptrs.push(std::ptr::null_mut());
    unsafe extern "C" {
        static environ: *const *mut libc::c_char;
    }
    let own_env = child_environ();
    let mut env_ptrs: Vec<*mut libc::c_char> = Vec::new();
    if let Some(env) = &own_env {
        env_ptrs = env.iter().map(|e| e.as_ptr().cast_mut()).collect();
        env_ptrs.push(std::ptr::null_mut());
    }
    // SAFETY: the attribute and action objects are initialised before use and destroyed after;
    // every pointer passed outlives the call.
    unsafe {
        let mut actions: libc::posix_spawn_file_actions_t = std::mem::zeroed();
        libc::posix_spawn_file_actions_init(&mut actions);
        match stdin {
            Some(fd) => {
                libc::posix_spawn_file_actions_adddup2(&mut actions, fd, libc::STDIN_FILENO);
            }
            None => {
                libc::posix_spawn_file_actions_addopen(&mut actions, libc::STDIN_FILENO, c"/dev/null".as_ptr(), libc::O_RDONLY, 0);
            }
        }
        for (target, out) in [(libc::STDOUT_FILENO, &stdout), (libc::STDERR_FILENO, &stderr)] {
            match out {
                Output::Inherit => {}
                Output::Null => {
                    libc::posix_spawn_file_actions_addopen(&mut actions, target, c"/dev/null".as_ptr(), libc::O_WRONLY, 0);
                }
                Output::Fd(fd) => {
                    libc::posix_spawn_file_actions_adddup2(&mut actions, *fd, target);
                }
            }
        }
        let mut attr: libc::posix_spawnattr_t = std::mem::zeroed();
        libc::posix_spawnattr_init(&mut attr);
        let mut none: libc::sigset_t = std::mem::zeroed();
        libc::sigemptyset(&mut none);
        let mut defaults: libc::sigset_t = std::mem::zeroed();
        libc::sigemptyset(&mut defaults);
        libc::sigaddset(&mut defaults, libc::SIGPIPE);
        libc::posix_spawnattr_setsigmask(&mut attr, &none);
        libc::posix_spawnattr_setsigdefault(&mut attr, &defaults);
        libc::posix_spawnattr_setflags(&mut attr, (libc::POSIX_SPAWN_SETSIGMASK | libc::POSIX_SPAWN_SETSIGDEF) as libc::c_short);
        let mut pid = -1;
        let envp = if own_env.is_some() { env_ptrs.as_ptr() } else { environ };
        let result = libc::posix_spawnp(&mut pid, ptrs[0], &actions, &attr, ptrs.as_ptr(), envp);
        libc::posix_spawn_file_actions_destroy(&mut actions);
        libc::posix_spawnattr_destroy(&mut attr);
        // Arguments can carry a password; wipe our copies.
        for arg in args {
            arg.into_bytes_with_nul().fill(0);
        }
        if result != 0 {
            return Err(io::Error::from_raw_os_error(result));
        }
        Ok(pid)
    }
}

/// The child's pid until it is reaped; `-1` after, so a kill never reaches a reused pid.
type Pid = Arc<Mutex<libc::pid_t>>;

fn kill_child(pid: &Pid) {
    let pid = pid.lock().unwrap_or_else(|e| e.into_inner());
    if *pid > 0 {
        // SAFETY: the pid is unreaped (see `reap`), so it still names our child.
        unsafe { libc::kill(*pid, libc::SIGKILL) };
    }
}

/// Waits off the loop thread: the exit is observed without reaping, the pid retired under the
/// lock, and only then reaped.
async fn reap(pid: Pid) {
    blocking::unblock(move || {
        let raw = *pid.lock().unwrap_or_else(|e| e.into_inner());
        // SAFETY: plain wait syscalls on our own child.
        unsafe {
            let mut info: libc::siginfo_t = std::mem::zeroed();
            while libc::waitid(libc::P_PID, raw as libc::id_t, &mut info, libc::WEXITED | libc::WNOWAIT) < 0
                && io::Error::last_os_error().kind() == io::ErrorKind::Interrupted
            {}
            *pid.lock().unwrap_or_else(|e| e.into_inner()) = -1;
            while libc::waitpid(raw, std::ptr::null_mut(), 0) < 0 && io::Error::last_os_error().kind() == io::ErrorKind::Interrupted {}
        }
    })
    .await
}

/// One child at a time; starting again kills the previous run.
#[derive(Default)]
pub struct AsyncProcess {
    generation: Rc<Cell<u64>>,
    pid: Option<Pid>,
    running: Rc<Cell<bool>>,
}

impl AsyncProcess {
    /// Resolves `None` when this run is cancelled or fails to start, else its stdout
    /// (with stderr when `merge_stderr`).
    pub fn start(&mut self, argv: &[&str], merge_stderr: bool) -> impl Future<Output = Option<String>> + 'static {
        self.cancel();
        let spawned = self.spawn(argv, merge_stderr);
        let generation = self.generation.get();
        let (current, running) = (self.generation.clone(), self.running.clone());
        async move {
            let (mut reader, pid) = spawned?;
            let mut output = Vec::new();
            let _ = reader.read_to_end(&mut output).await;
            reap(pid).await;
            if current.get() != generation {
                return None;
            }
            running.set(false);
            Some(String::from_utf8_lossy(&output).into_owned())
        }
    }

    fn spawn(&mut self, argv: &[&str], merge_stderr: bool) -> Option<(Async<io::PipeReader>, Pid)> {
        let (reader, writer) = io::pipe().inspect_err(|e| ::log::error!("async_process: pipe: {e}")).ok()?;
        let fd = writer.as_raw_fd();
        let stderr = if merge_stderr { Output::Fd(fd) } else { Output::Null };
        let pid = spawn_child(argv, None, Output::Fd(fd), stderr)
            .inspect_err(|e| ::log::error!("async_process: cannot run {}: {e}", argv.first().unwrap_or(&"")))
            .ok()?;
        let pid = Arc::new(Mutex::new(pid));
        // Our write end closes here, so the read sees EOF when the child exits.
        drop(writer);
        let reader = match Async::new(reader) {
            Ok(reader) => reader,
            Err(e) => {
                ::log::error!("async_process: {e}");
                kill_child(&pid);
                // SAFETY: reaps the child just killed; nothing else waits for it.
                unsafe { libc::waitpid(*pid.lock().unwrap_or_else(|e| e.into_inner()), std::ptr::null_mut(), 0) };
                return None;
            }
        };
        self.pid = Some(pid.clone());
        self.running.set(true);
        Some((reader, pid))
    }

    pub fn cancel(&mut self) {
        if let Some(pid) = self.pid.take() {
            kill_child(&pid);
        }
        self.generation.set(self.generation.get() + 1);
        self.running.set(false);
    }

    pub fn running(&self) -> bool {
        self.running.get()
    }
}

impl Drop for AsyncProcess {
    fn drop(&mut self) {
        self.cancel();
    }
}

/// Runs `command` through `sh` in the background; it reparents to init, so nothing is left to reap.
pub fn spawn_detached(command: &str) {
    ::log::info!("spawn: {command}");
    match spawn_child(&["/bin/sh", "-c", "eval \"$1\" &", "sh", command], None, Output::Inherit, Output::Inherit) {
        // SAFETY: waits for the `sh` just spawned, which exits once the job is backgrounded.
        Ok(pid) => unsafe {
            while libc::waitpid(pid, std::ptr::null_mut(), 0) < 0 && io::Error::last_os_error().kind() == io::ErrorKind::Interrupted {}
        },
        Err(e) => ::log::error!("spawn: {e}"),
    }
}

/// Spawns `argv` with stdin and stdout on pipes and stderr inherited; returns the pid, the
/// stdin writer and the stdout reader. The caller reaps the pid.
pub fn spawn_piped(argv: &[&str]) -> io::Result<(libc::pid_t, io::PipeWriter, io::PipeReader)> {
    let (child_in, stdin) = io::pipe()?;
    let (stdout, child_out) = io::pipe()?;
    let pid = spawn_child(argv, Some(child_in.as_raw_fd()), Output::Fd(child_out.as_raw_fd()), Output::Inherit)?;
    Ok((pid, stdin, stdout))
}

pub fn trim_heap() {
    // SAFETY: malloc_trim only releases free heap pages.
    unsafe { libc::malloc_trim(0) };
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures_lite::future::block_on;
    use std::sync::Mutex;

    // Process tests count pipes and children, so they run one at a time.
    static SERIAL: Mutex<()> = Mutex::new(());

    fn open_pipes() -> usize {
        std::fs::read_dir("/proc/self/fd")
            .unwrap()
            .filter_map(|e| std::fs::read_link(e.ok()?.path()).ok())
            .filter(|t| t.to_string_lossy().starts_with("pipe:"))
            .count()
    }

    fn child_cmdlines() -> Vec<String> {
        let mut out = Vec::new();
        for task in std::fs::read_dir("/proc/self/task").unwrap().flatten() {
            let children = std::fs::read_to_string(task.path().join("children")).unwrap_or_default();
            for pid in children.split_whitespace() {
                out.push(std::fs::read_to_string(format!("/proc/{pid}/cmdline")).unwrap_or_default().replace('\0', " "));
            }
        }
        out
    }

    #[test]
    fn async_process_cases() {
        let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
        let mut proc = AsyncProcess::default();
        let run = proc.start(&["echo", "hello"], false);
        assert!(proc.running(), "a started process is running");
        assert_eq!(block_on(run).as_deref(), Some("hello\n"), "output is delivered");
        assert!(!proc.running(), "a finished process is not running");

        assert_eq!(block_on(proc.start(&[], false)), None, "an empty command is rejected");
        assert!(!proc.running());
        assert_eq!(block_on(proc.start(&["definitely-not-a-real-command"], false)), None, "an unknown command is rejected");
        assert!(!proc.running());

        let sleeper = proc.start(&["sleep", "5"], false);
        proc.cancel();
        assert!(!proc.running(), "cancel clears running");
        assert_eq!(block_on(sleeper), None, "a cancelled process never reports");

        for _ in 0..10 {
            let stale = proc.start(&["sleep", "1"], false);
            let fresh = proc.start(&["echo", "fresh"], false);
            assert_eq!(block_on(fresh).as_deref(), Some("fresh\n"), "only the newest job reports");
            assert_eq!(block_on(stale), None, "restart cancels the previous job");
        }

        block_on(proc.start(&["echo", "warmup"], false));
        let before = open_pipes();
        for _ in 0..30 {
            assert!(block_on(proc.start(&["echo", "x"], false)).is_some(), "repeat job finishes");
        }
        // Other tests open pipes concurrently for a moment; a leak from these jobs never goes away.
        let settled = (0..50).any(|_| open_pipes() <= before + 2 || {
            std::thread::sleep(std::time::Duration::from_millis(20));
            false
        });
        assert!(settled, "repeated jobs do not leak descriptors");

        let merged = block_on(proc.start(&["sh", "-c", "echo out; echo err >&2"], true)).unwrap();
        assert!(merged.contains("out") && merged.contains("err"), "{merged}");
        let split = block_on(proc.start(&["sh", "-c", "echo out; echo err >&2"], false)).unwrap();
        assert_eq!(split, "out\n");
    }

    #[test]
    fn child_signal_mask_is_empty() {
        let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
        // calloop blocks SIGINT/SIGTERM on the loop thread; children must not inherit that.
        let mut set: libc::sigset_t = unsafe { std::mem::zeroed() };
        unsafe {
            libc::sigemptyset(&mut set);
            libc::sigaddset(&mut set, libc::SIGTERM);
            libc::pthread_sigmask(libc::SIG_BLOCK, &set, std::ptr::null_mut())
        };
        let out = block_on(AsyncProcess::default().start(&["grep", "SigBlk", "/proc/self/status"], false)).unwrap();
        unsafe { libc::pthread_sigmask(libc::SIG_UNBLOCK, &set, std::ptr::null_mut()) };
        assert!(out.trim_end().ends_with("0000000000000000"), "{out}");
    }

    #[test]
    fn spawn_detached_reparents() {
        let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
        let dir = tempfile::tempdir().unwrap();
        let marker = dir.path().join("spawned");
        let mut set: libc::sigset_t = unsafe { std::mem::zeroed() };
        unsafe {
            libc::sigemptyset(&mut set);
            libc::sigaddset(&mut set, libc::SIGTERM);
            libc::pthread_sigmask(libc::SIG_BLOCK, &set, std::ptr::null_mut())
        };
        spawn_detached(&format!("grep SigBlk /proc/self/status > {}.tmp; mv {0}.tmp {0}", marker.display()));
        unsafe { libc::pthread_sigmask(libc::SIG_UNBLOCK, &set, std::ptr::null_mut()) };
        let created = (0..50).any(|_| {
            std::thread::sleep(std::time::Duration::from_millis(20));
            marker.exists()
        });
        assert!(created, "spawn_detached runs the command");
        let mask = std::fs::read_to_string(&marker).unwrap();
        assert!(mask.trim_end().ends_with("0000000000000000"), "{mask}");
        let tag = format!("sleep 3.{}", std::process::id());
        for _ in 0..5 {
            spawn_detached(&tag);
        }
        assert!(!child_cmdlines().iter().any(|c| c.contains(&tag) || c.starts_with("sh ")), "spawn_detached leaves no children to reap");
    }

    #[test]
    fn second_acquire_refused() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("x.lock");
        let first = Instance::acquire(&path).expect("first lock");
        assert!(Instance::acquire(&path).is_err());
        drop(first);
        // A child spawned on another test thread holds a copy of the descriptor until it execs.
        let reacquired = (0..50).any(|_| Instance::acquire(&path).is_ok() || {
            std::thread::sleep(std::time::Duration::from_millis(2));
            false
        });
        assert!(reacquired);
    }
}
