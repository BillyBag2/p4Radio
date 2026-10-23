# p4Radio

A simple internet radio targeting the Waveshare ESP32-P4-86-Panel-ETH-2RO (SKU 31570).

## Features

In the absence of information to connect to wifi. default to an AP and display an appropriate message. Allow a phone to connect without a password and use capture to redirect to a simple web page that lists available wifi networks and allows the user to select one and enter a password. The device will then attempt to connect to the selected network and display an appropriate message.

When connected to a network, the device tune to the last station or a default station. 

The list of available stations will be BBC1 BBC2 BBC4 BBC4 BBC5.

Display the station name.

Add up and down buttons to change station an + and - buttons to change volume.

A power button to turn the device off and on. When turned off, the device will go into deep sleep and wake up when the power button is pressed.

## Hardware specifications

| Component | Specification |
| --- | --- |
| Main processor | ESP32-P4NRW32, dual-core 32-bit RISC-V high-performance CPU up to 360 MHz, plus a low-power RISC-V core up to 40 MHz |
| Chip revision | ESP32-P4 revision 1.3 (reported for this board); firmware targets the pre-v3 silicon branch. |
| Internal memory | 768 KB high-performance L2 memory, 32 KB low-power SRAM, 128 KB HP ROM and 16 KB LP ROM |
| PSRAM | 32 MB, stacked in the ESP32-P4 package; 1.8 V, 16-line (HEX) interface, up to 200 MHz |
| Flash | 32 MB external SPI NOR flash |
| Display | 4-inch IPS, 720 x 720 pixels, 2-lane MIPI DSI; ST7703 driver; backlight enable GPIO33, PWM GPIO26, reset GPIO27 |
| Touch | GT911 five-point capacitive touch controller over I2C; SDA GPIO7, SCL GPIO8, reset GPIO23; interrupt is routed to a test point |
| Wi-Fi and Bluetooth | ESP32-C6-MINI-1U-H8 coprocessor connected over SDIO; Wi-Fi 6 (2.4 GHz) and Bluetooth 5 LE. Wireless support is provided by the C6, not the P4. |
| Ethernet | IP101 10/100 Mbit/s Ethernet PHY; RJ45 is on the 86 Panel bottom board |
| Audio | ES8311 audio codec, ES7210 microphone ADC, onboard microphone and 8 ohm / 2 W speaker output |
| Other board I/O | TF/microSD slot, USB 2.0 High-Speed OTG, USB-to-UART, RS485 and two isolated relay outputs |
| Power | DC 6-30 V input on the ETH-2RO bottom board; USB-C power/programming on the main board |

The P4 does not contain a Wi-Fi radio. Wi-Fi and Bluetooth use the onboard ESP32-C6 coprocessor, which needs its own compatible firmware and SDIO host support in addition to P4 settings. `sdkconfig.defaults` includes the ESP-Hosted settings for that C6, 32 MB flash, 32 MB HEX PSRAM at 200 MHz, and the pre-v3 P4 silicon branch with a 360 MHz CPU clock for revision 1.3.

## Radio controls and setup

At first boot, the radio opens the unsecured `p4Radio-Setup` Wi-Fi network. Join it from a phone; its captive portal scans nearby networks and saves the chosen SSID and password in NVS before connecting. After Wi-Fi comes up, p4Radio fetches the selected BBC HLS playlist, extracts AAC audio from its MPEG-TS segments, and sends decoded audio to the onboard ES8311 codec and speaker output. The five presets are BBC Radio 1, 2, 4, 4 Extra, and 5 Live; Radio 4 Extra fills the repeated BBC 4 entry in the original list. Station and volume selections are retained across restarts.

The display has previous/next station, volume down/up, and a power control. Power enters ESP32-P4 deep sleep. The GT911 touch interrupt is not connected to an MCU GPIO on this board, so touch cannot wake the sleeping P4; press RESET or cycle board power to boot it again.

Wi-Fi provisioning and playback require a compatible ESP32-C6 ESP-Hosted slave image. The P4 host dependencies are pinned in [`main/idf_component.yml`](main/idf_component.yml). Waveshare's published examples do not include the C6 image, so a board with a missing or incompatible image cannot provide the setup AP until its C6 firmware is installed. The app reports that link failure on screen.

