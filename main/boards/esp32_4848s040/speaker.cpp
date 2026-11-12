#include "sdkconfig.h"
#include "board.h"
#include "pcm_channels.hpp"
#include <algorithm>
#include <cstdlib>
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#ifndef BOARD_ESP32_4848S040
#error "NS4168 speaker implementation is only for ESP32-4848S040"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "ESP32-4848S040 requires ESP32-S3"
#endif

namespace p4radio {
namespace {
constexpr size_t kWriteFrames = 1024;
// The codec library maps mono to LEFT only. NS4168 CTRL selects LEFT or RIGHT;
// retain mono software volume/queue but duplicate to both wire slots after gain.
// All calls are serialized by button_audio.cpp's existing speaker lock.
struct SpeakerData {
    audio_codec_data_if_t base;
    const audio_codec_data_if_t *driver;
    int16_t *stereo;
    bool mono;
    bool reported_signal;
    bool reported_block;
};

SpeakerData *context(const audio_codec_data_if_t *data)
{
    return reinterpret_cast<SpeakerData *>(const_cast<audio_codec_data_if_t *>(data));
}

bool data_is_open(const audio_codec_data_if_t *data)
{
    auto *self = context(data);
    return self->driver->is_open(self->driver);
}

int data_enable(const audio_codec_data_if_t *data, esp_codec_dev_type_t type, bool enable)
{
    auto *self = context(data);
    return self->driver->enable(self->driver, type, enable);
}

int data_set_format(const audio_codec_data_if_t *data, esp_codec_dev_type_t type,
                    esp_codec_dev_sample_info_t *format)
{
    if (!format || format->bits_per_sample != 16 || (format->channel != 1 && format->channel != 2))
        return ESP_CODEC_DEV_NOT_SUPPORT;
    auto *self = context(data);
    auto wire_format = *format;
    wire_format.channel = 2;
    wire_format.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
    const int result = self->driver->set_fmt(self->driver, type, &wire_format);
    if (result == ESP_CODEC_DEV_OK) {
        self->mono = format->channel == 1;
        self->reported_signal = false;
        self->reported_block = false;
        ESP_LOGI("NS4168", "%lu Hz, 16-bit, both I2S slots; %s input",
                 static_cast<unsigned long>(format->sample_rate), self->mono ? "mono" : "stereo");
    }
    return result;
}

int data_write(const audio_codec_data_if_t *data, uint8_t *samples, int bytes)
{
    auto *self = context(data);
    if (!samples || bytes < 0 || bytes % (self->mono ? 2 : 4)) return ESP_CODEC_DEV_INVALID_ARG;
    // Runs after codec software gain, under the speaker lock. Report the
    // first block and first nonzero block per format without printing PCM or
    // adding stack buffers. A zero first block can expose a muted gain path.
    if (!self->reported_block || !self->reported_signal) {
        int32_t peak = 0;
        const auto *pcm = reinterpret_cast<const int16_t *>(samples);
        for (size_t sample = 0; sample < static_cast<size_t>(bytes) / sizeof(int16_t); ++sample) {
            const int32_t value = pcm[sample];
            peak = std::max(peak, value < 0 ? -value : value);
        }
        if (peak || !self->reported_block) {
            ESP_LOGI("NS4168", "PCM after software volume: peak %ld/32768; "
                     "BCLK=1 WS=2 DATA=40, both wire slots", static_cast<long>(peak));
            self->reported_block = true;
            if (peak) self->reported_signal = true;
        }
    }
    if (!self->mono) return self->driver->write(self->driver, samples, bytes);
    const auto *mono = reinterpret_cast<const int16_t *>(samples);
    size_t remaining = static_cast<size_t>(bytes) / sizeof(int16_t);
    while (remaining) {
        const size_t frames = std::min(remaining, kWriteFrames);
        duplicate_mono_to_stereo(mono, self->stereo, frames);
        const int result = self->driver->write(self->driver,
            reinterpret_cast<uint8_t *>(self->stereo), frames * 2 * sizeof(int16_t));
        if (result != ESP_CODEC_DEV_OK) return result;
        mono += frames;
        remaining -= frames;
    }
    return ESP_CODEC_DEV_OK;
}

int data_close(const audio_codec_data_if_t *data)
{
    auto *self = context(data);
    const int result = audio_codec_delete_data_if(self->driver);
    heap_caps_free(self->stereo);
    return result;
}

const audio_codec_data_if_t *create_speaker_data(const audio_codec_data_if_t *driver)
{
    auto *self = static_cast<SpeakerData *>(std::calloc(1, sizeof(SpeakerData)));
    if (!self) return nullptr;
    self->stereo = static_cast<int16_t *>(heap_caps_malloc(kWriteFrames * 2 * sizeof(int16_t),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!self->stereo) { std::free(self); return nullptr; }
    self->driver = driver;
    self->base.is_open = data_is_open;
    self->base.enable = data_enable;
    self->base.set_fmt = data_set_format;
    self->base.write = data_write;
    self->base.close = data_close;
    return &self->base;
}
} // namespace

esp_codec_dev_handle_t board_speaker_init()
{
    static esp_codec_dev_handle_t speaker = nullptr;
    if (speaker) return speaker;
    // NS4168 has no I2C control port or MCLK. esp_codec_dev supplies software
    // volume when codec_if is null; retain the shared speaker lock and mixing.
    // These pins are relays on CIY relay variants: this port is speaker-only.
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, &tx, nullptr));
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = GPIO_NUM_NC;
    config.gpio_cfg.bclk = GPIO_NUM_1;
    config.gpio_cfg.ws = GPIO_NUM_2;
    config.gpio_cfg.dout = GPIO_NUM_40;
    config.gpio_cfg.din = GPIO_NUM_NC;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &config));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    audio_codec_i2s_cfg_t data_config{};
    data_config.port = I2S_NUM_0;
    data_config.tx_handle = tx;
    const audio_codec_data_if_t *driver = audio_codec_new_i2s_data(&data_config);
    const audio_codec_data_if_t *data = driver ? create_speaker_data(driver) : nullptr;
    if (!data) {
        if (driver) audio_codec_delete_data_if(driver);
        i2s_channel_disable(tx);
        i2s_del_channel(tx);
        return nullptr;
    }
    esp_codec_dev_cfg_t device{};
    device.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    device.data_if = data;
    speaker = esp_codec_dev_new(&device);
    if (!speaker) {
        audio_codec_delete_data_if(data);
        i2s_channel_disable(tx);
        i2s_del_channel(tx);
    }
    return speaker;
}

} // namespace p4radio
