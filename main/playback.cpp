#include "radio.hpp"
#include "button_audio.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "driver/gpio.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "nvs.h"

namespace p4radio {

std::atomic<int> g_buffer_percent{0};

bool open_speaker(const esp_audio_simple_dec_info_t &info)
{
    if (!g_speaker) {
        g_speaker = bsp_audio_codec_speaker_init();
        if (!g_speaker) return false;
    }
    esp_codec_dev_sample_info_t sample{};
    sample.sample_rate = info.sample_rate;
    sample.channel = info.channel;
    sample.bits_per_sample = info.bits_per_sample;
    sample.channel_mask = 0;
    lock_speaker();
    esp_codec_dev_close(g_speaker);
    if (esp_codec_dev_open(g_speaker, &sample) != ESP_CODEC_DEV_OK) {
        unlock_speaker();
        ESP_LOGE(kTag, "ES8311 open failed for %lu Hz", static_cast<unsigned long>(info.sample_rate));
        return false;
    }
    esp_codec_dev_set_out_vol(g_speaker, g_volume.load() * 10);
    set_button_audio_format(info.sample_rate, info.channel, info.bits_per_sample);
    unlock_speaker();
    return true;
}

// One producer (network/decoder), one consumer (speaker). PCM lives in PSRAM.
struct AudioBuffer {
    StreamBufferHandle_t stream = nullptr;
    StaticStreamBuffer_t stream_state{};
    StaticSemaphore_t done_state{};
    SemaphoreHandle_t done = nullptr;
    uint8_t *storage = nullptr;
    size_t bytes_per_second = 0;
    size_t frame_bytes = 0;
    int station = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> error{false};
    std::atomic<uint32_t> played_ms{0};
    std::atomic<uint32_t> underruns{0};
    std::atomic<TickType_t> last_output{0};
};

bool audio_cancelled(const AudioBuffer &buffer)
{
    return buffer.stop.load() || g_scan_requested.load() || g_sleep_requested.load() ||
        !g_playback_requested.load() || g_station.load() != buffer.station;
}

void audio_output_task(void *argument)
{
    auto &buffer = *static_cast<AudioBuffer *>(argument);
    uint8_t pcm[4096];
    bool priming = true;
    size_t threshold = buffer.bytes_per_second / 2; // Fast start: 0.5 seconds.
    TickType_t last_log = xTaskGetTickCount();
    while (!audio_cancelled(buffer)) {
        const size_t available = xStreamBufferBytesAvailable(buffer.stream);
        g_buffer_percent.store(static_cast<int>(available * 100 / (buffer.bytes_per_second * 10)));
        if (xTaskGetTickCount() - last_log >= pdMS_TO_TICKS(5000)) {
            ESP_LOGI(kTag, "Audio buffer: %u ms / 10000 ms, underruns: %lu",
                static_cast<unsigned>(available * 1000 / buffer.bytes_per_second),
                static_cast<unsigned long>(buffer.underruns.load()));
            last_log = xTaskGetTickCount();
        }
        if (priming) {
            if (available < threshold) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            priming = false;
            set_status((std::string("Playing ") + kStations[buffer.station].name).c_str());
        }
        const size_t wanted = std::min(available, sizeof(pcm)) / buffer.frame_bytes * buffer.frame_bytes;
        if (!wanted) {
            ++buffer.underruns;
            buffer.played_ms.store(0);
            ESP_LOGW(kTag, "Audio underrun; refilling to 2 seconds");
            set_status("Buffering audio...");
            threshold = buffer.bytes_per_second * 2;
            priming = true;
            continue;
        }
        const size_t count = xStreamBufferReceive(buffer.stream, pcm, wanted, 0);
        if (audio_cancelled(buffer)) break;
        lock_speaker();
        mix_button_sound(pcm, count);
        const int written = esp_codec_dev_write(g_speaker, pcm, count);
        unlock_speaker();
        if (written != ESP_CODEC_DEV_OK) {
            buffer.error.store(true);
            break;
        }
        buffer.played_ms.fetch_add(static_cast<uint32_t>(count * 1000 / buffer.bytes_per_second));
        buffer.last_output.store(xTaskGetTickCount());
    }
    g_buffer_percent.store(0);
    xSemaphoreGive(buffer.done);
    vTaskDelete(nullptr);
}

bool play_stream(const Station &station, int station_index)
{
    g_stream_kbps.store(0);
    g_download_kbps.store(0);
    set_status((std::string("Loading ") + station.name).c_str());
    const std::string playlist_url = resolve_station_url(station, station_index);
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_audio_simple_dec_cfg_t decoder_config{};
    decoder_config.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    decoder_config.use_frame_dec = false;
    esp_audio_simple_dec_handle_t decoder = nullptr;
    if (esp_audio_simple_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK) {
        esp_audio_simple_dec_unregister_default();
        esp_audio_dec_unregister_default();
        set_status("AAC decoder could not start");
        return false;
    }
    constexpr size_t output_size = 8192;
    auto *transport_stream = static_cast<uint8_t *>(heap_caps_malloc(kHlsSegmentMaxSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *aac_data = static_cast<uint8_t *>(heap_caps_malloc(kHlsSegmentMaxSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *output = static_cast<uint8_t *>(heap_caps_malloc(output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!transport_stream || !aac_data || !output) {
        heap_caps_free(transport_stream);
        heap_caps_free(aac_data);
        heap_caps_free(output);
        esp_audio_simple_dec_close(decoder);
        esp_audio_simple_dec_unregister_default();
        esp_audio_dec_unregister_default();
        set_status("Not enough PSRAM for audio buffers");
        return false;
    }

    AudioBuffer audio;
    audio.station = station_index;
    bool output_task_started = false;
    bool audio_open = false;
    bool first_audio = true;
    bool failed = false;
    bool cached = cached_stream(station_index) == playlist_url;
    TickType_t last_audio_tick = xTaskGetTickCount();
    std::string last_segment;
    std::string media_url = playlist_url;
    int playlist_depth = 0;
    while (g_connected.load() && g_playback_requested.load() && !g_scan_requested.load() && !g_sleep_requested.load() && g_station.load() == station_index) {
        if (audio.error.load()) { failed = true; break; }
        const TickType_t last_progress = output_task_started ? audio.last_output.load() : last_audio_tick;
        if (xTaskGetTickCount() - last_progress > pdMS_TO_TICKS(30000)) { failed = true; break; }
        const std::string playlist = fetch_hls_playlist(media_url);
        std::array<std::string, kMaxHlsSegments> segments{};
        const size_t segment_count = parse_hls_segments(playlist, media_url, segments);
        if (playlist.find("#EXT-X-STREAM-INF:") != std::string::npos && segment_count) {
            if (++playlist_depth > 3) { failed = true; break; }
            // Pick the advertised variant nearest 96 kbit/s rather than the first one.
            size_t position = 0;
            int best_distance = 2147483647;
            std::string best = segments.front();
            while ((position = playlist.find("#EXT-X-STREAM-INF:", position)) != std::string::npos) {
                const size_t end = playlist.find('\n', position);
                if (end == std::string::npos) break;
                const std::string attributes = playlist.substr(position, end - position);
                size_t rate_pos = attributes.find("AVERAGE-BANDWIDTH=");
                size_t prefix = 18;
                if (rate_pos == std::string::npos) { rate_pos = attributes.find("BANDWIDTH="); prefix = 10; }
                const long rate = rate_pos == std::string::npos ? 0 : std::strtol(attributes.c_str() + rate_pos + prefix, nullptr, 10);
                const size_t uri_end = playlist.find('\n', end + 1);
                std::string uri = playlist.substr(end + 1, uri_end == std::string::npos ? uri_end : uri_end - end - 1);
                if (!uri.empty() && uri.back() == '\r') uri.pop_back();
                if (rate > 0 && rate < 100000000 && !uri.empty() && uri.front() != '#' && std::abs(rate - 96000L) < best_distance) {
                    best_distance = static_cast<int>(std::abs(rate - 96000L));
                    best = make_hls_segment_url(media_url, uri);
                }
                position = end + 1;
            }
            media_url = best;
            continue;
        }
        if (playlist.find("#EXT-X-KEY:") != std::string::npos || playlist.find("#EXT-X-MAP:") != std::string::npos) {
            ESP_LOGW(kTag, "Encrypted or fragmented MP4 HLS is unsupported");
            failed = true;
            break;
        }
        if (!segment_count) {
            audio.played_ms.store(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Start one published segment behind the live edge so the producer can
        // immediately fetch ahead instead of waiting for the next publication.
        size_t segment_index = last_segment.empty() && segment_count > 1 ? segment_count - 2 : segment_count - 1;
        bool wait_for_next = false;
        if (!last_segment.empty()) {
            for (size_t i = 0; i < segment_count; ++i) {
                if (segments[i] == last_segment) {
                    if (i + 1 < segment_count) segment_index = i + 1;
                    else wait_for_next = true;
                    break;
                }
            }
        }
        if (wait_for_next) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        size_t transport_size = 0;
        if (!download_hls_segment(segments[segment_index], transport_stream, kHlsSegmentMaxSize, transport_size)) {
            audio.played_ms.store(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        last_segment = segments[segment_index];
        size_t aac_size = 0;
        if (!extract_aac_from_mpeg_ts(transport_stream, transport_size, aac_data, kHlsSegmentMaxSize, aac_size)) {
            ESP_LOGE(kTag, "Could not extract AAC from HLS segment (%u bytes)", static_cast<unsigned>(transport_size));
            set_status("BBC segment format not recognized");
            failed = true;
            break;
        }

        esp_audio_simple_dec_raw_t raw{};
        raw.buffer = aac_data;
        raw.len = aac_size;
        uint64_t segment_audio_us = 0;
        while (raw.len && !g_scan_requested.load() && !g_sleep_requested.load() && g_station.load() == station_index) {
            esp_audio_simple_dec_out_t decoded{};
            decoded.buffer = output;
            decoded.len = output_size;
            const esp_audio_err_t decode_result = esp_audio_simple_dec_process(decoder, &raw, &decoded);
            if (decode_result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                set_status("AAC frame exceeds decoder buffer");
                failed = true;
                raw.len = 0;
                break;
            }
            if (decode_result != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(kTag, "AAC decode returned %d", decode_result);
                failed = true;
                raw.len = 0;
                break;
            }
            if (decoded.decoded_size) {
                if (first_audio) {
                    esp_audio_simple_dec_info_t info{};
                    if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK || !open_speaker(info)) {
                        set_status("Audio codec setup failed");
                        failed = true;
                        raw.len = 0;
                        break;
                    }
                    audio.frame_bytes = info.channel * (info.bits_per_sample / 8);
                    audio.bytes_per_second = static_cast<size_t>(info.sample_rate) * audio.frame_bytes;
                    if (!audio.frame_bytes || !audio.bytes_per_second || audio.bytes_per_second > 768000) {
                        failed = true;
                        break;
                    }
                    const size_t capacity = audio.bytes_per_second * 10;
                    audio.storage = static_cast<uint8_t *>(heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                    if (!audio.storage) { failed = true; break; }
                    audio.stream = xStreamBufferCreateStatic(capacity + 1, 1, audio.storage, &audio.stream_state);
                    audio.done = xSemaphoreCreateBinaryStatic(&audio.done_state);
                    audio.last_output.store(xTaskGetTickCount());
                    if (!audio.stream || !audio.done || xTaskCreate(audio_output_task, "audio_output", 6144, &audio, 6, nullptr) != pdPASS) {
                        failed = true;
                        break;
                    }
                    output_task_started = true;
                    audio_open = true;
                    first_audio = false;
                    ESP_LOGI(kTag, "Playing %s as AAC HLS", station.name);
                    set_status("Priming audio buffer...");
                }
                if (audio_open) {
                    size_t sent = 0;
                    while (sent < decoded.decoded_size && !audio_cancelled(audio) && !audio.error.load()) {
                        sent += xStreamBufferSend(audio.stream, decoded.buffer + sent,
                            decoded.decoded_size - sent, pdMS_TO_TICKS(20));
                    }
                    if (audio.error.load()) { failed = true; break; }
                    if (audio_cancelled(audio)) break;
                    // Pace decoding around the five-second watermark, leaving room for bursts.
                    while (xStreamBufferBytesAvailable(audio.stream) > audio.bytes_per_second * 5 &&
                           !audio_cancelled(audio) && !audio.error.load()) vTaskDelay(pdMS_TO_TICKS(20));
                    last_audio_tick = xTaskGetTickCount();
                    esp_audio_simple_dec_info_t info{};
                    if (esp_audio_simple_dec_get_info(decoder, &info) == ESP_AUDIO_ERR_OK &&
                        info.sample_rate && info.channel && info.bits_per_sample) {
                        const uint64_t duration = static_cast<uint64_t>(decoded.decoded_size) * 8000000ULL /
                            (static_cast<uint64_t>(info.sample_rate) * info.channel * info.bits_per_sample);
                        segment_audio_us += duration;
                    }
                    if (!cached && audio.played_ms.load() >= 30000 && !audio.error.load()) {
                        cache_stream(station_index, playlist_url);
                        cached = true;
                        ESP_LOGI(kTag, "Cached verified stream for %s", station.name);
                    }
                }
            }
            if (raw.consumed == 0) break;
            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;
            raw.consumed = 0;
        }
        if (!failed && segment_audio_us) g_stream_kbps.store(static_cast<int>(static_cast<uint64_t>(aac_size) * 8000 / segment_audio_us));
        if (failed) break;
        if (!first_audio && !audio_open) {
            set_status("AAC audio output could not start");
            break;
        }
    }
    audio.stop.store(true);
    if (output_task_started) xSemaphoreTake(audio.done, portMAX_DELAY);
    if (audio.stream) vStreamBufferDelete(audio.stream);
    if (audio.done) vSemaphoreDelete(audio.done);
    heap_caps_free(audio.storage);
    // open_speaker may have succeeded before a buffer/task allocation failed.
    if (g_speaker && (audio_open || !first_audio || failed)) {
        close_radio_speaker();
    }
    esp_audio_simple_dec_close(decoder);
    esp_audio_simple_dec_unregister_default();
    esp_audio_dec_unregister_default();
    heap_caps_free(transport_stream);
    heap_caps_free(aac_data);
    heap_caps_free(output);
    if (failed && g_connected.load() && g_station.load() == station_index && g_playback_requested.load()) {
        if (cached) cache_stream(station_index, {});
        ++g_candidate_index[station_index];
        set_status("Stream failed; trying another source");
    }
    return !failed;
}

void playback_task(void *)
{
    while (true) {
        if (g_sleep_requested.load()) {
            // Audio output has joined and released the codec. Keep LVGL, touch
            // polling, Wi-Fi and the PWM peripheral powered for tap-to-resume.
            g_stream_kbps.store(0);
            g_download_kbps.store(0);
            set_status("Stopped; tap the screen to resume");
            g_idle_ready.store(true);
            while (g_sleep_requested.load()) vTaskDelay(pdMS_TO_TICKS(50));
            g_idle_ready.store(false);
        }
        if (g_scan_requested.exchange(false)) {
            // Playback has returned and released its decoder, speaker and HTTP clients.
            nvs_handle_t cache;
            esp_err_t result = nvs_open("streams96", NVS_READWRITE, &cache);
            if (result == ESP_OK) {
                result = nvs_erase_all(cache);
                if (result == ESP_OK) result = nvs_set_u32(cache, "revision", 2);
                if (result == ESP_OK) result = nvs_commit(cache);
                nvs_close(cache);
            }
            for (auto &candidates : g_candidates) candidates.clear();
            g_candidate_index.fill(0);
            g_url_bitrates.clear();
            g_stream_kbps.store(0);
            g_download_kbps.store(0);
            if (result != ESP_OK) {
                ESP_LOGE(kTag, "Stream cache clear failed: %s", esp_err_to_name(result));
                set_status("Cache clear failed; press SCAN to retry");
                g_playback_requested.store(false);
            } else {
                set_status("Cache cleared; searching for a stream");
                g_playback_requested.store(true);
            }
        }
        if (!g_connected.load() || !g_playback_requested.load()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (g_internet_probe_pending.exchange(false)) {
            test_internet_connection();
        }
        const int selected = g_station.load();
        play_stream(kStations[selected], selected);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void initialize_audio()
{
    g_speaker = bsp_audio_codec_speaker_init();
    if (!g_speaker) {
        set_status("Audio codec initialization failed");
    }
}


} // namespace p4radio
