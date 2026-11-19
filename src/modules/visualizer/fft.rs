//! Radix-2 DIT FFT adapted from GLava by jarcode-foss, licensed under GPL-3.0.

// The truncated value is part of the reference spectrum.
#[allow(clippy::approx_constant)]
const TWO_PI: f64 = 6.28318530718;

fn hann(t: f64, sz: f64) -> f64 {
    0.53836 - 0.46164 * (TWO_PI * t / sz).cos()
}

/// In-place spectrum of interleaved complex samples: windowed, transformed,
/// then each pair holds the tilted log magnitude twice.
pub fn fft(data: &mut [f32], fft_scale: f32, fft_cutoff: f32) {
    let n_samples = data.len();
    if n_samples < 2 {
        return;
    }
    let nn = n_samples / 2;
    for (i, v) in data.iter_mut().enumerate() {
        *v *= hann(i as f64, (n_samples - 1) as f64) as f32;
    }

    let n = nn << 1;
    let mut j = 1usize;
    let mut i = 1usize;
    while i < n {
        if j > i {
            data.swap(j - 1, i - 1);
            data.swap(j, i);
        }
        let mut m = nn;
        while m >= 2 && j > m {
            j -= m;
            m >>= 1;
        }
        j += m;
        i += 2;
    }

    let mut mmax = 2usize;
    while n > mmax {
        let istep = mmax << 1;
        let theta = -(2.0 * std::f64::consts::PI / mmax as f64) as f32;
        let mut wtemp = (0.5f32 * theta).sin();
        let wpr = -2.0f32 * wtemp * wtemp;
        let wpi = theta.sin();
        let mut wr = 1.0f32;
        let mut wi = 0.0f32;
        let mut m = 1usize;
        while m < mmax {
            let mut i = m;
            while i <= n {
                let j = i + mmax;
                let tempr = wr * data[j - 1] - wi * data[j];
                let tempi = wr * data[j] + wi * data[j - 1];
                data[j - 1] = data[i - 1] - tempr;
                data[j] = data[i] - tempi;
                data[i - 1] += tempr;
                data[i] += tempi;
                i += istep;
            }
            wtemp = wr;
            wr += wr * wpr - wi * wpi;
            wi += wi * wpr + wtemp * wpi;
            m += 2;
        }
        mmax = istep;
    }

    let mut n = 0usize;
    while n + 1 < n_samples {
        if data[n] < 0.0 {
            data[n] = -data[n];
        }
        data[n] = (data[n] * data[n] + data[n + 1] * data[n + 1]).sqrt();
        data[n] = (data[n] + 1.0).ln() / 3.0;
        data[n] *= ((n as f32 / n_samples as f32 * fft_scale) + (1.0 - fft_cutoff)).max(1.0);
        data[n + 1] = data[n];
        n += 2;
    }
}

#[cfg(test)]
mod tests {
    use super::fft;
    use crate::config::visualizer_config::{FFT_CUT_OFF, FFT_SCALE, FRAGMENT_SIZE};

    fn peak_bin(data: &[f32], complex_points: usize) -> usize {
        let mut best = 1;
        let mut best_v = -1.0f32;
        for k in 1..complex_points / 2 {
            if data[2 * k] > best_v {
                best_v = data[2 * k];
                best = k;
            }
        }
        best
    }

    #[test]
    fn check_visualizer_fft() {
        let n = FRAGMENT_SIZE;
        let complex_points = n / 2;
        let mut silence = vec![0.0f32; n];
        fft(&mut silence, FFT_SCALE, FFT_CUT_OFF);
        assert!(silence.iter().all(|&v| v == 0.0), "silence stays silent");

        let tone_bin = 64usize;
        let mut tone = vec![0.0f32; n];
        for i in 0..complex_points {
            tone[2 * i] = (2.0 * std::f32::consts::PI * tone_bin as f32 * i as f32 / complex_points as f32).cos();
        }
        fft(&mut tone, FFT_SCALE, FFT_CUT_OFF);
        let p = peak_bin(&tone, complex_points);
        assert!(p.abs_diff(tone_bin) <= 2, "a tone peaks at its bin");
        let at_tone = tone[2 * tone_bin];
        let far = tone[2 * (complex_points / 2 - 4)];
        assert!(at_tone > far * 4.0, "the tone stands out from the far bins");
    }
}
