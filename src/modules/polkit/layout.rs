//! Polkit card geometry.

use crate::config::polkit_config as cfg;

pub fn card_height(show_info: bool) -> f64 {
    let height = cfg::CARD_PAD * 2.0 + cfg::TITLE_LINE_HEIGHT + cfg::SPACING + cfg::MESSAGE_LINE_HEIGHT + cfg::SPACING + cfg::FIELD_HEIGHT;
    if show_info { height + cfg::SPACING + cfg::INFO_LINE_HEIGHT } else { height }
}

/// How many password dots fit in `width`.
pub fn visible_dots(length: usize, width: f64) -> usize {
    length.min((width.max(0.0) / f64::from(cfg::DOT_SIZE)) as usize)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::text_field::text_field_utf8_len;

    #[test]
    fn check_polkit_layout() {
        assert_eq!(text_field_utf8_len(""), 0, "empty utf8 length");
        assert_eq!(text_field_utf8_len("pass"), 4, "ascii utf8 length");
        assert_eq!(text_field_utf8_len("mật"), 3, "multibyte utf8 length");
        assert_eq!(card_height(false), 183.0, "card height without info");
        assert_eq!(card_height(true), 213.0, "card height with info");
        assert_eq!(visible_dots(5, 404.0), 5, "dots fit");
        assert_eq!(visible_dots(40, 404.0), 25, "dots clamp to width");
        assert_eq!(visible_dots(3, -1.0), 0, "no width shows no dots");
    }
}
