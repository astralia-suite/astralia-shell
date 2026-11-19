//! Bar layout over the smoothed spectrum.

use crate::config::visualizer_config::{BAR_HEIGHT_RATIO, BAR_MIN_HEIGHT, BAR_SPACING, BAR_WIDTH};

pub fn bar_count(width: f32) -> i32 {
    let slot = BAR_WIDTH + BAR_SPACING;
    (((width - BAR_SPACING) / slot) as i32).max(1)
}

pub fn bar_texel(index: i32, count: i32, size: i32) -> i32 {
    let position = (index as f32 + 0.5) / count.max(1) as f32;
    let coordinate = position.max(1e-4).powf(1.8) * 0.5;
    ((coordinate * size as f32).floor() as i32).clamp(0, (size - 1).max(0))
}

pub fn bar_height(magnitude: f32, area_height: f32) -> f32 {
    BAR_MIN_HEIGHT.max(magnitude.clamp(0.0, 1.0) * BAR_HEIGHT_RATIO * area_height)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::visualizer_config::{BAR_HEIGHT_RATIO, BAR_MIN_HEIGHT, BAR_SPACING, BAR_WIDTH};

    #[test]
    fn check_visualizer_bars() {
        assert!(
            bar_count(0.0) == 1 && bar_count(1000.0) == ((1000.0 - BAR_SPACING) / (BAR_WIDTH + BAR_SPACING)) as i32,
            "the bar count follows the slot width and never drops below one"
        );
        let mut last = -1;
        let mut rising = true;
        for i in 0..40 {
            let texel = bar_texel(i, 40, 4096);
            rising = rising && texel >= last && (0..4096).contains(&texel);
            last = texel;
        }
        assert!(rising && last < 4096 / 2, "bars sample the lower half of the spectrum, rising");
        assert_eq!(bar_texel(0, 0, 0), 0, "an empty spectrum maps to the first texel");
        assert!(
            bar_height(0.0, 800.0) == BAR_MIN_HEIGHT && bar_height(2.0, 800.0) == BAR_HEIGHT_RATIO * 800.0,
            "a bar has a minimum height and clamps at the ratio"
        );
    }
}
