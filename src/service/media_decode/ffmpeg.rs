//! FFmpeg decode: hardware (VAAPI, then CUDA) when available, zero-copy DRM PRIME frames for VAAPI, the CPU
//! path through a filter graph otherwise.

use std::collections::HashMap;
use std::ffi::{CStr, CString, c_int, c_void};
use std::os::fd::{FromRawFd, OwnedFd};
use std::ptr::{null, null_mut};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use ffmpeg_sys_next as ff;

use super::{MediaDecodeStatus, MediaFrame};

const PREFERRED_HW_TYPES: [ff::AVHWDeviceType; 2] = [ff::AVHWDeviceType::AV_HWDEVICE_TYPE_VAAPI, ff::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA];
const PAUSE_POLL: Duration = Duration::from_millis(50);
const PACE_STEP: Duration = Duration::from_millis(20);

const fn fourcc(a: u8, b: u8, c: u8, d: u8) -> u32 {
    a as u32 | (b as u32) << 8 | (c as u32) << 16 | (d as u32) << 24
}
const DRM_FORMAT_NV12: u32 = fourcc(b'N', b'V', b'1', b'2');
const DRM_FORMAT_R8: u32 = fourcc(b'R', b'8', b' ', b' ');
const DRM_FORMAT_GR88: u32 = fourcc(b'G', b'R', b'8', b'8');
const DRM_FORMAT_RG88: u32 = fourcc(b'R', b'G', b'8', b'8');

/// A decoded RGBA frame; `stride_px` is the row length in pixels, 0 when rows are packed.
pub struct RgbaFrame {
    pub rgba: Vec<u8>,
    pub width: i32,
    pub height: i32,
    pub stride_px: i32,
}

/// One plane of a DRM PRIME surface; the fd is shared with the decoder's export cache.
#[derive(Clone)]
pub struct MediaDrmPlane {
    pub fd: Arc<OwnedFd>,
    pub modifier: u64,
    pub offset: i32,
    pub pitch: i32,
}

/// The decoder's hold on a hardware frame, released on drop.
struct HeldFrame(*mut ff::AVFrame);

// SAFETY: an `AVFrame` reference may be released from any thread.
unsafe impl Send for HeldFrame {}

impl Drop for HeldFrame {
    fn drop(&mut self) {
        // SAFETY: the frame came from `av_frame_clone` and is freed once.
        unsafe { ff::av_frame_free(&mut self.0) };
    }
}

/// An NV12 VAAPI surface as two DRM PRIME planes, pinned until dropped.
pub struct MediaDrmFrame {
    pub planes: [MediaDrmPlane; 2],
    pub width: i32,
    pub height: i32,
    pub surface_id: u64,
    _held: HeldFrame,
}

type FrameSink = Box<dyn FnMut(RgbaFrame) + Send>;
type DrmSink = Box<dyn FnMut(MediaDrmFrame) + Send>;

fn status_from(v: u8) -> MediaDecodeStatus {
    match v {
        1 => MediaDecodeStatus::ZeroCopy,
        2 => MediaDecodeStatus::CpuFallback,
        _ => MediaDecodeStatus::Idle,
    }
}

fn status_to(s: MediaDecodeStatus) -> u8 {
    match s {
        MediaDecodeStatus::Idle => 0,
        MediaDecodeStatus::ZeroCopy => 1,
        MediaDecodeStatus::CpuFallback => 2,
    }
}

#[derive(Default)]
struct Flags {
    stop: AtomicBool,
    pause: AtomicBool,
    status: AtomicU8,
    egl_import_failed: AtomicBool,
}

/// A decode worker; dropping it stops and joins the worker.
pub struct MediaDecodePlayback {
    worker: Option<JoinHandle<()>>,
    flags: Arc<Flags>,
}

impl MediaDecodePlayback {
    pub fn stop(&mut self) {
        self.flags.stop.store(true, Ordering::Relaxed);
        if let Some(worker) = self.worker.take()
            && worker.join().is_err()
        {
            ::log::error!("media_decode: decode worker panicked");
        }
    }

    /// Stops the worker and joins it on a thread of its own, so the caller never waits.
    pub fn stop_detached(mut self) {
        self.flags.stop.store(true, Ordering::Relaxed);
        if let Some(worker) = self.worker.take() {
            let joined = std::thread::Builder::new().name("astralia-shell-media-join".into()).spawn(move || {
                let _ = worker.join();
            });
            if let Err(e) = joined {
                ::log::error!("media_decode: cannot join the decode worker off the loop: {e}");
            }
        }
    }

