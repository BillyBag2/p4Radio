#include "hls_aac.hpp"
constexpr bool checks()
{
    size_t start = 0;
    constexpr uint8_t adts[]{0xff, 0xf1, 0x50, 0x80, 0, 0xff, 0xfc};
    constexpr uint8_t tagged[]{'I','D','3',4,0,0,0,0,0,0,0xff,0xf1,0x50,0x80,0,0xff,0xfc};
    constexpr uint8_t broken[]{'I','D','3',4,0,0,0,0,0,127};
    return p4radio::packed_hls_aac(adts, sizeof(adts), start) && start == 0 &&
           p4radio::packed_hls_aac(tagged, sizeof(tagged), start) && start == 10 &&
           !p4radio::packed_hls_aac(adts, sizeof(adts) - 1, start) &&
           !p4radio::packed_hls_aac(broken, sizeof(broken), start) &&
           !p4radio::packed_hls_aac(adts, 0, start);
}
static_assert(checks());
