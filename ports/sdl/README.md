# FallOut — modern port

A port of the reconstructed FallOut source to current systems (macOS,
Linux, Windows). `src/fallout.c` and `src/gfx.c` are compiled **unmodified**:
a platform layer stands in for DOS, the BIOS and the Turbo C library, the
same way the differential-test harness in `tools/emu/` does.

Status: **milestone 3** — playable with sound (video, keyboard, real-time
clock, PC speaker). Next: `FALLOUT.HIS` (high scores are not saved yet).

## Build

Needs CMake ≥ 3.16, a C compiler and Python 3 (used at build time to
generate the compiled copy of `fallout.c`).

```
cmake -S ports/sdl -B build/sdl -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdl -j8
ctest --test-dir build/sdl          # unit tests + replay parity
build/sdl/fallout                   # play
```

SDL3 is built from a pinned release (3.4.18, SHA-256 checked) and linked
statically, so the binary needs nothing beyond the OS. The first configure
downloads it. To build offline, set `FETCHCONTENT_SOURCE_DIR_SDL3` to an
unpacked SDL3 source tree. `-DFALLOUT_SYSTEM_SDL=ON` links an installed SDL3
instead.

## Playing

The game's own keys are listed on its About screen (`A` in the main menu).
The port adds:

| Key | |
|---|---|
| F11, Alt+Enter, Cmd+Ctrl+F | fullscreen on/off |
| F12 | 4:3 picture (like a CRT) ↔ square pixels with integer scaling |
| Esc (during play) | pause, like `P` (the original's boss key ran a DOS command) |
| closing the window, Cmd+Q | quits as F10 does; a second request quits at once |

Keypad digits always act as digits (NumLock on), which the game treats like
the arrow keys. Keys pressed with Alt or Cmd never reach the game.

Options: `--fullscreen`, `--scale N` (window size N × 320×240, default 3),
`--square`, `--seed N` (fixed random seed), `--volume N` (0–100, default 50;
0 opens no audio device). `--help` lists them. The game's own `S` key turns
its sound on and off.

### Timing

The game is a DOS program that spins in loops. The port uses its calls into
the machine to pump events, draw and sleep:

* The clock is the BIOS tick count (18.2 Hz) since start. It never wraps at
  midnight; the original can stall when it does.
* A loop that keeps reading the same tick sleeps until the next tick or the
  next 70 Hz retrace, whichever is first. A key press wakes it at once.
* Loops that only poll the keyboard (pause screen, difficulty menu) ran as
  fast as the CPU allowed on the original. Here they advance once per 70 Hz
  retrace, mode 13h's refresh rate.
* `delay()` sleeps for exactly the requested time.
* The screen is redrawn when the frame buffer changes, at most once per
  display refresh. An idle game uses about 1–2% of a core.

### Sound

The game plays the PC speaker through Turbo C's `sound()`, `nosound()` and
`delay()`. `src/speaker.c` models what that did to the hardware (EXE offsets
0x7110 and 0x713C):

* `sound()` with the speaker off starts PIT channel 2 in mode 3, a square wave
  that starts high. An odd divisor N is high for (N+1)/2 clocks and low for
  (N−1)/2.
* `sound()` with the speaker already on only writes a new count. The 8254
  loads it at the end of the current half-cycle, so sweeps have no breaks.
* `sound(f)` with f ≤ 18 does nothing; `nosound()` drops the output at once.

Each sample is the exact fraction of its interval for which the output was
high, so even microsecond pulses come out right (the end-of-game tally makes
some). A gentle low-pass (8 kHz) and a DC-blocking high-pass (40 Hz) then
shape it like a small speaker. Samples are rendered at the device's own rate.

`src/audio.c` stamps every speaker change with the time the game made it,
renders up to "now" and queues the samples on an SDL audio stream. The queue
is the latency: the device buffer (512 frames) plus 20 ms, about 30 ms in all.
The sound card and the system clock drift apart, and the queue can run dry if
the game thread stalls. Both are corrected only after the speaker has been off
for longer than the queue is long, when everything queued is silence.

The original's `delay()` counted PIT clocks (1193 per ms) and nothing else ran.
Here a delay can overrun, for example while a frame is presented. While the
speaker sounds, its clock leaves the overrun out, so a 1 ms beep lasts 1 ms;
once the speaker is off, the speaker clock catches up with silence.

## How it fits together

```
src/fallout.c ──tools/emu/mkport.py──> fallout_port.c ─┐
src/gfx.c ──────#include──> src/gfx_port.c ────────────┼──> fallout
shim/ (dos.h conio.h io.h fo_prefix.h)                  │
src/machine.c  far memory, registers, INT 10h/1Ah/21h   │
src/tclib.c    Turbo C rand/srand, exit, system, files  │
src/replay.c   deterministic backend + trace            │
src/rt.c       realtime backend: clock, pacing, keys    │
src/video.c    window, palette, scaling                 │
src/speaker.c  PIT channel 2 → samples                  │
src/audio.c    SDL audio stream, latency ───────────────┘
```

* **`tools/emu/mkport.py`** (shared with the test harness) packs the
  uninitialised globals into one struct in their original order, so the
  original's reads past array ends hit the same neighbours (rotating on the
  bottom row reads `shell_cmd` through `board[17][col]`). It also applies three
  16-bit arithmetic patches where Turbo C's 16-bit `int` makes a difference.
  The one that matters in play is `bonus`: the end-of-game tally adds up to
  1000 per level, and on DOS that wraps at 65536.
