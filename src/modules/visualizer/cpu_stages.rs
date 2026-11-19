//! CPU reference of the GL gravity, averaging and smoothing stages.

use crate::config::visualizer_config::{
    ADJACENT_SAMPLE_NUMS, GRAVITY_AVERAGE_FRAMES, GRAVITY_STEP, SAMPLE_HYBRID_WEIGHT, SAMPLE_MODE, SAMPLE_RANGE,
    SAMPLE_SCALE, SMOOTH_FACTOR,
};
use std::f32::consts::PI;

const RING_SIZE: usize = 5;
const MAX_SMOOTH_ITERATIONS: i32 = 512;

fn clamp01(value: f32) -> f32 {
    value.clamp(0.0, 1.0)
}

fn hamming(t: f32, size: f32) -> f32 {
    0.53836 - 0.46164 * (2.0 * PI * t / size).cos()
}

fn sinusoidal(x: f32) -> f32 {
    0.5 * (PI * x - PI / 2.0).sin() + 0.5
}

fn scale_audio(index: f32) -> f32 {
    -(-SAMPLE_RANGE * index + 1.0).ln() / SAMPLE_SCALE
}

fn fetch(source: &[f32], s: f32) -> f32 {
    let coordinate = (s + 0.5).floor() as i32;
    source[coordinate.clamp(0, source.len() as i32 - 1) as usize]
}

fn smooth_at(source: &[f32], u: f32) -> f32 {
    let size = source.len() as f32;
    let smin = scale_audio(clamp01(u - SMOOTH_FACTOR)) * size;
    let smax = scale_audio(clamp01(u + SMOOTH_FACTOR)) * size;
    let half = (smax - smin) / 2.0;
    let middle = smin + half;
    let mut total = 0.0f32;
    let mut weight = 0.0f32;
    let mut peak = 0.0f32;
    for n in 0..MAX_SMOOTH_ITERATIONS {
        let s = smin + n as f32;
        if s > smax {
            break;
        }
        let weight_of_distance = if half > 0.0 { sinusoidal(clamp01((half - (middle - s).abs()) / half)) } else { 0.0 };
        let value = fetch(source, s);
        weight += weight_of_distance;
        total += value * weight_of_distance;
        peak = peak.max(value * weight_of_distance);
    }
    let average = if weight > 0.0 { total / weight } else { 0.0 };
    let result = match SAMPLE_MODE {
        0 => average,
        1 => peak,
        _ => peak * (1.0 - SAMPLE_HYBRID_WEIGHT) + average * SAMPLE_HYBRID_WEIGHT,
    };
    result / (2.0 * (ADJACENT_SAMPLE_NUMS as f32 - 1.0) + 1.0)
}

#[derive(Default)]
struct Channel {
    held: Vec<f32>,
    ring: [Vec<f32>; RING_SIZE],
    average: Vec<f32>,
    ring_index: usize,
}

impl Channel {
    fn run(&mut self, size: usize, raw: &[f32], fps: i32) {
        if self.held.len() != size {
            self.held = vec![0.0; size];
            for frame in &mut self.ring {
                *frame = vec![0.0; size];
            }
            self.average = vec![0.0; size];
            self.ring_index = 0;
        }
        let drop = GRAVITY_STEP / fps as f32;
        for (held, &raw) in self.held.iter_mut().zip(raw) {
            *held = clamp01(held.max(clamp01(raw)) - drop);
        }
        self.ring[self.ring_index].copy_from_slice(&self.held);
        for i in 0..size {
            let mut sum = 0.0f32;
            for t in 0..RING_SIZE {
                let frame = (self.ring_index + RING_SIZE - t) % RING_SIZE;
                sum += hamming(t as f32, (GRAVITY_AVERAGE_FRAMES - 1) as f32) * self.ring[frame][i];
            }
            self.average[i] = clamp01(sum / GRAVITY_AVERAGE_FRAMES as f32);
        }
        self.ring_index = (self.ring_index + 1) % RING_SIZE;
    }
}

#[derive(Default)]
pub struct CpuStages {
    channels: [Channel; 2],
    size: i32,
}

impl CpuStages {
    pub fn run(&mut self, size: i32, left: &[f32], right: &[f32], fps: i32) -> bool {
        if size <= 0 || fps <= 0 || (left.len() as i32) < size || (right.len() as i32) < size {
            return false;
        }
        self.size = size;
        self.channels[0].run(size as usize, left, fps);
        self.channels[1].run(size as usize, right, fps);
        true
    }

    pub fn smooth_texel(&self, channel: i32, index: i32) -> f32 {
        let average = &self.channels[channel.clamp(0, 1) as usize].average;
        if average.is_empty() {
            return 0.0;
        }
        let u = (index.clamp(0, self.size - 1) as f32 + 0.5) / self.size as f32;
        clamp01(smooth_at(average, u))
    }

    pub fn size(&self) -> i32 {
        self.size
    }

    pub fn ready(&self) -> bool {
        self.size > 0
    }
}

#[cfg(test)]
mod tests {
    use super::CpuStages;

    #[test]
    fn check_visualizer_cpu_stages() {
        let mut stages = CpuStages::default();
        assert!(!stages.ready() && !stages.run(0, &[], &[], 60), "nothing runs without samples");
        let small = vec![0.5f32; 8];
        assert!(!stages.run(16, &small, &small, 60), "a short input is refused");

        let size = 256;
        let loud = vec![0.8f32; size];
        let quiet = vec![0.0f32; size];
        assert!(stages.run(size as i32, &loud, &quiet, 60) && stages.ready() && stages.size() == size as i32, "a full input runs");
        let q = size as i32 / 4;
        let first = stages.smooth_texel(0, q);
        assert!(first > 0.0 && first < 0.8, "the newest frame carries the smallest weight");
        assert_eq!(stages.smooth_texel(1, q), 0.0, "a silent channel stays at zero");

        let mut previous = first;
        let mut rose = false;
        for _ in 0..4 {
            stages.run(size as i32, &loud, &quiet, 60);
            let current = stages.smooth_texel(0, q);
            rose = rose || current > previous;
            previous = current;
        }
        assert!(rose, "a held tone builds up through the averaging window");

        let peak = previous;
        for _ in 0..80 {
            stages.run(size as i32, &quiet, &quiet, 60);
        }
        assert!(stages.smooth_texel(0, q) == 0.0 && peak > 0.0, "gravity pulls a silent input back to zero");

        stages.run(size as i32, &loud, &loud, 60);
        assert!(
            stages.smooth_texel(-5, 0) == stages.smooth_texel(0, 0)
                && stages.smooth_texel(9, size as i32 + 10) == stages.smooth_texel(1, size as i32 - 1),
            "channel and index clamp into range"
        );
    }
}
