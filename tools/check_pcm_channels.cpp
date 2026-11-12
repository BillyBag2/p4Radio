#include "pcm_channels.hpp"
#include <array>

using namespace p4radio;

constexpr bool check_downmix()
{
    // Distinct channels expose dropping one channel or corrupting unread
    // input during in-place compaction. Full-scale pairs expose overflow.
    std::array<int16_t, 12> samples{32767, 32767, -32768, -32768,
        30000, -10000, -30000, 10000, 32767, -32768, 1, 2};
    downmix_stereo_to_mono(samples.data(), 6);
    return samples[0] == 32767 && samples[1] == -32768 &&
        samples[2] == 10000 && samples[3] == -10000 &&
        samples[4] == 0 && samples[5] == 1;
}
static_assert(check_downmix());

constexpr bool check_empty_and_single()
{
    int16_t samples[]{8, -4};
    downmix_stereo_to_mono(samples, 0);
    if (samples[0] != 8 || samples[1] != -4) return false;
    downmix_stereo_to_mono(samples, 1);
    return samples[0] == 2;
}
static_assert(check_empty_and_single());

constexpr bool check_both_i2s_slots()
{
    const int16_t mono[]{32767, -32768, 0, 1234, -5678};
    std::array<int16_t, 10> stereo{};
    // Split across two writes to catch sample loss at a chunk boundary.
    duplicate_mono_to_stereo(mono, stereo.data(), 2);
    duplicate_mono_to_stereo(mono + 2, stereo.data() + 4, 3);
    for (size_t frame = 0; frame < 5; ++frame) {
        if (stereo[2 * frame] != mono[frame] || stereo[2 * frame + 1] != mono[frame]) return false;
    }
    duplicate_mono_to_stereo(mono, stereo.data(), 0);
    return stereo[0] == 32767 && stereo[9] == -5678;
}
static_assert(check_both_i2s_slots());