* **`shim/fo_prefix.h`** is force-included into the game. It redirects
  `rand`/`srand` to Turbo C's generator. The host `rand()` would break the
  game, because it returns values up to 2³¹−1 and the game scales by 32767.
  It also redirects `exit`, `system`, `open`, `creat` and `close` to the
  platform, and renames the game's `shutdown()` so it cannot capture the
  POSIX socket call.
* **`src/gfx_port.c`** includes `src/gfx.c` and replaces the four routines that
  pass a buffer to the BIOS/DOS as `_DX = (unsigned)ptr`. On a 64-bit host that
  cast loses the pointer, so the replacements pass it in `shim_ptr` instead.
* **`src/machine.c`** implements the DOS machine. Anything that depends on how
  the game is being run goes through a `struct backend`.
* Compiler flags for the game: `-fsigned-char` (Turbo C's `char` is signed),
  `-fwrapv`, `-fno-strict-aliasing`.

## Keeping it faithful

`fallout --harness SCRIPT TRACE SEED [MAX_TICKS]` runs the game against the
same deterministic BIOS/DOS model as `tools/emu/harness.c` and
`tools/emu/run_original.py`. It takes the same environment variables
(`TICK_DIV`, `NO_VGA`, `ALLOC_FAIL`, `DUMP_TICKS`, `SHOT_TICKS`/`SHOT_DIR`) and
writes the same trace: a CRC of the frame buffer plus score, level, bonus and
total at every clock read and `delay()`, and every `sound()`/`nosound()` call.

* `tests/parity.sh` replays every `tests/*.keys` script and checks each trace
  against the **original executable's** trace. It compares SHA-256 hashes from
  `tests/golden.txt`, so no emulator is needed, and also compares byte for byte
  when `build/difftest/*.orig.trace` exists. The same binary and platform code
  serve both the replay and the game.
* `tests/mkgolden.sh` regenerates `golden.txt` after `tools/emu/run_all.sh` has
  produced fresh original traces.
* `port_unit` (`tests/test_port.c`) covers what replay cannot reach: the key
  map, the BIOS clock maths and the speaker (waveform clock by clock, reloads
  while sounding, area sampling, a filtered 1 kHz tone).
* The realtime build can be scripted for smoke tests: `--keys SCRIPT` types a
  key script's bytes at real BIOS ticks, and `--shot TICK:FILE.bmp` saves the
  window. Headless:
  `SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software build/sdl/fallout --keys …`
  Add `SDL_AUDIO_DRIVER=disk` to record the sound to `sdlaudio.raw` (float,
  stereo, at the rate SDL reports). Set `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` to
  1/100 of that rate: the disk driver sleeps in whole milliseconds.
* To compare against the emulated original directly:
  `NATIVE_BIN=build/sdl/fallout NATIVE_ARGS=--harness tools/emu/difftest.sh tests/smoke.keys`
