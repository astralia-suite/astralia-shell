# Threads and lifetime

Worker threads, async results and change flags across services and modules. Sources: `src/core/deferred_call.cpp` and the module models under `src/modules/`.

## Worker threads

- Signal every worker's stop flag before joining any. Signalling and joining one at a time made shutdown block serially.
- Tear a module's background thread down in its destructor, not only its close path. A joinable thread's destruction calls `std::terminate`.
- A detached reader thread must own what it touches through a `shared_ptr`. The owner may be destroyed before the child exits.
- Never destroy the Wayland state at shutdown. Detached threads keep references into it and abort the process when it is freed.
- Expect threads you did not spawn. Mesa, PipeWire and Pango each start their own, so the process is multithreaded anyway.
- A mutex must cover the read side of a shared buffer. Ring-buffer reads outside the lock raced the PipeWire thread.
- A second writer to shared per-bin state should prompt an audit of every existing writer. One recomputed off-mutex and became a race.

## Async results

- Kill a cancelled background process instead of detaching it. Retyped searches otherwise pile up competing processes.
- Reusing a handle across restarts needs a generation counter. A cancelled worker can wake later with a stale result.
- Track "request in flight" with your own flag. The worker resets `pid` to `-1` the instant it marks the run done.
- Never score an async result against a live mutable field. Freeze the input into its own field at start.
- An async result polled on a throttle lands a throttle period late. Poll every tick while a request runs.

## State changes

- A timer- or signal-driven service must emit only on a real state diff. A once-a-second tick otherwise repainted every surface.
- Keep earliest-deadline logic out of list order. Entries with different timeouts need the timer to take the minimum deadline.
- Capture a value synchronously at the action site. Deferring to the next repaint left panels opening at position zero.
- Capture click coordinates with the click event itself. The live shared pointer position may belong to another monitor.
- Set a reactive `*_changed` flag on every path that changes the value. One mutator forgetting it breaks only that path.
- An optimistic local write can suppress the `*_changed` flag it should trigger. Raise the flag at the write.
