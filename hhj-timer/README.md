# hhj-timer v1.2 — 4-digit display of the server's timing

XIAO ESP32-C3 driving four 7-segment digits through four chained TPIC6B595.
Overview of the whole system: [../README.md](../README.md).

| File                  | What it does                                              | Tested by                                                     |
|-----------------------|-----------------------------------------------------------|---------------------------------------------------------------|
| `include/display.h`   | digits, time formats, shift-register order                | `test/test_display.cpp`                                       |
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
| `0.00`                | the operator is registering a rider who has not started yet                                     |
| counting              | a run is on: seconds and hundredths; from 100 s on minutes and seconds                          |
| blinking, then steady | the result; it blinks for 3 s, then stays for as long as the server shows it                    |

`0.00`, the clock and the result only ever come from the server. Without the
server — not started yet, crashed, or not heard for 5 s — the display shows
the gate status (or the blinking `----` without WiFi), whatever the gates do.

## How it gets the time

The display never times a run by itself. The server does: only it knows
whether a session is open and which START and FINISH count. It sends a SHOW
datagram once a second and at every change: nothing to show (the gate status
above), ready (`0.00`), a run that started so-and-so long ago, or a result.
So the display starts only for the rider being registered, and when the
operator picks a different START or FINISH the display jumps to the new
time. The rules are `R-1 … R-6` at the top of `remote.h`, what is shown when
is `T-1 … T-12` at the top of `timer_app.h`.

A running clock is placed on the display's own clock: the moment the SHOW
arrived minus the `age_ms` it carries (how long ago the START was cut), the
quickest of the once-a-second copies winning. A result is shown exactly as
the server sends it.

The gates still send every crossing to the display as well (it is one of the
peers they know); the display ignores those datagrams. It only listens to the
gates' HELLOs, for the gate status.

When the server falls silent for 5 s — also in the middle of a run — the
display goes back to the gate status. When the server is back it says again
what to show, and the result comes out right: the gates keep repeating their
events until the server has stored them, and each says how long ago it happened.

## Without gates

Serial monitor, 115200 (`make monitor-timer`): `t` = all segments on for 2 s.
There is no way to start or stop a clock on the display itself: runs are
timed by the server.

## Connections

XIAO D8 → data, D9 → clock, D10 → latch (through a 74AHCT125); the pin map
of the build is in `stoper_hh_pinmap.md` (kept with the hardware notes, not in this repo).
