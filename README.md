# p4Radio

A simple internet radio targeting the Waveshare ESP32-P4-86-Panel-ETH-2RO (SKU 31570).

## Usage

### First setup, or new wifi location

#### Connect your phone directly to the p4Radio

If the p4Radio cannot connect to it's configured WiFi it displays a
QRcode. Scan the with your phone, or follow the instructions, to connect
directly to p4radio. A new QR code will be displayed.

#### Go to setup page

Scan the second QR code with your phone, or follow the instructions, to go to
the p4radio setup page. On the page, Wait for the WiFi scan to complete. Click
the list of available networks to select one for p4radio to use. Enter the WiFi
password and continue.

---

## Requirements

When unable to connect to the wifi network after a period of trying, enable AP
mode and display a QR code advertising the AP SSID.

When a client joins the AP network display an QR code to take them to the setup
page.

Allow entry of wifi details.

When connected to a network, the device will be configures in the same way as it was when previously used. It will attempt to provide the current station.

The list of available stations will be BBC1 BBC2 BBC4 BBC4 BBC5.

Display the station name.

GUI allows navigation to the different stations.
GUI allows changing the volume.

GUI allows changing to a dormant state where the device appears to be off.

## Hardware specifications

| Component | ESP32-P4-86-Panel-ETH-2RO |
| --- | --- |
| Main processor | ESP32-P4NRW32, dual-core 32-bit RISC-V high-performance CPU up to 360 MHz, plus a low-power RISC-V core up to 40 MHz |
| Chip revision | ESP32-P4 revision 1.3. Target pre-v3 silicon branch. |
| PSRAM | 32 MB up to 200 MHz |
| Flash | 32 MB external SPI NOR flash |
| Display | 4-inch IPS, 720 x 720 pixels, 2-lane MIPI DSI; ST7703 driver; backlight enable GPIO33, PWM GPIO26, reset GPIO27 |
| Touch | GT911 five-point capacitive touch controller over I2C; SDA GPIO7, SCL GPIO8, reset GPIO23; interrupt is routed to a test point |
| Wi-Fi and Bluetooth | ESP32-C6-MINI-1U-H8 coprocessor connected over SDIO; Wi-Fi 6 (2.4 GHz) and Bluetooth 5 LE. Wireless support is provided by the C6, not the P4. |
| Ethernet | IP101 10/100 Mbit/s Ethernet PHY; RJ45 is on the 86 Panel bottom board |
| Audio | ES8311 audio codec, ES7210 microphone ADC, onboard microphone and 8 ohm / 2 W speaker output |
| Other board I/O | TF/microSD slot, USB 2.0 High-Speed OTG, USB-to-UART, RS485 and two isolated relay outputs |
| Power | DC 6-30 V input on the ETH-2RO bottom board; USB-C power/programming on the main board |

## Radio controls and setup

At first boot, the radio opens the unsecured `p4Radio-Setup` Wi-Fi network. Join it from a phone; its captive portal scans nearby networks and saves the chosen SSID and password in NVS before connecting. After Wi-Fi comes up, p4Radio fetches the selected BBC HLS playlist, extracts AAC audio from its MPEG-TS segments, and sends decoded audio to the onboard ES8311 codec and speaker output. The five presets are BBC Radio 1, 2, 4, 4 Extra, and 5 Live; Radio 4 Extra fills the repeated BBC 4 entry in the original list. Station and volume selections are retained across restarts.

The display has previous/next station, volume down/up, STOP, and SCAN controls. STOP uses an awake idle mode so touch polling and Wi-Fi remain active; tapping the dark screen resumes playback. This consumes more power than deep sleep. The GT911 interrupt is not connected to an MCU GPIO on this board, so deep sleep cannot provide touch wake.

Wi-Fi provisioning and playback require a compatible ESP32-C6 ESP-Hosted slave image. The P4 host dependencies are pinned in [`main/idf_component.yml`](main/idf_component.yml). Waveshare's published examples do not include the C6 image, so a board with a missing or incompatible image cannot provide the setup AP until its C6 firmware is installed. The app reports that link failure on screen.

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

