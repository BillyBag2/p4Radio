# Working on p4Radio

## Project and hardware

This is ESP-IDF C++ firmware for the Waveshare ESP32-P4-86-Panel-ETH-2RO,
with a 720 x 720 LVGL touchscreen and ES8311 speaker codec. Use ESP-IDF 6.1.
The target board reports ESP32-P4 revision 1.3: preserve the pre-v3 silicon
selection, 360 MHz clock, 32 MB flash, and 32 MB HEX PSRAM configuration in
`sdkconfig.defaults`.

Wi-Fi is provided by the ESP32-C6 coprocessor over SDIO using ESP-Hosted and
ESP-WiFi-Remote. The P4 itself has no Wi-Fi. Do not replace this initialization
with a standalone-P4 Wi-Fi example. Hardware ECDSA verification is disabled
because this silicon revision does not support that peripheral.

Read the relevant source before changing behavior. README contains historical
descriptions as well as current instructions; check implementation when they
disagree, especially around dormant-state wake handling.

## Source map

| File | Responsibility |
| --- | --- |
| `main/main.cpp` | Startup, NVS initialization, stream cache migration |
| `main/radio.hpp`, `main/radio_state.cpp` | Shared interfaces, constants, state, station presets |
| `main/settings.cpp` | Preferences, hostname validation, shared status helpers |
| `main/network.cpp` | Wi-Fi/AP lifecycle, join deadline, mDNS, setup HTTP handlers |
| `main/streams.cpp` | Radio Browser discovery, URL cache, HTTP, HLS, MPEG-TS extraction |
| `main/radio_browser_dns.cpp`, `.hpp` | DNS SRV server discovery, bounded wire parser and shuffled HTTPS origins |
| `main/station_catalog.cpp`, `.hpp`, `station_rules.hpp` | Stable station slots/identities, NVS catalog, mute/navigation and cache-owner rules |
| `main/station_web.cpp`, `main/web/stations.*` | Station-management JSON API, async directory discovery and client-side browser/previews |
| `main/playback.cpp` | AAC decoding, PCM buffering, audio output task, idle/resume lifecycle |
| `main/display.cpp` | BSP display startup, selected skin and refresh timer |
| `main/display_skin.cpp`, `.hpp` | Abstract skin base, shared widgets, controls, QR and wake behavior |
| `main/display_skin_square.cpp`, `display_skin_circular.cpp` | Runtime-selectable compiled layouts; circular clipping and touch boundary |
| `main/display_skin_retro.cpp` | Square vintage cabinet, cloth drawing, Bakelite controls and preset dial pointer |
| `main/button_audio.cpp`, `.hpp` | Sound layers, speaker serialization, deferred volume/preferences |
| `main/sound_data_*.hpp` | Generated PCM assets; regenerate rather than hand-edit |
| `tools/convert_*_sound.py` | Standard-library Python asset conversion scripts |

Keep new functionality in its owning module; keep `main.cpp` focused on startup.
Add new compilation units to `main/CMakeLists.txt`. Keep module-private state
local; add declarations to `radio.hpp` only when another module needs them.
All display skins are compiled and selected on the web settings page, saved
as the `skin` byte in `p4radio` NVS (0 square, 1 circular, 2 retro). Never reuse
or reorder persisted IDs; `kDisplaySkins` supplies web choices and validation. Menuconfig's
`p4Radio > Default display skin` only supplies the initial choice without NVS.
Keep shared behavior in `DisplaySkin` and layout in derived classes. Switch
skins only in the LVGL context; retain each widget tree after first use, and
carry QR dismissal/manual state and the wake overlay across switches. Preserve
black, inactive corners for the circular skin, including QR and wake overlays.
Circular and Retro share one lifetime 3 MB PSRAM drawing pool in `DisplaySkin`.
Do not allocate a separate pool each time a skin is activated. Keep
`CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=3072` in defaults and active config:
LVGL 9.3 derives its TLSF block limit from the base heap plus this allowance;
the default 64 KB limit rejects the drawing pool even if PSRAM is available.
Retro draws textures in LVGL draw callbacks; keep those callbacks limited to
drawing, with decorative widgets non-clickable. `refresh_theme()` updates its
preset needle without changing playback or persistence behavior.

