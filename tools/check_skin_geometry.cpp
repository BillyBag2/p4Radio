#include "skin_geometry.hpp"

using namespace p4radio;

static_assert(skin_side(720, 720) == 720);
static_assert(skin_side(1280, 720) == 720);
static_assert(skin_side(720, 1280) == 720);
static_assert(skin_side(800, 480) == 480);
static_assert(skin_coordinate(720, 480) == 480);
static_assert(skin_coordinate(-60, 480) == -40);
static_assert(skin_coordinate(52, 720) == 52);
static_assert(skin_coordinate(360, 1080) == 540);
static_assert(skin_layout_side(360, 360, true, false) == 254);
static_assert(skin_layout_side(360, 360, true, true) == 360);
static_assert(skin_layout_side(1280, 720, false, false) == 720);
static_assert(skin_drawing_pool_bytes(720, 720) == 3 * 1024 * 1024);
static_assert(skin_drawing_pool_bytes(360, 360) == 768 * 1024);
static_assert(skin_font_size(24, 720) == 24);
static_assert(skin_font_size(40, 720) == 40);
static_assert(skin_font_size(48, 720) == 48);
static_assert(skin_font_size(24, 360) == 12);
static_assert(skin_font_size(40, 360) == 20);
static_assert(skin_font_size(48, 360) == 24);
static_assert(skin_font_size(24, 254) == 8);
static_assert(skin_font_size(40, 254) == 14);
static_assert(skin_font_size(48, 254) == 16);

// TAB5 landscape: a centered 720-pixel circle starts at x=280. Corners and
// unused screen margins must not become controls or dismiss/wake targets.
static_assert(skin_circle_contains(640, 360, 280, 0, 720));
static_assert(skin_circle_contains(280, 360, 280, 0, 720));
static_assert(!skin_circle_contains(280, 0, 280, 0, 720));
static_assert(!skin_circle_contains(279, 360, 280, 0, 720));
static_assert(!skin_circle_contains(1000, 360, 280, 0, 720));
static_assert(skin_circle_contains(400, 240, 160, 0, 480));
static_assert(!skin_circle_contains(160, 0, 160, 0, 480));
