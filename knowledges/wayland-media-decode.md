# Wayland media and decode

Animated wallpapers, the `ffmpeg` media plugin and its frame cache. Sources: `src/backend/wayland/plugin/media_plugin.cpp`, `src/service/wayland/media_service.cpp`, `src/backend/wayland/render/video_texture.cpp`, `src/backend/wayland/render/wallpaper_column.cpp`.

## Frame cache

- [wayland] Include every decode-shaping parameter, such as `fit`, in the `.rgba` cache key. Otherwise old-shape frames are reused silently.
- [wayland] Decode frames into a `.tmp` directory and rename on success. A killed decode left a partial set that later runs trusted.
- [wayland] Remove the rename target first. `rename(2)` into a non-empty directory fails, and a stale `.png` cache blocked every promotion.
- [wayland] Prune a frame cache oldest-first and touch files on read. Use order then drives eviction.

## Hardware decode

- [wayland] Pick the hardware decoder from a preference list of `AVHWDeviceType`s. Try `CUDA`, then `VAAPI`, then software.
- [wayland] Zero-copy texture import is vendor-specific. `VAAPI` uses `DMA-BUF` and `EGLImage` on Mesa; `CUDA` interop is separate.
- [wayland] `AVCodecContext::get_format` takes a plain function pointer. Pass the wanted hardware format through `opaque`.
- [wayland] Build the decode filter graph from the first decoded frame. The hardware transfer format varies by driver.
- [wayland] Loop in-process video with a seek and flush. `av_seek_frame` plus `avcodec_flush_buffers` on EOF replaces the CLI loop flag.
- [wayland] A decoder can hold a frame back until the next packet or flush. Send a `nullptr` packet and drain before flushing.
- [wayland] Pace playback by each frame's `pts` against a wall-clock anchor. A fixed sleep per frame throttles decode, not display.
- [wayland] The zero-copy `VAAPI` path skips the filter graph. An `fps=` filter there changes nothing.
- [wayland] Scale a thumbnail decode inside the filter graph. `decode_first_frame` emits `scale=W:H,format=rgba`, so frames are KB, not MB.

## Threads and memory

- [wayland] Pin `thread_count = 1` for first-frame decodes. Frame threading buys nothing and multiplies arenas.
- [wayland] Avoid one detached decode thread per tile. About 35 concurrent decodes kept eight 64 MB arenas.

## Texture sync

- [wayland] A sync-from-config function uploading only non-empty paths must clear the texture on an empty path. Reset it and bump the generation.
- [wayland] Clearing a texture in memory does not repaint. Request a frame on both branches, or the old picture stays.
- [wayland] Capture a `std::weak_ptr<int>` lifetime token in decode closures. Per-monitor state can be freed on hotplug before the closure runs.