## Build and dependencies

Run commands from the repository root in an initialized ESP-IDF terminal:

```sh
idf.py build
idf.py -p PORT flash monitor
```

For a fresh target configuration, use `idf.py set-target esp32p4`. Do not run
this routinely on an existing setup: it regenerates configuration. Use the
actual device port, not a hardcoded COM number. Close other serial monitors
before flashing. The board's USB-to-UART and native USB interfaces are distinct.

Dependencies are declared in `main/idf_component.yml` and resolved in
`dependencies.lock`. After manifest changes, run `idf.py update-dependencies`
and review the lock diff for unrelated updates. IDF 6 uses managed
`espressif/cjson`, not the removed built-in `json` component.

`sdkconfig` is ignored and machine-local; durable configuration changes belong
in `sdkconfig.defaults`. Existing configurations may also need their active
settings updated because defaults do not override them. Do not edit downloaded
`managed_components/` or generated `build/` files to implement project fixes.

If the IDF Python environment/toolchain is missing, report the exact blocker;
do not claim a successful build or install/replace the user's setup implicitly.

## Behavior to preserve

- Finite bitrate ceilings prefer the highest permitted AAC HLS rate. Unlimited
  selects the highest available rate without a ceiling. BBC candidates include UK 128/320 kbit/s
  and worldwide 48/96 kbit/s variants.
  Respect the NVS-backed maximum bitrate for directory entries, cache and HLS
  variants; lower caps can select the 48 kbit/s BBC fallback. Never silently
  exceed a finite cap. Unlimited also selects the highest available rate.
  The player supports unencrypted AAC in MPEG-TS and basic master playlists;
  enabled decoder libraries do not imply support for other stream containers.
- Decoding/download and speaker output run concurrently. PCM capacity is twenty
  seconds, startup threshold 0.5 seconds, target ten seconds, and underrun
  refill two seconds. The I2S clock stays fixed; bounded fractional PCM resampling corrects drift
  within +/-1000 ppm, with a smoothed ten-second target and 9-11 second dead
  band. Preserve the interpolation phase across blocks and reset on refill. Keep PSRAM buffers
  out of task stacks and preserve cancellation/join-before-free ordering.
- Volume is 1-10. The optional right-edge bar shows volume; the optional left-edge bar shows buffer fill.
  Both default off and are configured at `/` (`/settings` alias), persisted in NVS.
  The horizontal power/ratio meter is independently optional and defaults off
  (`display_flags` bit 2); preserve the other display flag bits when saving. There is no
  numeric volume label. Station buttons are `<`/`>` and volume buttons `-`/`+`.
- OFF is awake idle, not deep sleep: backlight off, Wi-Fi and touch polling
  active. The touch interrupt is not wired to an MCU wake pin. A wake tap must
  be consumed without activating an underlying button. Restore the screen
  promptly even if playback cleanup is still pending.
- TUNE clears stream URLs/candidates and forces fresh discovery. Preserve Wi-Fi,
  hostname, volume, and station preferences.
- Obtain Radio Browser servers through the documented `_api._tcp.radio-browser.info`
  SRV lookup using the network's DNS configuration. Shuffle discovered origins
  and try the next on failure; never pin directory hosts. Cached streams bypass
  discovery. Run DNS in playback or the dedicated browser-discovery task, off LVGL/HTTP callbacks, and retain
  BBC fallbacks if DNS/directory access fails. Truncated UDP replies retry over
  TCP; DNS buffers are on the heap, with bounded, cancellable socket waits.
- Wi-Fi joins have a 30-second deadline including DHCP. Failure restores AP
  setup; successful IP acquisition cancels the deadline.
- Default hostname is `p4radio`, saved in NVS and advertised by mDNS on AP/LAN.
  Keep the bookmark `http://<hostname>.local/` usable in both modes.
- AP setup first shows a Wi-Fi join QR, then a hostname `/wifi` URL QR after a client
  associates. Submission returns to the normal display. Captive interception
  is intentionally removed: do not add wildcard DNS, probe routes, or redirects.
