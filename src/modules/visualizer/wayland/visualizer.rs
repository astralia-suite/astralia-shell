//! The visualizer window's render thread: PipeWire fragments through the FFT and GL stages into bars or the sphere.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use glow::HasContext;
use khronos_egl as egl;

use super::audio_stages::AudioStages;
use super::bars::Bars;
use super::shaders::Colors;
use super::sphere::Sphere;
use crate::wayland::render::egl::{Egl, load_gl};
use crate::config::settings::{VisualizerParams, VisualizerShape};
use crate::config::visualizer_config::{BAR_FPS, FFT_CUT_OFF, FFT_SCALE, FPS_MAX, FPS_MIN, FRAGMENT_SIZE};
use crate::modules::visualizer::audio_capture::{Accumulator, AudioCapture};
use crate::modules::visualizer::fft::fft;

const TRACE_FRAMES: i32 = 40;

struct Shared {
    shutdown: bool,
    params: VisualizerParams,
    colors: Colors,
}

type Signal = Arc<(Mutex<Shared>, Condvar)>;

/// The EGL handles the thread draws with.
struct ThreadEgl {
    display: egl::Display,
    surface: egl::Surface,
    context: egl::Context,
}

// SAFETY: EGL handles are process-wide; the context is made current only on the render
// thread, and the loop does not touch the surface until that thread has joined.
unsafe impl Send for ThreadEgl {}

fn pack(size: (i32, i32)) -> u64 {
    (size.0 as u32 as u64) << 32 | size.1 as u32 as u64
}

fn unpack(v: u64) -> (i32, i32) {
    ((v >> 32) as u32 as i32, v as u32 as i32)
}

struct RenderThread {
    signal: Signal,
    size: Arc<AtomicU64>,
    handle: JoinHandle<()>,
    context: egl::Context,
}

#[derive(Default)]
pub struct Visualizer {
    pub capture: AudioCapture,
    thread: Option<RenderThread>,
    sent: Option<(VisualizerParams, Colors)>,
}

impl Visualizer {
    /// Creates the share context and starts the thread drawing to `surface`, which the
    /// loop must not have current.
    pub fn start_thread(&mut self, egl: &Egl, surface: egl::Surface, size: (i32, i32), params: &VisualizerParams, colors: Colors) -> bool {
        if self.thread.is_some() {
            return true;
        }
        let Some(context) = egl.share_context() else {
            ::log::error!("visualizer: eglCreateContext failed");
            return false;
        };
        let signal: Signal = Arc::new((Mutex::new(Shared { shutdown: false, params: params.clone(), colors }), Condvar::new()));
        let shared_size = Arc::new(AtomicU64::new(pack(size)));
        let handles = ThreadEgl { display: egl.display, surface, context };
        let (thread_signal, thread_size, source) = (signal.clone(), shared_size.clone(), self.capture.source());
        let spawned = std::thread::Builder::new()
            .name("astralia-shell-visualizer".into())
            .spawn(move || render_thread_main(handles, thread_signal, thread_size, source));
        match spawned {
            Ok(handle) => {
                self.thread = Some(RenderThread { signal, size: shared_size, handle, context });
                self.sent = Some((params.clone(), colors));
                true
            }
            Err(e) => {
                ::log::error!("visualizer: cannot start the render thread: {e}");
                let _ = egl.egl.destroy_context(egl.display, context);
                false
            }
        }
    }

    #[cfg(test)]
    fn context(&self) -> Option<egl::Context> {
        self.thread.as_ref().map(|t| t.context)
    }

    /// Hands new params and colours to the thread; unchanged ones wake nothing.
    pub fn apply_params(&mut self, params: &VisualizerParams, colors: Colors) {
        let Some(thread) = &self.thread else { return };
        if self.sent.as_ref().is_some_and(|(p, c)| p == params && *c == colors) {
            return;
        }
        if let Ok(mut shared) = thread.signal.0.lock() {
            shared.params = params.clone();
            shared.colors = colors;
        }
        thread.signal.1.notify_one();
        self.sent = Some((params.clone(), colors));
    }

    pub fn resize(&self, size: (i32, i32)) {
        if let Some(thread) = &self.thread {
            thread.size.store(pack(size), Ordering::Relaxed);
        }
    }

    /// Stops capture, then signals and joins the thread, then destroys its context.
    pub fn shutdown(&mut self, egl: &Egl) {
        self.capture.stop();
        let Some(thread) = self.thread.take() else { return };
        if let Ok(mut shared) = thread.signal.0.lock() {
            shared.shutdown = true;
        }
        thread.signal.1.notify_one();
        if thread.handle.join().is_err() {
            ::log::error!("visualizer: render thread panicked");
        }
        if let Err(e) = egl.egl.destroy_context(egl.display, thread.context) {
            ::log::error!("visualizer: eglDestroyContext failed 0x{:04x}", e.native());
        }
        self.sent = None;
    }
}

