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

Two independent sources feed **one shared output**, `nidmi_core::RtpMidiService`:

- **`MidiPlayer`** (`src/MidiPlayer.*`) — file playback. `SmfParser` (`src/SmfParser.*`) parses a whole `.mid` (format 0 and 1) into an in-memory, tick-sorted `std::vector<MidiEvent>` with absolute times pre-computed in microseconds. `MidiPlayer::update()` walks that list against the clock and also emits MIDI Clock (24 ppqn) + Start/Stop/Continue. Format 1 tracks are merged; SMPTE division and SysEx are not supported; only the tempo meta event (0x51) is honored.
- **`EspSequencerAdapter`** (`src/sequencer/`) — the ESP-side glue around `nidmi_seq::SequencerEngine`. It owns the engine + a `SequencerClockDriver`, translates transport calls into `SequencerCommand`s via `SequencerCommandApi::dispatch`, and drains the engine's event queue (`engine.events()`) into `rtp_.sendNoteOn/Off`. **The sequencer's logic lives in nidmi-sequencer-core, not here** — this repo only adapts it to hardware. Pattern editing goes through `g_seq.engine()` directly (see `loadDemoPattern()` in main.cpp).

The player and sequencer can run alone or simultaneously; both write to the same RTP-MIDI stream. `MidiOscRouter` (prefix `/nidmi`) mirrors RTP-MIDI traffic to OSC and back over the AP interface.

### Serial command handling — note the split

`main.cpp`'s `handleSerial()` implements **single-key** commands inline (`p`/`s`/space/`l`/`i`/`f` for the player; `1`/`2`/`3`/`t` delegated to `sequencerCommandKey`). Separately, `src/app/` contains **line-based** command parsers — `SerialLineReader`, `playerCommandLine`, `sequencerCommandLine` — that are a more capable text-command interface but are **not currently wired into `main.cpp`'s loop**. If you add commands, decide which of the two paths you're extending rather than assuming the app/ handlers are live.

## Ongoing studies

`docs/USB_CONFIG.md` — feasibility study (not yet implemented) for a config/control server over USB-CDC on both S3 and C3, using the existing-but-unwired `src/app/` line-command handlers as its foundation. Read it before touching serial command handling or adding USB features.

## Note on the README

`README.md` is partly stale: it describes `StepData.h` and `StepSequencer.h/cpp` in `src/`, but that code has since moved into the `nidmi-sequencer-core` library. Trust the actual source tree over the README's architecture section; the timing formulas and step-sequencer concepts it documents still apply (they now live in nidmi-sequencer-core).
