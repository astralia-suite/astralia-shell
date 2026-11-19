#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Bounce {
    pub x: f32,
    pub y: f32,
    pub vx: f32,
    pub vy: f32,
}

impl Default for Bounce {
    fn default() -> Self {
        Bounce { x: 40.0, y: 40.0, vx: 0.0, vy: 0.0 }
    }
}

/// Moves the logo by `dt` seconds and reflects it off the area's edges.
pub fn bounce_step(bounce: &mut Bounce, dt: f32, width: f32, height: f32, size: f32) {
    bounce.x += bounce.vx * dt;
    bounce.y += bounce.vy * dt;
    let (max_x, max_y) = (width - size, height - size);
    if bounce.x < 0.0 {
        bounce.x = 0.0;
        bounce.vx = -bounce.vx;
    } else if bounce.x > max_x {
        bounce.x = max_x;
        bounce.vx = -bounce.vx;
    }
    if bounce.y < 0.0 {
        bounce.y = 0.0;
        bounce.vy = -bounce.vy;
    } else if bounce.y > max_y {
        bounce.y = max_y;
        bounce.vy = -bounce.vy;
    }
}
