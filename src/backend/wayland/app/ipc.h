#pragma once

#include <vector>

#include "app/shell.h"

struct WaylandState;

std::vector<astralia::ShellBinding> collect_shell_bindings(WaylandState &state);
