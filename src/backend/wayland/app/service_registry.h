#pragma once

#include <memory>
#include <vector>

#include "wayland/app/service.h"

std::vector<std::unique_ptr<Service>> build_services();