- Setup POST uses HTTP 303 to GET `/connecting`. Its busy page waits three
  seconds and polls status without resubmitting credentials or restarting joins.
- `/stations` searches Radio Browser directly from the browser, with debounced
  and cancellable requests, pagination and filters. `/api/directory` supplies
  SRV-discovered HTTPS origins asynchronously, including while OFF. Previews
  play on the client (native audio or pinned HLS.js), not the speaker. Treat
  directory data as untrusted text; never interpolate it into HTML.
- Keep five immutable preset slots (0-4) and up to 19 custom slots. Muted entries
  remain in settings but are skipped by navigation; only custom entries can be
  removed. All-muted state is valid: `g_station == -1`, token 0, no playback.
  Use station snapshots/name helpers instead of indexing presets with g_station.
  Retro's dial maps the enabled list, not raw slot numbers.
- Catalog writers run only in the HTTP task: edit a heap snapshot, commit the
  bounded versioned `p4radio/stations` NVS blob, then publish POD records under
  the short catalog critical section. Never perform NVS or string allocation
  inside that section. Reused slots receive monotonically increasing IDs;
  compare the selected token in audio cancellation and cache ownership to
  prevent delete/re-add races. Keep candidate arrays private to playback.

## Concurrency and audio

LVGL callbacks must not block on speaker locks, HTTP, audio writes, or NVS commits.
Queue requests instead. Create/modify LVGL objects in the UI context or with the
BSP display lock. Cross-task state uses atomics or explicit locking; do not
introduce unsynchronized flags or shared string mutations.

Speaker open/write/close operations share the lock in `button_audio.cpp`.
Volume changes during playback apply on an audio block. Preference writes are
debounced in a separate task. Avoid reintroducing UI lock waits, long drain
delays, or closing the standalone speaker immediately after queuing a click.
Account for scheduler tick duration when sizing loop output: tiny chunks caused
audible gaps at 48 kHz stereo. Increasing task stack usage requires reviewing
the corresponding task allocation.

Wi-Fi data audio takes priority over stream tuning audio; button feedback layers
over both. Preserve original WAV files. Regenerate checked-in 24 kHz mono PCM:

```sh
python tools/convert_button_sound.py
python tools/convert_data_sound.py
python tools/convert_tuning_sound.py
```

## Persistence and verification

`p4radio` NVS stores credentials, hostname, volume, station selection, skin and
the station catalog (a blob, not a string: it can exceed NVS's string limit).
`streams96` stores qualified stream URLs and cache revision. Preset cache keys
refer to slots, with owner IDs for added stations; legacy untagged cache is
accepted only for unchanged BBC presets. Reordering presets requires a migration. Do not
erase all NVS to clear stream URLs. Full NVS erase also removes Wi-Fi credentials.
Do not log passwords or commit device credentials.

Preserve unrelated working-tree and staged changes. Use `git diff --check` and
build when the environment permits. There is no established automated test
suite; do not substitute host checks for hardware verification. For relevant
changes, describe board checks such as wrong-password timeout, AP QR transitions,
refresh without POST replay, uninterrupted playback, immediate volume/clicks,
TUNE, and OFF/tap resume. State which checks actually ran and what remains
unverified. Do not flash, erase storage, stage, or commit unless requested.

DNS wire-parser regression checks run at compile time with the ESP-IDF compiler:
`riscv32-esp-elf-g++ -std=gnu++26 -fsyntax-only -I main tools/check_radio_browser_dns.cpp`.
They check the actual parser, not live DNS/network behavior on the board.

Station checks: `riscv32-esp-elf-g++ -std=gnu++26 -fsyntax-only -I main tools/check_station_rules.cpp`
and `node tools/check_station_browser.mjs`. The latter runs the actual page
script with mocked DOM/network/audio; it does not verify rendering, CORS or
media decoding. On-board checks must include NVS round-trip, all stations muted,
mute/delete during playback, stale tab actions, delete/re-add cache isolation,
TUNE retaining the catalog, and OFF/wake after changing the enabled list.
