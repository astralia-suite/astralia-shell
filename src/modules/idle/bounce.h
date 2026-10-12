#pragma once

namespace astralia {

struct Bounce {
    float x = 40.0f;
    float y = 40.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

void bounce_step(Bounce &bounce, float dt, float width, float height, float size);

} // namespace astralia
