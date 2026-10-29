#include "icy_audio.hpp"
constexpr bool checks()
{
    p4radio::IcyAudio icy;
    icy.reset(2);
    uint8_t first[]{1,2,1,'x','x'};
    if (icy.filter(first, sizeof(first)) != 2 || first[0] != 1 || first[1] != 2) return false;
    uint8_t middle[13]{};
    if (icy.filter(middle, sizeof(middle))) return false;
    uint8_t last[]{0,3,4,0,5,6,1}; // Last metadata byte, audio, empty metadata, audio, next tag.
    if (icy.filter(last, sizeof(last)) != 4 || last[0] != 3 || last[3] != 6) return false;
    icy.reset(0);
    return icy.filter(last, sizeof(last)) == sizeof(last);
}
static_assert(checks());
