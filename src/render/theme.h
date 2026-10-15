#pragma once

#include "render/palette_extract.h"
#include "render/tokens.h"

namespace astralia {

enum class Scheme { light };

struct Theme {
    Scheme scheme;

    Color ink;
    Color paper;
    Color surface;
    Color surface_alt;
    Color shadow;

    Color accent;
    Color accent_alpha12;
    Color accent_alpha19;
    Color accent_alpha25;
    Color accent_container;

    Color accent_alt;
    Color accent_alt_alpha50;
    Color accent_alt_container;

    Color pop_a;
    Color pop_b;
    Color pop_c;

    Color base;
    Color overlay;

    Color lavender;

    Color field_bg;

    Color text;
    Color text_alpha04;
    Color text_alpha06;
    Color text_alpha08;
    Color text_alpha11;
    Color text_alpha15;
    Color text_alpha20;
    Color text_alpha65;
    Color text_muted;
    Color text_dim;

    Color electro;

    Color critical;
    Color critical_alpha15;
    Color critical_container;

    Color warn;

    Color window_backdrop;
};

Theme theme_from_seed(const Extraction &extraction);
void theme_apply(const Theme &theme);

} // namespace astralia
