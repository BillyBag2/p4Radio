#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace p4radio {
// Positive ppm consumes input faster; negative ppm stretches output slightly.
class BufferRateController {
public:
    void reset(float target) { filtered_ = target; ppm_ = 0; }
    int update(float seconds, float target) {
        filtered_ += 0.1f * (seconds - filtered_);
        const float error = filtered_ - target;
        const float outside = error > 1.0f ? error - 1.0f : error < -1.0f ? error + 1.0f : 0.0f;
        const int desired = std::clamp(static_cast<int>(outside * 200.0f), -1000, 1000);
        ppm_ += std::clamp(desired - ppm_, -50, 50);
        return ppm_;
    }
    int ppm() const { return ppm_; }
private:
    float filtered_ = 10.0f;
    int ppm_ = 0;
};

// Streaming linear interpolation, preserving fractional phase across blocks.
// Input/output are interleaved 16-bit mono or stereo. Caller provides at least
// input_frames * 1.002 + 2 output frames for the bounded +/-1000 ppm range.
class PcmRateMatcher {
public:
    void reset() { phase_ = 0; initialized_ = false; }
    size_t process(const int16_t *input, size_t frames, size_t channels,
                   int ppm, int16_t *output) {
        if (!frames || !channels || channels > 2) return 0;
        if (!initialized_) {
            for (size_t ch = 0; ch < channels; ++ch) previous_[ch] = input[ch];
            initialized_ = true;
        }
        constexpr uint64_t unit = 1ULL << 32;
        const uint64_t step = static_cast<uint64_t>(static_cast<int64_t>(unit) +
            static_cast<int64_t>(unit) * std::clamp(ppm, -1000, 1000) / 1000000);
        size_t produced = 0;
        while (phase_ < frames * unit) {
            const size_t index = phase_ >> 32;
            const uint32_t fraction = static_cast<uint32_t>(phase_);
            for (size_t ch = 0; ch < channels; ++ch) {
                const int32_t left = index == 0 ? previous_[ch] : input[(index - 1) * channels + ch];
                const int32_t right = input[index * channels + ch];
                output[produced * channels + ch] = static_cast<int16_t>(left +
                    static_cast<int64_t>(right - left) * fraction / static_cast<int64_t>(unit));
            }
            ++produced;
            phase_ += step;
        }
        phase_ -= frames * unit;
        for (size_t ch = 0; ch < channels; ++ch) previous_[ch] = input[(frames - 1) * channels + ch];
        return produced;
    }
private:
    uint64_t phase_ = 0;
    int16_t previous_[2] = {};
    bool initialized_ = false;
};
}
