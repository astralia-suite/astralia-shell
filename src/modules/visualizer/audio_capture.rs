//! PipeWire capture of the sink monitor into sliding spectrum fragments.

use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;

use pipewire as pw;
use pw::spa;

use crate::config::visualizer_config::{CHANNELS, FRAGMENT_SIZE, SAMPLE_RATE, SAMPLE_SIZE};

const HALF_SAMPLE: usize = SAMPLE_SIZE / 2;
const QUARTER_SAMPLE: usize = SAMPLE_SIZE / 4;
const SLIDE: usize = QUARTER_SAMPLE + QUARTER_SAMPLE;
const TAIL: usize = FRAGMENT_SIZE - QUARTER_SAMPLE - QUARTER_SAMPLE;

/// Sliding window over the captured samples. Every buffer is allocated in
/// `new`; `push` never allocates, so it is safe on the realtime thread.
pub struct Accumulator {
    audio: Vec<f32>,
    right: Vec<f32>,
    left: Vec<f32>,
    filled: usize,
    modified: bool,
    have_data: bool,
}

impl Accumulator {
    pub fn new() -> Self {
        Accumulator {
            audio: vec![0.0; HALF_SAMPLE + 1],
            right: vec![0.0; FRAGMENT_SIZE],
            left: vec![0.0; FRAGMENT_SIZE],
            filled: 0,
            modified: false,
            have_data: false,
        }
    }

    /// Interleaved samples; a mono stream averages each pair into both sides.
    pub fn push(&mut self, samples: &[f32], channels: usize) {
        for &sample in samples {
            if self.filled <= HALF_SAMPLE {
                self.audio[self.filled] = sample;
                self.filled += 1;
                continue;
            }
            self.right.copy_within(SLIDE.., 0);
            self.left.copy_within(SLIDE.., 0);
            for i in (0..HALF_SAMPLE).step_by(2) {
                let idx = TAIL + i;
                if channels == 1 {
                    let s = (self.audio[i] + self.audio[i + 1]) / 2.0;
                    self.right[idx] = s;
                    self.left[idx] = s;
                } else {
                    self.right[idx] = self.audio[i + 1];
                    self.left[idx] = self.audio[i];
                }
                self.right[idx + 1] = 0.0;
                self.left[idx + 1] = 0.0;
            }
            self.filled = 0;
            self.modified = true;
            self.have_data = true;
        }
    }

    /// `None` before the first fragment; otherwise whether a new fragment was
    /// copied into `l` and `r` since the last call.
    pub fn take(&mut self, l: &mut Vec<f32>, r: &mut Vec<f32>) -> Option<bool> {
        if !self.have_data {
            return None;
        }
        let modified = std::mem::take(&mut self.modified);
        if modified {
            l.clear();
            l.extend_from_slice(&self.left);
            r.clear();
            r.extend_from_slice(&self.right);
        }
        Some(modified)
    }
}

/// Capture stream on its own PipeWire loop thread.
#[derive(Default)]
pub struct AudioCapture {
    shared: Option<Arc<Mutex<Accumulator>>>,
    quit: Option<pw::channel::Sender<()>>,
    thread: Option<JoinHandle<()>>,
}

impl AudioCapture {
    pub fn start(&mut self) -> bool {
        if self.thread.is_some() {
            return true;
        }
        let shared = Arc::new(Mutex::new(Accumulator::new()));
        let (quit, quit_rx) = pw::channel::channel();
        let (ready_tx, ready) = std::sync::mpsc::channel();
        let thread_shared = shared.clone();
        let thread = match std::thread::Builder::new()
            .name("astralia-shell-visualizer-audio".into())
            .spawn(move || capture_thread(thread_shared, quit_rx, ready_tx))
        {
            Ok(thread) => thread,
            Err(e) => {
                ::log::error!("visualizer_audio: failed to start the capture thread: {e}");
                return false;
            }
        };
        if let Err(e) = ready.recv().unwrap_or_else(|_| Err("the capture thread exited".into())) {
            ::log::error!("visualizer_audio: {e}");
            let _ = thread.join();
            return false;
        }
        self.shared = Some(shared);
        self.quit = Some(quit);
        self.thread = Some(thread);
        true
    }

