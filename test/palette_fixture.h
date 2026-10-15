#pragma once

#include "render/theme.h"

namespace test {

inline astralia::Extraction base_extraction() { return {{{195.0f, 0.09f, 0.75f}}, true}; }

inline void apply_base_theme() { astralia::theme_apply(astralia::theme_from_seed(base_extraction())); }

} // namespace test
