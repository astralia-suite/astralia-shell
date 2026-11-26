//! Video decode for animated wallpapers and first-frame stills, with an on-disk RGBA cache.

use std::path::{Path, PathBuf};
use std::time::SystemTime;

use crate::render::decode::Pixels;

#[cfg(feature = "video")]
pub const ANIMATE_WALLPAPER_FPS: i32 = 15;
#[cfg(any(feature = "video", test))]
pub const ANIMATE_MAX_DECODE_DIM: i32 = 1440;
const RGBA_CACHE_MAX_BYTES: u64 = 512 * 1024 * 1024;

#[cfg(any(feature = "video", test))]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AnimateFit {
    Crop,
    Fit,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum MediaDecodeStatus {
    #[default]
    Idle,
    // Only the `video` decoder reports zero-copy playback.
    #[cfg_attr(not(feature = "video"), allow(dead_code))]
    ZeroCopy,
    CpuFallback,
}

/// Straight RGBA, `width * height * 4` bytes.
#[derive(Clone, Debug, PartialEq)]
pub struct MediaFrame {
    pub width: i32,
    pub height: i32,
    pub rgba: Vec<u8>,
}

pub fn animate_cache_home_dir() -> PathBuf {
    match std::env::var_os("XDG_CACHE_HOME").filter(|v| !v.is_empty()) {
        Some(dir) => PathBuf::from(dir),
        None => PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".cache"),
    }
}

/// The target size, scaled down to `max_height` rows when taller.
#[cfg(any(feature = "video", test))]
pub fn animate_decode_size(target_w: i32, target_h: i32, max_height: i32) -> (i32, i32) {
    if target_w <= 0 || target_h <= 0 || max_height <= 0 || target_h <= max_height {
        return (target_w.max(0), target_h.max(0));
    }
    let ratio = f64::from(max_height) / f64::from(target_h);
    (((f64::from(target_w) * ratio).round() as i32).max(1), max_height)
}

#[cfg(any(feature = "video", test))]
pub fn animate_scale_filter(w: i32, h: i32, fit: AnimateFit) -> String {
    match fit {
        AnimateFit::Fit => format!("scale={w}:{h}:force_original_aspect_ratio=decrease,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2:color=black,format=rgba"),
        AnimateFit::Crop => format!("scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h},format=rgba"),
    }
}

/// Averages each destination pixel over the source pixels it covers.
pub fn box_downsample_rgba(src: &[u8], sw: i32, sh: i32, dst: &mut [u8], dw: i32, dh: i32) {
    for y in 0..dh {
        let y0 = (i64::from(y) * i64::from(sh) / i64::from(dh)) as i32;
        let y1 = ((i64::from(y + 1) * i64::from(sh) / i64::from(dh)) as i32).max(y0 + 1);
        for x in 0..dw {
            let x0 = (i64::from(x) * i64::from(sw) / i64::from(dw)) as i32;
            let x1 = ((i64::from(x + 1) * i64::from(sw) / i64::from(dw)) as i32).max(x0 + 1);
            let mut sum = [0u64; 4];
            let mut count = 0u64;
            for sy in y0..y1.min(sh) {
                for sx in x0..x1.min(sw) {
                    let px = &src[(sy as usize * sw as usize + sx as usize) * 4..][..4];
                    for (s, &c) in sum.iter_mut().zip(px) {
                        *s += u64::from(c);
                    }
                    count += 1;
                }
            }
            let count = count.max(1);
            let out = &mut dst[(y as usize * dw as usize + x as usize) * 4..][..4];
            for (o, s) in out.iter_mut().zip(sum) {
                *o = (s / count) as u8;
            }
        }
    }
}

/// `$XDG_CACHE_HOME/astralia-shell/wallpaper`, created on demand.
pub fn rgba_cache_dir() -> PathBuf {
    let dir = animate_cache_home_dir().join("astralia-shell").join("wallpaper");
    if let Err(e) = std::fs::create_dir_all(&dir) {
        ::log::warn!("media_decode: cannot create the frame cache {}: {e}", dir.display());
    }
    dir
}

fn fnv1a(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325u64, |h, &b| (h ^ u64::from(b)).wrapping_mul(0x0000_0100_0000_01b3))
}

/// The entry for `path` at `mtime` decoded for a `target_w` by `target_h` area.
pub fn rgba_cache_path(dir: &Path, path: &str, mtime: i64, target_w: i32, target_h: i32) -> PathBuf {
    let key = format!("{path}:{mtime}:{target_w}x{target_h}");
    dir.join(format!("{}.rgba", fnv1a(key.as_bytes())))
}