## Build

### Station discovery and stream cache

On first selection, the firmware searches Radio Browser for the preset's exact name (case insensitive), trying two directory servers. It considers AAC HLS results and retains a BBC worldwide 96 kbit/s URL as a fallback. The existing five station names remain the presets; this does not add a text search interface. Directory API: https://api.radio-browser.info/

A stream is saved in the `streams96` NVS namespace after 30 seconds of decoded PCM audio has been accepted by the speaker driver without a reported decode or output error. Playlist or segment download failures reset that qualification period. Saved URLs survive reboot and are tried before searching again. This is a software health check, not a measurement of audible quality.

Decode/output errors, unsupported segment formats, or 30 seconds without audio cause the player to try the next candidate. A failing cached URL is removed. After all candidates fail, the directory is searched again. Playback also retries after Wi-Fi reconnects. Search is performed when a station is selected, not as a background scan of every preset.

The player supports unencrypted AAC in MPEG-TS HLS, including basic master playlists. MP3, direct continuous AAC, encrypted HLS, and fragmented MP4 are not supported and are not intentionally cached. Changing preset names/order in firmware should be accompanied by clearing the `streams96` namespace, since cache keys identify preset slots.

Use ESP-IDF 6.1 (or a compatible ESP-IDF release supported by the pinned components):

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```

The first configure/build resolves the pinned components from the ESP Component Registry. The 32 MB custom partition table reserves an 8 MB app slot and the remaining space for storage.

## ESP-IDF configuration

The project targets `esp32p4`. [`sdkconfig.defaults`](sdkconfig.defaults) selects the pre-v3 P4 silicon branch for this board's revision 1.3. ESP-IDF does not permit one binary to support both pre-v3 and v3-or-later P4 revisions, so this firmware configuration is for the reported rev 1.x board.

Board specifications: [Waveshare ESP32-P4-WIFI6-Touch-LCD-4B / ETH-2RO documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B). The 4B documentation covers the shared main board; the ETH-2RO SKU adds the Ethernet/RS485/relay bottom board and omits the camera connector.

### Volume and stream rates

Volume runs from 1 to 10 in single steps, with a thin vertical indicator at the right edge. Existing percentage preferences are converted on load. Button captions use the larger 40-pixel font.

The display shows measured encoded AAC bitrate and recent segment download throughput (Wi-Fi RX), both in kbit/s. Download throughput includes connection setup and is not a measurement of maximum Wi-Fi capacity; stale measurements show `--` after 15 seconds. AAC bitrate is calculated after a complete decoded segment.

Directory candidates and advertised HLS variants are ranked by distance from 96 kbit/s. Availability determines the actual bitrate; directory results can be higher. A new `streams96` cache namespace ensures old high-bitrate cache entries do not bypass the new selection preference.

Fallback URLs were checked on 23 October 2026: all five worldwide BBC 96 kbit/s endpoints returned HLS playlists and MPEG-TS segment samples. This verifies HTTP availability and transport format, not audible content or uninterrupted playback. Cache policy revision 2 clears the `streams96` namespace once after installing this firmware, preserving Wi-Fi and radio preferences.

To manually clear the entire NVS partition from an ESP-IDF terminal, close the serial monitor and run `python -m esptool --chip esp32p4 --port COM12 erase-region 0x9000 0x6000`. This partition address and length come from this project's `partitions.csv`. It removes saved Wi-Fi credentials, volume, station selection, and stream URLs; the next boot enters Wi-Fi setup. Normal flashing does not clear NVS.

### STOP and SCAN

STOP turns off the backlight and enters deep sleep. The backlight GPIO is held in its inactive state during sleep. Press RESET or cycle power to wake this board.

SCAN stops playback, clears all stream URLs in `streams96` and the in-memory candidate lists, then searches again for the selected station and resumes playback. Wi-Fi credentials, volume, and station selection are preserved. If offline, discovery waits for Wi-Fi. An active HTTP request may take up to its configured timeout before the playback task can clear the cache. Cache-clear failures are shown on screen and require another SCAN attempt.
