#pragma once

#include <cstdint>

namespace p4radio {

// Layout geometry and typography use the original 720-pixel square as reference.
constexpr int skin_coordinate(int value, int side, int design_side = 720)
{
    const int64_t product = static_cast<int64_t>(value) * side;
    return static_cast<int>((product + (product < 0 ? -design_side / 2 : design_side / 2)) / design_side);
}

constexpr int skin_side(int width, int height) { return width < height ? width : height; }

inline constexpr int kSkinFontSizes[] = {8, 12, 14, 16, 20, 24, 40, 48};

constexpr int skin_font_size(int design_pixels, int side)
{
    // Compare before rounding so small inscribed layouts choose the closest
    // compiled face to the true ratio, rather than to an intermediate integer.
    const int64_t target = static_cast<int64_t>(design_pixels) * side;
    int best = kSkinFontSizes[0];
    int64_t distance = target - static_cast<int64_t>(best) * 720;
    if (distance < 0) distance = -distance;
    for (int size : kSkinFontSizes) {
        int64_t candidate = target - static_cast<int64_t>(size) * 720;
        if (candidate < 0) candidate = -candidate;
        if (candidate < distance) {
            best = size;
            distance = candidate;
        }
    }
    return best;
}

constexpr int skin_layout_side(int width, int height, bool round_panel, bool circular_skin)
{
    const int side = skin_side(width, height);
    // Inscribe square skins in round glass, leaving their corners visible.
    return round_panel && !circular_skin ? static_cast<int>(side * 707106LL / 1000000) : side;
}

constexpr uint32_t skin_drawing_pool_bytes(int width, int height)
{
    const int side = skin_side(width, height) < 720 ? skin_side(width, height) : 720;
    return static_cast<uint32_t>(3LL * 1024 * 1024 * side * side / (720 * 720));
}

constexpr bool skin_circle_contains(int x, int y, int left, int top, int side)
{
    const int64_t dx = 2LL * (x - left) - (side - 1);
    const int64_t dy = 2LL * (y - top) - (side - 1);
    return dx * dx + dy * dy <= static_cast<int64_t>(side) * side;
}

} // namespace p4radio