    pub fn stop(&mut self) {
        if let Some(quit) = self.quit.take() {
            let _ = quit.send(());
        }
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
        self.shared = None;
    }

    /// The window the stream fills, for a reader on another thread; `None` while stopped.
    pub fn source(&self) -> Option<Arc<Mutex<Accumulator>>> {
        self.shared.clone()
    }
}

impl Drop for AudioCapture {
    fn drop(&mut self) {
        self.stop();
    }
}

struct StreamData {
    channels: usize,
    shared: Arc<Mutex<Accumulator>>,
}

fn format_param() -> Result<Vec<u8>, String> {
    let mut info = spa::param::audio::AudioInfoRaw::new();
    info.set_format(spa::param::audio::AudioFormat::F32LE);
    info.set_rate(SAMPLE_RATE);
    info.set_channels(CHANNELS as u32);
    let obj = spa::pod::Object {
        type_: spa::utils::SpaTypes::ObjectParamFormat.as_raw(),
        id: spa::param::ParamType::EnumFormat.as_raw(),
        properties: info.into(),
    };
    spa::pod::serialize::PodSerializer::serialize(std::io::Cursor::new(Vec::new()), &spa::pod::Value::Object(obj))
        .map(|(cursor, _)| cursor.into_inner())
        .map_err(|e| format!("failed to build the stream format: {e:?}"))
}

fn capture_thread(
    shared: Arc<Mutex<Accumulator>>,
    quit: pw::channel::Receiver<()>,
    ready: std::sync::mpsc::Sender<Result<(), String>>,
) {
    pw::init();
    let main_loop = match pw::main_loop::MainLoopRc::new(None) {
        Ok(main_loop) => main_loop,
        Err(e) => {
            let _ = ready.send(Err(format!("failed to create the PipeWire loop: {e}")));
            return;
        }
    };
    let connected = (|| {
        let context = pw::context::ContextRc::new(&main_loop, None).map_err(|e| e.to_string())?;
        let core = context.connect_rc(None).map_err(|e| format!("failed to connect pipewire core: {e}"))?;
        let props = pw::properties::properties! {
            "media.type" => "Audio",
            "media.category" => "Capture",
            "media.role" => "Music",
            "media.name" => "astralia-shell visualizer",
            "node.always-process" => "true",
            "stream.capture.sink" => "true",
        };
        let stream = pw::stream::StreamRc::new(core.clone(), "astralia-shell-visualizer", props).map_err(|e| e.to_string())?;
        let listener = stream
            .add_local_listener_with_user_data(StreamData { channels: CHANNELS, shared })
            .state_changed(|_, _, _, state| {
                if let pw::stream::StreamState::Error(e) = state {
                    ::log::error!("visualizer_audio: stream error: {e}");
                }
            })
            .param_changed(|_, data, id, param| {
                let Some(param) = param else { return };
                if id != spa::param::ParamType::Format.as_raw() {
                    return;
                }
                let Ok((media_type, media_subtype)) = spa::param::format_utils::parse_format(param) else { return };
                if media_type != spa::param::format::MediaType::Audio || media_subtype != spa::param::format::MediaSubtype::Raw {
                    return;
                }
                let mut info = spa::param::audio::AudioInfoRaw::new();
                if info.parse(param).is_ok() && info.channels() > 0 {
                    data.channels = info.channels() as usize;
                }
            })
            .process(|stream, data| {
                let Some(mut buffer) = stream.dequeue_buffer() else { return };
                let Some(d) = buffer.datas_mut().first_mut() else { return };
                let (offset, size) = (d.chunk().offset() as usize, d.chunk().size() as usize);
                let Some(bytes) = d.data() else { return };
                let Some(bytes) = bytes.get(offset..offset.saturating_add(size)) else { return };
                // SAFETY: f32 has no invalid bit patterns; misaligned edges are skipped.
                let (head, samples, _) = unsafe { bytes.align_to::<f32>() };
                if !head.is_empty() {
                    return;
                }
                if let Ok(mut acc) = data.shared.lock() {
                    acc.push(samples, data.channels);
                }
            })
            .register()
            .map_err(|e| e.to_string())?;
        let values = format_param()?;
        let pod = spa::pod::Pod::from_bytes(&values).ok_or("invalid stream format")?;
        stream
            .connect(
                spa::utils::Direction::Input,
                None,
                pw::stream::StreamFlags::AUTOCONNECT | pw::stream::StreamFlags::MAP_BUFFERS | pw::stream::StreamFlags::RT_PROCESS,
                &mut [pod],
            )
            .map_err(|e| format!("failed to connect the stream: {e}"))?;
        Ok::<_, String>((context, core, stream, listener))
    })();
    let parts = match connected {
        Ok(parts) => parts,
        Err(e) => {
            let _ = ready.send(Err(e));
            return;
        }
    };
    let loop_weak = main_loop.downgrade();
    let _quit = quit.attach(main_loop.loop_(), move |()| {
        if let Some(main_loop) = loop_weak.upgrade() {
            main_loop.quit();
        }
    });
    let _ = ready.send(Ok(()));
    main_loop.run();
    drop(parts);
}

