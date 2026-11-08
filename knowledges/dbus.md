# D-Bus

Rules for `sdbus` proxies, async replies and BlueZ on the reactor thread. Sources: `src/core/dbus.cpp`, `src/service/bluetooth_service.cpp` and every D-Bus-backed service under `src/service/`.

- Never read D-Bus properties synchronously on the reactor thread. Use `dbus_get_all_async`; a stalled BlueZ once froze the whole shell.
- No reactor-thread callback may make a blocking D-Bus call. A synchronous Bluetooth read froze the shell for 18 seconds.
- BlueZ refuses confirmation and passkey pairing when no agent is registered (`No agent available for request type 2`). `BluetoothService` registers one, and its replies must stay open until the user answers, cancels or BlueZ sends `Cancel`.
- Do not build textures or rasterize text from a D-Bus callback. Build lazily in the paint path instead.
- Hold an async reply as an `sdbus::Slot` member. Destroying the owner then cancels the reply instead of calling a dead `this`.
- Coalesce D-Bus-signal refreshes to one call in flight plus a repeat flag. A signal burst otherwise queues one full refresh each.
- Never destroy an sdbus proxy inside its own async reply callback. Prune per-device proxies from the signal-match handler instead.
- Hold one sdbus proxy per fixed object, but keep changing paths one-off. Caching per-reconnect paths grows without bound.
- A local proxy destroyed right after firing an async call corrupts the connection it borrowed. Cache one proxy per object path.
- `Bus` polls both the bus fd and its `eventFd`. Sync calls queue signals internally, and only `eventFd` wakes the loop.
