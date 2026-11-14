# hhj-timer v1.1 — 4-digit stopwatch display

XIAO ESP32-C3 driving four 7-segment digits through four chained TPIC6B595.
Overview of the whole system: [../README.md](../README.md).

| File                  | What it does                                              | Tested by                                                     |
|-----------------------|-----------------------------------------------------------|---------------------------------------------------------------|
| `include/display.h`   | digits, time formats, shift-register order                | `test/test_display.cpp`                                       |
| `include/stopwatch.h` | START / FINISH events → running time, result              | `test/test_stopwatch.cpp`                                     |
| `include/remote.h`    | what the server says the display should show              | `test/test_remote.cpp`                                        |
| `include/timer_app.h` | what to show when, the conversation on the network        | `test/test_timer_app.cpp`                                     |
| `include/config.h`    | wiring (pins, which bit drives which segment) and timings | `test/test_display.cpp` checks it against the soldering notes |
| `src/main.cpp`        | glue only: pins, WiFi, UDP socket, serial                 | — (keep it free of logic)                                     |

## Two firmwares

1. `test` — flash this first after soldering (`make flash-timer-test`). It
   lights every segment of every digit in turn (the serial monitor names the
   segment), then 0–9 on all digits, then `1234`.
   - a segment other than the one named → fix `SEG_x` in `include/config.h`
   - `1234` reads backwards → `CHAIN_FIRST_IS_LEFT 0`
2. `stoper` — the real thing (`make flash-timer`).

## What the display shows

| Display               | Meaning                                                                                         |
|-----------------------|-------------------------------------------------------------------------------------------------|
| `8888`                | the first second after power-up (all segments work?)                                            |
| `----` blinking       | no WiFi                                                                                         |
| `----` steady         | both gates online and ready (left two digits = START, right = FINISH)                           |
| `__` on one side      | that gate has not been heard from for 7 s                                                       |
| `¯¯` on one side      | that gate is online but a beam is blocked / not aligned                                         |
| `0.00`                | the operator is registering a rider who has not started yet (server on)                         |
| counting              | a run is on: seconds and hundredths; from 100 s on minutes and seconds                          |
| blinking, then steady | the result; it blinks for 3 s and stays until the next rider (server off: until the next START) |

## How it gets the time

**With the server on** the display shows what the operator console shows.
The server sends a SHOW datagram once a second and at every change: nothing
to show, ready (`0.00`), a run that started so-and-so long ago, or a result.
So the display starts only for the rider being registered, and when the
operator picks a different START or FINISH the display jumps to the new
time. The rules are `R-1 … R-6` at the top of `remote.h`.

**Without the server** — none heard for 5 s — the display times the gates by
itself and takes over without a gap, because it listens to the gates all the
time anyway. The gates announce every crossing over UDP (port 3004) and say
how long ago the beam was cut. The display subtracts that from the moment
the datagram arrived, so the whole run is timed on the display's own clock.
The rules (`S-1 … S-9` at the top of `stopwatch.h`) in short:

- the newest START defines the run — a new START restarts the clock;
- the run ends at the first FINISH that comes at least 1 s after the START;
- copies, delays and reordering of datagrams do not change the result;
- no FINISH for 10 minutes → back to ready.

## Without gates

Serial monitor, 115200 (`make monitor-timer`): `s` = start, `f` = finish,
`r` = reset, `t` = all segments on for 2 s.

## Connections

XIAO D8 → data, D9 → clock, D10 → latch (through a 74AHCT125); the pin map
of the build is in `stoper_hh_pinmap.md` (kept with the hardware notes, not in this repo).