#[cfg(test)]
mod tests {
    use super::Accumulator;
    use crate::config::visualizer_config::{FRAGMENT_SIZE, SAMPLE_SIZE};

    const HALF: usize = SAMPLE_SIZE / 2;
    const TAIL: usize = FRAGMENT_SIZE - SAMPLE_SIZE / 2;

    #[test]
    fn audio_capture_slide() {
        let mut acc = Accumulator::new();
        let (mut l, mut r) = (Vec::new(), Vec::new());
        assert_eq!(acc.take(&mut l, &mut r), None, "nothing before any data");

        // Stereo: left = 1.0, right = 2.0 interleaved; HALF + 1 samples fill the window.
        let stereo: Vec<f32> = (0..=HALF).map(|i| if i % 2 == 0 { 1.0 } else { 2.0 }).collect();
        acc.push(&stereo, 2);
        assert_eq!(acc.take(&mut l, &mut r), None, "the window is full but has not slid yet");
        acc.push(&[0.0], 2);
        assert_eq!(acc.take(&mut l, &mut r), Some(true));
        assert_eq!((l.len(), r.len()), (FRAGMENT_SIZE, FRAGMENT_SIZE));
        assert!(l[..TAIL].iter().all(|&v| v == 0.0) && r[..TAIL].iter().all(|&v| v == 0.0));
        for k in (0..HALF).step_by(2) {
            assert_eq!((l[TAIL + k], r[TAIL + k]), (1.0, 2.0));
            assert_eq!((l[TAIL + k + 1], r[TAIL + k + 1]), (0.0, 0.0), "odd entries are zero imaginary parts");
        }
        assert_eq!(acc.take(&mut l, &mut r), Some(false), "a fragment is handed out once");

        // Mono averages each sample pair into both channels; the old tail slides left.
        let mono: Vec<f32> = (0..=HALF).map(|i| if i % 2 == 0 { 3.0 } else { 5.0 }).collect();
        acc.push(&mono, 1);
        acc.push(&[0.0], 1);
        assert_eq!(acc.take(&mut l, &mut r), Some(true));
        assert_eq!((l[TAIL], r[TAIL], l[TAIL + 1]), (4.0, 4.0, 0.0));
        assert_eq!((l[TAIL - HALF], r[TAIL - HALF]), (1.0, 2.0), "the previous block slid by half a sample");
    }

    /// Needs a running PipeWire; `cargo test audio_capture_live -- --ignored`.
    #[test]
    #[ignore]
    fn audio_capture_live() {
        let mut capture = super::AudioCapture::default();
        assert!(capture.start(), "the stream connects");
        std::thread::sleep(std::time::Duration::from_millis(300));
        assert!(capture.source().is_some());
        capture.stop();
        assert!(capture.source().is_none(), "stop joins and drops the window");
    }
}