fn clear_backbuffer(gl: &glow::Context, instance: &egl::Instance<egl::Static>, h: &ThreadEgl, size: (i32, i32), backdrop: [f32; 4]) {
    // SAFETY: the share context is current on this thread.
    unsafe {
        gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        gl.viewport(0, 0, size.0, size.1);
        gl.clear_color(backdrop[0], backdrop[1], backdrop[2], backdrop[3]);
        gl.clear(glow::COLOR_BUFFER_BIT);
    }
    if let Err(e) = instance.swap_buffers(h.display, h.surface) {
        ::log::error!("visualizer: eglSwapBuffers failed 0x{:x}", e.native());
    }
}

fn ms(d: Duration) -> f32 {
    d.as_secs_f32() * 1000.0
}

fn render_thread_main(h: ThreadEgl, signal: Signal, size: Arc<AtomicU64>, source: Option<Arc<Mutex<Accumulator>>>) {
    let instance = egl::Instance::new(egl::Static);
    if let Err(e) = instance.make_current(h.display, Some(h.surface), Some(h.surface), Some(h.context)) {
        ::log::error!("visualizer: render thread eglMakeCurrent failed, eglGetError=0x{:x}", e.native());
        return;
    }
    let gl = load_gl(&instance);
    // SAFETY: the share context is current on this thread.
    unsafe { gl.enable(glow::BLEND) };
    let (w, h0) = unpack(size.load(Ordering::Relaxed));
    ::log::info!("visualizer: render thread current {w}x{h0} scale 1");

    let mut stages = AudioStages::default();
    let mut sphere = Sphere::default();
    let mut bar = Bars::default();
    let stages_ok = stages.init(&gl);
    let (mut sphere_ok, mut bar_ok, mut sphere_tried, mut bar_tried) = (false, false, false, false);
    ::log::info!("visualizer: audio stages init {}", if stages_ok { "ok" } else { "FAILED, showing cleared window" });

    let (mut audio_l, mut audio_r) = (Vec::with_capacity(FRAGMENT_SIZE), Vec::with_capacity(FRAGMENT_SIZE));
    let (mut fft_l, mut fft_r) = (vec![0.0f32; FRAGMENT_SIZE], vec![0.0f32; FRAGMENT_SIZE]);
    let (mut audio_size, mut tick, mut logged_frames) = (0i32, 0i32, 0i32);
    let mut first_frame_done = false;
    let mut last_heartbeat = Instant::now();
    let (mut heartbeat_frames, mut heartbeat_draw_ms) = (0i32, 0.0f32);
    let mut next = Instant::now();

    loop {
        let (params, colors) = {
            let (lock, cv) = &*signal;
            let Ok(guard) = lock.lock() else { break };
            let wait = next.saturating_duration_since(Instant::now());
            let Ok((guard, _)) = cv.wait_timeout_while(guard, wait, |s| !s.shutdown) else { break };
            if guard.shutdown {
                break;
            }
            (guard.params.clone(), guard.colors)
        };
        let now = Instant::now();
        let want_sphere = params.visualizer_shape == VisualizerShape::Sphere;
        let fps = (if want_sphere { params.fps } else { BAR_FPS }).clamp(FPS_MIN, FPS_MAX);
        next += Duration::from_nanos(1_000_000_000 / fps as u64);
        if next < now {
            next = now;
        }
        let (width, height) = unpack(size.load(Ordering::Relaxed));

        if want_sphere && !sphere_tried {
            sphere_tried = true;
            sphere_ok = sphere.init(&gl);
            ::log::info!("visualizer: sphere renderer init {}", if sphere_ok { "ok" } else { "FAILED" });
        } else if !want_sphere && !bar_tried {
            bar_tried = true;
            bar_ok = bar.init(&gl);
            ::log::info!("visualizer: bar renderer init {}", if bar_ok { "ok" } else { "FAILED" });
        }
        if !stages_ok || (if want_sphere { !sphere_ok } else { !bar_ok }) {
            clear_backbuffer(&gl, &instance, &h, (width, height), colors.backdrop);
            continue;
        }

        let modified = source.as_ref().and_then(|s| s.lock().ok().and_then(|mut acc| acc.take(&mut audio_l, &mut audio_r))) == Some(true);
        if modified && audio_l.len() >= FRAGMENT_SIZE && audio_r.len() >= FRAGMENT_SIZE {
            fft_l.copy_from_slice(&audio_l[..FRAGMENT_SIZE]);
            fft_r.copy_from_slice(&audio_r[..FRAGMENT_SIZE]);
            fft(&mut fft_l, FFT_SCALE, FFT_CUT_OFF);
            fft(&mut fft_r, FFT_SCALE, FFT_CUT_OFF);
            if audio_size == 0 {
                ::log::info!("visualizer: first audio at frame {tick}");
            }
            audio_size = FRAGMENT_SIZE as i32;
        }

        tick += 1;
        let trace = logged_frames < TRACE_FRAMES;
        let t0 = Instant::now();
        if audio_size > 0 {
            stages.run(&gl, audio_size, &fft_l, &fft_r, fps);
        }
        let t1 = Instant::now();
        let (al, ar) = if stages.ready() { (stages.smooth_l(), stages.smooth_r()) } else { (None, None) };
        if want_sphere {
            sphere.render(&gl, width, height, tick, al, ar, stages.size(), &params, &colors);
        } else {
            bar.render(&gl, width, height, al, ar, &colors);
        }
        // SAFETY: the share context is current on this thread.
        unsafe { gl.finish() };
        let t2 = Instant::now();
        let render_ms = ms(t2 - t0);
        if let Err(e) = instance.swap_buffers(h.display, h.surface) {
            ::log::error!("visualizer: eglSwapBuffers failed 0x{:x}", e.native());
        }
        let t3 = Instant::now();

        if !first_frame_done {
            first_frame_done = true;
            ::log::info!("visualizer: first frame presented at {tick} ({render_ms:.1}ms)");
        }
        if trace {
            // SAFETY: the share context is current on this thread.
            let error = unsafe { gl.get_error() };
            if error != glow::NO_ERROR {
                ::log::error!("gl: visualizer render error 0x{error:04x}");
            }
            ::log::info!("visualizer: frame {tick} stages={:.1}ms draw={:.1}ms swap={:.1}ms", ms(t1 - t0), ms(t2 - t1), ms(t3 - t2));
            logged_frames += 1;
        }
        heartbeat_frames += 1;
        heartbeat_draw_ms = heartbeat_draw_ms.max(render_ms);
        if t3 - last_heartbeat >= Duration::from_secs(1) {
            let fps_seen = heartbeat_frames as f32 / (t3 - last_heartbeat).as_secs_f32();
            ::log::info!("visualizer: heartbeat tick={tick} frames={heartbeat_frames} fps={fps_seen:.1} worst={heartbeat_draw_ms:.1}ms {width}x{height}");
            last_heartbeat = t3;
            heartbeat_frames = 0;
            heartbeat_draw_ms = 0.0;
        }
    }

    stages.destroy(&gl);
    sphere.destroy(&gl);
    bar.destroy(&gl);
    let _ = instance.make_current(h.display, None, None, None);
}

