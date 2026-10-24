#pragma once
#include <cstddef>
#include <cstdint>
namespace p4radio {
void initialize_button_audio();
void request_button_sound();
void set_stream_loading_sound(bool loading);
void request_preferences_save();
void lock_speaker();
void unlock_speaker();
void set_button_audio_format(uint32_t rate, uint8_t channels, uint8_t bits);
void mix_button_sound(uint8_t *pcm, size_t bytes);
void close_radio_speaker();
}