    pub fn pause(&self) {
        self.flags.pause.store(true, Ordering::Relaxed);
    }

    pub fn resume(&self) {
        self.flags.pause.store(false, Ordering::Relaxed);
    }

    pub fn status(&self) -> MediaDecodeStatus {
        status_from(self.flags.status.load(Ordering::Relaxed))
    }

    /// The consumer could not import a DRM frame; the worker drops to the CPU path.
    pub fn egl_import_failed(&self) {
        self.flags.egl_import_failed.store(true, Ordering::Relaxed);
    }

    pub fn finished(&self) -> bool {
        self.worker.as_ref().is_none_or(JoinHandle::is_finished)
    }
}

impl Drop for MediaDecodePlayback {
    fn drop(&mut self) {
        self.stop();
    }
}

/// Streams `path` through `filter_desc` at `fps` on a worker thread, looping at the end.
pub fn media_decode_stream(path: &str, filter_desc: &str, fps: i32, supports_row_length: bool, on_frame: FrameSink, on_drm_frame: Option<DrmSink>) -> MediaDecodePlayback {
    let flags = Arc::new(Flags::default());
    let (path, filter_desc, worker_flags) = (path.to_string(), filter_desc.to_string(), flags.clone());
    let worker = std::thread::Builder::new()
        .name("astralia-shell-media".into())
        .spawn(move || {
            let mut sinks = Sinks { on_frame, on_drm_frame, supports_row_length };
            decode_loop(&path, &filter_desc, fps, &mut sinks, &worker_flags);
        })
        .inspect_err(|e| ::log::error!("media_decode: cannot start the decode worker: {e}"))
        .ok();
    MediaDecodePlayback { worker, flags }
}

struct Sinks {
    on_frame: FrameSink,
    on_drm_frame: Option<DrmSink>,
    supports_row_length: bool,
}

fn av_error(code: c_int) -> String {
    let mut buf = [0 as std::ffi::c_char; 128];
    // SAFETY: `buf` is writable for its length and NUL-terminated by `av_strerror`.
    unsafe {
        ff::av_strerror(code, buf.as_mut_ptr(), buf.len());
        CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned()
    }
}

struct Input(*mut ff::AVFormatContext);

impl Drop for Input {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: opened by `avformat_open_input`, closed once.
            unsafe { ff::avformat_close_input(&mut self.0) };
        }
    }
}

struct Codec(*mut ff::AVCodecContext);

impl Drop for Codec {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: allocated by `avcodec_alloc_context3`, freed once.
            unsafe { ff::avcodec_free_context(&mut self.0) };
        }
    }
}

struct BufferRef(*mut ff::AVBufferRef);

impl Drop for BufferRef {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: a reference we own, released once.
            unsafe { ff::av_buffer_unref(&mut self.0) };
        }
    }
}

struct Frame(*mut ff::AVFrame);

impl Frame {
    fn new() -> Option<Frame> {
        // SAFETY: plain allocation.
        let f = unsafe { ff::av_frame_alloc() };
        (!f.is_null()).then_some(Frame(f))
    }
}

impl Drop for Frame {
    fn drop(&mut self) {
        // SAFETY: allocated by `av_frame_alloc`, freed once.
        unsafe { ff::av_frame_free(&mut self.0) };
    }
}

struct Packet(*mut ff::AVPacket);

impl Packet {
    fn new() -> Option<Packet> {
        // SAFETY: plain allocation.
        let p = unsafe { ff::av_packet_alloc() };
        (!p.is_null()).then_some(Packet(p))
    }
}

impl Drop for Packet {
    fn drop(&mut self) {
        // SAFETY: allocated by `av_packet_alloc`, freed once.
        unsafe { ff::av_packet_free(&mut self.0) };
    }
}

/// `buffer` source, the parsed filter chain, `buffersink`; built from the first decoded frame.
struct FilterGraph {
    graph: *mut ff::AVFilterGraph,
    src: *mut ff::AVFilterContext,
    sink: *mut ff::AVFilterContext,
}

impl Drop for FilterGraph {
    fn drop(&mut self) {
        // SAFETY: allocated by `avfilter_graph_alloc`, freed once with its filters.
        unsafe { ff::avfilter_graph_free(&mut self.graph) };
    }
}