/// A cached frame; touching the entry keeps it from the next prune.
pub fn rgba_read(path: &Path) -> Option<(i32, i32, Vec<u8>)> {
    let data = std::fs::read(path).ok()?;
    let word = |at: usize| data.get(at..at + 4).map(|b| u32::from_ne_bytes([b[0], b[1], b[2], b[3]]) as i32);
    let (width, height) = (word(0)?, word(4)?);
    if width <= 0 || height <= 0 || data.len() != 8 + width as usize * height as usize * 4 {
        return None;
    }
    if let Ok(file) = std::fs::File::options().write(true).open(path) {
        let _ = file.set_modified(SystemTime::now());
    }
    let mut data = data;
    data.drain(..8);
    Some((width, height, data))
}

pub fn rgba_write(path: &Path, width: i32, height: i32, rgba: &[u8], max_bytes: u64) {
    let mut out = Vec::with_capacity(8 + rgba.len());
    out.extend((width as u32).to_ne_bytes());
    out.extend((height as u32).to_ne_bytes());
    out.extend_from_slice(rgba);
    if std::fs::write(path, out).is_err() {
        return;
    }
    if let Some(dir) = path.parent() {
        rgba_prune(dir, max_bytes);
    }
}

/// Deletes the least recently used entries until the total fits in `max_bytes`, always keeping the newest.
pub fn rgba_prune(dir: &Path, max_bytes: u64) {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let _guard = LOCK.lock();
    let Ok(read) = std::fs::read_dir(dir) else { return };
    let mut entries: Vec<(PathBuf, u64, SystemTime)> = read
        .flatten()
        .filter(|e| e.path().extension().is_some_and(|x| x == "rgba"))
        .filter_map(|e| {
            let meta = e.metadata().ok().filter(|m| m.is_file())?;
            Some((e.path(), meta.len(), meta.modified().unwrap_or(SystemTime::UNIX_EPOCH)))
        })
        .collect();
    let mut total: u64 = entries.iter().map(|e| e.1).sum();
    entries.sort_by_key(|e| e.2);
    for (path, size, _) in entries.iter().take(entries.len().saturating_sub(1)) {
        if total <= max_bytes {
            break;
        }
        if std::fs::remove_file(path).is_ok() {
            total -= size;
        }
    }
}

/// A still image as straight RGBA, else (with `video`) the first video frame scaled to cover the target.
fn decode_first_frame(path: &str, target_w: i32, target_h: i32) -> Option<MediaFrame> {
    let still = if target_w > 0 && target_h > 0 {
        crate::render::decode::decode_cover(path, target_w, target_h)
    } else {
        crate::render::decode::decode_pixels(path, 0)
    };
    if let Ok(p) = still {
        return Some(MediaFrame { width: p.width, height: p.height, rgba: p.data });
    }
    #[cfg(feature = "video")]
    {
        let filter = if target_w > 0 && target_h > 0 {
            format!("scale={target_w}:{target_h}:force_original_aspect_ratio=increase,format=rgba")
        } else {
            "format=rgba".to_string()
        };
        media_decode_frames(path, &filter, 1).into_iter().next().filter(|f| !f.rgba.is_empty())
    }
    #[cfg(not(feature = "video"))]
    {
        let _ = (target_w, target_h);
        None
    }
}

/// The first frame of `path` downsampled to cover `target_w` by `target_h`, through the frame cache.
pub fn animate_decode_scaled(path: &str, target_w: i32, target_h: i32) -> Option<MediaFrame> {
    decode_scaled_cached(&rgba_cache_dir, path, target_w, target_h)
}

/// `decode_cover` (or a video's first frame) through the frame cache, so a large wallpaper is decoded
/// once per size instead of on every picker open or start. An SVG is rendered directly.
pub fn decode_cover_cached(path: &str, width: i32, height: i32) -> Result<Pixels, String> {
    if crate::render::decode::is_svg(path) {
        return crate::render::decode::decode_cover(path, width, height);
    }
    let frame = animate_decode_scaled(path, width, height).ok_or_else(|| format!("cannot decode {path}"))?;
    Ok(Pixels { width: frame.width, height: frame.height, data: frame.rgba, premultiplied: false })
}