STOP stops audio and turns off the PWM backlight while leaving LVGL/touch polling and Wi-Fi active. A full-screen black overlay consumes the wake tap, restores brightness, and resumes the station without deliberately disconnecting Wi-Fi. Wake becomes available once the playback task has finished its current HTTP operation and stopped the audio task. No deep sleep or GPIO hold is used; touch wake therefore works without a wired touch interrupt. This mode uses more power than deep sleep. The stream itself is reopened on resume, so some startup delay remains.

SCAN stops playback, clears all stream URLs in `streams96` and the in-memory candidate lists, then searches again for the selected station and resumes playback. Wi-Fi credentials, volume, and station selection are preserved. If offline, discovery waits for Wi-Fi. An active HTTP request may take up to its configured timeout before the playback task can clear the cache. Cache-clear failures are shown on screen and require another SCAN attempt.

### Buffered playback

Downloading/decoding and speaker output run in separate FreeRTOS tasks. A PSRAM PCM buffer holds ten seconds at the decoded format (about 1.92 MB for 48 kHz stereo, 16-bit). Playback starts at 0.5 seconds; decoding pauses above a five-second target. After an underrun the output waits for two seconds before resuming. Startup still requires downloading the first complete HLS segment. Playback begins one published segment behind the live edge when possible, allowing immediate prefetch at the cost of extra live latency.

The right-edge bar shows volume; numeric volume text and the left buffer indicator are omitted. Station buttons use `<` and `>`, and volume buttons use `-` and `+`. Buffer duration and underrun count are logged every five seconds. The output clock stays at the decoded nominal sample rate; automatic clock tuning/resampling is not implemented. STOP, SCAN, and station changes cancel output and discard buffered audio. Cache qualification counts successful speaker output and restarts after underruns.

### Hostname and setup display

The hostname defaults to `p4radio` and is saved as `hostname` in the `p4radio` NVS namespace. Enter a new name alongside Wi-Fi details on the setup form; names are normalized to lowercase and accept 1?32 ASCII letters/digits with internal hyphens. Enter the name without `.local`. The screen header uses the saved name.

The managed `espressif/mdns` component advertises the hostname and HTTP service on the AP and station interfaces. Open `http://p4radio.local/` (or the saved name) from the same network. The setup web server remains available after joining Wi-Fi. Clients must support mDNS and the network must permit multicast; when connected to the setup AP, `http://192.168.4.1/` is also available.

In AP mode, the display shows a QR code for joining the open `p4Radio-Setup` Wi-Fi network while no clients are associated. Once any client associates, the QR code switches to `http://<hostname>.local/` to open the setup page, with the URL and IP fallback printed below. Disconnecting all AP clients restores the Wi-Fi join QR code and instruction. Successful Wi-Fi provisioning restores the normal radio display. The first QR code joins Wi-Fi; the second QR code opens the setup page.

After adding these changes, run `idf.py update-dependencies` and `idf.py build` in the configured ESP-IDF terminal to resolve mDNS. `CONFIG_LV_USE_QRCODE=y` enables the LVGL QR widget in existing and fresh configurations.

Wi-Fi joins have a 30-second overall deadline, including DHCP, in addition to the disconnect retry limit. This applies after submitting setup details and at saved-network startup. On timeout, the pending join is disconnected and automatic retries stop; the AP setup/QR screen returns. Submitting revised details starts a fresh deadline. Successful IP acquisition cancels the deadline. A separate watchdog task enforces it even if no disconnect event arrives.

Captive interception code is removed: no wildcard DNS responder, catch-all HTTP route, redirects, or captive-probe endpoints are installed. The setup server matches only `/`, `/api/scan`, and `/connect`; other URLs receive the normal HTTP not-found response. Clients join `p4Radio-Setup`, then scan the hostname URL QR or manually open `http://<hostname>.local/`. The same bookmark works on the home network through mDNS. `http://192.168.4.1/` remains an AP-only fallback. The AP has no internet access.
