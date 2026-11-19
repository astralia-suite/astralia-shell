//! The three bar styles and the island frame geometry they share.

use crate::config::bar_layout;
pub use crate::config::settings::BarStyle;
use crate::modules::bar::model::zones::{Zones, zones};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics};

/// The autohidden bar keeps a strip this tall to catch the pointer.
const AUTOHIDE_STRIP_PX: i32 = 1;

#[derive(Clone, Copy, Debug)]
pub struct StyleSpec {
    pub bg: Color,
    pub border: Color,
    pub border_width: f32,
    pub radius: f32,
    pub pad_ratio: f32,
    pub top_margin: i32,
    pub side_margin: i32,
    pub rail_height: f32,
    pub island_radius: f32,
    pub fillet_radius: f32,
    pub shadow: f32,
    pub continuous: bool,
    pub shadow_color: Color,
    pub shadow_dx: f32,
    pub zones: &'static Zones,
    pub text_lift: f32,
    pub uniform_fill: bool,
    pub workspace_badges: bool,
    pub workspace_size: f32,
    pub workspace_gap: f32,
    pub workspace_scale: f32,
    pub icon_gap: f32,
}

impl StyleSpec {
    pub fn has_rail(&self) -> bool {
        self.rail_height > 0.0
    }

    pub fn has_pill_bg(&self) -> bool {
        !self.has_rail() && !self.continuous
    }
}

/// The style's constants, colours left transparent.
fn raw(style: BarStyle) -> StyleSpec {
    let base = StyleSpec {
        bg: Color::default(),
        border: Color::default(),
        border_width: metrics::BORDER_THICK,
        radius: bar_layout::POP_RADIUS,
        pad_ratio: 0.25,
        top_margin: bar_layout::TOP_MARGIN,
        side_margin: bar_layout::SIDE_MARGIN,
        rail_height: 0.0,
        island_radius: 0.0,
        fillet_radius: 0.0,
        shadow: metrics::SHADOW_OFFSET,
        continuous: false,
        shadow_color: Color::default(),
        shadow_dx: 0.0,
        zones: zones(),
        text_lift: metrics::SHADOW_OFFSET / 2.0,
        uniform_fill: true,
        workspace_badges: false,
        workspace_size: bar_layout::WORKSPACE_PILL_HEIGHT,
        workspace_gap: bar_layout::WORKSPACE_PILL_SPACING,
        workspace_scale: bar_layout::WORKSPACE_ACTIVE_SCALE,
        icon_gap: bar_layout::POP_ICON_GAP,
    };
    match style {
        BarStyle::Okinami => StyleSpec {
            radius: 0.0,
            top_margin: 0,
            side_margin: 0,
            rail_height: 6.0,
            island_radius: metrics::RADIUS_CARD,
            fillet_radius: metrics::RADIUS_CARD,
            shadow: 0.0,
            text_lift: 0.0,
            uniform_fill: false,
            ..base
        },
        BarStyle::Continuous => StyleSpec { continuous: true, ..base },
        BarStyle::Pop => StyleSpec {
            pad_ratio: 0.325,
            side_margin: bar_layout::POP_SIDE_MARGIN,
            workspace_badges: true,
            workspace_size: bar_layout::POP_BADGE,
            workspace_gap: bar_layout::POP_BADGE_GAP,
            workspace_scale: bar_layout::POP_BADGE_ACTIVE / bar_layout::POP_BADGE,
            ..base
        },
    }
}

/// Pill styles need island support from the compositor; without it they fall back to `Continuous`.
pub fn style_resolve(requested: BarStyle, islands_supported: bool) -> BarStyle {
    if raw(requested).has_pill_bg() && !islands_supported { BarStyle::Continuous } else { requested }
}

