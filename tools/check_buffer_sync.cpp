#include "buffer_sync.hpp"

using namespace p4radio;

// Exercise the actual streaming resampler with successive PCM blocks. No
// duplicate model: these checks evaluate the firmware's process() directly.
constexpr size_t matched_frames(int ppm, size_t frames, size_t channels, size_t block)
{
    PcmRateMatcher matcher;
    matcher.reset();
    int16_t input[256 * 2]{};
    int16_t output[260 * 2]{};
    size_t produced = 0;
    while (frames) {
        const size_t count = std::min(frames, block);
        produced += matcher.process(input, count, channels, ppm, output);
        frames -= count;
    }
    return produced;
}

// One second of 48 kHz input: positive correction sends fewer output frames,
// negative correction sends more, with the DAC clock remaining fixed.
static_assert(matched_frames(0, 48000, 1, 256) == 48000);
static_assert(matched_frames(1000, 48000, 1, 256) == 47953);
static_assert(matched_frames(-1000, 48000, 1, 256) == 48049);
// Mono/stereo and differently sized blocks preserve the same fractional phase.
static_assert(matched_frames(1000, 4800, 2, 127) == matched_frames(1000, 4800, 1, 256));
static_assert(matched_frames(-1000, 4800, 2, 127) == matched_frames(-1000, 4800, 1, 256));
static_assert(matched_frames(5000, 4800, 1, 256) == matched_frames(1000, 4800, 1, 256));
static_assert(matched_frames(-5000, 4800, 1, 256) == matched_frames(-1000, 4800, 1, 256));

constexpr bool reset_matches_fresh()
{
    PcmRateMatcher used, fresh;
    int16_t input[8] = {0, 100, 200, 300, 400, 500, 600, 700};
    int16_t discarded[12]{}, actual[12]{}, expected[12]{};
    used.process(input, 8, 1, -1000, discarded);
    used.reset();
    const size_t count = used.process(input, 8, 1, 1000, actual);
    if (count != fresh.process(input, 8, 1, 1000, expected)) return false;
    for (size_t i = 0; i < count; ++i)
        if (actual[i] != expected[i]) return false;
    return true;
}
static_assert(reset_matches_fresh());
