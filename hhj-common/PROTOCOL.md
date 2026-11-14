# HHJ link protocol (v1)

How the gates (`hhj-sensor`), the display (`hhj-timer`) and the server
(`hhj-backend`) find each other and exchange timing events. No IP address is
configured anywhere: every node only has to be on the same WiFi / L2 segment.

The C++ implementation is `hhj-common/include/hhj/link.h`, the Python one is
`hhj-backend/timings/link.py`. Both are checked against the same conformance
file, `hhj-common/protocol/vectors.tsv`.

## Transport

- UDP over IPv4, port **3004**. Every node binds this port.
- One datagram = one flat JSON object (no nested objects or arrays), ASCII,
  at most 512 bytes and at most 16 keys. Integers are plain decimals (no
  floats, no leading zeros).
- Keys, and the values of `time_mark`, `hello`, `ack` and `fw`, are compared as
  written: a JSON escape sequence in any of them (`"\u0053TART"`) makes the
  datagram invalid. Escapes are tolerated only inside the string value of an
  unknown key. Bytes above 0x7F may only appear inside strings.
- Unknown keys are ignored. A datagram that is not exactly one of the four
  kinds below, or that has a missing / mistyped / out-of-range required field,
  or a duplicated key, is dropped as a whole.

The kind is decided by which of these keys is present (exactly one must be):
`time_mark` → EVENT, `hello` → HELLO, `ack` → ACK, `show` → SHOW.

## Roles

`START`, `FINISH` (gates), `TIMER` (display), `BACKEND` (server).

## HELLO — discovery and liveness

```json
{"hello":"START","boot":3735928559,"up_ms":123456,"fw":"2.1.0","st":24,"seq":7,"pending":0,"rssi":-61,"net":0}
{"hello":"TIMER","boot":77,"up_ms":5000,"fw":"1.1.0","st":1,"ms":12340,"rssi":-55,"net":0}
{"hello":"BACKEND","boot":42,"up_ms":1000,"fw":"1.0.0"}
```

| key       | type            | required | meaning                                                        |
|-----------|-----------------|----------|----------------------------------------------------------------|
| `hello`   | role            | yes      | who is talking                                                 |
| `boot`    | 0..4294967295   | yes      | random nonce picked at power-up; changes on every reboot       |
| `up_ms`   | 0..2^53         | no (0)   | sender uptime                                                  |
| `fw`      | ≤15 of `[A-Za-z0-9._-]` | no | firmware / software version                              |
| `st`      | 0..4294967295   | no (0)   | role-specific status, see below                                |
| `seq`     | 0..4294967295   | no (0)   | gates: sequence number of the last event (0 = none yet)        |
| `pending` | 0..65535        | no (0)   | gates: events not yet acknowledged by a backend                |
| `ms`      | 0..4294967295   | no (0)   | timer: running time, or last result, in ms                     |
| `rssi`    | -128..127       | no (0)   | WiFi signal, dBm                                               |
| `net`     | 0..255          | no (0)   | 0 = primary WiFi, 1 = fallback WiFi                            |

Gate `st` bits: 1 = beam A broken, 2 = beam B broken, 4 = stuck (a beam broken
longer than the stuck limit), 8 = ready (both beams have been seen free since
power-up), 16 = armed (not in the quiet period after an event). A gate is
"OK" when `st & 15 == 8`.

Timer `st`: 0 = idle, 1 = running, 2 = finished (showing a result).

Rules:

- Gates and the timer send a HELLO every 2 s, and immediately after
  (re)joining WiFi: one datagram to the subnet broadcast address, plus one
  unicast to every node they currently know (gates: every `TIMER` and
  `BACKEND`; the timer: every gate). The broadcast is what lets strangers find
  each other; the unicast copies keep that knowledge alive when broadcasts are
  lost.
- The backend never broadcasts. It answers every HELLO it receives with its own
  HELLO sent **unicast to the source address and port** of that datagram.
- A node that has not been heard from for 7 s is considered gone.
- Gates remember the source IP of every `TIMER` and `BACKEND` HELLO; those are
  the unicast targets for events.

## EVENT — a rider crossed a gate

```json
{"time_mark":"START","age_ms":12,"t":123456,"boot":3735928559,"seq":7,"dir":1,"gap_ms":38,"n":0}
```

