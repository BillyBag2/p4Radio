#pragma once
#include "radio.hpp"
#include "icy_audio.hpp"
#include <string>

namespace p4radio {
// Owns a continuous HTTP stream. HLS playlists are handed back to the segment player.
class StreamInput {
public:
    ~StreamInput();
    bool open(std::string url, const Station &station, int slot);
    int read(uint8_t *buffer, size_t capacity);
    bool hls = false;
    bool mp3 = false;
    std::string url;
    std::string playlist;
private:
    static esp_err_t event(esp_http_client_event_t *event);
    void close();
    esp_http_client_handle_t client = nullptr;
    std::string content_type;
    std::string pending;
    size_t interval = 0;
    IcyAudio icy;
    int selected_slot = -1;
    uint32_t selected_id = 0;
    bool cancelled() const;
};
}
