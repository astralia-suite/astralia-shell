//! Where the logout buttons sit on their ring.

use crate::config::logout_config as cfg;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

pub fn button_center(index: usize, center: Point) -> Point {
    let angle = cfg::START_ANGLE + cfg::STEP_ANGLE * index as f64;
    Point { x: center.x + cfg::RING_RADIUS * angle.cos(), y: center.y + cfg::RING_RADIUS * angle.sin() }
}

pub fn button_at(pointer: Point, center: Point) -> Option<usize> {
    let half = cfg::BUTTON_SIZE / 2.0;
    (0..cfg::BUTTON_COUNT as usize).find(|&i| {
        let c = button_center(i, center);
        pointer.x >= c.x - half && pointer.x < c.x + half && pointer.y >= c.y - half && pointer.y < c.y + half
    })
}
