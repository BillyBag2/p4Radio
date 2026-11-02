# p4Radio

An internet radio for Waveshare ESP32-P4-86-Panel-ETH-2RO, M5Stack TAB5 and Waveshare ESP32-S3-Touch-LCD-1.85C V2.

The strategy for supporting additional hardware is documented in
[ESP-IDF-BOARDS.md](ESP-IDF-BOARDS.md), covering board selection, chip compatibility,
per-board build directories and SDK configuration, and shared hardware interfaces.

For operation see: [MANUAL.md](MANUAL.md).

## Web pages and screenshots

Open `http://p4radio.local/` (or your saved hostname) and use the Radio,
Stations, Wi-Fi and Advanced tabs. On **Advanced**, select **Save screenshot
to SD card** to capture the radio display as a BMP image. Insert a FAT-formatted
microSD card first; each capture gets a unique filename under `/screens/` on
the card. Wait for the saved message before removing the card. The firmware
unmounts the card after each capture and never formats it on mount failure.
An OFF display is captured as its current black screen without waking the radio.

Screenshot support requires `CONFIG_LV_USE_SNAPSHOT=y` (included in common
defaults); enable it in an existing SDK configuration as well. On Waveshare 86,
leave `CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL` disabled; screenshot mounting refuses
that destructive recovery setting. The capture uses a temporary RGB565 PSRAM
buffer (about 253 KB on 1.85C, 1013 KB on Waveshare 86, or 1800 KB on TAB5).

Hardware verification: capture all three skins and setup QR screens; inspect BMP
colours, orientation, black corners and TAB5 margins. Check missing, full and
write-protected cards, repeated captures without overwriting, concurrent browser
requests, retry after inserting a card, uninterrupted playback and OFF/tap wake.
Run `node tools/check_advanced_browser.mjs` for mocked browser workflow checks;
these do not verify LVGL rendering, SD writes or hardware behaviour.

---

## TODO

- [ ] Add a "factory reset" in configuration.
- [ ] Add a "clear saved stations" in configuration.
- [ ] Add artwork to the display.
- [ ] Workflow.
- [ ] TTS (PicoTTS?) for station names and status.
- [ ] Speech commands
- [ ] Documentation.
- [ ] Pages support.
- [ ] Secret hand shake to get to AP mode.
- [ ] Show directory-server details in settings.
- [ ] Off timers. (Bed, Meals)
- [ ] RTC support.
- [ ] NTP support.
- [ ] Set time zone ?
- [x] Set hostname.
- [x] Beep when waking from dormant.
- [x] Different colour to QR codes.
- [x] Sounds for QR codes.
- [x] Statically compiled display skins.
- [x] Circular layout and simulation on the square display.
- [x] Live station browser, filters and browser previews.
- [x] Add, save and remove custom stations.
- [x] Mute and re-enable saved stations.


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

