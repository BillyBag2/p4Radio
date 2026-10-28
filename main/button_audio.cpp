#include "radio.hpp"
#include "button_audio.hpp"
#include "sound_data_button.hpp"
#include "sound_data_data.hpp"
#include "sound_data_tuning.hpp"
#include "sound_data_connect.hpp"
#include "sound_data_disconnect.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

namespace p4radio {
namespace {
StaticSemaphore_t mutex_state;
SemaphoreHandle_t speaker_mutex = nullptr;
std::atomic<bool> pending{false};
std::atomic<int> notification_pending{0};
std::atomic<bool> notification_active{false};
const int16_t *notification_samples = nullptr;
size_t notification_count = 0;
uint64_t notification_position = 0;
bool notification_needed() { return notification_pending.load() != 0 || notification_active.load(); }
std::atomic<bool> preferences_dirty{false};
std::atomic<TickType_t> preferences_changed_at{0};
TaskHandle_t click_task_handle = nullptr;
std::atomic<bool> volume_dirty{false};
std::atomic<TickType_t> requested_at{0};
bool radio_open = false;
bool click_speaker_open = false;
uint32_t sample_rate = 24000;
uint8_t channels = 1;
uint8_t sample_bits = 16;
uint64_t position = sizeof(kButtonSound) / sizeof(kButtonSound[0]) * 65536ULL;
uint64_t data_position = 0;
int previous_loop = 0;
std::atomic<bool> stream_loading{false};
bool tuning_in_progress()
{
    return stream_loading.load() && g_connected.load() && g_playback_requested.load() && !g_sleep_requested.load();
}
TickType_t last_radio_mix = 0;
constexpr size_t data_count = sizeof(kDataSound) / sizeof(kDataSound[0]);
constexpr size_t count = sizeof(kButtonSound) / sizeof(kButtonSound[0]);

// Called with the speaker lock held. Linear interpolation matches the radio clock.
void render(uint8_t *pcm, size_t bytes)
{
    if (pending.exchange(false)) position = 0;
    const int notification = notification_pending.exchange(0);
    if (notification) {
        notification_samples = notification == 1 ? kConnectSound : kDisconnectSound;
        notification_count = notification == 1 ? sizeof(kConnectSound) / sizeof(kConnectSound[0]) : sizeof(kDisconnectSound) / sizeof(kDisconnectSound[0]);
        notification_position = 0;
        notification_active.store(true);
    }
    if (sample_bits != 16 || !channels || !sample_rate) return;
    const uint64_t step = (24000ULL << 16) / sample_rate;
    const size_t frames = bytes / (channels * sizeof(int16_t));
    // Wi-Fi feedback takes priority while the network itself is reconnecting.
    const int loop = wifi_join_in_progress() ? 1 : tuning_in_progress() ? 2 : 0;
    const int16_t *loop_samples = loop == 1 ? kDataSound : kTuningSound;
    const size_t loop_count = loop == 1 ? data_count : sizeof(kTuningSound) / sizeof(kTuningSound[0]);
    if (loop != previous_loop) data_position = 0;
    previous_loop = loop;
    for (size_t frame = 0; frame < frames; ++frame) {
        if (loop != 0) {
            const size_t index = data_position >> 16;
            const int fraction = data_position & 65535;
            const int value = loop_samples[index] +
                static_cast<int64_t>(loop_samples[(index + 1) % loop_count] - loop_samples[index]) * fraction / 65536;
            for (size_t ch = 0; ch < channels; ++ch) {
                const size_t offset = (frame * channels + ch) * sizeof(int16_t);
                int16_t original;
                std::memcpy(&original, pcm + offset, sizeof(original));
                const int16_t mixed = std::clamp<int>(original + value / 2, -32768, 32767);
                std::memcpy(pcm + offset, &mixed, sizeof(mixed));
            }
            data_position = (data_position + step) % (loop_count * 65536ULL);
        }
        if (notification_active.load()) {
            const size_t index = notification_position >> 16;
            const int fraction = notification_position & 65535;
            const int value = notification_samples[index] + static_cast<int64_t>(
                notification_samples[std::min(index + 1, notification_count - 1)] - notification_samples[index]) * fraction / 65536;
            for (size_t ch = 0; ch < channels; ++ch) {
                const size_t offset = (frame * channels + ch) * sizeof(int16_t);
                int16_t original;
                std::memcpy(&original, pcm + offset, sizeof(original));
                const int16_t mixed = std::clamp<int>(original / 2 + value, -32768, 32767);
                std::memcpy(pcm + offset, &mixed, sizeof(mixed));
            }
            notification_position += step;
            if ((notification_position >> 16) >= notification_count) notification_active.store(false);
        }
        if ((position >> 16) >= count) continue;
        const size_t index = position >> 16;
        const int fraction = position & 65535;
        const int value = kButtonSound[index] +
            static_cast<int64_t>(kButtonSound[std::min(index + 1, count - 1)] - kButtonSound[index]) * fraction / 65536;
        for (size_t ch = 0; ch < channels; ++ch) {
            const size_t offset = (frame * channels + ch) * sizeof(int16_t);
            int16_t original;
            std::memcpy(&original, pcm + offset, sizeof(original));
            // Attenuate the radio while the click plays, then saturate safely.
            const int16_t mixed = std::clamp<int>(original / 2 + value, -32768, 32767);
            std::memcpy(pcm + offset, &mixed, sizeof(mixed));
        }
        position += step;
    }
}

bool finish_click()
{
    int16_t silence[256];
    bool wrote_audio = false;
    while (pending.load() || (position >> 16) < count || notification_needed()) {
        std::memset(silence, 0, sizeof(silence));
        render(reinterpret_cast<uint8_t *>(silence), sizeof(silence));
        if (sample_bits != 16 || esp_codec_dev_write(g_speaker, silence, sizeof(silence)) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(kTag, "Button sound output failed");
            break;
        }
        wrote_audio = true;
    }
    return wrote_audio;
}

void preferences_task(void *)
{
    while (true) {
        // Coalesce rapid presses; flash writes never run in the audio task.
        if (preferences_dirty.load() &&
            xTaskGetTickCount() - preferences_changed_at.load() >= pdMS_TO_TICKS(500) &&
            preferences_dirty.exchange(false)) save_preferences();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void click_task(void *)
{
    // At 48 kHz stereo, 256 samples held only 2.7 ms of audio, less
    // than one scheduler tick. Use roughly 20 ms blocks to survive scheduling.
    int16_t loop_pcm[2048];
    while (true) {
        // Never make LVGL wait for a DMA write or codec operation.
        const bool connecting = wifi_join_in_progress() || tuning_in_progress() || notification_needed();
        if (pending.load() || volume_dirty.load() || connecting) {
            if (xSemaphoreTake(speaker_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (volume_dirty.exchange(false) && g_speaker)
                esp_codec_dev_set_out_vol(g_speaker, g_volume.load() * 10);
            if (g_speaker && radio_open && connecting &&
                (tuning_in_progress() || xTaskGetTickCount() - last_radio_mix >= pdMS_TO_TICKS(100))) {
                const size_t frames = std::min<size_t>(sample_rate / 50, sizeof(loop_pcm) / sizeof(loop_pcm[0]) / channels);
                const size_t bytes = frames * channels * sizeof(int16_t);
                std::memset(loop_pcm, 0, bytes);
                render(reinterpret_cast<uint8_t *>(loop_pcm), bytes);
                if (esp_codec_dev_write(g_speaker, loop_pcm, bytes) != ESP_CODEC_DEV_OK)
                    ESP_LOGW(kTag, "Connection/loading sound output failed");
            } else if (g_speaker && radio_open && pending.load() &&
                xTaskGetTickCount() - requested_at.load() >= pdMS_TO_TICKS(30)) {
                // During priming/refill there may be no radio PCM to mix into.
                finish_click();
            } else if (g_speaker && !radio_open && (pending.load() || connecting)) {
                esp_codec_dev_sample_info_t info{};
                info.sample_rate = 24000;
                info.channel = 1;
                info.bits_per_sample = 16;
                const bool already_open = click_speaker_open;
                if (already_open || esp_codec_dev_open(g_speaker, &info) == ESP_CODEC_DEV_OK) {
                    click_speaker_open = true;
                    if (!already_open) esp_codec_dev_set_out_vol(g_speaker, g_volume.load() * 10);
                    sample_rate = 24000; channels = 1; sample_bits = 16;
                    if (connecting) {
                        const size_t bytes = 480 * sizeof(int16_t); // 20 ms at 24 kHz mono.
                        std::memset(loop_pcm, 0, bytes);
                        render(reinterpret_cast<uint8_t *>(loop_pcm), bytes);
                        if (esp_codec_dev_write(g_speaker, loop_pcm, bytes) != ESP_CODEC_DEV_OK)
                            ESP_LOGW(kTag, "Connection/loading sound output failed");
                    } else finish_click();
                    // Keep I2S running so DMA can actually play the short sound.
                    // The radio takes over/reconfigures this device under the
                    // same lock when decoded audio becomes available.
                } else {
                    ESP_LOGW(kTag, "Button sound speaker open failed");
                    pending.store(false);
                }
            }
            unlock_speaker();
        }
        if (connecting) vTaskDelay(1);
        else ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}
}
void lock_speaker() { if (speaker_mutex) xSemaphoreTake(speaker_mutex, portMAX_DELAY); }
void unlock_speaker() { if (speaker_mutex) xSemaphoreGive(speaker_mutex); }
void request_preferences_save()
{
    volume_dirty.store(true);
    preferences_changed_at.store(xTaskGetTickCount());
    preferences_dirty.store(true);
    if (click_task_handle) xTaskNotifyGive(click_task_handle);
}
void request_connect_sound()
{
    notification_pending.store(1);
    if (click_task_handle) xTaskNotifyGive(click_task_handle);
}
void request_disconnect_sound()
{
    notification_pending.store(2);
    if (click_task_handle) xTaskNotifyGive(click_task_handle);
}
void set_stream_loading_sound(bool loading) { stream_loading.store(loading); }
void request_button_sound()
{
    requested_at.store(xTaskGetTickCount());
    pending.store(true);
    if (click_task_handle) xTaskNotifyGive(click_task_handle);
}
void set_button_audio_format(uint32_t rate, uint8_t channel_count, uint8_t bits)
{
    sample_rate = rate; channels = channel_count; sample_bits = bits;
    click_speaker_open = false;
    radio_open = true;
}
void mix_button_sound(uint8_t *pcm, size_t bytes)
{
    last_radio_mix = xTaskGetTickCount();
    // The output task already holds the speaker lock. Apply volume on the next
    // PCM block instead of waiting for the background worker to win that lock.
    if (volume_dirty.exchange(false) && g_speaker)
        esp_codec_dev_set_out_vol(g_speaker, g_volume.load() * 10);
    render(pcm, bytes);
}
void close_radio_speaker()
{
    lock_speaker();
    if (g_speaker) {
        // Finish feedback for STOP, SCAN and station changes before closing audio.
        if (radio_open) finish_click();
        esp_codec_dev_close(g_speaker);
    }
    radio_open = false;
    click_speaker_open = false;
    unlock_speaker();
}
void initialize_button_audio()
{
    speaker_mutex = xSemaphoreCreateMutexStatic(&mutex_state);
    if (xTaskCreate(click_task, "button_audio", 8192, nullptr, 6, &click_task_handle) != pdPASS)
        ESP_LOGE(kTag, "Button sound task could not start");
    if (xTaskCreate(preferences_task, "preferences", 3072, nullptr, 2, nullptr) != pdPASS)
        ESP_LOGE(kTag, "Preferences task could not start");
}
}
