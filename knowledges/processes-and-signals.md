# Processes and signals

Spawning children, reaping them, and keeping one daemon alive and debuggable. Sources: `src/core/spawn.cpp`, `src/core/async_process.cpp`, `src/core/single_instance.cpp`, `src/core/daemon.cpp`, `src/core/log.cpp`, `src/core/poll_reactor.cpp`.

## Spawning

- Never allocate in a forked child before `exec`. `fork()` may copy a lock another thread held.
- Spawn with `posix_spawn`, not `fork`, on the reactor thread. glibc takes allocator locks other threads hold.
- Ignoring `SIGCHLD` and calling `waitpid` cannot coexist. Ignoring is process-wide and lets the kernel reap, breaking `waitpid` everywhere.
- Fire-and-forget processes double-fork instead of relying on global reaping. The grandchild reparents to init and is reaped there.
- Spawned children need an empty signal mask and default `SIGPIPE`. `exec` inherits the blocked and ignored signals of the shell.
- A blocking `waitpid` thread beats watching a child's pipe in the poll loop. `poll()` once missed readiness for tens of seconds.

## Single instance and crashes

- A connect-to-socket liveness probe is unreliable against a leftover socket file. Use a `flock`-guarded lock file instead.
- Take the single-instance `flock` before `daemonize()`. The child inherits the lock, so it survives the parent's exit.
- Ignore `SIGPIPE` at startup. A write to a closed peer otherwise terminates the process without a core.
- Install a `std::set_terminate` hook next to the crash handler. `daemonize()` sends `stderr` to `/dev/null`, hiding the exception text.
- A `backtrace_symbols_fd` trace maps to source only with `addr2line` against the exact crashed binary. A rebuild invalidates the offsets.
- The poll loop must `continue` on `EINTR` and log any other `poll()` error. Breaking on `EINTR` exited the whole process.
