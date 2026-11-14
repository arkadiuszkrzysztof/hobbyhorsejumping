# hhj-sensor v2.1 — two-beam gate

One firmware, built twice: `start` and `finish` (the only difference is the
name the gate reports). Overview of the whole system: [../README.md](../README.md).

| File             | What it does                                           | Tested by                |
|------------------|--------------------------------------------------------|--------------------------|
| `beam_logic.h`   | decides when a rider crossed (pure C++)                | `test/test_beam_logic.cpp` |
| `outbox.h`       | repeats every event until it is delivered              | `test/test_outbox.cpp`   |
| `gate_app.h`     | LEDs, log, the conversation on the network             | `test/test_gate_app.cpp` |
| `hhj-sensor.ino` | glue only: pins, WiFi, UDP socket, serial              | — (keep it free of logic) |

Shared with the timer, in `../hhj-common`: WiFi plan, link protocol.

## Wiring

Receiver A → GPIO2 (D2, `SIGNAL_IR` in v1), receiver B → GPIO4 (D4);
`INPUT_PULLUP`, open collector, beam cut = LOW. LEDs as in v1: GPIO21 blue
(WiFi), GPIO23 red (beams). All four pins can be overridden with `-D` in
`platformio.ini`.

Every event reports which beam was cut first (`dir=A->B` or `B->A`) and the
time between the two beams.

## LEDs

| LED  | blinking slowly                   | on               | off                  | blinking fast            |
|------|-----------------------------------|------------------|----------------------|--------------------------|
| blue | looking for WiFi                  | connected        | —                    | —                        |
| red  | a beam was never free since power-up (align the emitters) | both beams free  | a beam is cut now | a beam cut for > 5 s (obstacle?) |

## What counts as a crossing

Both beams cut for at least 20 ms each, their cuts at most 150 ms apart, both
still cut at that moment. The reported time is the cut of the *first* beam.
After a crossing the gate ignores everything until both beams have been free
for 1.5 s (one rider = one event). The numbers are `GateConfig` in
`beam_logic.h`; the exact rules are the `G-1 … G-8` list at the top of that file.

## Build, flash, watch

From the repo root: `make flash-start`, `make flash-finish`, `make monitor-sensor`
(or in this folder: `pio run -e start -t upload`, `pio device monitor`).

The serial log (115200) has one line per thing that happened, each starting
with the milliseconds since power-up:

```
[   5000] A broken
[   5040] B broken
[   5060] EVENT #1 t=5000 dir=A->B gap=40 ms (lone A/B 0/0, short 0)
[   5060] event #1 sent, 60 ms after the cut, to 2 peer(s) + broadcast
[   5063] event #1 confirmed by backend
[   5250] A free after 250 ms
[   6790] gate armed
```

Also logged: every single-beam break (`B alone: no A within 150 ms -> ignored`),
breaks that were too short, WiFi changes, peers found and lost, and every 30 s a
one-line health summary (events, unconfirmed events, longest pause of the
measuring loop, bad datagrams, failed sends).

## Tests

`make test-firmware` from the repo root (no hardware needed).