impl FilterGraph {
    /// # Safety
    /// `sample` is a valid decoded frame.
    unsafe fn build(sample: *const ff::AVFrame, time_base: ff::AVRational, filter_desc: &str, path: &str) -> Option<FilterGraph> {
        // SAFETY: every pointer below is checked or comes from FFmpeg; the graph owns its filters.
        unsafe {
            let mut fg = FilterGraph { graph: ff::avfilter_graph_alloc(), src: null_mut(), sink: null_mut() };
            if fg.graph.is_null() {
                return None;
            }
            let s = &*sample;
            let aspect = if s.sample_aspect_ratio.num != 0 { s.sample_aspect_ratio } else { ff::AVRational { num: 1, den: 1 } };
            let args = CString::new(format!(
                "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
                s.width, s.height, s.format, time_base.num, time_base.den, aspect.num, aspect.den
            ))
            .ok()?;
            let buffersrc = ff::avfilter_get_by_name(c"buffer".as_ptr());
            let buffersink = ff::avfilter_get_by_name(c"buffersink".as_ptr());
            if ff::avfilter_graph_create_filter(&mut fg.src, buffersrc, c"in".as_ptr(), args.as_ptr(), null_mut(), fg.graph) < 0
                || ff::avfilter_graph_create_filter(&mut fg.sink, buffersink, c"out".as_ptr(), null(), null_mut(), fg.graph) < 0
            {
                ::log::error!("media_decode: filter source/sink setup failed '{path}'");
                return None;
            }
            let mut outputs = ff::avfilter_inout_alloc();
            let mut inputs = ff::avfilter_inout_alloc();
            if outputs.is_null() || inputs.is_null() {
                ff::avfilter_inout_free(&mut outputs);
                ff::avfilter_inout_free(&mut inputs);
                return None;
            }
            (*outputs).name = ff::av_strdup(c"in".as_ptr());
            (*outputs).filter_ctx = fg.src;
            (*outputs).pad_idx = 0;
            (*outputs).next = null_mut();
            (*inputs).name = ff::av_strdup(c"out".as_ptr());
            (*inputs).filter_ctx = fg.sink;
            (*inputs).pad_idx = 0;
            (*inputs).next = null_mut();
            let desc = CString::new(filter_desc).ok()?;
            let ret = ff::avfilter_graph_parse_ptr(fg.graph, desc.as_ptr(), &mut inputs, &mut outputs, null_mut());
            ff::avfilter_inout_free(&mut inputs);
            ff::avfilter_inout_free(&mut outputs);
            if ret < 0 || ff::avfilter_graph_config(fg.graph, null_mut()) < 0 {
                ::log::error!("media_decode: filter graph '{filter_desc}' failed for '{path}'");
                return None;
            }
            Some(fg)
        }
    }
}

/// The opened video stream of `path` and a decoder context for it.
struct Opened {
    input: Input,
    codec: Codec,
    stream_index: c_int,
    time_base: ff::AVRational,
    decoder: *const ff::AVCodec,
}

/// # Safety
/// FFmpeg must be usable from this thread (it always is after `avformat` loads).
unsafe fn open(path: &str) -> Option<Opened> {
    let cpath = CString::new(path).ok()?;
    // SAFETY: every result is checked before use; the guards free what was opened.
    unsafe {
        let mut input = Input(null_mut());
        if ff::avformat_open_input(&mut input.0, cpath.as_ptr(), null(), null_mut()) < 0 {
            ::log::info!("media_decode: open failed '{path}'");
            return None;
        }
        if ff::avformat_find_stream_info(input.0, null_mut()) < 0 {
            ::log::info!("media_decode: stream info failed '{path}'");
            return None;
        }
        let stream_index = ff::av_find_best_stream(input.0, ff::AVMediaType::AVMEDIA_TYPE_VIDEO, -1, -1, null_mut(), 0);
        if stream_index < 0 {
            ::log::info!("media_decode: no video stream '{path}'");
            return None;
        }
        let stream = *(*input.0).streams.add(stream_index as usize);
        let decoder = ff::avcodec_find_decoder((*(*stream).codecpar).codec_id);
        if decoder.is_null() {
            ::log::info!("media_decode: no decoder for '{path}'");
            return None;
        }
        let codec = Codec(ff::avcodec_alloc_context3(decoder));
        if codec.0.is_null() || ff::avcodec_parameters_to_context(codec.0, (*stream).codecpar) < 0 {
            ::log::error!("media_decode: codec context setup failed '{path}'");
            return None;
        }
        (*codec.0).pkt_timebase = (*stream).time_base;
        Some(Opened { input, codec, stream_index, time_base: (*stream).time_base, decoder })
    }
}

