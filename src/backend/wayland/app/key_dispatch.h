#pragma once

#include <vector>

#include "service/wayland/input_service.h"

struct WaylandState;

void dispatch_key_events(WaylandState &state, const std::vector<KeyEvent> &events);
