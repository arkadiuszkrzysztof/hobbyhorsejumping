# Hobby Horse Jumping — timing system

Two light gates, a big 4-digit display and a server. Switch them on in any
order, on the same WiFi, and they find each other: nothing has an IP address
configured.

| Folder         | What it is                                                                                              |
|----------------|---------------------------------------------------------------------------------------------------------|
| `hhj-sensor`   | Gate firmware (ESP32, two IR beams). Built twice: START gate, FINISH gate.                              |
| `hhj-timer`    | Display firmware (XIAO ESP32-C3, four 7-segment digits).                                                |
| `hhj-backend`  | Django server: stores every crossing, keeps the competition, drives the display.                        |
| `hhj-frontend` | Operator console in the browser (React): who rides, when to register, penalties, which crossings count. |
| `hhj-common`   | What the firmwares share: WiFi plan, link protocol, test tools.                                         |

## How it fits together

```
 START gate ──┐                       ┌── hhj-timer    the big display
              ├── UDP 3004, same WiFi ┤        ▲ what to show, once a second
 FINISH gate ─┘                       └── hhj-backend  stores every crossing, ACKs it,
                                               ▲       keeps competitions / riders / runs
                                               │ HTTP /api + WebSocket /ws/live/<id>/
                                          hhj-frontend  the operator console
```

- A gate reports a rider when both of its beams are cut together. It says
  *how long ago* that happened (`age_ms`), not *when* — so the display and the
  server each time the run on their own clock and no clock has to be synced.
- Every crossing is sent seven times within a third of a second (and then once
  a second until the server confirms it). Receivers keep the quickest copy,
  which takes WiFi delay out of the result.
- While the server is on, it tells the display what to show, so the display
  always agrees with the operator console — also when the operator picks a
  different START or FINISH for the run. If the server is silent for 5 s the
  display goes back to timing the gates by itself; the server needs no display.

The wire format is in [hhj-common/PROTOCOL.md](hhj-common/PROTOCOL.md), the
console's API in [hhj-backend/API.md](hhj-backend/API.md).

## Switching it on

1. **Server** — `make backend` (HTTP on 3003, gates on UDP 3004).
2. **Gates** — power them. Blue LED: blinking = looking for WiFi, on = connected.
   Red LED: blinking = a beam has never been free since power-up (align the
   emitters), on = both beams free, off = a beam is cut right now, fast blink =
   a beam has been cut for more than 5 s.
3. **Display** — power it. `8888` for a second, then:

   | Display               | Meaning                                                                              |
   |-----------------------|--------------------------------------------------------------------------------------|
   | `----` blinking       | no WiFi                                                                              |
   | `----` steady         | both gates online and ready (left two digits = START, right = FINISH)                |
   | `__` on one side      | that gate has not been heard from for 7 s                                            |
   | `¯¯` on one side      | that gate is online but a beam is blocked / not aligned                              |
   | `0.00`                | the console is registering a rider who has not started yet                           |
   | counting              | a run is on (`SS.hh`; from 100 s on `M:SS`)                                          |
   | blinking, then steady | the result; it stays until the next rider (or the next START when the server is off) |

4. **Console** — `make frontend`, then open <http://localhost:3000> (from a
   tablet on the same WiFi: `http://<this computer's IP address>:3000`).

On the Windows machine at the arena, [start-hhj.bat](start-hhj.bat) does steps
1 and 4: it starts the server and the console in two windows and opens the
console in the browser. To run it at logon press Win+R, type `shell:startup`
and put a shortcut to the file there. Its header lists what it needs.

Check what the server sees: `curl localhost:3003/devices/` and
`curl localhost:3003/timereadings/`.

## Running a competition

Create a competition in the console (name, number of fences), then:

1. **Riders** are the list on the left. Add, rename, remove and drag them into
   order at any time. Saving a run moves on to the next rider down; click
   anyone — also somebody who already rode — to give them the next run. A
   rider can have any number of runs.
2. **Start registering** opens a session for the selected rider. Only crossings
   made while it is registering belong to the session, so children running
   through the gates between rides do not count. Stop and resume as needed.
3. **Fences**: one click on a number marks a penalty on that fence, one click
   on A / B / C under it marks that element (`9B`). Click again to take it back.
4. **START and FINISH signals** of the session are listed in two columns. The
   first START and the last FINISH after it are used unless you click another
   one; the time on the console and on the big display follow at once.
   Crossings from just before or after the session are offered too, marked
   *outside session*.
5. **Save** stores the run (time, penalties, chosen signals) with the rider.
   A saved run can be reopened or deleted; a session can be discarded or
   moved to another rider if the wrong one was selected.

Everything is in the server's database (`hhj-backend/db.sqlite3`); several
browsers can be open at once and stay in step.

No hardware at hand? With the backend running:
`make sim && ./build/hhj-gate-sim gate --mode START --peer 127.0.0.1:3004 --cross 300`
plays a START gate with one rider (it is the real gate code, built for the desktop).

## WiFi

Both firmwares try the competition router first (`NotForSquares`), then a
fallback network once, then keep looking for the competition router. The same
plan restarts whenever a connection is lost. The fallback's name and password
are not in git: copy `hhj-common/include/hhj/wifi_secrets.example.h` to
`wifi_secrets.h` next to it and fill it in. Without that file there is simply
no fallback.

## Everything from the repo root

```
make                 list all targets
make test            every automated test
make build           compile START gate, FINISH gate and timer
make flash-start     also: flash-finish, flash-timer, flash-timer-test   [PORT=/dev/cu...]
make monitor-sensor  also: monitor-timer
make backend         migrate + run the server
make frontend        the operator console on http://localhost:3000
```

In VS Code open `hobbyhorsejumping.code-workspace`: the same targets are under
*Terminal → Run Task* (`HHJ: ...`), and the PlatformIO side bar lists the
firmware environments (`start`, `finish`, `stoper`, `test`).

`make backend` creates `hhj-backend/.venv` from `Pipfile.lock` on first use,
`make frontend` installs the npm packages of `hhj-frontend` on first use.

## How it is tested

The firmware is split so that everything that decides something is plain C++
with no Arduino in it, and the `.ino` / `main.cpp` files only connect pins,
WiFi and the UDP socket to it. That logic is compiled and tested on the
desktop:

- `make test-firmware` — unit tests per module, the gate and the display each
  running whole on fake hardware, and both gates + display + a model server on
  a simulated WiFi (delay, loss, duplicates, access-point broadcast hold,
  drifting clocks, reboots, outages). Built with `-Werror`, AddressSanitizer
  and UndefinedBehaviorSanitizer.
- `make test-backend` — Django tests (the session rules, the API, the
  WebSocket), plus the real C++ gate and display code talking to the real
  server over UDP, and the C++ and Python decoders compared on about
  140 000 valid and damaged datagrams.
- `make test-frontend` — the console's logic and screens against a fake server.
- `make coverage` — fails unless every line and function of the firmware logic
  is executed by the tests.
- `make mutation` — makes about 1400 copies of the logic with one small
  change each (`<` → `<=`, `20` → `21`, a statement removed ...) and fails if a
  copy passes the tests without being listed, with the reason why the change
  cannot matter, in `hhj-common/tools/mutation_equivalent.txt`. Slow (about
  ten minutes with `MUT_JOBS=4`); run it after changing the logic or its tests.

Each requirement has an id in the header that implements it (`G-3`, `S-4`,
`W-2` ...), and most tests carry the id of what they check in their name.

What the tests cannot see is the glue itself and the hardware: pin numbers,
beam polarity, the display wiring. For the display there is a soldering-test
firmware (`make flash-timer-test`).