unsafe extern "C" fn get_hw_format(ctx: *mut ff::AVCodecContext, fmts: *const ff::AVPixelFormat) -> ff::AVPixelFormat {
    // SAFETY: `opaque` points at the boxed wanted format for the codec's lifetime; `fmts` ends with NONE.
    unsafe {
        let wanted = *((*ctx).opaque as *const ff::AVPixelFormat);
        let mut p = fmts;
        while *p != ff::AVPixelFormat::AV_PIX_FMT_NONE {
            if *p == wanted {
                return *p;
            }
            p = p.add(1);
        }
        *fmts
    }
}

/// Picks the first preferred hardware type the decoder supports and the system opens.
///
/// # Safety
/// `decoder` is a valid codec.
unsafe fn hw_device(decoder: *const ff::AVCodec) -> Option<(BufferRef, ff::AVPixelFormat)> {
    for ty in PREFERRED_HW_TYPES {
        let mut candidate = None;
        for i in 0.. {
            // SAFETY: `decoder` is valid; a null config ends the list.
            let config = unsafe { ff::avcodec_get_hw_config(decoder, i) };
            if config.is_null() {
                break;
            }
            // SAFETY: non-null config from FFmpeg.
            let config = unsafe { &*config };
            if config.methods & ff::AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX as c_int != 0 && config.device_type == ty {
                candidate = Some(config.pix_fmt);
                break;
            }
        }
        let Some(pix_fmt) = candidate else { continue };
        let mut device = BufferRef(null_mut());
        // SAFETY: `device.0` receives a new reference on success.
        if unsafe { ff::av_hwdevice_ctx_create(&mut device.0, ty, null(), null_mut(), 0) } == 0 {
            return Some((device, pix_fmt));
        }
    }
    None
}

fn pix_fmt_name(fmt: ff::AVPixelFormat) -> String {
    // SAFETY: returns a static string or null.
    let name = unsafe { ff::av_get_pix_fmt_name(fmt) };
    if name.is_null() {
        return "none".into();
    }
    // SAFETY: non-null, NUL-terminated, static.
    unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned()
}

/// The two NV12 planes of a DRM PRIME descriptor, with their fds duplicated.
///
/// # Safety
/// `drm` is a mapped `AV_PIX_FMT_DRM_PRIME` frame.
unsafe fn drm_planes(drm: *const ff::AVFrame, path: &str) -> Option<[MediaDrmPlane; 2]> {
    // SAFETY: a DRM PRIME frame carries its descriptor in `data[0]`.
    let desc = unsafe { ((*drm).data[0] as *const ff::AVDRMFrameDescriptor).as_ref() }?;
    let (mut y, mut uv) = (None, None);
    for layer in desc.layers.iter().take(desc.nb_layers.clamp(0, 4) as usize) {
        if layer.format == DRM_FORMAT_NV12 && layer.nb_planes == 2 {
            (y, uv) = (Some(&layer.planes[0]), Some(&layer.planes[1]));
        } else if layer.nb_planes == 1 {
            if layer.format == DRM_FORMAT_R8 {
                y = Some(&layer.planes[0]);
            } else if layer.format == DRM_FORMAT_GR88 || layer.format == DRM_FORMAT_RG88 {
                uv = Some(&layer.planes[0]);
            }
        }
    }
    let (Some(y), Some(uv)) = (y, uv) else {
        if desc.nb_layers >= 1 {
            ::log::info!(
                "media_decode: unsupported DRM layout for '{path}': nb_layers={} format=0x{:x} nb_planes={}, disabling zero-copy for this playback",
                desc.nb_layers,
                desc.layers[0].format,
                desc.layers[0].nb_planes
            );
        } else {
            ::log::info!("media_decode: DRM_PRIME frame for '{path}' has no layers, disabling zero-copy for this playback");
        }
        return None;
    };
    let mut fds: Vec<(c_int, Arc<OwnedFd>)> = Vec::new();
    let mut plane = |p: &ff::AVDRMPlaneDescriptor| -> Option<MediaDrmPlane> {
        if p.object_index < 0 || p.object_index >= desc.nb_objects {
            return None;
        }
        let obj = &desc.objects[p.object_index as usize];
        let fd = match fds.iter().find(|(raw, _)| *raw == obj.fd) {
            Some((_, fd)) => fd.clone(),
            None => {
                // SAFETY: `obj.fd` is open while the mapping lives; the duplicate is ours.
                let dup = unsafe { libc::dup(obj.fd) };
                if dup < 0 {
                    return None;
                }
                // SAFETY: `dup` is a fresh fd nothing else owns.
                let fd = Arc::new(unsafe { OwnedFd::from_raw_fd(dup) });
                fds.push((obj.fd, fd.clone()));
                fd
            }
        };
        Some(MediaDrmPlane { fd, modifier: obj.format_modifier, offset: p.offset as i32, pitch: p.pitch as i32 })
    };
    Some([plane(y)?, plane(uv)?])
}

