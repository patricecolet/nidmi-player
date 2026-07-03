# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

PlatformIO/Arduino firmware for the **nidmi-player** hardware (ESP32-S3 / ESP32-C3). It plays Standard MIDI Files (`.mid`) from flash and runs an XOX step sequencer, emitting events over WiFi via RTP-MIDI (AppleMIDI) and OSC UDP. Comments and serial UI are in French; keep new user-facing strings and doc consistent with that.

## Build, flash, run

```bash
./scripts/setup.sh                    # clone/update ../nidmi-core (run once after git clone)
pio run -e seeed_xiao_esp32s3         # build (default env; also esp32-c3-devkitm-1)
pio run -t upload                     # flash firmware
pio run -t uploadfs                   # upload data/ (.mid files) to LittleFS — separate step
pio device monitor                    # serial @ 115200; line commands + single keys, 'h' for help
```

Hardware is a Seeed XIAO ESP32-S3 (single native USB port). On macOS, opening the
serial port with DTR/RTS manipulation resets the board (sometimes into a sticky
download mode). `monitor_dtr = 0` / `monitor_rts = 0` are set in platformio.ini; for
scripted serial access open with pyserial `dsrdtr=True` after the app has booted.
If esptool is flaky, use `--no-stub --baud 115200`.

The sequencer engine dimensions are reduced for embedded RAM via `-DNIDMI_SEQ_MAX_*`
build flags in platformio.ini (defaults in nidmi-sequencer-core would need ~1.9 MB;
the reduced set fits in ~43 KB). Adjust there if patterns need more rows/steps/bars.

There is no test suite. Verification is done on-device via the serial monitor and an RTP-MIDI client (connect a Mac/iPad to WiFi AP `nidmi-player` / `nidmipass`, session name `nidmi-player`, AP IP `192.168.4.1`).

## External dependencies — sibling repos, not submodules

`platformio.ini` pulls two libraries via `lib_extra_dirs` from **sibling directories that must exist next to this repo**:

- `../nidmi-core` — namespace `nidmi_core`. Networking (`netBeginSoftAp`), `RtpMidiService`, `OscUdpService`, `MidiOscRouter`. `scripts/setup.sh` clones this from GitHub.
- `../nidmi-sequencer-core` — namespace `nidmi_seq`. The platform-independent `SequencerEngine`, `SequencerCommandApi`, `SequencerClockDriver`. **Not** cloned by setup.sh; it must be checked out manually alongside the others.

`extra_scripts` runs a pre-build script from nidmi-core that disables Bluetooth/SLIP so the cnmat/OSC library links on ESP32-S3. If a build fails at link time complaining about OSC/Bluetooth, that script (or a missing nidmi-core) is the usual cause.

## Architecture

Everything runs cooperatively in a single `loop()` (`src/main.cpp`) — no RTOS tasks, no blocking. Each subsystem exposes a non-blocking `update()` called every iteration and uses `esp_timer_get_time()` for scheduling.

Two independent sources feed **one shared output**, `MidiFanOut` (`src/MidiFanOut.h`), which duplicates every event to both `nidmi_core::RtpMidiService` (WiFi) and `nidmi_core::EspUartMidiTransport` (wired MIDI DIN/TRS out, GPIO43 TX on the XIAO):

- **`MidiPlayer`** (`src/MidiPlayer.*`) — file playback. `SmfParser` (`src/SmfParser.*`) parses a whole `.mid` (format 0 and 1) into an in-memory, tick-sorted `std::vector<MidiEvent>` with absolute times pre-computed in microseconds. `MidiPlayer::update()` walks that list against the clock and also emits MIDI Clock (24 ppqn) + Start/Stop/Continue. Format 1 tracks are merged; SMPTE division and SysEx are not supported; only the tempo meta event (0x51) is honored.
- **`EspSequencerAdapter`** (`src/sequencer/`) — the ESP-side glue around `nidmi_seq::SequencerEngine`. It owns the engine + a `SequencerClockDriver`, translates transport calls into `SequencerCommand`s via `SequencerCommandApi::dispatch`, and drains the engine's event queue (`engine.events()`) into `out_.sendNoteOn/Off` (the shared `MidiFanOut`). **The sequencer's logic lives in nidmi-sequencer-core, not here** — this repo only adapts it to hardware. Pattern editing goes through `g_seq.engine()` directly (see `loadDemoPattern()` in main.cpp).

The player and sequencer can run alone or simultaneously; both write to the same fan-out. `MidiOscRouter` (prefix `/nidmi`) additionally mirrors RTP-MIDI traffic to OSC and back over the AP interface.

### Serial command handling

`SerialLineReader` (`src/app/SerialLineReader.h`) is wired into `main.cpp`'s `loop()`; every line is dispatched by `handleLine()`. A single-character line is a keyboard shortcut (`handleKey()`: `p`/`s`/space/`l`/`i`/`f`, `1`/`2`/`3`/`t` for the sequencer). A line starting with `player`/`seq`/`help` goes through the matching text parser (`playerCommandLine`, `sequencerCommandLine`). **A line starting with `{` is JSON** and goes to `JsonCommandApi::handle()` (`src/app/JsonCommandApi.*`) — see below.

### JSON command API (config, player control, MIDI file upload)

`JsonCommandApi` is a transport-agnostic dispatcher: `handle(const char* requestLine) -> String`, newline-delimited JSON request/response. It doesn't know about the serial port — a future WiFi transport (WebSocket/HTTP) can call the exact same method. Commands: `config.get/set/reboot` (network settings — SSID/pass/mDNS/RTP name/OSC target — persisted to `NetworkConfig`/`/config.json` on LittleFS, applied on next `config.reboot`, not hot-reloaded), `player.play/stop/pause/toggle/loop/load/info` (thin wrapper over the existing `MidiPlayer`), `file.begin/chunk/end/abort/list/delete` (upload a `.mid` at runtime over base64-chunked JSON lines — previously the only way to add files was `pio run -t uploadfs`, which requires PlatformIO on the host and rewrites the whole filesystem). Full protocol and design rationale in `docs/USB_CONFIG.md` §7-8. Uses `bblanchon/ArduinoJson` (added to `platformio.ini`); base64 decoding uses `libb64/cdecode.h`, already bundled in the Arduino-ESP32 core (no extra dependency).

`main.cpp`'s `setup()` mounts LittleFS and loads `/config.json` **before** starting WiFi/RTP-MIDI, so persisted network settings apply from boot.

## Ongoing studies

`docs/USB_CONFIG.md` — steps 1 (line protocol), 2 (JSON config/player/file-upload API), and 3 (`tools/web-serial-console.html`, a host-side control page) are implemented. Steps 1-2 are hardware-validated; step 3 is validated via a mocked Playwright scenario (Web Serial needs a real user gesture + OS port picker, not automatable against real hardware in a sandbox). An actual WiFi transport reusing the same `JsonCommandApi` is not built yet. Read the doc before touching serial command handling, `JsonCommandApi`, or adding a WiFi control surface.

`tools/web-serial-console.html` is a self-contained file (no CDN, no build step) meant to be opened directly in Chrome/Edge desktop — Web Serial doesn't work in an embedded/iframe context, so it's a real repo file, not a hosted preview.

## Note on the README

`README.md` is partly stale: it describes `StepData.h` and `StepSequencer.h/cpp` in `src/`, but that code has since moved into the `nidmi-sequencer-core` library. Trust the actual source tree over the README's architecture section; the timing formulas and step-sequencer concepts it documents still apply (they now live in nidmi-sequencer-core).
