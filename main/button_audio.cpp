#include "radio.hpp"
#include "button_audio.hpp"
#include "button_sound_data.hpp"
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
std::atomic<bool> preferences_dirty{false};
std::atomic<bool> volume_dirty{false};
std::atomic<TickType_t> requested_at{0};
bool radio_open = false;
bool click_speaker_open = false;
uint32_t sample_rate = 24000;
uint8_t channels = 1;
uint8_t sample_bits = 16;
uint64_t position = sizeof(kButtonSound) / sizeof(kButtonSound[0]) * 65536ULL;
constexpr size_t count = sizeof(kButtonSound) / sizeof(kButtonSound[0]);

// Called with the speaker lock held. Linear interpolation matches the radio clock.
void render(uint8_t *pcm, size_t bytes)
{
    if (pending.exchange(false)) position = 0;
    if (sample_bits != 16 || !channels || !sample_rate) return;
    const uint64_t step = (24000ULL << 16) / sample_rate;
    const size_t frames = bytes / (channels * sizeof(int16_t));
    for (size_t frame = 0; frame < frames && (position >> 16) < count; ++frame) {
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
    while (pending.load() || (position >> 16) < count) {
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

void click_task(void *)
{
    while (true) {
        if (preferences_dirty.exchange(false)) save_preferences();
        // Never make LVGL wait for a DMA write or codec operation.
        if (pending.load() || volume_dirty.load()) {
            if (xSemaphoreTake(speaker_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (volume_dirty.exchange(false) && g_speaker)
                esp_codec_dev_set_out_vol(g_speaker, g_volume.load() * 10);
            if (g_speaker && radio_open && pending.load() &&
                xTaskGetTickCount() - requested_at.load() >= pdMS_TO_TICKS(100)) {
                // During priming/refill there may be no radio PCM to mix into.
                finish_click();
            } else if (g_speaker && !radio_open && pending.load()) {
                esp_codec_dev_sample_info_t info{};
                info.sample_rate = 24000;
                info.channel = 1;
                info.bits_per_sample = 16;
                if (click_speaker_open || esp_codec_dev_open(g_speaker, &info) == ESP_CODEC_DEV_OK) {
                    click_speaker_open = true;
                    esp_codec_dev_set_out_vol(g_speaker, g_volume.load() * 10);
                    sample_rate = 24000; channels = 1; sample_bits = 16;
                    finish_click();
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
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
}
void lock_speaker() { if (speaker_mutex) xSemaphoreTake(speaker_mutex, portMAX_DELAY); }
void unlock_speaker() { if (speaker_mutex) xSemaphoreGive(speaker_mutex); }
void request_preferences_save() { volume_dirty.store(true); preferences_dirty.store(true); }
void request_button_sound() { requested_at.store(xTaskGetTickCount()); pending.store(true); }
void set_button_audio_format(uint32_t rate, uint8_t channel_count, uint8_t bits)
{
    sample_rate = rate; channels = channel_count; sample_bits = bits;
    click_speaker_open = false;
    radio_open = true;
}
void mix_button_sound(uint8_t *pcm, size_t bytes)
{
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
    if (xTaskCreate(click_task, "button_audio", 3072, nullptr, 4, nullptr) != pdPASS)
        ESP_LOGE(kTag, "Button sound task could not start");
}
}