| Component | ESP32-P4-86-Panel-ETH-2RO | M5Stack TAB5 | Waveshare ESP32-S3-Touch-LCD-1.85C V2 |
| --- | --- | --- | --- |
| Project configuration | `waveshare86` | `tab5` | `waveshare185c` (`esp32s3`) |
| Main processor | ESP32-P4NRW32, dual-core 32-bit RISC-V high-performance CPU up to 360 MHz, plus a low-power RISC-V core up to 40 MHz | ESP32-P4NRW32; dual-core RISC-V HP CPU at 360 MHz, LP core up to 40 MHz | ESP32-S3R8; dual-core Xtensa LX7, up to 240 MHz |
| Chip / board revision | ESP32-P4 revision 1.3. Target pre-v3 silicon branch. | Firmware targets pre-v3 silicon; individual board revision not verified | Board V2 / Rev2.0; S3 silicon revision not verified |
| PSRAM | 32 MB up to 200 MHz | 32 MB; firmware configured for HEX mode at 200 MHz | 8 MB octal PSRAM, configured at 80 MHz |
| Flash | 32 MB external SPI NOR flash | 16 MB SPI NOR flash | 16 MB SPI NOR flash |
| Display | 4-inch IPS, 720 x 720 pixels, 2-lane MIPI DSI; ST7703 driver; backlight enable GPIO33, PWM GPIO26, reset GPIO27 | 5-inch IPS, 1280 x 720 landscape; MIPI DSI; ILI9881C or ST7123/ST7121 depending on hardware revision; backlight GPIO22 | 1.85-inch round, 360 x 360; ST77916 over QSPI; backlight GPIO5 |
| Touch | GT911 five-point capacitive touch controller over I2C; SDA GPIO7, SCL GPIO8, reset GPIO23; interrupt is routed to a test point | Capacitive; GT911 or integrated ST7123/ST7121; I2C SDA GPIO31, SCL GPIO32, interrupt GPIO23 | CST816 capacitive touch; I2C SDA GPIO11, SCL GPIO10; interrupt GPIO4; reset via TCA9554 |
| Wi-Fi and Bluetooth | ESP32-C6-MINI-1U-H8 coprocessor connected over SDIO; Wi-Fi 6 (2.4 GHz) and Bluetooth 5 LE. Wireless support is provided by the C6, not the P4. | ESP32-C6-MINI-1U over SDIO; 2.4 GHz Wi-Fi 6 and Bluetooth LE | Native ESP32-S3 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 LE; no coprocessor |
| Ethernet | IP101 10/100 Mbit/s Ethernet PHY; RJ45 is on the 86 Panel bottom board | No onboard Ethernet | None onboard |
| Audio | ES8311 audio codec, ES7210 microphone ADC, onboard microphone and 8 ohm / 2 W speaker output | ES8388 codec, ES7210 dual-microphone front end; 8 ohm / 1 W speaker; 3.5 mm headphone jack | V2: ES8311 codec, ES7210 ADC, dual microphones, NS4150B amplifier; BOX speaker rated 4 ohm / 5 W |
| Storage expansion | TF/microSD slot | microSD slot | TF/microSD slot |
| USB | USB 2.0 High-Speed OTG and separate USB-to-UART interface | USB-A host and USB-C OTG | USB-C native USB 2.0 Full Speed |
| RS485 | Interface on the ETH-2RO bottom board | SIT3088; switchable 120-ohm termination | None onboard |
| Relays | Two isolated relay outputs on the ETH-2RO bottom board | None onboard | None onboard |
| Camera | No camera fitted; the ETH-2RO main PCB omits the camera connector found on the LCD-4B variant | Onboard 2 MP MIPI-CSI camera; sensor varies by revision | None onboard |
| Motion sensor | No onboard IMU listed | BMI270 accelerometer/gyroscope | No IMU listed |
| RTC | Rechargeable RTC battery header; RTC hardware details are not specified in the board overview | RX8130CE RTC with supercapacitor backup | PCF85063; rechargeable RTC battery header |
| Battery | RTC backup connection; main supply is external DC or USB-C | Removable NP-F550; included with TAB5 Kit, optional for standard TAB5 | 3.7 V lithium-battery connector with charging; battery supply varies by package |
| Expansion | 2.0 mm expansion headers and ETH-2RO bottom-board connection | HY2.0-4P, M5-Bus, GPIO_EXT and STAMP pads | UART, shared I2C and reserved 28-pin header |
| Power | DC 6-30 V input on the ETH-2RO bottom board; USB-C power/programming on the main board | USB-C, battery or 6-24 V RS485 power input | USB-C 5 V or 3.7 V battery |

