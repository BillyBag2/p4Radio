#pragma once
#include <cstddef>
#include <cstdint>

namespace p4radio {
// Packed-audio HLS can prefix ADTS frames with an ID3 timestamp tag.
inline constexpr bool packed_hls_aac(const uint8_t *data, size_t size, size_t &start)
{
    start = 0;
    while (size - start >= 10 && data[start] == 'I' && data[start + 1] == 'D' && data[start + 2] == '3') {
        const auto *tag = data + start;
        if ((tag[6] | tag[7] | tag[8] | tag[9]) & 0x80) return false;
        const size_t length = (size_t(tag[6]) << 21) | (size_t(tag[7]) << 14) |
                              (size_t(tag[8]) << 7) | tag[9];
        const size_t total = 10 + length + (tag[3] == 4 && (tag[5] & 0x10) ? 10 : 0);
        if (total > size - start) return false;
        start += total;
    }
    size_t offset = start;
    while (size - offset >= 7) {
        const auto *frame = data + offset;
        if (frame[0] != 0xff || (frame[1] & 0xf6) != 0xf0 || ((frame[2] >> 2) & 15) >= 13) return false;
        const size_t length = (size_t(frame[3] & 3) << 11) | (size_t(frame[4]) << 3) | (frame[5] >> 5);
        if (length < ((frame[1] & 1) ? 7u : 9u) || length > size - offset) return false;
        offset += length;
    }
    return offset == size && offset > start;
}
} // namespace p4radio
