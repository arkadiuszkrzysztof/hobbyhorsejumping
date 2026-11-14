"""Codec for the HHJ link protocol v1 (see hhj-common/PROTOCOL.md).

`decode()` never raises: anything that is not exactly one valid datagram
returns None. The encoders produce the canonical byte strings listed in
hhj-common/protocol/vectors.tsv.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from typing import Optional, Union

MAX_DATAGRAM = 512
MAX_KEYS = 16

U16 = 65535
U32 = 4294967295
U53 = 2 ** 53

GATE_ROLES = ('START', 'FINISH')
ROLES = ('START', 'FINISH', 'TIMER', 'BACKEND')

_FW_RE = re.compile(r'[A-Za-z0-9._-]{1,15}')


@dataclass(frozen=True)
class Event:
    mark: str
    age_ms: int
    boot: int
    seq: int
    t: int = 0
    dir: int = 0
    gap_ms: int = 0
    n: int = 0


@dataclass(frozen=True)
class Hello:
    role: str
    boot: int
    up_ms: int = 0
    fw: Optional[str] = None
    st: int = 0
    seq: int = 0
    pending: int = 0
    ms: int = 0
    rssi: int = 0
    net: int = 0


@dataclass(frozen=True)
class Ack:
    mark: str
    boot: int
    seq: int


Message = Union[Event, Hello, Ack]


class _Reject(Exception):
    pass


# --- decoding -----------------------------------------------------------------
#
# _scan() is a statement-for-statement port of hhj::FlatJson::parse
# (hhj-common/include/hhj/json_scan.h) and _decode() of hhj::decode (link.h),
# so that both ends accept and reject exactly the same datagrams. Python's json
# module is deliberately not used: it is laxer in some places (duplicate keys,
# NaN, escapes that spell a protocol word) and stricter in others (non-UTF-8
# bytes inside strings). `make test-link` feeds thousands of damaged datagrams
# to both implementations and compares the verdicts.

_WS = frozenset(b' \t\r\n')
_DIGIT = frozenset(b'0123456789')
_QUOTE, _BACKSLASH = 0x22, 0x5C

_STRING, _INT, _OTHER = 's', 'i', 'o'


def _skip_ws(data, i):
    n = len(data)
    while i < n and data[i] in _WS:
        i += 1
    return i


def _scan_key(data, i):
    n = len(data)
    if i >= n or data[i] != _QUOTE:
        raise _Reject('key')
    j = i + 1
    while True:
        if j >= n:
            raise _Reject('key')
        c = data[j]
        if c == _QUOTE:
            break
        if c < 0x20 or c == _BACKSLASH:      # keys never carry escapes
            raise _Reject('key')
        j += 1
    return data[i + 1:j], j + 1


def _scan_value(data, i):
    """Returns ((type, value), next index). value: bytes or None (string with
    escapes) for strings, int for integers, None otherwise."""
    n = len(data)
    if i >= n:
        raise _Reject('value')
    c = data[i]
    if c == _QUOTE:
        j = i + 1
        escaped = False
        while True:
            if j >= n:
                raise _Reject('string')
            ch = data[j]
            if ch == _QUOTE:
                break
            if ch < 0x20:
                raise _Reject('string')
            if ch == _BACKSLASH:
                escaped = True
                j += 2
                continue
            j += 1
        return (_STRING, None if escaped else data[i + 1:j]), j + 1
    if c == 0x2D or c in _DIGIT:
        j = i
        neg = data[j] == 0x2D
        if neg:
            j += 1
        if j >= n or data[j] not in _DIGIT:
            raise _Reject('number')
        if data[j] == 0x30 and j + 1 < n and data[j + 1] in _DIGIT:
            raise _Reject('leading zero')
        first = j
        while j < n and data[j] in _DIGIT:
            j += 1
        digits = j - first
        is_float = False
        if j < n and data[j] == 0x2E:
            j += 1
            if j >= n or data[j] not in _DIGIT:
                raise _Reject('number')
            while j < n and data[j] in _DIGIT:
                j += 1
            is_float = True
        if j < n and data[j] in (0x65, 0x45):
            j += 1
            if j < n and data[j] in (0x2B, 0x2D):
                j += 1
            if j >= n or data[j] not in _DIGIT:
                raise _Reject('number')
            while j < n and data[j] in _DIGIT:
                j += 1
            is_float = True
        if is_float or digits > 18:
            return (_OTHER, None), j
        value = int(data[first:first + digits])
        return (_INT, -value if neg else value), j
    for word in (b'true', b'false', b'null'):
        if data.startswith(word, i):
            return (_OTHER, None), i + len(word)
    raise _Reject('value')                  # includes nested objects and arrays


def _scan(data):
    """One flat JSON object -> {key: (type, value)}."""
    n = len(data)
    if n > MAX_DATAGRAM:
        raise _Reject('size')
    i = _skip_ws(data, 0)
    if i >= n or data[i] != 0x7B:
        raise _Reject('not an object')
    i = _skip_ws(data, i + 1)
    fields = {}
    if i < n and data[i] == 0x7D:
        i += 1
    else:
        while True:
            key, i = _scan_key(data, i)
            i = _skip_ws(data, i)
            if i >= n or data[i] != 0x3A:
                raise _Reject('colon')
            i = _skip_ws(data, i + 1)
            value, i = _scan_value(data, i)
            if key in fields:
                raise _Reject('duplicate key')
            if len(fields) == MAX_KEYS:
                raise _Reject('too many keys')
            fields[key] = value
            i = _skip_ws(data, i)
            if i >= n:
                raise _Reject('unterminated')
            if data[i] == 0x2C:
                i = _skip_ws(data, i + 1)
                continue
            if data[i] == 0x7D:
                i += 1
                break
            raise _Reject('separator')
    if _skip_ws(data, i) != n:
        raise _Reject('trailing garbage')
    return fields


def _int(fields, key, lo, hi, default=None):
    if key not in fields:
        if default is None:
            raise _Reject('missing')
        return default
    kind, value = fields[key]
    if kind != _INT or not (lo <= value <= hi):
        raise _Reject('bad integer')
    return value


def _token(fields, key):
    """A string value without escape sequences, as text."""
    kind, value = fields[key]
    if kind != _STRING or value is None:
        raise _Reject('bad string')
    return value.decode('latin-1')


def _role(fields, key, allowed):
    value = _token(fields, key)
    if value not in allowed:
        raise _Reject('bad role')
    return value


def _decode(data) -> Message:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise _Reject('not bytes')
    fields = _scan(bytes(data))

    kinds = [k for k in (b'time_mark', b'hello', b'ack') if k in fields]
    if len(kinds) != 1:
        raise _Reject('kind')
    kind = kinds[0]

    if kind == b'time_mark':
        return Event(
            mark=_role(fields, b'time_mark', GATE_ROLES),
            age_ms=_int(fields, b'age_ms', 0, U32),
            boot=_int(fields, b'boot', 0, U32),
            seq=_int(fields, b'seq', 1, U32),
            t=_int(fields, b't', 0, U53, 0),
            dir=_int(fields, b'dir', -1, 1, 0),
            gap_ms=_int(fields, b'gap_ms', 0, U16, 0),
            n=_int(fields, b'n', 0, U16, 0),
        )
    if kind == b'hello':
        role = _role(fields, b'hello', ROLES)
        boot = _int(fields, b'boot', 0, U32)
        fw = None
        if b'fw' in fields:
            fw = _token(fields, b'fw')
            if not _FW_RE.fullmatch(fw):
                raise _Reject('bad fw')
        return Hello(
            role=role,
            boot=boot,
            up_ms=_int(fields, b'up_ms', 0, U53, 0),
            fw=fw,
            st=_int(fields, b'st', 0, U32, 0),
            seq=_int(fields, b'seq', 0, U32, 0),
            pending=_int(fields, b'pending', 0, U16, 0),
            ms=_int(fields, b'ms', 0, U32, 0),
            rssi=_int(fields, b'rssi', -128, 127, 0),
            net=_int(fields, b'net', 0, 255, 0),
        )
    return Ack(
        mark=_role(fields, b'ack', GATE_ROLES),
        boot=_int(fields, b'boot', 0, U32),
        seq=_int(fields, b'seq', 1, U32),
    )


def decode(data) -> Optional[Message]:
    """Decode one datagram. Returns Event, Hello, Ack or None (never raises)."""
    try:
        return _decode(data)
    except Exception:  # _Reject and anything unforeseen: a datagram must never raise
        return None


def describe(msg: Optional[Message]) -> str:
    """The message in the notation of hhj-common/protocol/vectors.tsv ('-' for None)."""
    if isinstance(msg, Event):
        return (f'kind=event mark={msg.mark} age_ms={msg.age_ms} t={msg.t} boot={msg.boot} seq={msg.seq} '
                f'dir={msg.dir} gap_ms={msg.gap_ms} n={msg.n}')
    if isinstance(msg, Hello):
        return (f'kind=hello role={msg.role} boot={msg.boot} up_ms={msg.up_ms} fw={msg.fw or "-"} st={msg.st} '
                f'seq={msg.seq} pending={msg.pending} ms={msg.ms} rssi={msg.rssi} net={msg.net}')
    if isinstance(msg, Ack):
        return f'kind=ack mark={msg.mark} boot={msg.boot} seq={msg.seq}'
    return '-'


# --- encoding -----------------------------------------------------------------

def _dump(pairs) -> bytes:
    out = json.dumps(dict(pairs), separators=(',', ':'), ensure_ascii=True).encode('ascii')
    if len(out) > MAX_DATAGRAM:  # cannot happen with valid field values
        raise ValueError('datagram too long')
    return out


def encode_event(ev: Event) -> bytes:
    return _dump([
        ('time_mark', ev.mark), ('age_ms', ev.age_ms), ('t', ev.t), ('boot', ev.boot),
        ('seq', ev.seq), ('dir', ev.dir), ('gap_ms', ev.gap_ms), ('n', ev.n),
    ])


# Optional HELLO keys always written for a given role (the canonical form);
# any other optional key is written only when it is not the default.
_HELLO_ALWAYS = {
    'START': ('st', 'seq', 'pending', 'rssi', 'net'),
    'FINISH': ('st', 'seq', 'pending', 'rssi', 'net'),
    'TIMER': ('st', 'ms', 'rssi', 'net'),
    'BACKEND': (),
}


def encode_hello(h: Hello) -> bytes:
    pairs = [('hello', h.role), ('boot', h.boot), ('up_ms', h.up_ms)]
    if h.fw is not None:
        pairs.append(('fw', h.fw))
    always = _HELLO_ALWAYS[h.role]
    for key in ('st', 'seq', 'pending', 'ms', 'rssi', 'net'):
        value = getattr(h, key)
        if key in always or value != 0:
            pairs.append((key, value))
    return _dump(pairs)


def encode_ack(a: Ack) -> bytes:
    return _dump([('ack', a.mark), ('boot', a.boot), ('seq', a.seq)])


def encode(msg: Message) -> bytes:
    if isinstance(msg, Event):
        return encode_event(msg)
    if isinstance(msg, Hello):
        return encode_hello(msg)
    if isinstance(msg, Ack):
        return encode_ack(msg)
    raise TypeError(type(msg))


def gate_flags(st: int) -> dict:
    """Decode a gate HELLO `st` bit field."""
    return {
        'beam_a': bool(st & 1),
        'beam_b': bool(st & 2),
        'stuck': bool(st & 4),
        'ready': bool(st & 8),
        'armed': bool(st & 16),
        'ok': (st & 15) == 8,
    }