Hardware references: [Waveshare board documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B),
[M5Stack TAB5 specifications and pin map](https://docs.m5stack.com/en/core/Tab5),
and [Waveshare 1.85C hardware revisions](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85C).
Firmware selections are recorded in each `sdkconfig.<board>` file. The 1.85C
configuration targets V2 / Rev2.0; V1 uses different audio hardware.
TAB5 display/touch variants are described by the vendor; the pinned BSP's support
and this firmware still require verification on the particular hardware revision.
Listed peripherals describe the hardware; p4Radio does not currently use every peripheral.

## Radio controls and setup

At first boot, the radio opens the unsecured `p4Radio-Setup` Wi-Fi network. Join it from a phone; its captive portal scans nearby networks and saves the chosen SSID and password in NVS before connecting. After Wi-Fi comes up, p4Radio fetches the selected BBC HLS playlist, extracts AAC audio from its MPEG-TS segments, and sends decoded audio to the selected board's codec and speaker output. The five presets are BBC Radio 1, 2, 4, 4 Extra, and 5 Live; Radio 4 Extra fills the repeated BBC 4 entry in the original list. Station and volume selections are retained across restarts.

The display has previous/next station, volume down/up, OFF, and TUNE controls. OFF uses an awake idle mode so touch polling and Wi-Fi remain active; tapping the dark screen resumes playback. This consumes more power than deep sleep. On the Waveshare 86 panel, the GT911 interrupt is not connected to an MCU GPIO, so deep sleep cannot provide touch wake.

On the P4 boards, Wi-Fi provisioning and playback require a compatible ESP32-C6 ESP-Hosted slave image. The S3 board uses its native Wi-Fi. The P4 host dependencies are pinned in [`main/idf_component.yml`](main/idf_component.yml). Waveshare's published examples do not include the C6 image, so a board with a missing or incompatible image cannot provide the setup AP until its C6 firmware is installed. The app reports that link failure on screen.

### Station browser and saved stations

Open **Stations** from the links near the top of Radio settings or Wi-Fi
settings (`http://<hostname>.local/stations`). **Find stations** searches as
names are typed, with country, language, genre/tag, codec, maximum bitrate,
HTTPS and working-stream filters, sorting and pagination. **Radio-compatible
only** filters each result page to AAC HLS, direct AAC and MP3 entries; use Next results to inspect
more matches or disable that filter to preview other codecs.

**Listen here** plays a preview on your phone/computer without changing the
radio's audio. Stop it beside the station or in the preview controls. HLS uses
native browser support or HLS.js 1.7.3, loaded on demand from jsDelivr. Browser
codec support, stream availability and CORS can prevent a preview; Open stream
provides a direct link. A browser preview is not proof the radio can decode it.
The radio supports continuous HTTP MP3/AAC (including AAC+) and unencrypted AAC in MPEG-TS or packed ADTS HLS, not
encrypted HLS or fragmented MP4. Unsupported formats cannot be added through
this browser.

**Add to radio** saves a compatible entry in NVS. The five presets plus up to
19 added stations share 24 stable slots; available NVS space and URL lengths
can impose a lower limit. If storage is full, the save fails visibly and the
current list stays active. If NVS space is exhausted, TUNE can clear the separate
stream cache before retrying; it retains the station list. No NVS partition
erase is needed to manage stations.

**My stations** lists enabled and muted entries, with local name/status filters.
**Mute** removes an entry from the radio's previous/next navigation while
keeping it in settings. **Enable** restores it. Added stations also have a
**Remove** button; presets can only be muted. Muting/deleting the playing
station selects another enabled entry. If all are muted, playback stops and the
screen asks for a station to be enabled. The catalog, mute states and selection
survive reboot; TUNE clears stream caches without deleting this list.

Searches and preview streams go directly from the browser to the directory and
broadcaster. The radio must be connected to Wi-Fi to discover directory servers;
saved-list management remains available on the setup AP. Discovery runs in a
background task and can run while the radio is OFF. Station edits are committed
to one versioned NVS blob before the live catalog changes. Stable entry IDs and
cache-owner tags prevent a newly added station from inheriting a removed one's
stream when its slot is reused. Custom entries refresh by Radio Browser UUID,
with the saved chosen URL as a fallback; BBC rate variants remain preset-only.

### Station discovery and stream cache

On first selection, the firmware searches Radio Browser for the preset's exact name (case insensitive), using a dynamically discovered directory server list. It considers AAC HLS, direct AAC and MP3 results and retains a BBC worldwide 96 kbit/s URL as a fallback. The five BBC presets remain available alongside added stations; the web Stations page provides live text search. Directory API: https://api.radio-browser.info/

The server list follows [Radio Browser's documented DNS SRV alternative](https://api.radio-browser.info/):
query `_api._tcp.radio-browser.info` through the network's configured DNS servers,
use the returned hostnames and ports for HTTPS, shuffle the list, then try the
next server if a request fails or yields no usable streams. No directory hostnames
are hardcoded. A fresh directory search performs a fresh lookup; a working cached
stream starts without contacting DNS or the directory. If discovery fails, the
existing bitrate-limited BBC stream fallbacks remain available. Truncated DNS
responses are retried over TCP. DNS replies are bounded to 4 KB and validated
before their hostnames are used.

A stream is saved in the `streams96` NVS namespace after 30 seconds of decoded PCM audio has been accepted by the speaker driver without a reported decode or output error. Playlist or segment download failures reset that qualification period. Saved URLs survive reboot and are tried before searching again. This is a software health check, not a measurement of audible quality.

Decode/output errors, unsupported segment formats, or 30 seconds without audio cause the player to try the next candidate. A failing cached URL is removed. After all candidates fail, the directory is searched again. Playback also retries after Wi-Fi reconnects. Search is performed when a station is selected, not as a background scan of every preset.

The player supports continuous HTTP MP3/AAC (including AAC+) and unencrypted AAC in MPEG-TS or packed ADTS HLS, including basic master playlists. M3U/PLS links and HTTP redirects are resolved with bounded nesting; ICY metadata is stripped before decoding. Encrypted HLS, fragmented MP4, Ogg and Opus are not supported and are not intentionally cached. Changing preset names/order in firmware should be accompanied by clearing the `streams96` namespace, since cache keys identify preset slots.

Use ESP-IDF 6.1 (or a compatible ESP-IDF release supported by the pinned components):

```sh
idf.py --preset waveshare86 set-target esp32p4
idf.py --preset waveshare86 build
idf.py --preset waveshare86 -p PORT flash monitor
```

Application defaults select performance optimization (`-O2`). Debug symbols,
assertions and diagnostic logging remain available.

Run `set-target` only for a fresh configuration; it regenerates SDK settings.
In VS Code, select chip `esp32p4` with **ESP-IDF: Set Espressif Device Target**,
then choose `waveshare86` with **ESP-IDF: Select Project Configuration** and use
the normal Build, Flash, Monitor and Debug commands. Chip selection remains
separate from board selection, following the extension's
[project configuration workflow](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/project-configuration.html).

Implemented configurations are `waveshare86` (Waveshare ESP32-P4-86-Panel-ETH-2RO)
and `tab5` (M5Stack TAB5), both targeting `esp32p4`, plus `waveshare185c`
(Waveshare ESP32-S3-Touch-LCD-1.85C V2) targeting `esp32s3`. Each keeps its build output
and generated SDK configuration in `build/<board>/`; previous root-level
`sdkconfig` and build outputs are left in place and are not migrated automatically.
Common defaults are in `sdkconfig.defaults`; hardware defaults are in
`sdkconfig.<board>`. Board/chip compatibility is
defined in `cmake/boards.cmake`, and display/audio BSP calls are implemented in
`main/boards/<board>/board.cpp` behind `main/board.h`.

To build TAB5 alongside Waveshare, select **ESP-IDF: Select Project Configuration >
tab5**, or run `idf.py --preset tab5 build` in the ESP-IDF 6.1 terminal. For a
fresh setup, use `idf.py --preset tab5 set-target esp32p4` first. Switch back by
selecting `waveshare86` or building with its preset; do not routinely run
`set-target` when returning to an existing build. Dependency versions are pinned
in `dependencies.lock` for Waveshare 86, `dependencies.tab5.lock` for TAB5 and
`dependencies.waveshare185c.lock` for the S3 board.
ESP-IDF manages a shared `managed_components/` directory. A shared board marker
causes cached builds to reconfigure when switching back, restoring their BSP
dependencies while retaining compiled output and SDK settings. Do not build
different boards concurrently in the same checkout.

TAB5 uses the [Espressif TAB5 BSP](https://components.espressif.com/components/espressif/m5stack_tab5/versions/1.3.2/readme)
for its display, touch, ES8388 speaker and Wi-Fi power control. Its defaults use
16 MB flash, 32 MB HEX PSRAM and pre-v3 ESP32-P4 silicon at 360 MHz. Its separate
partition table retains the 8 MB app slot and fits storage into 16 MB. The C6
SDIO connection uses CLK 12, CMD 13, D0-D3 11/10/9/8 and active-high reset 15,
following the [M5Stack pin map](https://docs.m5stack.com/en/core/Tab5).
The C6 must have firmware compatible with ESP-Hosted 2.12.11; this project does
not flash or update that coprocessor automatically.

ESP-Hosted's TAB5 reset setting must be **Active High**: its reset sequence ends
with GPIO15 (C6 EN) high, releasing the C6. Selecting Active Low leaves it low,
causing `sdmmc_init_ocr` timeouts (`0x107`). See the
[TAB5 reset-polarity report](https://github.com/m5stack/M5Tab5-UserDemo/issues/1).
For an existing build, search for `ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH` in
`idf.py --preset tab5 menuconfig` and select **RESET: Active High**, then rebuild.
Defaults do not override an existing `sdkconfig`; the TAB5 build rejects the
incorrect setting so an old configuration cannot silently retain it.

TAB5 starts in landscape (1280 x 720). Every skin fits the shorter display edge
and stays centered: Square and Retro stay square, and Circular stays circular
with inactive black corners. At this resolution the skin is 720 x 720, with
280-pixel black margins on each side. Geometry and fonts adapt to other display
sizes, using the closest compiled Montserrat face. Setup QR and wake overlays
stay inside the skin, and margin taps do not activate controls. Smaller screens
use the same scaling as the rest of the skin.

For a fresh 1.85C V2 configuration, run:

```sh
idf.py --preset waveshare185c set-target esp32s3
idf.py --preset waveshare185c build
```

In VS Code, select chip `esp32s3` and project configuration `waveshare185c`.
Its 16 MB flash, 8 MB octal PSRAM and 240 MHz CPU defaults are separate from the
P4 boards. Wi-Fi runs on the S3 itself; ESP-Hosted is used only by the P4 boards.
The port follows the [Waveshare V2 examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C)
for QSPI panel initialization, touch/LCD reset via TCA9554 and ES8311 audio.
It does not support the V1 PCM5101 audio hardware.

On the round 360 x 360 panel, Circular uses the full diameter. Square and Retro
are centered 254 x 254 squares inscribed in the circle, keeping their corners
visible. Fonts follow the skin's pixel ratio to the original 720-pixel layout:
Circular uses 50% sizes (24 -> 12, 40 -> 20, 48 -> 24 pixels). Inscribed Square
and Retro use the 254/720 ratio, choosing 8, 14 and 16 pixels respectively.
Button-symbol enlargement keeps its original multiplier after font selection;
letter/line spacing scales with the layout. Small-screen text fit and touch
alignment need on-board evaluation. The shared drawing pool is 768 KB on this
panel, leaving PSRAM for the unchanged twenty-second PCM buffer; P4 boards
retain their 3 MB pool. The default skin is Circular when NVS has no saved skin.

The 1.85C allocates TLS contexts and buffers in PSRAM so HTTPS segment downloads
can continue after the audio decoder starts. In ESP-IDF 6.1, TLS error `-0x008D`
means insufficient memory. Existing builds must also select **External SPIRAM**
under **mbedTLS > Memory allocation strategy** in menuconfig; changing defaults
does not replace the previous internal-only allocation setting.

The 1.85C also disables `CONFIG_MBEDTLS_HARDWARE_AES`: the IDF hardware AES
path can require temporary internal DMA buffers even when TLS records live in
PSRAM. Keeping two HTTPS clients exposed allocation failures in that path.
Software AES avoids those transient DMA allocations; encryption and certificate
verification remain enabled. Existing 1.85C configurations must disable
**Enable hardware AES acceleration** in menuconfig as well as selecting external
TLS memory. This board-specific setting leaves the P4 configurations unchanged.
HLS clients use 2 KB receive and 1 KB transmit staging buffers to reduce SRAM
pressure. Failed HLS requests log free and largest-block sizes for internal SRAM,
internal DMA memory and PSRAM before releasing the failed client, so the log
captures the remaining memory pressure before application cleanup. IDF may already
have released a failed transport internally; these numbers are not an exact
measurement of peak allocation.

The first configure/build resolves the pinned components from the ESP Component Registry.
Each board's partition table reserves an 8 MB app slot and uses the remaining
flash for storage: 32 MB total on Waveshare 86, 16 MB on TAB5 and the 1.85C V2.

## ESP-IDF configuration

### Display skins

Open `http://<hostname>.local/` and choose **Display skin** on the Radio
settings page, then save. All three skins are compiled into the firmware. The choice
applies within the next display refresh (up to half a second), is stored in NVS,
and survives reboot without reflashing or interrupting playback.

**Square** retains the existing layout. **Circular** fits the interface into a
720-pixel-diameter circle, with black, inactive corners on the square panel.
It keeps volume above station controls, with matching 236 x 100 pixel buttons
fully inside the curved edge and their symbols centered. The optional
volume and buffer indicators become shorter bars near the sides. QR setup
screens and OFF/tap wake use the same circular boundary; switching preserves
the current QR/dismissal and wake state.

**Retro** is a square vintage radio: a walnut-colored cabinet, a woven cloth
speaker grille, brass trim, and an amber station window with a red pointer
that follows the selected station. Four smaller round Bakelite-style buttons
handle volume and stations, with two separate OFF/TUNE keys. The controls have
cream symbols and a darker pressed appearance. Materials are drawn with LVGL
lines, gradients and borders; the visible buttons are the actual touch targets.
It keeps the hostname settings link, optional meters, QR setup and tap wake.

With LVGL's default built-in allocator, first use of Circular or Retro adds one
shared 3 MB PSRAM pool for drawing and clipping. The pool and each skin's widget tree stay
allocated for reuse; repeated switches do not keep allocating new screens.
`CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=3072` allows LVGL's TLSF allocator
to accept this pool. It is set in `sdkconfig.defaults`; existing configurations
also need this value in their active `sdkconfig` (menuconfig: Component config >
LVGL configuration > Memory Settings). A compile-time check rejects a pool
limit too small for these skins, rather than silently keeping Square.
The audio buffer keeps its existing capacity. A different physical panel
still needs its own BSP/driver.

For a radio with no saved skin preference, **p4Radio > Default display skin**
in `idf.py menuconfig` selects the initial choice. Fresh configurations default
to Square through `CONFIG_P4RADIO_SKIN_SQUARE=y` in `sdkconfig.defaults`.

`DisplaySkin` in `main/display_skin.hpp` owns the widgets and shared radio,
QR, meter and wake behavior. Each derived skin implements `build_controls()`
and `arrange_widgets()`; a shaped skin can override `contains_point()` to
restrict touch input. New skins also need a stable preference ID, a web settings
option, and a factory used by `main/display.cpp`; compile their sources in
`main/CMakeLists.txt`.

### Board configuration

The Waveshare board requires `esp32p4`. [`sdkconfig.waveshare86`](sdkconfig.waveshare86) selects the pre-v3 P4 silicon branch for this board's revision 1.3. ESP-IDF does not permit one binary to support both pre-v3 and v3-or-later P4 revisions, so this firmware configuration is for the reported rev 1.x board.

Board specifications: [Waveshare ESP32-P4-WIFI6-Touch-LCD-4B / ETH-2RO documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B). The 4B documentation covers the shared main board; the ETH-2RO SKU adds the Ethernet/RS485/relay bottom board and omits the camera connector.

### Volume and stream rates

Volume runs from 1 to 10 in single steps, with a thin vertical indicator at the right edge. Existing percentage preferences are converted on load. Button captions use the larger 40-pixel font.

The playing status includes measured encoded AAC bitrate in kbit/s and decoded mono/stereo channel mode. AAC bitrate is calculated after a complete decoded segment; until available it shows `--`. A horizontal red/green meter replaces the numeric Wi-Fi rate display. Its dark needle shows `log2(Wi-Fi download throughput / stream bitrate)`, clamped to -3 through +3: equal rates at center, one-eighth at the left stop and eight times at the right stop. The left half is light red and the right half light green, without numbers. Needle travel leaves a small margin at each end. Missing, zero, disconnected, or stale (15-second) throughput readings park the needle at its left stop. Download throughput includes connection setup and is not maximum Wi-Fi capacity.

Directory candidates and advertised HLS variants are ranked by distance from 96 kbit/s. Availability determines the actual bitrate; directory results can be higher. A new `streams96` cache namespace ensures old high-bitrate cache entries do not bypass the new selection preference.

Fallback URLs were checked on 23 October 2026: all five worldwide BBC 96 kbit/s endpoints returned HLS playlists and MPEG-TS segment samples. This verifies HTTP availability and transport format, not audible content or uninterrupted playback. Cache policy revision 2 clears the `streams96` namespace once after installing this firmware, preserving Wi-Fi and radio preferences.

To manually clear the entire NVS partition from an ESP-IDF terminal, close the serial monitor and run `python -m esptool --chip esp32p4 --port COM12 erase-region 0x9000 0x6000`. This partition address and length come from this project's `partitions.csv`. It removes saved Wi-Fi credentials, volume, station selection, and stream URLs; the next boot enters Wi-Fi setup. Normal flashing does not clear NVS.

### OFF and TUNE

OFF stops audio and turns off the PWM backlight while leaving LVGL/touch polling and Wi-Fi active. A full-screen black overlay consumes the wake tap, restores brightness, and resumes the station without deliberately disconnecting Wi-Fi. Wake becomes available once the playback task has finished its current HTTP operation and stopped the audio task. No deep sleep or GPIO hold is used; touch wake therefore works without a wired touch interrupt. This mode uses more power than deep sleep. The stream itself is reopened on resume, so some startup delay remains.

TUNE stops playback, clears all stream URLs in `streams96` and the in-memory candidate lists, then searches again for the selected station and resumes playback. Wi-Fi credentials, volume, and station selection are preserved. If offline, discovery waits for Wi-Fi. An active HTTP request may take up to its configured timeout before the playback task can clear the cache. Cache-clear failures are shown on screen and require another TUNE attempt.

### Buffered playback

Downloading/decoding and speaker output run in separate FreeRTOS tasks. A PSRAM PCM buffer holds twenty seconds at the decoded format (about 3.84 MB for 48 kHz stereo, 16-bit). Playback starts when the buffer reaches its ten-second target (half full). Downloading/decoding continues as source data becomes available, including above that target; bounded PCM writes wait only when the full twenty-second capacity is occupied, without overwriting buffered audio. Rate correction aims for half full rather than throttling downloads there. HLS arrives in published segment bursts, so buffer levels fluctuate and can exceed the target. After an underrun the output also waits for ten seconds of buffered audio before resuming. This adds startup/refill delay in exchange for more protection against download gaps. Startup still requires downloading the first complete HLS segment. Playback begins one published segment behind the live edge when possible, allowing immediate prefetch at the cost of extra live latency.

HLS retains separate HTTP clients for playlists and media segments, reusing TLS connections when the server allows it. All known pending segments are consumed before refreshing the playlist. Failed requests discard their connection; failed segment downloads refresh the playlist before retrying. If the previous segment has left the live window, playback logs a continuity warning and resumes at the oldest available segment.

Serial telemetry:

- `HLS HTTP`: request wall time, response bytes, HTTP/error status and number of new connections. Zero new connections indicates transport reuse.
- `HLS segment`: decoded audio duration, total cycle time, and separate playlist/download/extraction/decoder/PCM-queue timings. Decode timing surrounds decoder calls; queue timing includes waiting for PCM capacity. Sustained processing slower than the audio duration drains the buffer. Playlist-publication waits occur between these cycles.
- `Audio buffer`: fill, underruns, requested/applied correction, accepted PCM frame counts, output frames per wall-clock second and minimum free output-task stack. Output rate includes refill pauses and DMA queue effects; it is not a physical clock measurement.
- `GUI` every ten seconds: actual redraw cycles per second, average/maximum render and status-update durations, and percentage of elapsed time spent inside render cycles. Rendering includes scheduling/flush waits and excludes final asynchronous DMA completion, so this percentage is not CPU utilisation. Idle displays can report near-zero FPS. LVGL's configured refresh interval remains 33 ms; status/meter updates remain 500 ms. Unchanged labels are no longer reset.

After flashing, capture several minutes of these logs with each skin, including an underrun if one occurs. Also check direct HTTP playback, station changes, TUNE, and OFF/tap resume; host builds cannot verify server keep-alive behaviour, physical audio clocks, touch responsiveness, or uninterrupted playback.

The right-edge bar shows volume, and the left-edge blue bar shows buffer fill against its twenty-second capacity (halfway is the ten-second target). The buffer bar turns amber below one second. Numeric volume text is omitted. Station buttons use `<` and `>`, and volume buttons use `-` and `+`. Buffer duration and underrun count are logged every five seconds. The output clock stays at the decoded nominal sample rate. A streaming fractional resampler corrects gradual buffer drift with a maximum +/-1000 ppm (+/-0.1%) adjustment; positive correction drains the buffer faster. The controller samples once a second, smooths the level with a ten-second time constant, uses a 9-11 second dead band around the ten-second target, and ramps at no more than 50 ppm per second. Startup and underrun refill reset the controller. Buffer logs include the applied ppm. This is slow drift correction, not a replacement for network buffering, and can slightly change pitch/duration. The IDF clock-tuning API requires a stopped I2S channel, so live hardware retuning is not used. OFF, TUNE, and station changes cancel output and discard buffered audio. Cache qualification counts successful speaker output and restarts after underruns.

Both vertical meters have an opaque dark grey track marking their full range,
even when almost empty: the buffer track represents twenty seconds (ten seconds
at its midpoint), and the volume track represents maximum volume 10.

The five-second audio log also includes `PCM frames in/out` and `applied avg` in
ppm. These count only resampled blocks accepted by the codec: positive correction
sends fewer output frames than input frames, while negative correction sends
more. The applied average covers the preceding log interval, so it can differ
from the current controller setting while that setting changes; frame rounding
also causes small differences.

### Hostname and setup display

The hostname defaults to `p4radio` and is saved as `hostname` in the `p4radio` NVS namespace. Enter a new name alongside Wi-Fi details on the setup form; names are normalized to lowercase and accept 1?32 ASCII letters/digits with internal hyphens. Enter the name without `.local`. The screen header uses the saved name.

The managed `espressif/mdns` component advertises the hostname and HTTP service on the AP and station interfaces. Open `http://p4radio.local/` (or the saved name) from the same network. The setup web server remains available after joining Wi-Fi. Clients must support mDNS and the network must permit multicast; when connected to the setup AP, `http://192.168.4.1/` is also available.

In AP mode, the display shows a QR code for joining the open `p4Radio-Setup` Wi-Fi network while no clients are associated. Once any client associates, the QR code switches to `http://<hostname>.local/wifi` to open the setup page, with the URL and IP fallback printed below. Disconnecting all AP clients restores the Wi-Fi join QR code and instruction. Successful Wi-Fi provisioning restores the normal radio display. The first QR code joins Wi-Fi; the second QR code opens the setup page.

After adding these changes, run `idf.py update-dependencies` and `idf.py build` in the configured ESP-IDF terminal to resolve mDNS. `CONFIG_LV_USE_QRCODE=y` enables the LVGL QR widget in existing and fresh configurations.

Wi-Fi joins have a 30-second overall deadline, including DHCP, in addition to the disconnect retry limit. This applies after submitting setup details and at saved-network startup. On timeout, the pending join is disconnected and automatic retries stop; the AP setup/QR screen returns. Submitting revised details starts a fresh deadline. Successful IP acquisition cancels the deadline. A separate watchdog task enforces it even if no disconnect event arrives.

Captive interception code is removed: no wildcard DNS responder, catch-all HTTP route, redirects, or captive-probe endpoints are installed. The setup server matches the registered settings, Wi-Fi and Stations routes; other URLs receive the normal HTTP not-found response. Clients join `p4Radio-Setup`, then scan the hostname URL QR or manually open `http://<hostname>.local/`. The same bookmark works on the home network through mDNS. `http://192.168.4.1/` remains an AP-only fallback. The AP has no internet access.

Successful setup submissions use HTTP 303 to redirect from POST `/connect` to GET `/connecting`. Refreshing that page does not save credentials again or restart the Wi-Fi join deadline.

The connecting page shows a busy indicator, waits three seconds, then polls `/api/connection` once per second after each response. It navigates to `/` using GET only once Wi-Fi is connected, and shows a retry message if the join fails. Unreachable requests leave the page open and retry. A manual return link is available. If the setup AP closes on a successful join, reconnect the phone to the home network to reach the bookmarked hostname URL.

### Firmware source layout

- `main/main.cpp`: NVS initialization, cache migration, and startup orchestration.
- `main/radio.hpp` and `main/radio_state.cpp`: shared constants, interfaces, and state.
- `main/settings.cpp`: saved preferences, hostname validation, and status helpers.
- `main/network.cpp`: Wi-Fi lifecycle, join timeout, mDNS, and setup HTTP pages.
- `main/streams.cpp`: stream discovery/cache, HTTP fetches, HLS parsing, and MPEG-TS extraction.
- `main/playback.cpp`: AAC decoding, PSRAM buffering, speaker task, and playback lifecycle.
- `main/display.cpp`: LVGL layout, QR setup display, controls, and tap-to-resume.

### Button feedback

`assets/sounds/button.wav` is preserved unchanged. Run `python tools/convert_button_sound.py` to regenerate `main/sound_data_button.hpp`: a 24 kHz mono, signed 16-bit PCM array (1,560 samples / 3,120 bytes). The converter uses a windowed-sinc low-pass resampler and short endpoint fades; Python standard library only.

Button clicks and the wake tap request feedback without blocking the LVGL handler. During radio playback the sound is interpolated to the output sample rate and mixed into 16-bit PCM, with brief radio attenuation and clipping protection. A small task handles feedback when no radio audio is playing, including buffering periods. Speaker writes/open/close operations are serialized; OFF/TUNE/station transitions finish pending feedback before closing the codec. Rapid requests coalesce or restart the effect instead of building an unbounded queue.

UI button callbacks queue preference saves for a separate low-priority task and volume updates for audio processing. They do not wait for the speaker mutex, codec writes, or NVS commits. The audio task uses a short speaker-lock timeout and retries without blocking LVGL. Click requests wake it immediately. Preference writes are coalesced until 500 ms after the last button change; a sudden power loss during that interval can lose the most recent selection.

### Wi-Fi connection sound

`assets/sounds/data-24.wav` is converted by `python tools/convert_data_sound.py` to `main/sound_data_data.hpp` (24 kHz mono signed 16-bit PCM, 48,000 samples / 96,000 bytes). The original WAV is unchanged. The data sound loops at half sample amplitude while attempting to join saved Wi-Fi, including reconnects and joins submitted through setup, until an IP address is obtained or the join fails/times out. OFF suppresses it. Button feedback mixes over this layer. Loop playback uses the existing speaker task and mutex, without blocking UI callbacks or adding a decoder instance.

### Stream tuning sound

`assets/sounds/radio-tuning.wav` is preserved and converted by `python tools/convert_tuning_sound.py` from 44.1 kHz mono to `main/sound_data_tuning.hpp`: 24 kHz signed 16-bit PCM, 120,000 samples / 240,000 bytes. It loops at half amplitude during stream lookup, loading, initial buffering, and underrun refill. It stops when buffered playback starts, the attempt fails, or playback is stopped. Wi-Fi connection feedback takes priority if the network is reconnecting; button clicks layer over either background effect. No extra runtime decoder is required.

### Setup QR feedback

The Wi-Fi join QR screen uses a dark red background; the setup URL QR screen uses dark amber. QR modules remain black on white for scanning and text stays near-white. Entering either QR stage plays `disconnect.wav` once, including switching stages or returning after a failed join; the regular UI refresh does not retrigger it. Accepting new Wi-Fi details while setup is displayed dismisses the QR and requests `connect.wav`. This acknowledges saving/submitting details, not successful Wi-Fi association.

Run `python tools/convert_connect_sound.py` and `python tools/convert_disconnect_sound.py` to regenerate the 24 kHz mono PCM notification arrays (14,880 samples / 29,760 bytes each). Original WAV files are preserved. Notifications mix over the background effects, with button feedback remaining a separate layer. Notification requests coalesce to the latest event if transitions occur rapidly.

The tuning converter normalizes generated PCM peaks to -1 dBFS (about +11.2 dB gain for the current source). Compression is unnecessary for this asset; mixer attenuation and saturation protection remain. The source WAV is unchanged and the generated array has no clipped samples.

### Web radio settings

Open `http://<hostname>.local/` for radio settings (`/settings` remains an alias). Wi-Fi setup is at `/wifi`; each page links to the other near its top. This form saves independently of Wi-Fi setup and does not restart the connection or stream. It contains the hostname and **Show volume bar** / **Show buffer bar** checkboxes. Both bars default to off when no setting exists; enabling them shows the right volume or left buffer indicator on the next UI refresh. Their values survive normal flashing and power cycles.

Hostnames accept 1-32 ASCII letters/digits with internal hyphens and are normalized to lowercase. Empty names, spaces, dots (including `.local`), leading/trailing hyphens, and other characters are rejected by the firmware as well as the browser. Changes update mDNS and the screen header; update bookmarks after renaming. If an old hostname stops resolving during the save redirect, open the newly named URL (or `http://192.168.4.1/settings` in AP mode).

Settings are saved in the `p4radio` NVS namespace using `hostname` and `display_flags` (bit 0: volume bar, bit 1: buffer bar). Live settings update only after the write/commit succeeds. POST `/settings` redirects to GET `/?saved=1` to prevent refresh resubmissions. Wi-Fi credentials, station, volume level, stream cache, and buffer operation are preserved.

The second setup QR goes directly to `http://<hostname>.local/wifi`, with `http://192.168.4.1/wifi` as its AP-only fallback. Successful Wi-Fi connection returns the browser to the radio-settings home page.

The hostname header is an underlined `http://<hostname>.local/` link. Tapping it plays button feedback and shows a settings URL QR without interrupting playback. Tapping anywhere on the QR screen plays button feedback once and returns to the radio screen. Dismissed automatic setup QR screens remain hidden until the AP/client setup stage changes; the header can always reopen the settings QR from the normal screen. Automatic Wi-Fi QR codes still target `/wifi`.

### Desired stream bitrate

Radio settings offers Unlimited (default), 24, 32, 48, 64, 96, 128, 160, 192, 256, and 320 kbit/s. The desired rate uses the existing `max_bitrate` NVS key. Saving a changed preference clears stream candidates/cache and retunes; saving other settings does not.

Choose the highest known available rate at or below the preference. If none fit, choose the lowest known available rate, even if higher than requested. Unknown rates are tried last. Unlimited chooses the highest known rate. The same ordering applies to directory candidates and HLS master variants; alternative candidates are retained for failure recovery. For example, a 96 kbit/s preference allows Heart Gloucestershire's 128 kbit/s MP3 stream when no lower rate is available. BBC preferences below 48 kbit/s can still use the 48 kbit/s fallback.

HLS selection uses the nominal bitrate encoded in a BBC URL when present, otherwise advertised average bandwidth/BANDWIDTH. The setting concerns encoded stream rates, not instantaneous network throughput. Cache entries remain tagged with the preference used during qualification. The Stations page's maximum-bitrate search filter is independent and starts unrestricted so higher-rate fallback stations remain visible. Existing saved settings and stations are retained.

### Main screen layout

The 720 x 720 radio screen groups the large station controls under **STATION** and the volume controls under **VOLUME**. All button columns align and are 284 pixels wide; primary controls are 112 pixels tall, while OFF/TUNE are 76 pixels tall with clear spacing. Captions and the station title use a 48-pixel font. The status uses a brighter 24-pixel font and reads `Playing - <bitrate> kbit/s - mono/stereo`, avoiding repetition of the station title. The optional power meter sits above the control groups; optional side bars and the underlined hostname QR link retain their behavior.
