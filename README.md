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
  *how long ago* that happened (`age_ms`), not *when* — so the server times
  the run on its own clock and no clock has to be synced.
- Every crossing is sent seven times within a third of a second (and then once
  a second until the server confirms it). Receivers keep the quickest copy,
  which takes WiFi delay out of the result.
- The display never times a run by itself. The server tells it what to show,
  so it always agrees with the operator console — also when the operator picks
  a different START or FINISH — and the clock only ever runs for a rider who is
  being registered. Without the server (not started yet, or silent for 5 s) it
  shows the state of the gates and nothing else. The server needs no display.

The wire format is in [hhj-common/PROTOCOL.md](hhj-common/PROTOCOL.md), the
console's API in [hhj-backend/API.md](hhj-backend/API.md).

## Switching it on

1. **Server** — `make backend` (HTTP on 3003, gates on UDP 3004).
2. **Gates** — power them. Blue LED: blinking = looking for WiFi, on = connected.
   Red LED: blinking = a beam has never been free since power-up (align the
   emitters), on = both beams free, off = a beam is cut right now, fast blink =
   a beam has been cut for more than 5 s.
3. **Display** — power it. `8888` for a second, then:

   | Display               | Meaning                                                               |
   |-----------------------|-----------------------------------------------------------------------|
   | `----` blinking       | no WiFi                                                               |
   | `----` steady         | both gates online and ready (left two digits = START, right = FINISH) |
   | `__` on one side      | that gate has not been heard from for 7 s                             |
   | `¯¯` on one side      | that gate is online but a beam is blocked / not aligned               |
   | `0.00`                | the console is registering a rider who has not started yet            |
   | counting              | a run is on (`SS.hh`; from 100 s on `M:SS`)                           |
   | blinking, then steady | the result; it stays until the next rider                             |

4. **Console** — `make frontend`, then open <http://localhost:3000> (from a
   tablet on the same WiFi: `http://<this computer's IP address>:3000`).

On the Windows machine at the arena, [start-hhj.bat](start-hhj.bat) does steps
1 and 4: it fetches the newest version (`git pull`; if that is not possible it
carries on with what is there), starts the server and the console in two
windows and opens the console in the browser. To run it at logon press Win+R, type `shell:startup`
and put a shortcut to the file there. Its header lists what it needs.

Check what the server sees: `curl localhost:3003/devices/` and
`curl localhost:3003/timereadings/`.

## Running a competition

Create a competition in the console: its name, the number of fences, the time
allowed for the course (optional) and what a fault costs — 4 points for a
knockdown, 4 for a refusal and 0.4 for every full second over the time allowed
unless you change them. All of it can be edited later; results are rescored.
Then:

1. **Riders** are the list on the left. Add, rename, remove and drag them into
   order at any time. A rider can sign up with the name of their horse
   (optional; add or correct it later with the pencil); it is shown under the
   rider's name in the list and for the current rider. Saving a run moves on
   to the next rider down; click anyone — also somebody who already rode — to
   give them the next run. A rider can have any number of runs.
2. **Start registering** opens a session for the selected rider. Only crossings
   made while it is registering belong to the session, so children running
   through the gates between rides do not count. Stop and resume as needed.
   Every console rings a bell when registering starts or resumes — the
   rider's signal to go. *Bell on / off* in the top bar is per browser, and
   switching it on rings it once to test the speakers.
3. **Fences**: one click on a number marks that fence as knocked down, one
   click on A / B / C under it marks that element (`9B`); click again to take
   it back. **R** marks a refusal at the fence: a second click a second
   refusal, a third clears them. The panel shows the score as it stands.
4. **START and FINISH signals** of the session are listed in two columns. The
   first START and the last FINISH after it are used unless you click another
   one; the time on the console and on the big display follow at once.
   Crossings from just before or after the session are offered too, marked
   *outside session*.
5. **Save** stores the run (time, faults, chosen signals) with the rider.
   If the round was not completed use **Retire** (the rider gave up) or
   **Eliminate** (e.g. after a fall) instead: the round is kept with its
   faults and, if you say so, the fence where it ended — but without a time.
   A saved run can be reopened or deleted; a session can be discarded or
   moved to another rider if the wrong one was selected.

A result reads like `24.50 · 9.6 pts (8+1.6)`: the time, the penalty points,
and in brackets how they split into fences + time when the time cost
something. `RET @7` / `ELI` mark a round that was retired from (at fence 7) or
eliminated.

**Ranking**, above the rider list, sorts it by result instead of start order:
fewest penalty points first, then the fastest time, so a clean 30 s beats 20 s
with one knockdown. A rider's best completed run counts and is the one
highlighted; riders who only retired or were eliminated follow without a
place, then those who have not ridden.

The list follows the selection: whenever another rider becomes the selected
one — by a click or by *Save & next* — the list brings them to the middle.
Clicking the selected rider again deselects (not while their session is open).

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
Before a change to the database layout is applied, `make backend` and
`start-hhj.bat` keep a copy of the database next to it
(`hhj-backend/db.before-migration-<date>.sqlite3`).

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
