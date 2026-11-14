import random
from pathlib import Path

from django.test import SimpleTestCase

from timings import link

VECTORS = Path(__file__).resolve().parents[3] / 'hhj-common' / 'protocol' / 'vectors.tsv'

EVENT_FIELDS = {'mark': 'mark', 'age_ms': 'age_ms', 't': 't', 'boot': 'boot', 'seq': 'seq',
                'dir': 'dir', 'gap_ms': 'gap_ms', 'n': 'n'}
HELLO_FIELDS = {'role': 'role', 'boot': 'boot', 'up_ms': 'up_ms', 'fw': 'fw', 'st': 'st', 'seq': 'seq',
                'pending': 'pending', 'ms': 'ms', 'rssi': 'rssi', 'net': 'net'}
ACK_FIELDS = {'mark': 'mark', 'boot': 'boot', 'seq': 'seq'}
SHOW_FIELDS = {'state': 'state', 'rev': 'rev', 'age_ms': 'age_ms', 'ms': 'ms'}
KINDS = {'event': (link.Event, EVENT_FIELDS), 'hello': (link.Hello, HELLO_FIELDS), 'ack': (link.Ack, ACK_FIELDS),
         'show': (link.Show, SHOW_FIELDS)}


def load_vectors():
    out = []
    with open(VECTORS, encoding='ascii', newline='') as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip('\r\n')
            if not line.strip() or line.startswith('#'):
                continue
            cols = line.split('\t')
            if len(cols) != 4:
                raise AssertionError(f'vectors.tsv:{lineno}: expected 4 columns, got {len(cols)}')
            out.append((lineno, *cols))
    return out


def parse_expect(expect):
    pairs = dict(item.split('=', 1) for item in expect.split(' '))
    kind = pairs.pop('kind')
    cls, fields = KINDS[kind]
    values = {}
    for key, raw in pairs.items():
        attr = fields[key]
        if attr in ('mark', 'role', 'state'):
            values[attr] = raw
        elif attr == 'fw':
            values[attr] = None if raw == '-' else raw
        else:
            values[attr] = int(raw)
    assert set(values) == set(fields.values()), f'expect must list every field: {expect}'
    return cls(**values)


class VectorTests(SimpleTestCase):
    def test_vector_file_is_sane(self):
        vectors = load_vectors()
        self.assertGreater(len(vectors), 80)
        self.assertTrue(any(v[1] == 'ok' for v in vectors))
        self.assertTrue(any(v[1] == 'bad' for v in vectors))

    def test_every_vector(self):
        for lineno, verdict, canon, expect, payload in load_vectors():
            with self.subTest(line=lineno, payload=payload):
                self.assertIn(verdict, ('ok', 'bad'))
                self.assertIn(canon, ('c', '-'))
                got = link.decode(payload.encode('ascii'))
                self.assertEqual(link.describe(got), expect)
                if verdict == 'bad':
                    self.assertIsNone(got)
                    self.assertEqual(expect, '-')
                    continue
                want = parse_expect(expect)
                self.assertEqual(got, want)
                self.assertIs(type(got), type(want))
                if canon == 'c':
                    self.assertEqual(link.encode(want), payload.encode('ascii'))
                # every accepted vector round-trips through the encoder
                self.assertEqual(link.decode(link.encode(got)), got)


VALID = b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2}'


