#include "radio.hpp"
#include "button_audio.hpp"
#include "buffer_sync.hpp"
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
    uint32_t station_id = 0;
    const char *station_name = nullptr; // Owned by play_stream's Station until output joins.
    std::atomic<bool> stop{false};
    std::atomic<bool> error{false};
    std::atomic<uint32_t> played_ms{0};
    std::atomic<uint32_t> underruns{0};
    std::atomic<TickType_t> last_output{0};
};

bool audio_cancelled(const AudioBuffer &buffer)
{
    return buffer.stop.load() || g_scan_requested.load() || g_sleep_requested.load() ||
        !g_playback_requested.load() || g_station.load() != buffer.station || g_station_token.load() != buffer.station_id;
}

void audio_output_task(void *argument)
{
    auto &buffer = *static_cast<AudioBuffer *>(argument);
    alignas(int16_t) uint8_t pcm[4096];
    // Keep the extra output block off the 6 KB task stack.
    auto *matched = static_cast<int16_t *>(heap_caps_malloc(sizeof(pcm) + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!matched) {
        buffer.error.store(true);
        xSemaphoreGive(buffer.done);
        vTaskDelete(nullptr);
        return;
    }
    BufferRateController controller;
    PcmRateMatcher matcher;
    controller.reset(kAudioBufferTargetSeconds);
    TickType_t last_control = xTaskGetTickCount();
    const size_t pcm_channels = buffer.frame_bytes / sizeof(int16_t);
    bool priming = true;
    size_t threshold = buffer.bytes_per_second / 2; // Fast start: 0.5 seconds.
    TickType_t last_log = xTaskGetTickCount();
    while (!audio_cancelled(buffer)) {
        const size_t available = xStreamBufferBytesAvailable(buffer.stream);
        g_buffer_percent.store(static_cast<int>(available * 100 / (buffer.bytes_per_second * kAudioBufferSeconds)));
        if (xTaskGetTickCount() - last_log >= pdMS_TO_TICKS(5000)) {
            ESP_LOGI(kTag, "Audio buffer: %u ms / %u ms, underruns: %lu, rate correction: %+d ppm",
                static_cast<unsigned>(available * 1000 / buffer.bytes_per_second),
                static_cast<unsigned>(kAudioBufferSeconds * 1000),
                static_cast<unsigned long>(buffer.underruns.load()), controller.ppm());
            last_log = xTaskGetTickCount();
        }
        if (priming) {
            if (available < threshold) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            priming = false;
            controller.reset(kAudioBufferTargetSeconds);
            matcher.reset();
            last_control = xTaskGetTickCount();
            set_stream_loading_sound(false);
            set_status((std::string("Playing ") + buffer.station_name).c_str());
        }
        if (xTaskGetTickCount() - last_control >= pdMS_TO_TICKS(1000)) {
            controller.update(static_cast<float>(available) / buffer.bytes_per_second, kAudioBufferTargetSeconds);
            last_control = xTaskGetTickCount();
        }
        const size_t wanted = std::min(available, sizeof(pcm)) / buffer.frame_bytes * buffer.frame_bytes;
        if (!wanted) {
            ++buffer.underruns;
            buffer.played_ms.store(0);
            ESP_LOGW(kTag, "Audio underrun; refilling to 2 seconds");
            set_status("Buffering audio...");
            threshold = buffer.bytes_per_second * 2;
            priming = true;
            set_stream_loading_sound(true);
            continue;
        }
        const size_t count = xStreamBufferReceive(buffer.stream, pcm, wanted, 0);
        if (audio_cancelled(buffer)) break;
        const size_t output_frames = matcher.process(reinterpret_cast<const int16_t *>(pcm),
            count / buffer.frame_bytes, pcm_channels, controller.ppm(), matched);
        const size_t output_bytes = output_frames * buffer.frame_bytes;
        if (!output_bytes) continue;
        lock_speaker();
        mix_button_sound(reinterpret_cast<uint8_t *>(matched), output_bytes);
        const int written = esp_codec_dev_write(g_speaker, matched, output_bytes);
        unlock_speaker();
        if (written != ESP_CODEC_DEV_OK) {
            buffer.error.store(true);
            break;
        }
        buffer.played_ms.fetch_add(static_cast<uint32_t>(output_bytes * 1000 / buffer.bytes_per_second));
        buffer.last_output.store(xTaskGetTickCount());
    }
    heap_caps_free(matched);
    g_buffer_percent.store(0);
    xSemaphoreGive(buffer.done);
    vTaskDelete(nullptr);
}

bool play_stream(const Station &station, int station_index)
{
    set_stream_loading_sound(true);
    // All failure/cancellation exits stop the loading layer, including early returns.
    struct LoadingSoundScope {
        ~LoadingSoundScope() { set_stream_loading_sound(false); }
    } loading_sound_scope;
    g_stream_kbps.store(0);
    g_stream_channels.store(0);
    g_download_kbps.store(0);
    set_status((std::string("Loading ") + station.name).c_str());
    const std::string playlist_url = resolve_station_url(station, station_index);
    if (playlist_url.empty()) {
        set_status("No streams within bitrate limit; change settings");
        for (int i = 0; i < 100 && !g_scan_requested.load() && !g_sleep_requested.load() && g_station.load() == station_index; ++i)
            vTaskDelay(pdMS_TO_TICKS(100));
        return false;
    }
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
    audio.station_id = station.id;
    audio.station_name = station.name.c_str();
    bool output_task_started = false;
    bool audio_open = false;
    bool first_audio = true;
    bool failed = false;
    bool cached = cached_stream(station_index) == playlist_url;
    TickType_t last_audio_tick = xTaskGetTickCount();
    std::string last_segment;
    std::string media_url = playlist_url;
    int playlist_depth = 0;
    while (g_connected.load() && g_playback_requested.load() && !g_scan_requested.load() && !g_sleep_requested.load() && g_station.load() == station_index && g_station_token.load() == station.id) {
        if (audio.error.load()) { failed = true; break; }
        const TickType_t last_progress = output_task_started ? audio.last_output.load() : last_audio_tick;
        if (xTaskGetTickCount() - last_progress > pdMS_TO_TICKS(30000)) { failed = true; break; }
        const int limit = g_max_bitrate_kbps.load();
        if (limit > 0 && stream_url_bitrate(media_url) > limit) {
            set_status("Stream exceeds bitrate limit");
            failed = true;
            break;
        }
        const std::string playlist = fetch_hls_playlist(media_url);
        std::array<std::string, kMaxHlsSegments> segments{};
        const size_t segment_count = parse_hls_segments(playlist, media_url, segments);
        if (playlist.find("#EXT-X-STREAM-INF:") != std::string::npos && segment_count) {
            if (++playlist_depth > 3) { failed = true; break; }
            // Prefer the highest permitted rate; Unlimited applies no ceiling.
            size_t position = 0;
            int best_distance = 2147483647;
            std::string best;
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
                const std::string variant_url = make_hls_segment_url(media_url, uri);
                const int nominal = stream_url_bitrate(variant_url);
                const bool permitted = limit == 0 || (nominal > 0 ? nominal <= limit : rate > 0 && rate <= limit * 1000L);
                const long selection_rate = nominal > 0 ? nominal * 1000L : rate;
                const int score = -static_cast<int>(std::min(selection_rate, 100000000L));
                if (permitted && rate > 0 && rate < 100000000 && !uri.empty() && uri.front() != '#' && score < best_distance) {
                    best_distance = score;
                    best = make_hls_segment_url(media_url, uri);
                }
                position = end + 1;
            }
            if (best.empty()) {
                set_status("No HLS variant within bitrate limit");
                failed = true;
                break;
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
            ESP_LOGE(kTag, "Segment prefix: %02x %02x %02x %02x", transport_size > 0 ? transport_stream[0] : 0,
                     transport_size > 1 ? transport_stream[1] : 0, transport_size > 2 ? transport_stream[2] : 0,
                     transport_size > 3 ? transport_stream[3] : 0);
            set_status("Stream segment format not supported");
            failed = true;
            break;
        }

        esp_audio_simple_dec_raw_t raw{};
        raw.buffer = aac_data;
        raw.len = aac_size;
        uint64_t segment_audio_us = 0;
        while (raw.len && !g_scan_requested.load() && !g_sleep_requested.load() && g_station.load() == station_index && g_station_token.load() == station.id) {
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
                    if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK ||
                        info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) || !open_speaker(info)) {
                        set_status("Audio codec setup failed");
                        failed = true;
                        raw.len = 0;
                        break;
                    }
                    g_stream_channels.store(info.channel);
                    audio.frame_bytes = info.channel * (info.bits_per_sample / 8);
                    audio.bytes_per_second = static_cast<size_t>(info.sample_rate) * audio.frame_bytes;
                    if (!audio.frame_bytes || !audio.bytes_per_second || audio.bytes_per_second > 768000) {
                        failed = true;
                        break;
                    }
                    const size_t capacity = audio.bytes_per_second * kAudioBufferSeconds;
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
                    ESP_LOGI(kTag, "Playing %s as AAC HLS", station.name.c_str());
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
                    // Pace decoding around the configured watermark, leaving room for bursts.
                    while (xStreamBufferBytesAvailable(audio.stream) > audio.bytes_per_second * kAudioBufferTargetSeconds &&
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
                        cache_stream(station_index, playlist_url, station.id);
                        cached = true;
                        ESP_LOGI(kTag, "Cached verified stream for %s", station.name.c_str());
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
    if (failed && g_connected.load() && g_station.load() == station_index && g_station_token.load() == station.id && g_playback_requested.load()) {
        if (cached) cache_stream(station_index, {}, station.id);
        ++g_candidate_index[station_index];
        set_status("Stream failed; trying another source");
    }
    return !failed;
}

void playback_task(void *)
{
    bool empty_stations_reported = false;
    while (true) {
        if (g_sleep_requested.load()) {
            // Audio output has joined and released the codec. Keep LVGL, touch
            // polling, Wi-Fi and the PWM peripheral powered for tap-to-resume.
            g_stream_kbps.store(0);
            g_download_kbps.store(0);
            set_status("Stopped; tap the screen to resume");
            g_idle_ready.store(true);
            while (g_sleep_requested.load()) {
                if (g_resume_requested.exchange(false)) {
                    g_playback_requested.store(true);
                    g_sleep_requested.store(false);
                    set_status("Resuming radio...");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            g_idle_ready.store(false);
            empty_stations_reported = false;
        }
        if (g_scan_requested.exchange(false)) {
            empty_stations_reported = false;
            // Playback has returned and released its decoder, speaker and HTTP clients.
            nvs_handle_t cache;
            esp_err_t result = nvs_open("streams96", NVS_READWRITE, &cache);
            if (result == ESP_OK) {
                result = nvs_erase_all(cache);
                if (result == ESP_OK) result = nvs_set_u32(cache, "revision", 4);
                if (result == ESP_OK) result = nvs_commit(cache);
                nvs_close(cache);
            }
            for (auto &candidates : g_candidates) candidates.clear();
            g_candidate_index.fill(0);
            g_stream_kbps.store(0);
            g_download_kbps.store(0);
            if (result != ESP_OK) {
                ESP_LOGE(kTag, "Stream cache clear failed: %s", esp_err_to_name(result));
                set_status("Cache clear failed; press TUNE to retry");
                g_playback_requested.store(false);
            } else {
                set_status("Cache cleared; searching for a stream");
                g_playback_requested.store(true);
            }
        }
        if (!g_connected.load() || !g_playback_requested.load()) {
            empty_stations_reported = false;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (g_internet_probe_pending.exchange(false)) {
            test_internet_connection();
        }
        const int selected = g_station.load();
        const Station station = station_copy(selected);
        if (!station.id || !station.enabled) {
            if (!empty_stations_reported) set_status("No stations enabled; open Stations in settings");
            empty_stations_reported = true;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        empty_stations_reported = false;
        play_stream(station, selected);
        // Retain retry backoff, but do not make STOP/wake wait a full second.
        for (int i = 0; i < 50 && !g_sleep_requested.load(); ++i)
            vTaskDelay(pdMS_TO_TICKS(20));
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
