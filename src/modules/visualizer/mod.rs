//! Audio visualizer: spectrum, smoothing stages and bar layout.

pub mod audio_capture;
pub mod bars;
#[cfg(test)]
pub mod cpu_stages;
pub mod fft;
pub mod wayland;
