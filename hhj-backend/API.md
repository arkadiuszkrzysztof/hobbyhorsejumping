# hhj-backend — arena API

What the operator console (`hhj-frontend`) talks to. JSON over HTTP under
`/api/`, plus one WebSocket that pushes the same state object whenever
anything changes. No authentication: the system lives on its own WiFi.

## The idea

- A **competition** has an ordered list of **entries** (competitors). The list
  can be changed at any time: add, remove, rename, reorder.
- An entry can have any number of **runs** (tries).
- One entry is **selected**. Starting to register creates an *open* run for it —
  the **session**. At most one session is open at a time, in the whole system.
- While the session is **capturing**, every gate signal (START / FINISH) is
  attached to it. Signals that arrive when nothing is capturing are stored but
  belong to no run.
- Of the session's signals one START and one FINISH count. By default the
  **first START** and the **last FINISH after it**; the operator can pick others.
  `time = FINISH - START`.
- **Penalties** are fence labels (`"7"`, `"9B"`), toggled with one click.
- **Saving** freezes the run (time, penalties, the two signals), closes the
  session and selects the next entry in the list.
- The external display follows the session: 0.00 when it is open and nothing
  has started, the running clock from the counting START, the result once a
  FINISH counts. Picking another START / FINISH changes the display too.

## State

Every endpoint below (except the competition list) answers with the full state
of one competition, and the WebSocket pushes the same object.

```jsonc
{
  "server_time": 1794000000000,          // epoch ms on the server when this was produced
  "competition": { "id": "…", "name": "Saturday open", "fence_count": 12 },
  "entries": [                           // in list order
    { "id": "…", "name": "Zosia",
      "runs": [ { "id": "…", "number": 1, "status": "SAVED", "time_ms": 23450, "penalties": ["7", "9B"] },
                { "id": "…", "number": 2, "status": "OPEN",  "time_ms": null,  "penalties": [] } ] }
  ],
  "selected_entry": "…",                 // entry id or null
  "session": {                           // the open run of THIS competition, or null
    "run_id": "…", "entry_id": "…", "number": 2,
    "capturing": true,
    "penalties": ["7"],
    "starts":   [ { "id": "…", "sensor_time": 1794000001000, "in_session": true } ],
    "finishes": [ { "id": "…", "sensor_time": 1794000024450, "in_session": true, "time_ms": 23450 } ],
    "start_id": "…", "finish_id": "…",   // the two that count (null = none)
    "start_auto": true, "finish_auto": true,   // chosen by the rule (true) or by the operator (false)
    "time_ms": 23450                     // null until both count
  },
  "busy": null,                          // or { "competition_id": "…", "competition_name": "…" } when the
                                         // open session belongs to ANOTHER competition
  "devices": [ { "role": "START", "online": true, "ok": true },
               { "role": "FINISH", "online": true, "ok": false },
               { "role": "TIMER", "online": false, "ok": null } ]
}
```

- `sensor_time` is epoch ms of the beam cut on the server's clock. A running
  clock is `server_now - sensor_time(start)`; estimate `server_now` from
  `server_time` (keep the largest `server_time - Date.now()` seen).
- `starts` / `finishes` are sorted by `sensor_time`. `in_session: false` marks
  a signal near the session that was NOT captured (it arrived while registering
  was off, up to two minutes before the session was opened). It can still be
  picked, which attaches it to the run.
- `finishes[].time_ms` is what the result would be with that FINISH and the
  START that currently counts (null if there is no START or it is not after it).
- `devices`: `ok` is null for the timer; for a gate `false` means online but a
  beam is blocked / not aligned.

## HTTP

Bodies and answers are JSON. Errors: `400` (bad input), `404`, `409` (not
possible right now), each with `{"error": "human readable"}`.

| Method & path | Body | Does |
|---|---|---|
| `GET /api/competitions/` | | list, newest first: `[{id, name, fence_count, entries, runs}]` |
| `POST /api/competitions/` | `{name, fence_count?}` | create (fence_count 1–24, default 12) → state, `201` |
| `GET /api/competitions/<cid>/state/` | | state |
| `PATCH /api/competitions/<cid>/` | `{name?, fence_count?}` | rename / change fence count → state |
| `POST /api/competitions/<cid>/entries/` | `{name}` | add a competitor at the end of the list → state |
| `PATCH /api/entries/<eid>/` | `{name}` | rename → state |
| `DELETE /api/entries/<eid>/` | | remove from the list **with its saved runs** → state. `409` while its session is open |
| `POST /api/competitions/<cid>/order/` | `{entries: [eid, …]}` | new order; must list every entry exactly once → state |
| `POST /api/competitions/<cid>/select/` | `{entry: eid \| null, move?: bool}` | select. `409` if a session is open for another entry, unless `move: true`, which hands the open session over to the selected entry → state |
| `POST /api/competitions/<cid>/capture/` | `{on: bool}` | start / stop registering for the selected entry. `on: true` opens a session if none is open. `409` without a selection, or while a session is open in another competition → state |
| `POST /api/runs/<rid>/penalty/` | `{fence}` | toggle the penalty on that fence (`"1"`…`"24"`, optionally with `A`/`B`/`C`) → state |
| `POST /api/runs/<rid>/pick/` | `{start?: id \| null, finish?: id \| null}` | make that signal count; `null` = back to the automatic choice → state |
| `POST /api/runs/<rid>/save/` | | freeze the run, close the session, select the next entry → state |
| `POST /api/runs/<rid>/discard/` | | throw the open session away (its signals stay stored, unattached) → state |
| `POST /api/runs/<rid>/reopen/` | | make a saved run the session again (not capturing) to correct it. `409` while another session is open → state |
| `DELETE /api/runs/<rid>/` | | delete a saved run → state |

`penalty`, `pick`, `save`, `discard` need the run to be the open session (`409` otherwise).

## WebSocket

`/ws/live/<cid>/` — server to client only.

```jsonc
{ "type": "state", "state": { … } }                                  // on connect and after every change
{ "type": "devices", "server_time": 1794000000000, "devices": [ … ] } // every 2 s
```

`/ws/timings/` (every raw gate signal as it is stored) is unchanged.
