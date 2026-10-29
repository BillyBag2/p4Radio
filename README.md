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

## TODO

- [ ] Set hostname.
- [ ] Beep when waking from dormant.
- [ ] Different colour to QR codes.
- [ ] Sounds for QR codes.
- [ ] Secret hand shake to get to AP mode.
- [x] Statically compiled display skins.
- [x] Circular layout and simulation on the square display.
- [x] Live station browser, filters and browser previews.
- [x] Add, save and remove custom stations.
- [ ] Show directory-server details in settings.
- [x] Mute and re-enable saved stations.
- [ ] Off timers. (Bed Meals)
- [ ] RTC support.
- [ ] NTP support.
- [ ] Set time zone ?]


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

The display has previous/next station, volume down/up, OFF, and TUNE controls. OFF uses an awake idle mode so touch polling and Wi-Fi remain active; tapping the dark screen resumes playback. This consumes more power than deep sleep. The GT911 interrupt is not connected to an MCU GPIO on this board, so deep sleep cannot provide touch wake.

Wi-Fi provisioning and playback require a compatible ESP32-C6 ESP-Hosted slave image. The P4 host dependencies are pinned in [`main/idf_component.yml`](main/idf_component.yml). Waveshare's published examples do not include the C6 image, so a board with a missing or incompatible image cannot provide the setup AP until its C6 firmware is installed. The app reports that link failure on screen.

### Station browser and saved stations

Open **Stations** from the links near the top of Radio settings or Wi-Fi
settings (`http://<hostname>.local/stations`). **Find stations** searches as
names are typed, with country, language, genre/tag, codec, maximum bitrate,
HTTPS and working-stream filters, sorting and pagination. **Radio-compatible
only** filters each result page to AAC HLS entries; use Next results to inspect
more matches or disable that filter to preview other codecs.

**Listen here** plays a preview on your phone/computer without changing the
radio's audio. Stop it beside the station or in the preview controls. HLS uses
native browser support or HLS.js 1.7.3, loaded on demand from jsDelivr. Browser
codec support, stream availability and CORS can prevent a preview; Open stream
provides a direct link. A browser preview is not proof the radio can decode it.
The radio still supports unencrypted AAC in MPEG-TS or packed ADTS HLS, not MP3, direct AAC,
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

On first selection, the firmware searches Radio Browser for the preset's exact name (case insensitive), using a dynamically discovered directory server list. It considers AAC HLS results and retains a BBC worldwide 96 kbit/s URL as a fallback. The five BBC presets remain available alongside added stations; the web Stations page provides live text search. Directory API: https://api.radio-browser.info/

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

The player supports unencrypted AAC in MPEG-TS or packed ADTS HLS, including basic master playlists. MP3, direct continuous AAC, encrypted HLS, and fragmented MP4 are not supported and are not intentionally cached. Changing preset names/order in firmware should be accompanied by clearing the `streams96` namespace, since cache keys identify preset slots.

Use ESP-IDF 6.1 (or a compatible ESP-IDF release supported by the pinned components):

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```

The first configure/build resolves the pinned components from the ESP Component Registry. The 32 MB custom partition table reserves an 8 MB app slot and the remaining space for storage.

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

The project targets `esp32p4`. [`sdkconfig.defaults`](sdkconfig.defaults) selects the pre-v3 P4 silicon branch for this board's revision 1.3. ESP-IDF does not permit one binary to support both pre-v3 and v3-or-later P4 revisions, so this firmware configuration is for the reported rev 1.x board.

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

Downloading/decoding and speaker output run in separate FreeRTOS tasks. A PSRAM PCM buffer holds twenty seconds at the decoded format (about 3.84 MB for 48 kHz stereo, 16-bit). Playback starts at 0.5 seconds; decoding pauses above a ten-second target. After an underrun the output waits for two seconds before resuming. Startup still requires downloading the first complete HLS segment. Playback begins one published segment behind the live edge when possible, allowing immediate prefetch at the cost of extra live latency.

The right-edge bar shows volume, and the left-edge blue bar shows buffer fill against its twenty-second capacity (halfway is the ten-second target). The buffer bar turns amber below one second. Numeric volume text is omitted. Station buttons use `<` and `>`, and volume buttons use `-` and `+`. Buffer duration and underrun count are logged every five seconds. The output clock stays at the decoded nominal sample rate. A streaming fractional resampler corrects gradual buffer drift with a maximum +/-1000 ppm (+/-0.1%) adjustment; positive correction drains the buffer faster. The controller samples once a second, smooths the level with a ten-second time constant, uses a 9-11 second dead band around the ten-second target, and ramps at no more than 50 ppm per second. Startup and underrun refill reset the controller. Buffer logs include the applied ppm. This is slow drift correction, not a replacement for network buffering, and can slightly change pitch/duration. The IDF clock-tuning API requires a stopped I2S channel, so live hardware retuning is not used. OFF, TUNE, and station changes cancel output and discard buffered audio. Cache qualification counts successful speaker output and restarts after underruns.

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

### Maximum stream bitrate

Radio settings offers Unlimited (default), 24, 32, 48, 64, 96, 128, 160, 192, 256, and 320 kbit/s. The selected maximum is stored as `max_bitrate` in `p4radio` NVS. Saving a changed limit clears stream candidates/cache and retunes; saving other settings does not. A finite ceiling prefers the highest available rate within that ceiling; Unlimited selects the highest available advertised bitrate without a ceiling.

Finite limits exclude directory entries with unknown or excessive advertised rates. BBC candidates include 320 and 128 kbit/s UK endpoints plus 96 and 48 kbit/s worldwide endpoints, filtered by the ceiling. Higher UK endpoints may be unavailable outside the UK. Lower limits than 48 have no BBC fallback. HLS variants must fit the limit using the nominal bitrate encoded in a BBC URL when present, otherwise their advertised average bandwidth/BANDWIDTH. This limits advertised audio rates, not instantaneous network traffic or variable-bitrate peaks. Cache entries are tagged with the limit under which they were qualified, so old unrestricted cache entries cannot bypass a finite limit after reboot. If no eligible stream is available, the radio displays a message and retries; increase the limit to restore more choices.

Stream cache revision 3 invalidates URLs selected by the old always-prefer-96 policy once after flashing. Finite bitrate ceilings now try the highest permitted directory or BBC candidate first, falling through to lower rates on failure.

Cache revision 4 invalidates selections made under the former Unlimited-prefer-96 policy. Unlimited now ranks streams and HLS variants by highest advertised bitrate, including the BBC 320 kbit/s candidates. Displayed bitrate is measured and can differ slightly from the nominal value.

**Show power meter** on the radio settings page controls the horizontal Wi-Fi/stream ratio meter. It defaults off, applies immediately after saving, and persists in bit 2 of the NVS `display_flags` value. Existing volume and buffer visibility settings are preserved.

### Main screen layout

The 720 x 720 radio screen groups the large station controls under **STATION** and the volume controls under **VOLUME**. All button columns align and are 284 pixels wide; primary controls are 112 pixels tall, while OFF/TUNE are 76 pixels tall with clear spacing. Captions and the station title use a 48-pixel font. The status uses a brighter 24-pixel font and reads `Playing - <bitrate> kbit/s - mono/stereo`, avoiding repetition of the station title. The optional power meter sits above the control groups; optional side bars and the underlined hostname QR link retain their behavior.
