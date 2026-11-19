//! Seedable per-thread generator for the sims.

use std::cell::Cell;

thread_local! {
    static STATE: Cell<u64> = Cell::new(
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0x9E37_79B9, |d| d.as_nanos() as u64),
    );
}

pub fn seed(seed: u32) {
    STATE.with(|s| s.set(u64::from(seed)));
}

fn next() -> u64 {
    STATE.with(|s| {
        let x = s.get().wrapping_add(0x9E37_79B9_7F4A_7C15);
        s.set(x);
        let mut z = x;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    })
}

/// Uniform in `[0, 1)`.
pub fn random01() -> f32 {
    (next() >> 40) as f32 / (1u64 << 24) as f32
}

pub fn random_range(lo: f32, hi: f32) -> f32 {
    lo + (hi - lo) * random01()
}