/// The decode worker: pacing, loop at end of stream, pause, zero-copy with CPU fallback.
fn decode_loop(path: &str, filter_desc: &str, fps: i32, sinks: &mut Sinks, flags: &Flags) {
    // SAFETY: the FFI calls below follow FFmpeg's ownership rules; every allocation is guarded.
    unsafe {
        let Some(opened) = open(path) else { return };
        let codec = opened.codec.0;
        let hw = hw_device(opened.decoder);
        let mut hw_pix_fmt = Box::new(ff::AVPixelFormat::AV_PIX_FMT_NONE);
        let _device = hw.map(|(device, fmt)| {
            *hw_pix_fmt = fmt;
            (*codec).hw_device_ctx = ff::av_buffer_ref(device.0);
            (*codec).opaque = &mut *hw_pix_fmt as *mut ff::AVPixelFormat as *mut c_void;
            (*codec).get_format = Some(get_hw_format);
            ::log::info!("media_decode: hw decode enabled for '{path}'");
            device
        });
        let hw_fmt = *hw_pix_fmt;
        if sinks.on_drm_frame.is_none() || hw_fmt != ff::AVPixelFormat::AV_PIX_FMT_VAAPI {
            ::log::info!(
                "media_decode: zero-copy unavailable for '{path}' (drm_cb={} hw_pix_fmt={}), using CPU upload path",
                i32::from(sinks.on_drm_frame.is_some()),
                if hw_fmt == ff::AVPixelFormat::AV_PIX_FMT_NONE { "none".into() } else { pix_fmt_name(hw_fmt) }
            );
            flags.status.store(status_to(MediaDecodeStatus::CpuFallback), Ordering::Relaxed);
        }
        if ff::avcodec_open2(codec, opened.decoder, null_mut()) < 0 {
            ::log::error!("media_decode: avcodec_open2 failed '{path}'");
            return;
        }
        let (Some(packet), Some(decoded), Some(downloaded), Some(filtered)) = (Packet::new(), Frame::new(), Frame::new(), Frame::new()) else { return };
        let mut filter: Option<FilterGraph> = None;
        let mut exports: HashMap<u64, [MediaDrmPlane; 2]> = HashMap::new();
        let mut zero_copy_disabled = false;
        let frame_interval = 1.0 / f64::from(fps.max(1));
        let pts_time_base = f64::from(opened.time_base.num) / f64::from(opened.time_base.den.max(1));
        let mut anchor: Option<(f64, Instant)> = None;
        let mut last_shown: Option<f64> = None;
        let stopped = || flags.stop.load(Ordering::Relaxed);

        let mut deliver_cpu = |sw: *mut ff::AVFrame, filter: &mut Option<FilterGraph>| {
            if filter.is_none() {
                *filter = FilterGraph::build(sw, opened.time_base, filter_desc, path);
            }
            let Some(fg) = filter else { return };
            if ff::av_buffersrc_add_frame_flags(fg.src, sw, 0) < 0 {
                return;
            }
            while ff::av_buffersink_get_frame(fg.sink, filtered.0) >= 0 {
                let f = &*filtered.0;
                let (w, h, linesize) = (f.width, f.height, f.linesize[0]);
                let frame = if sinks.supports_row_length {
                    let bytes = std::slice::from_raw_parts(f.data[0], linesize as usize * h as usize);
                    RgbaFrame { rgba: bytes.to_vec(), width: w, height: h, stride_px: linesize / 4 }
                } else {
                    let row = w as usize * 4;
                    let mut rgba = Vec::with_capacity(row * h as usize);
                    for y in 0..h as usize {
                        rgba.extend_from_slice(std::slice::from_raw_parts(f.data[0].add(y * linesize as usize), row));
                    }
                    RgbaFrame { rgba, width: w, height: h, stride_px: 0 }
                };
                (sinks.on_frame)(frame);
                ff::av_frame_unref(filtered.0);
            }
        };

        // Receives every frame the decoder has; false when stopped.
        let mut drain = |anchor: &mut Option<(f64, Instant)>, last_shown: &mut Option<f64>, filter: &mut Option<FilterGraph>| -> bool {
            while ff::avcodec_receive_frame(codec, decoded.0) >= 0 {
                let d = &*decoded.0;
                let raw_pts = if d.pts != ff::AV_NOPTS_VALUE { d.pts } else { d.pkt_dts };
                let pts = if raw_pts != ff::AV_NOPTS_VALUE { raw_pts as f64 * pts_time_base } else { last_shown.map_or(0.0, |l| l + frame_interval) };
                let due = last_shown.is_none_or(|l| pts - l >= frame_interval - 1e-6);
                if !due {
                    ff::av_frame_unref(decoded.0);
                    if stopped() {
                        return false;
                    }
                    continue;
                }
                let (anchor_pts, anchor_time) = *anchor.get_or_insert((pts, Instant::now()));
                let due_time = anchor_time + Duration::from_secs_f64((pts - anchor_pts).max(0.0));
                while !stopped() {
                    let now = Instant::now();
                    if now >= due_time {
                        break;
                    }
                    std::thread::sleep((due_time - now).min(PACE_STEP));
                }
                if stopped() {
                    return false;
                }

                let mut zero_copy = false;
                if let Some(on_drm) = &mut sinks.on_drm_frame
                    && !zero_copy_disabled
                    && !flags.egl_import_failed.load(Ordering::Relaxed)
                    && hw_fmt == ff::AVPixelFormat::AV_PIX_FMT_VAAPI
                    && d.format == hw_fmt as c_int
                {
                    let surface_id = d.data[3] as u64;
                    if !exports.contains_key(&surface_id)
                        && let Some(drm) = Frame::new()
                    {
                        (*drm.0).format = ff::AVPixelFormat::AV_PIX_FMT_DRM_PRIME as c_int;
                        let ret = ff::av_hwframe_map(drm.0, decoded.0, ff::AV_HWFRAME_MAP_READ as c_int);
                        if ret < 0 {
                            ::log::info!("media_decode: av_hwframe_map to DRM_PRIME failed for '{path}': {} ({ret}), disabling zero-copy for this playback", av_error(ret));
                        } else if let Some(planes) = drm_planes(drm.0, path) {
                            exports.insert(surface_id, planes);
                        }
                    }
                    let held = ff::av_frame_clone(decoded.0);
                    if let (Some(planes), false) = (exports.get(&surface_id), held.is_null()) {
                        on_drm(MediaDrmFrame { planes: planes.clone(), width: d.width, height: d.height, surface_id, _held: HeldFrame(held) });
                        flags.status.store(status_to(MediaDecodeStatus::ZeroCopy), Ordering::Relaxed);
                        zero_copy = true;
                    } else {
                        if !held.is_null() {
                            drop(HeldFrame(held));
                        }
                        zero_copy_disabled = true;
                        flags.status.store(status_to(MediaDecodeStatus::CpuFallback), Ordering::Relaxed);
                        ::log::info!("media_decode: zero-copy VAAPI import failed for '{path}', falling back to CPU decode path for the rest of this playback");
                    }
                }
                if !zero_copy {
                    let mut sw = decoded.0;
                    if hw_fmt != ff::AVPixelFormat::AV_PIX_FMT_NONE && d.format == hw_fmt as c_int {
                        if ff::av_hwframe_transfer_data(downloaded.0, decoded.0, 0) < 0 {
                            ff::av_frame_unref(decoded.0);
                            if stopped() {
                                return false;
                            }
                            continue;
                        }
                        sw = downloaded.0;
                    }
                    deliver_cpu(sw, filter);
                    ff::av_frame_unref(downloaded.0);
                }
                ff::av_frame_unref(decoded.0);
                *last_shown = Some(pts);
                if stopped() {
                    return false;
                }
            }
            true
        };

        while !stopped() {
            if flags.pause.load(Ordering::Relaxed) {
                while !stopped() && flags.pause.load(Ordering::Relaxed) {
                    std::thread::sleep(PAUSE_POLL);
                }
                if stopped() {
                    return;
                }
                (anchor, last_shown) = (None, None);
                continue;
            }
            if ff::av_read_frame(opened.input.0, packet.0) < 0 {
                ff::avcodec_send_packet(codec, null());
                if !drain(&mut anchor, &mut last_shown, &mut filter) {
                    return;
                }
                if ff::av_seek_frame(opened.input.0, opened.stream_index, 0, ff::AVSEEK_FLAG_BACKWARD) < 0 {
                    ::log::error!("media_decode: loop seek failed '{path}'");
                    return;
                }
                ff::avcodec_flush_buffers(codec);
                (anchor, last_shown) = (None, None);
                continue;
            }
            if (*packet.0).stream_index != opened.stream_index {
                ff::av_packet_unref(packet.0);
                continue;
            }
            let sent = ff::avcodec_send_packet(codec, packet.0);
            ff::av_packet_unref(packet.0);
            if sent < 0 {
                continue;
            }
            if !drain(&mut anchor, &mut last_shown, &mut filter) {
                return;
            }
        }
    }
}