#[cfg(test)]
mod tests {
    use super::Visualizer;
    use crate::wayland::render::egl::Egl;
    use crate::config::settings::{VisualizerParams, VisualizerShape};
    use crate::modules::visualizer::wayland::shaders::Colors;
    use khronos_egl as egl;
    use std::time::{Duration, Instant};

    #[test]
    fn visualizer_thread_lifecycle() {
        let egl = match Egl::surfaceless() {
            Ok(egl) => egl,
            Err(e) => return println!("skipped: {e}"),
        };
        let surface = egl.egl.create_pbuffer_surface(egl.display, egl.config, &[egl::WIDTH, 64, egl::HEIGHT, 64, egl::NONE]).expect("pbuffer");
        let mut visualizer = Visualizer::default();
        let params = VisualizerParams::default();
        assert!(visualizer.start_thread(&egl, surface, (64, 64), &params, Colors::default()));
        let context = visualizer.context().expect("share context");
        std::thread::sleep(Duration::from_millis(50));
        visualizer.apply_params(&VisualizerParams { visualizer_shape: VisualizerShape::Sphere, ..params.clone() }, Colors::default());
        std::thread::sleep(Duration::from_millis(50));
        visualizer.apply_params(&VisualizerParams { fps: 30, ..params }, Colors { accent: [1.0, 0.0, 0.0], backdrop: [0.0; 4] });
        let started = Instant::now();
        visualizer.shutdown(&egl);
        assert!(started.elapsed() < Duration::from_secs(1), "the thread joins within a second");
        assert!(visualizer.context().is_none());
        assert!(egl.egl.query_context(egl.display, context, egl::CONFIG_ID).is_err(), "the share context is destroyed");
        let _ = egl.egl.destroy_surface(egl.display, surface);
    }
}
