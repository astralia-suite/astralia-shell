# Memory

Heap behaviour, allocator tuning and buffers that must not linger. Sources: `src/core/allocator.cpp`, `src/service/polkit_service.cpp`.

- Wipe password buffers with `explicit_bzero` after responding or cancelling. `std::string::clear` leaves the bytes in the heap.
- Never size an allocation from an on-disk header without checking the file size. A corrupt cache header made `new[]` throw on a worker.
- Cap glibc arenas and pin the `mmap` and trim thresholds. A decode burst otherwise inflates RSS for good.
- Call `malloc_trim(0)` after bursts like decodes or broad searches. glibc keeps freed small chunks, pinning several megabytes otherwise.
- Slow idle heap growth is glyph-cache warm-up plus fragmentation, not a leak. `heaptrack` showed live heap flat after three idle minutes.