fn decode_scaled_cached(cache_dir: &dyn Fn() -> PathBuf, path: &str, target_w: i32, target_h: i32) -> Option<MediaFrame> {
    let mtime = std::fs::metadata(path).ok().and_then(|m| m.modified().ok());
    let cacheable = target_w > 0 && target_h > 0 && mtime.is_some();
    let cache_path = cacheable.then(|| {
        let secs = mtime.and_then(|t| t.duration_since(SystemTime::UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs() as i64);
        rgba_cache_path(&cache_dir(), path, secs, target_w, target_h)
    });
    if let Some((width, height, rgba)) = cache_path.as_deref().and_then(rgba_read) {
        return Some(MediaFrame { width, height, rgba });
    }
    let mut frame = decode_first_frame(path, target_w, target_h)?;
    if target_w <= 0 || target_h <= 0 {
        return Some(frame);
    }
    let scale = (target_w as f32 / frame.width as f32).max(target_h as f32 / frame.height as f32);
    if scale < 1.0 {
        let dw = ((frame.width as f32 * scale).round() as i32).max(1);
        let dh = ((frame.height as f32 * scale).round() as i32).max(1);
        let mut scaled = vec![0u8; dw as usize * dh as usize * 4];
        box_downsample_rgba(&frame.rgba, frame.width, frame.height, &mut scaled, dw, dh);
        frame = MediaFrame { width: dw, height: dh, rgba: scaled };
    }
    if let Some(cache_path) = &cache_path {
        rgba_write(cache_path, frame.width, frame.height, &frame.rgba, RGBA_CACHE_MAX_BYTES);
    }
    Some(frame)
}

#[cfg(feature = "video")]
mod ffmpeg;
#[cfg(feature = "video")]
pub use ffmpeg::{MediaDecodePlayback, MediaDrmFrame, MediaDrmPlane, RgbaFrame, media_decode_frames, media_decode_stream};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn animate_decode_size_clamps() {
        assert_eq!(animate_decode_size(1920, 1080, ANIMATE_MAX_DECODE_DIM), (1920, 1080));
        assert_eq!(animate_decode_size(3840, 2160, ANIMATE_MAX_DECODE_DIM), (2560, ANIMATE_MAX_DECODE_DIM));
        assert_eq!(animate_decode_size(1000, 4000, ANIMATE_MAX_DECODE_DIM), (360, ANIMATE_MAX_DECODE_DIM));
    }

    #[test]
    fn animate_scale_filters() {
        assert_eq!(animate_scale_filter(200, 200, AnimateFit::Crop), "scale=200:200:force_original_aspect_ratio=increase,crop=200:200,format=rgba");
        assert_eq!(
            animate_scale_filter(320, 180, AnimateFit::Fit),
            "scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:(ow-iw)/2:(oh-ih)/2:color=black,format=rgba"
        );
    }

    #[test]
    fn rgba_cache_round_trip_and_prune() {
        let dir = tempfile::tempdir().unwrap();
        let a = rgba_cache_path(dir.path(), "/w/a.mp4", 10, 4, 2);
        assert_eq!(a, rgba_cache_path(dir.path(), "/w/a.mp4", 10, 4, 2), "the key is stable");
        assert_ne!(a, rgba_cache_path(dir.path(), "/w/a.mp4", 11, 4, 2), "a new mtime is a new entry");
        let pixels: Vec<u8> = (0..4 * 2 * 4).map(|i| i as u8).collect();
        rgba_write(&a, 4, 2, &pixels, u64::MAX);
        assert_eq!(rgba_read(&a), Some((4, 2, pixels.clone())));
        std::fs::write(&a, b"short").unwrap();
        assert_eq!(rgba_read(&a), None, "a truncated entry is a miss");

        let entry = 8 + 4 * 2 * 4;
        let paths: Vec<_> = (0..3).map(|i| rgba_cache_path(dir.path(), &format!("/w/{i}.mp4"), 1, 4, 2)).collect();
        for p in &paths {
            rgba_write(p, 4, 2, &pixels, u64::MAX);
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        rgba_prune(dir.path(), (entry * 2) as u64);
        assert!(!paths[0].exists() && paths[1].exists() && paths[2].exists(), "the oldest goes first");
        rgba_prune(dir.path(), 0);
        assert!(paths[2].exists(), "the newest entry is always kept");
    }

    #[test]
    fn box_downsample_averages() {
        let src = [0, 0, 0, 255, 100, 100, 100, 255, 200, 200, 200, 255, 100, 100, 100, 255];
        let mut dst = [0u8; 4];
        box_downsample_rgba(&src, 2, 2, &mut dst, 1, 1);
        assert_eq!(dst, [100, 100, 100, 255]);
        let mut same = [0u8; 16];
        box_downsample_rgba(&src, 2, 2, &mut same, 2, 2);
        assert_eq!(same, src);
    }

    #[cfg(feature = "video")]
    mod video {
        use super::super::*;
        use std::sync::Arc;
        use std::sync::atomic::{AtomicUsize, Ordering};
        use std::time::{Duration, Instant};

        /// A short `testsrc` clip, made with the ffmpeg CLI; `None` without it.
        fn clip(dir: &std::path::Path) -> Option<String> {
            let path = dir.join("clip.mkv");
            let ok = std::process::Command::new("ffmpeg")
                .args(["-v", "error", "-f", "lavfi", "-i", "testsrc=size=64x48:rate=15:duration=2", "-c:v", "ffv1", "-y"])
                .arg(&path)
                .status()
                .is_ok_and(|s| s.success());
            if !ok {
                println!("skipped: no ffmpeg CLI to make a clip");
            }
            ok.then(|| path.to_string_lossy().into_owned())
        }

        #[test]
        fn decode_frames_lavfi() {
            let dir = tempfile::tempdir().unwrap();
            let Some(path) = clip(dir.path()) else { return };
            let frames = media_decode_frames(&path, &animate_scale_filter(32, 24, AnimateFit::Crop), 2);
            assert_eq!(frames.len(), 2);
            for f in &frames {
                assert_eq!((f.width, f.height, f.rgba.len()), (32, 24, 32 * 24 * 4));
                assert!(f.rgba.iter().any(|&b| b != 0), "the frame has content");
            }
            let cache = dir.path().join("cache");
            std::fs::create_dir(&cache).unwrap();
            let first = decode_scaled_cached(&|| cache.clone(), &path, 16, 16).expect("a first frame");
            assert!(first.width >= 16 && first.height >= 16 && first.rgba.len() == (first.width * first.height * 4) as usize);
            assert_eq!(std::fs::read_dir(&cache).unwrap().count(), 1, "the frame is cached");
            assert_eq!(decode_scaled_cached(&|| cache.clone(), &path, 16, 16), Some(first), "and read back from the cache");
        }

        #[test]
        fn decode_missing_file() {
            assert!(media_decode_frames("/nonexistent/clip.mkv", "format=rgba", 1).is_empty());
            let frames = Arc::new(AtomicUsize::new(0));
            let seen = frames.clone();
            let mut playback = media_decode_stream("/nonexistent/clip.mkv", "format=rgba", 15, Box::new(move |_| {
                seen.fetch_add(1, Ordering::Relaxed);
            }), None);
            let deadline = Instant::now() + Duration::from_secs(2);
            while !playback.finished() && Instant::now() < deadline {
                std::thread::sleep(Duration::from_millis(10));
            }
            assert!(playback.finished(), "the worker ends on its own");
            assert_eq!(playback.status(), MediaDecodeStatus::Idle);
            playback.stop();
            assert_eq!(frames.load(Ordering::Relaxed), 0);
        }

        #[test]
        fn decode_pause_stops_frames() {
            let dir = tempfile::tempdir().unwrap();
            let Some(path) = clip(dir.path()) else { return };
            let frames = Arc::new(AtomicUsize::new(0));
            let seen = frames.clone();
            let playback = media_decode_stream(&path, "format=rgba", 15, Box::new(move |_| {
                seen.fetch_add(1, Ordering::Relaxed);
            }), None);
            std::thread::sleep(Duration::from_millis(400));
            assert!(frames.load(Ordering::Relaxed) > 0, "frames arrive while playing");
            assert_eq!(playback.status(), MediaDecodeStatus::CpuFallback, "no DRM sink means the CPU path");
            playback.pause();
            std::thread::sleep(Duration::from_millis(100));
            let paused_at = frames.load(Ordering::Relaxed);
            std::thread::sleep(Duration::from_millis(300));
            assert_eq!(frames.load(Ordering::Relaxed), paused_at, "no frame runs while paused");
            playback.resume();
            std::thread::sleep(Duration::from_millis(300));
            assert!(frames.load(Ordering::Relaxed) > paused_at, "frames resume");
        }
    }
}
