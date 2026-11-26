//! Icon choice by level.

use super::icons;

pub fn volume_threshold(muted: bool, percent: i32) -> &'static str {
    if muted {
        icons::VOLUME_MUTE
    } else if percent < 1 {
        icons::VOLUME_EMPTY
    } else if percent < 50 {
        icons::VOLUME_LOW
    } else {
        icons::VOLUME_HIGH
    }
}

pub fn brightness_threshold(percent: i32) -> &'static str {
    if percent < 50 { icons::BRIGHTNESS_DOWN } else { icons::BRIGHTNESS_UP }
}

/// A `0..=1` level as a whole percent, nudged so `0.01` reads as 1.
#[cfg(test)]
pub fn level_percent(level: f32) -> i32 {
    (level * 100.0 + 1e-4) as i32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn glyph_thresholds() {
        assert_eq!(volume_threshold(true, 80), icons::VOLUME_MUTE);
        assert_eq!(volume_threshold(false, 0), icons::VOLUME_EMPTY);
        assert_eq!(volume_threshold(false, 1), icons::VOLUME_LOW);
        assert_eq!(volume_threshold(false, 49), icons::VOLUME_LOW);
        assert_eq!(volume_threshold(false, 50), icons::VOLUME_HIGH);
        assert_eq!(brightness_threshold(49), icons::BRIGHTNESS_DOWN);
        assert_eq!(brightness_threshold(50), icons::BRIGHTNESS_UP);
        assert!(level_percent(0.5) == 50 && level_percent(0.01) == 1 && level_percent(0.009) == 0);
    }
}
