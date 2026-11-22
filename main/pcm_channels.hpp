#pragma once
#include <cstddef>
#include <cstdint>

namespace p4radio {
// Average in a wide accumulator before narrowing. Writing at half the input
// stride makes this safe in-place without a second PCM allocation.
constexpr void downmix_stereo_to_mono(int16_t *samples, size_t frames)
{
    for (size_t frame = 0; frame < frames; ++frame) {
        const int32_t sum = static_cast<int32_t>(samples[2 * frame]) + samples[2 * frame + 1];
        samples[frame] = static_cast<int16_t>(sum / 2);
    }
}

// Separate, non-overlapping buffers. Keep the queue mono and expand only at
// the I2S boundary so an amplifier strapped to either slot hears the mix.
constexpr void duplicate_mono_to_stereo(const int16_t *mono, int16_t *stereo, size_t frames)
{
    for (size_t frame = 0; frame < frames; ++frame) {
        stereo[2 * frame] = mono[frame];
        stereo[2 * frame + 1] = mono[frame];
    }
}
} // namespace p4radio