class CodecEdgeTests(SimpleTestCase):
    def test_empty_and_whitespace_rejected(self):
        for data in (b'', b' ', b'\t', b'\r\n', b'   \n\t ', b'\x00'):
            with self.subTest(data=data):
                self.assertIsNone(link.decode(data))

    def test_size_limit(self):
        pad = 512 - len(VALID) - len(',"p":""')
        exact = VALID[:-1] + b',"p":"' + b'x' * pad + b'"}'
        self.assertEqual(len(exact), 512)
        self.assertIsNotNone(link.decode(exact))
        over = VALID[:-1] + b',"p":"' + b'x' * (pad + 1) + b'"}'
        self.assertEqual(len(over), 513)
        self.assertIsNone(link.decode(over))
        self.assertIsNone(link.decode(VALID[:-1] + b',"p":"' + b'x' * 70000 + b'"}'))

    def test_key_limit(self):
        def with_keys(n):
            extra = ''.join(f',"k{i}":0' for i in range(n - 4))
            return ('{"time_mark":"START","age_ms":5,"boot":9,"seq":2' + extra + '}').encode()
        self.assertIsNotNone(link.decode(with_keys(16)))
        self.assertIsNone(link.decode(with_keys(17)))

    def test_high_bytes_only_inside_strings(self):
        # Same rule as the C++ scanner: bytes >= 0x80 are ordinary string content
        # (whether or not they are valid UTF-8) and garbage anywhere else.
        self.assertIsNone(link.decode(b'\xff\xfe' + VALID))
        self.assertIsNone(link.decode(b'\xef\xbb\xbf' + VALID))  # BOM
        self.assertIsNotNone(link.decode(VALID[:-1] + b',"x":"\xc3\xa9"}'))
        self.assertIsNotNone(link.decode(VALID[:-1] + b',"x":"\x80\xff"}'))
        self.assertIsNone(link.decode(VALID[:-1] + b',"x\xc3\xa9":1,"x\xc3\xa9":2}'))  # still a duplicate key
        self.assertIsNone(link.decode(b'{"time_mark":"START\xc3\xa9","age_ms":5,"boot":9,"seq":2}'))
        self.assertIsNone(link.decode(b'{"hello":"TIMER","boot":5,"fw":"\xc3\xa9"}'))

    def test_json_words_that_are_not_json(self):
        for value in (b'NaN', b'Infinity', b'-Infinity', b'True', b'None', b'undefined'):
            with self.subTest(value=value):
                self.assertIsNone(link.decode(VALID[:-1] + b',"x":' + value + b'}'))
        # a huge float is still a float: valid in an unknown key, never a protocol integer
        self.assertIsNotNone(link.decode(VALID[:-1] + b',"x":1e400}'))
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":1e400,"boot":9,"seq":2}'))

    def test_escapes_never_spell_protocol_words(self):
        # JSON would read \u0053TART as START. Protocol keys and values are compared as written.
        self.assertIsNone(link.decode(b'{"time_mark":"\\u0053TART","age_ms":5,"boot":9,"seq":2}'))
        self.assertIsNone(link.decode(b'{"time_m\\u0061rk":"START","age_ms":5,"boot":9,"seq":2}'))
        self.assertIsNone(link.decode(b'{"hello":"\\u0054IMER","boot":5}'))
        self.assertIsNone(link.decode(b'{"hello":"TIMER","boot":5,"fw":"1\\u002e0"}'))
        self.assertIsNone(link.decode(b'{"ack":"\\u0053TART","boot":5,"seq":1}'))
        # an escaped string under an unknown key is skipped correctly
        self.assertIsNotNone(link.decode(VALID[:-1] + b',"note":"a \\" b \\\\ c \\u00e9"}'))
        # but a key with an escape rejects the datagram, known or not
        self.assertIsNone(link.decode(VALID[:-1] + b',"no\\te":1}'))

    def test_describe(self):
        self.assertEqual(link.describe(None), '-')
        self.assertEqual(link.describe(link.decode(VALID)),
                         'kind=event mark=START age_ms=5 t=0 boot=9 seq=2 dir=0 gap_ms=0 n=0')
        self.assertEqual(link.describe(link.Hello(role='TIMER', boot=5)),
                         'kind=hello role=TIMER boot=5 up_ms=0 fw=- st=0 seq=0 pending=0 ms=0 rssi=0 net=0')
        self.assertEqual(link.describe(link.Ack(mark='FINISH', boot=3, seq=1)), 'kind=ack mark=FINISH boot=3 seq=1')

    def test_types(self):
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":true}'))
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2.0}'))
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"dir":null}'))
        self.assertIsNone(link.decode(b'{"hello":"TIMER","boot":5,"fw":null}'))
        self.assertIsNone(link.decode(b'{"hello":"TIMER","boot":5,"up_ms":9007199254740993}'))
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"x":{}}'))
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"x":[]}'))
        self.assertIsNone(link.decode(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"x":{"a":1,"a":2}}'))

    def test_non_bytes_input(self):
        for value in (None, 5, 'text', VALID.decode(), [VALID]):
            self.assertIsNone(link.decode(value))
        self.assertIsNotNone(link.decode(bytearray(VALID)))
        self.assertIsNotNone(link.decode(memoryview(VALID)))

    def test_deep_nesting_does_not_raise(self):
        self.assertIsNone(link.decode(b'[' * 500 + b']' * 12))
        self.assertIsNone(link.decode(b'{"a":' * 100 + b'1' + b'}' * 100))

    def test_hello_encoding_keeps_non_default_extra_fields(self):
        h = link.Hello(role='TIMER', boot=1, seq=5, pending=2)
        self.assertEqual(link.decode(link.encode(h)), h)
        b = link.Hello(role='BACKEND', boot=2, up_ms=3, fw='1.0.0')
        self.assertEqual(link.encode(b), b'{"hello":"BACKEND","boot":2,"up_ms":3,"fw":"1.0.0"}')

    def test_gate_flags(self):
        self.assertEqual(link.gate_flags(24), {'beam_a': False, 'beam_b': False, 'stuck': False,
                                               'ready': True, 'armed': True, 'ok': True})
        self.assertFalse(link.gate_flags(8 | 1)['ok'])
        self.assertTrue(link.gate_flags(8 | 1)['beam_a'])
        self.assertTrue(link.gate_flags(2)['beam_b'])
        self.assertTrue(link.gate_flags(4 | 8)['stuck'])
        self.assertFalse(link.gate_flags(4 | 8)['ok'])
        self.assertFalse(link.gate_flags(0)['ok'])


class FuzzTests(SimpleTestCase):
    def test_random_bytes_never_raise(self):
        rng = random.Random(0xC0FFEE)
        alphabet = b'{}[]":,-+0123456789.eE \ttruefalsnul\\abcSTARTFINISHtime_markhelloack'
        for i in range(5000):
            n = rng.randrange(0, 600)
            if i % 2:
                data = bytes(rng.randrange(256) for _ in range(n))
            else:
                data = bytes(rng.choice(alphabet) for _ in range(n))
            result = link.decode(data)
            self.assertIn(type(result), (type(None), link.Event, link.Hello, link.Ack))

    def test_mutations_never_raise_and_accepted_ones_round_trip(self):
        rng = random.Random(1234)
        seeds = [p.encode('ascii') for _, verdict, _, _, p in load_vectors() if verdict == 'ok']
        interesting = b'{}[]":,-0123456789 eE.\\x'
        accepted = 0
        for i in range(20000):
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 3)):
                op = rng.randrange(3)
                pos = rng.randrange(len(data) + 1)
                byte = rng.choice(interesting) if rng.random() < 0.7 else rng.randrange(256)
                if op == 0 and pos < len(data):
                    data[pos] = byte
                elif op == 1 and pos < len(data):
                    del data[pos]
                else:
                    data.insert(pos, byte)
            msg = link.decode(bytes(data))
            if msg is None:
                continue
            accepted += 1
            again = link.decode(link.encode(msg))
            self.assertEqual(again, msg, msg=f'{bytes(data)!r}')
        self.assertGreater(accepted, 100)  # the fuzz actually exercises the accept path