| key         | type           | required | meaning                                                     |
|-------------|----------------|----------|-------------------------------------------------------------|
| `time_mark` | `START`/`FINISH` | yes    | which gate                                                  |
| `age_ms`    | 0..4294967295  | yes      | how long ago the beam was cut, measured when this datagram was sent |
| `boot`      | 0..4294967295  | yes      | the gate's boot nonce                                       |
| `seq`       | 1..4294967295  | yes      | event number since boot, +1 per event                       |
| `t`         | 0..2^53        | no (0)   | the cut on the gate's own clock, ms since its boot          |
| `dir`       | -1, 0, 1       | no (0)   | 1 = beam A first, -1 = beam B first, 0 = unknown            |
| `gap_ms`    | 0..65535       | no (0)   | time between the two beams                                  |
| `n`         | 0..65535       | no (0)   | transmission counter for this event (0 = first copy)        |

`(time_mark, boot, seq)` identifies an event. Every copy of the same event
carries the same identity and a fresh `age_ms`.

### Sending (gates)

Each transmission is one unicast datagram to every known `TIMER` / `BACKEND`
peer plus one datagram to the subnet broadcast address.

1. Fast burst: 7 transmissions at 0, 10, 20, 40, 80, 160 and 320 ms after the
   event is detected (or after WiFi comes back, if it was down).
2. Then one transmission per second until a backend acknowledges the event or
   the event is 5 minutes old.

An ACK never cuts the fast burst short (the timer does not acknowledge).
The gate keeps at most 8 events; when a 9th arrives the oldest is discarded.

### Receiving (timer, backend)

The receiver never needs the gate's clock. For every copy it computes, on its
own clock,

```
edge = time_of_arrival - age_ms
```

and keeps, per event identity, the **smallest** `edge` seen. Network delay can
only make a copy arrive late, never early, so the minimum is the best estimate
and a burst of copies cancels WiFi jitter (including the up to ~100 ms hold
that access points apply to broadcast frames). START and FINISH are measured
the same way, so the remaining constant delay cancels in the difference.

Copies with `age_ms` above 10 minutes are dropped as stale.

## ACK — the backend stored an event

```json
{"ack":"START","boot":3735928559,"seq":7}
```

| key    | type             | required | meaning                |
|--------|------------------|----------|------------------------|
| `ack`  | `START`/`FINISH` | yes      | `time_mark` of the event |
| `boot` | 0..4294967295    | yes      | from the event         |
| `seq`  | 1..4294967295    | yes      | from the event         |

Sent by the backend, unicast to the source address and port of the EVENT
datagram, for every copy it receives — but only after the reading is in the
database. Gates ignore ACKs whose `ack`/`boot` are not their own.

## SHOW — the backend tells the display what to show

```json
{"show":"RUN","rev":3141592653,"age_ms":4567,"ms":0}
{"show":"RESULT","rev":2718281828,"age_ms":0,"ms":23450}
```

| key      | type                             | required | meaning                                                   |
|----------|----------------------------------|----------|-----------------------------------------------------------|
| `show`   | `IDLE`/`READY`/`RUN`/`RESULT`    | yes      | what the display should show                              |
| `rev`    | 0..4294967295                    | no (0)   | identifies what is being timed: it changes when the run, or the START / FINISH that counts for it, changes |
| `age_ms` | 0..4294967295                    | no (0)   | `RUN`: how long ago the counting START was cut, measured when this datagram was sent |
| `ms`     | 0..4294967295                    | no (0)   | `RESULT`: the time to show                                |

The operator decides at the backend which rider is being timed and which
START and FINISH count, so with a backend present the display follows the
backend instead of the gates:

- The backend sends a SHOW, unicast, to every `TIMER` it knows: once a second,
  and at once when what should be shown changes.
- While SHOWs arrive the timer shows what they say. It keeps listening to the
  gates, but only to be able to carry on alone: after 5 s without a SHOW it
  goes back to its own stopwatch.
- `IDLE`: the idle picture (gate status). `READY`: `0.00` — a rider is about to
  start. `RUN`: the clock, running from `time_of_arrival - age_ms`; over the
  copies with the same `rev` the smallest such value is kept, a different
  `rev` starts afresh. `RESULT`: `ms` as given; it blinks when it appears or
  changes.

## Conformance file

`protocol/vectors.tsv` — tab-separated, `#` starts a comment line:

```
verdict <TAB> canon <TAB> expect <TAB> payload
```

- `verdict`: `ok` (must decode) or `bad` (must be rejected).
- `canon`: `c` if `payload` is byte-for-byte what the encoders must produce for
  the fields in `expect`, otherwise `-`.
- `expect`: space-separated `key=value`; `kind=event|hello|ack|show` first. Absent
  optional fields appear with their default (`0`, and `fw=-` for no version).
  `-` for `bad` lines.
- `payload`: the datagram, verbatim to the end of the line.
