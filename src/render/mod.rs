//! Backend-neutral drawing core: colours, theme, canvas and the widgets drawn through it.

pub mod app_fonts;
pub mod canvas;
pub mod decode;
pub mod field_view;
pub mod geometry;
pub mod glyphs;
pub mod icons;
pub mod marquee;
pub mod oklab;
pub mod palette_extract;
#[cfg(test)]
pub mod recording_canvas;
pub mod scroll;
pub mod scroll_sequence;
pub mod spread_map;
pub mod text_field;
pub mod sticker;
pub mod theme;
pub mod tokens;
