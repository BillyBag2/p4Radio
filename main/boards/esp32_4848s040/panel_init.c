/*
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

// Adapted from Arduino_GFX st7701_type9_init_operations (GUITION 86BOX).
// Source: https://raw.githubusercontent.com/moononournation/Arduino_GFX/master/src/display/Arduino_RGB_Display.h
// Preserve the vendor's RGB666 panel input mode (0x60). The MCU framebuffer
// remains RGB565; its 16 GPIOs drive the panel's wired colour inputs.
#include "esp_lcd_st7701.h"

static const st7701_lcd_init_cmd_t commands[] = {
    {0xff, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xc0, (const uint8_t[]){0x3b, 0x00}, 2, 0},
    {0xc1, (const uint8_t[]){0x0d, 0x02}, 2, 0},
    {0xc2, (const uint8_t[]){0x31, 0x05}, 2, 0},
    {0xcd, (const uint8_t[]){0x00}, 1, 0},
    {0xb0, (const uint8_t[]){0x00, 0x11, 0x18, 0x0e, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0f, 0xaa, 0x31, 0x18}, 16, 0},
    {0xb1, (const uint8_t[]){0x00, 0x11, 0x19, 0x0e, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xa9, 0x32, 0x18}, 16, 0},
    {0xff, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xb0, (const uint8_t[]){0x60}, 1, 0},
    {0xb1, (const uint8_t[]){0x32}, 1, 0},
    {0xb2, (const uint8_t[]){0x07}, 1, 0},
    {0xb3, (const uint8_t[]){0x80}, 1, 0},
    {0xb5, (const uint8_t[]){0x49}, 1, 0},
    {0xb7, (const uint8_t[]){0x85}, 1, 0},
    {0xb8, (const uint8_t[]){0x21}, 1, 0},
    {0xc1, (const uint8_t[]){0x78}, 1, 0},
    {0xc2, (const uint8_t[]){0x78}, 1, 0},
    {0xe0, (const uint8_t[]){0x00, 0x1b, 0x02}, 3, 0},
    {0xe1, (const uint8_t[]){0x08, 0xa0, 0x00, 0x00, 0x07, 0xa0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xe2, (const uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xed, 0xa0, 0x00, 0x00, 0xec, 0xa0, 0x00, 0x00}, 12, 0},
    {0xe3, (const uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xe4, (const uint8_t[]){0x44, 0x44}, 2, 0},
    {0xe5, (const uint8_t[]){0x0a, 0xe9, 0xd8, 0xa0, 0x0c, 0xeb, 0xd8, 0xa0, 0x0e, 0xed, 0xd8, 0xa0, 0x10, 0xef, 0xd8, 0xa0}, 16, 0},
    {0xe6, (const uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xe7, (const uint8_t[]){0x44, 0x44}, 2, 0},
    {0xe8, (const uint8_t[]){0x09, 0xe8, 0xd8, 0xa0, 0x0b, 0xea, 0xd8, 0xa0, 0x0d, 0xec, 0xd8, 0xa0, 0x0f, 0xee, 0xd8, 0xa0}, 16, 0},
    {0xeb, (const uint8_t[]){0x02, 0x00, 0xe4, 0xe4, 0x88, 0x00, 0x40}, 7, 0},
    {0xec, (const uint8_t[]){0x3c, 0x00}, 2, 0},
    {0xed, (const uint8_t[]){0xab, 0x89, 0x76, 0x54, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x20, 0x45, 0x67, 0x98, 0xba}, 16, 0},
    {0xff, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xe5, (const uint8_t[]){0xe4}, 1, 0},
    {0xff, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x3a, (const uint8_t[]){0x60}, 1, 0},
    {0x11, NULL, 0, 120},
    {0x29, NULL, 0, 0},
};

const st7701_lcd_init_cmd_t *esp32_4848s040_panel_commands(uint16_t *count)
{
    *count = sizeof(commands) / sizeof(commands[0]);
    return commands;
}
