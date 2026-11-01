#pragma once
#include <cstddef>
#include <cstdint>
namespace p4radio {
// Remove ICY metadata in place, retaining framing across arbitrary HTTP chunks.
class IcyAudio {
public:
    constexpr void reset(size_t interval) { every = left = interval; metadata = 0; }
    constexpr size_t filter(uint8_t *data, size_t size)
    {
        if (!every) return size;
        size_t out = 0;
        for (size_t i = 0; i < size; ++i) {
            if (metadata) { if (!--metadata) left = every; }
            else if (!left) { metadata = size_t(data[i]) * 16; if (!metadata) left = every; }
            else { data[out++] = data[i]; --left; }
        }
        return out;
    }
private:
    size_t every = 0, left = 0, metadata = 0;
};
}