/// Up to `max_frames` frames of `path` through `filter_desc`, decoded single-threaded on the calling thread.
pub fn media_decode_frames(path: &str, filter_desc: &str, max_frames: usize) -> Vec<MediaFrame> {
    let mut out = Vec::new();
    if max_frames == 0 {
        return out;
    }
    // SAFETY: the FFI calls below follow FFmpeg's ownership rules; every allocation is guarded.
    unsafe {
        let Some(opened) = open(path) else { return out };
        let codec = opened.codec.0;
        (*codec).thread_count = 1;
        (*codec).thread_type = 0;
        if ff::avcodec_open2(codec, opened.decoder, null_mut()) < 0 {
            ::log::error!("media_decode: codec setup failed '{path}'");
            return out;
        }
        let (Some(packet), Some(decoded), Some(filtered)) = (Packet::new(), Frame::new(), Frame::new()) else { return out };
        let mut filter: Option<FilterGraph> = None;
        let mut failed = false;
        let mut feed = |frame: *mut ff::AVFrame, out: &mut Vec<MediaFrame>| -> bool {
            if filter.is_none() {
                filter = FilterGraph::build(frame, opened.time_base, filter_desc, path);
            }
            let Some(fg) = &filter else { return false };
            if ff::av_buffersrc_add_frame_flags(fg.src, frame, 0) < 0 {
                return false;
            }
            while out.len() < max_frames && ff::av_buffersink_get_frame(fg.sink, filtered.0) >= 0 {
                let f = &*filtered.0;
                let (w, h, linesize) = (f.width, f.height, f.linesize[0] as usize);
                let mut rgba = Vec::with_capacity(w as usize * h as usize * 4);
                for y in 0..h as usize {
                    rgba.extend_from_slice(std::slice::from_raw_parts(f.data[0].add(y * linesize), w as usize * 4));
                }
                out.push(MediaFrame { width: w, height: h, rgba });
                ff::av_frame_unref(filtered.0);
            }
            true
        };
        let mut receive = |out: &mut Vec<MediaFrame>, failed: &mut bool| {
            while ff::avcodec_receive_frame(codec, decoded.0) >= 0 {
                let ok = feed(decoded.0, out);
                ff::av_frame_unref(decoded.0);
                if !ok {
                    *failed = true;
                }
                if !ok || out.len() >= max_frames {
                    break;
                }
            }
        };
        while !failed && out.len() < max_frames && ff::av_read_frame(opened.input.0, packet.0) >= 0 {
            if (*packet.0).stream_index == opened.stream_index && ff::avcodec_send_packet(codec, packet.0) >= 0 {
                receive(&mut out, &mut failed);
            }
            ff::av_packet_unref(packet.0);
        }
        if !failed && out.len() < max_frames {
            ff::avcodec_send_packet(codec, null());
            receive(&mut out, &mut failed);
        }
    }
    if out.is_empty() {
        ::log::info!("media_decode: decode produced no frames for '{path}'");
    }
    out
}
