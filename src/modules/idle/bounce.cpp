#include "modules/idle/bounce.h"

namespace astralia::idle {

void bounce_step(Bounce &bounce, float dt, float width, float height, float size) {
    bounce.x += bounce.vx * dt;
    bounce.y += bounce.vy * dt;
    float max_x = width - size;
    float max_y = height - size;
    if (bounce.x < 0.0f) {
        bounce.x = 0.0f;
        bounce.vx = -bounce.vx;
    } else if (bounce.x > max_x) {
        bounce.x = max_x;
        bounce.vx = -bounce.vx;
    }
    if (bounce.y < 0.0f) {
        bounce.y = 0.0f;
        bounce.vy = -bounce.vy;
    } else if (bounce.y > max_y) {
        bounce.y = max_y;
        bounce.vy = -bounce.vy;
    }
}

} // namespace astralia::idle