/// The resolved style with the live palette's colours.
pub fn style_spec(style: BarStyle, islands_supported: bool) -> StyleSpec {
    let p = palette();
    StyleSpec { bg: p.accent_container, border: p.border_outer, shadow_color: p.shadow, ..raw(style_resolve(style, islands_supported)) }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct IslandSpan {
    pub left: f32,
    pub right: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct IslandShape {
    pub outer_x: f32,
    pub outer_width: f32,
    pub inner_x: f32,
    pub inner_width: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Fillet {
    pub edge_x: f32,
    pub right_of_island: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Frame {
    pub islands: Vec<IslandShape>,
    pub fillets: Vec<Fillet>,
}

/// A flush side runs past the bar edge by the island radius and has no border.
pub fn island_shape(spec: &StyleSpec, left: f32, right: f32, flush_left: bool, flush_right: bool) -> IslandShape {
    let outer_x = if flush_left { left - spec.island_radius } else { left };
    let outer_width = (if flush_right { right + spec.island_radius } else { right }) - outer_x;
    let inner_x = if flush_left { outer_x } else { outer_x + spec.border_width };
    let inner_width = outer_x + outer_width - (if flush_right { 0.0 } else { spec.border_width }) - inner_x;
    IslandShape { outer_x, outer_width, inner_x, inner_width }
}

pub fn frame(spec: &StyleSpec, left_end: Option<f32>, center: Option<IslandSpan>, right_start: Option<f32>, width: f32) -> Frame {
    let mut f = Frame::default();
    if let Some(left_end) = left_end {
        f.islands.push(island_shape(spec, 0.0, left_end, true, false));
        f.fillets.push(Fillet { edge_x: left_end, right_of_island: true });
    }
    if let Some(c) = center {
        f.islands.push(island_shape(spec, c.left, c.right, false, false));
        f.fillets.push(Fillet { edge_x: c.left, right_of_island: false });
        f.fillets.push(Fillet { edge_x: c.right, right_of_island: true });
    }
    if let Some(right_start) = right_start {
        f.islands.push(island_shape(spec, right_start, width, false, true));
        f.fillets.push(Fillet { edge_x: right_start, right_of_island: false });
    }
    f
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Geometry {
    pub height: i32,
    pub margin_top: i32,
    pub exclusive_zone: i32,
}

pub fn autohide_geometry(autohide: bool, collapsed: bool, bar_height: i32, top_margin: i32, hug_radius: i32) -> Geometry {
    if !autohide {
        Geometry { height: bar_height + hug_radius, margin_top: top_margin, exclusive_zone: bar_height }
    } else if collapsed {
        Geometry { height: AUTOHIDE_STRIP_PX, margin_top: 0, exclusive_zone: 0 }
    } else {
        Geometry { height: top_margin + bar_height + hug_radius, margin_top: 0, exclusive_zone: 0 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::tokens::metrics;

    #[test]
    fn bar_style() {
        assert_eq!(style_resolve(BarStyle::Continuous, false), BarStyle::Continuous);
        assert_eq!(style_resolve(BarStyle::Okinami, false), BarStyle::Okinami, "supported styles are kept");
        assert!(style_spec(BarStyle::Okinami, false).has_rail() && !style_spec(BarStyle::Continuous, false).has_rail(), "only okinami has a rail");
        assert!(style_spec(BarStyle::Pop, true).has_pill_bg() && !style_spec(BarStyle::Continuous, true).has_pill_bg(), "only pop has pill backgrounds");
        assert!(style_resolve(BarStyle::Pop, false) == BarStyle::Continuous && style_resolve(BarStyle::Pop, true) == BarStyle::Pop, "pop falls back to continuous when unsupported");
        let pop = style_spec(BarStyle::Pop, true);
        assert!(pop.has_pill_bg() && !pop.has_rail() && !pop.continuous && pop.shadow_dx == 0.0, "pop is a pill style with a straight shadow");

        let shown = autohide_geometry(false, false, 40, 10, 0);
        assert!(shown.height == 40 && shown.margin_top == 10 && shown.exclusive_zone == 40, "autohide off keeps the bar shown");
        for pill in [BarStyle::Continuous, BarStyle::Pop] {
            let spec = style_spec(pill, true);
            assert!(spec.shadow == metrics::SHADOW_OFFSET && spec.shadow_dx == 0.0 && spec.radius == bar_layout::POP_RADIUS, "continuous takes the straight shadow and corner radius of pop");
        }
        let okinami = style_spec(BarStyle::Okinami, true);
        assert!(okinami.island_radius == metrics::RADIUS_CARD && okinami.fillet_radius == metrics::RADIUS_CARD && okinami.shadow == 0.0, "okinami uses the card radius and no shadow");
        let hugged = autohide_geometry(false, false, 40, 10, 30);
        assert!(hugged.height == 70 && hugged.exclusive_zone == 40, "hug radius grows the surface only");
        let revealed = autohide_geometry(true, false, 40, 10, 30);
        assert!(revealed.height == 80 && revealed.margin_top == 0 && revealed.exclusive_zone == 0, "revealed autohide reserves nothing");
        let collapsed = autohide_geometry(true, true, 40, 10, 30);
        assert!(collapsed.height == 1 && collapsed.margin_top == 0, "collapsed autohide is a strip");

        let okinami = style_spec(BarStyle::Okinami, false);
        let f = frame(&okinami, Some(300.0), Some(IslandSpan { left: 500.0, right: 700.0 }), Some(900.0), 1000.0);
        assert!(f.islands.len() == 3 && f.fillets.len() == 4, "three islands and four fillets");
        assert!(f.islands[0].outer_x == -okinami.island_radius && f.islands[2].outer_x + f.islands[2].outer_width == 1000.0 + okinami.island_radius, "flush islands extend past the edges");
        assert_eq!(f.islands[1].inner_x, 500.0 + okinami.border_width, "inner edge is inset by the border");
        let none = frame(&okinami, None, None, None, 400.0);
        assert!(none.islands.is_empty() && none.fillets.is_empty(), "no groups, no islands");
    }
}
