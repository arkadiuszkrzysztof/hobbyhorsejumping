"""The firmware's C++ code against this backend, for real.

Needs the host build of the gate (hhj-common/tools/gate_sim.cpp); `make
test-link` from the repo root builds it and points HHJ_GATE_SIM at it. Without
it these tests are skipped — `make test` never skips them.

  DifferentialDecodeTests  the C++ decoder (link.h) and the Python decoder
                           (link.py) must give the same verdict and the same
                           fields for every datagram, valid or damaged.
  GateAgainstBackendTests  the real gate application (gate_app.h) sends HELLOs
                           and a crossing over UDP to the real LinkServer and
                           must end up acknowledged, with the reading in the
                           database at the right time.
"""
import os
import random
import re
import subprocess
import time
import unittest
from pathlib import Path

from django.test import SimpleTestCase, TransactionTestCase

from timings import link
from timings.devices import DeviceRegistry
from timings.models import TimeReading
from timings.tests.helpers import LOCAL
from timings.tests.test_link import load_vectors
from timings.udp_link import LinkServer

SIM = os.environ.get('HHJ_GATE_SIM', '')
needs_sim = unittest.skipUnless(SIM and Path(SIM).exists(),
                                'gate simulator not built (run `make test-link` from the repo root)')


def cpp_describe(datagrams):
    """What the C++ decoder makes of each datagram ('-' = rejected)."""
    stdin = ''.join(d.hex() + '\n' for d in datagrams)
    out = subprocess.run([SIM, 'decode'], input=stdin, capture_output=True, text=True, timeout=120, check=True)
    lines = out.stdout.split('\n')[:-1]
    assert len(lines) == len(datagrams), (len(lines), len(datagrams))
    return lines


@needs_sim
class DifferentialDecodeTests(SimpleTestCase):
    def compare(self, datagrams):
        accepted = 0
        for data, cpp in zip(datagrams, cpp_describe(datagrams)):
            py = link.describe(link.decode(data))
            if py != cpp:
                self.fail(f'decoders disagree on {data!r}\n  python: {py}\n  c++:    {cpp}')
            accepted += py != '-'
        return accepted

    def test_vectors(self):
        vectors = load_vectors()
        datagrams = [payload.encode('ascii') for _, _, _, _, payload in vectors]
        for (lineno, verdict, _, expect, _), cpp in zip(vectors, cpp_describe(datagrams)):
            self.assertEqual(cpp, expect, msg=f'vectors.tsv:{lineno}')

    def test_mutated_valid_datagrams(self):
        rng = random.Random(20261114)
        seeds = [p.encode('ascii') for _, verdict, _, _, p in load_vectors() if verdict == 'ok']
        interesting = b'{}[]":,-+0123456789 eE.\\tfnu\t\r\n'
        datagrams = []
        for _ in range(60000):
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 4)):
                op = rng.randrange(4)
                pos = rng.randrange(len(data) + 1)
                byte = rng.choice(interesting) if rng.random() < 0.7 else rng.randrange(256)
                if op == 0 and pos < len(data):
                    data[pos] = byte
                elif op == 1 and pos < len(data):
                    del data[pos]
                elif op == 2 and pos < len(data):
                    # swap two neighbouring bytes
                    nxt = min(pos + 1, len(data) - 1)
                    data[pos], data[nxt] = data[nxt], data[pos]
                else:
                    data.insert(pos, byte)
            datagrams.append(bytes(data))
        accepted = self.compare(datagrams)
        self.assertGreater(accepted, 3000)                       # plenty stayed valid...
        self.assertGreater(len(datagrams) - accepted, 3000)      # ...and plenty did not

    def test_number_and_string_edge_cases(self):
        rng = random.Random(7)
        numbers = [b'0', b'-0', b'1', b'-1', b'00', b'01', b'1.0', b'1e3', b'1E+3', b'1e-3', b'1.', b'.5', b'-',
                   b'+1', b'4294967295', b'4294967296', b'9007199254740992', b'9007199254740993',
                   b'999999999999999999', b'1000000000000000000', b'99999999999999999999', b'-129', b'-128',
                   b'127', b'128', b'65535', b'65536', b'255', b'256', b'true', b'false', b'null', b'tru',
                   b'NaN', b'Infinity', b'""', b'"1"', b'[]', b'{}', b'"\\u0031"', b'1e400', b'0x10', b'1_0']
        keys = [b'time_mark', b'hello', b'ack', b'age_ms', b'boot', b'seq', b't', b'dir', b'gap_ms', b'n',
                b'up_ms', b'fw', b'st', b'pending', b'ms', b'rssi', b'net', b'x']
        strings = [b'"START"', b'"FINISH"', b'"TIMER"', b'"BACKEND"', b'"start"', b'"STARTS"', b'""', b'"1.0"',
                   b'"a b"', b'"\\u0053TART"', b'"x\\"y"', b'"123456789012345"', b'"1234567890123456"',
                   b'"\xc3\xa9"', b'"a\\\\"', b'"S\tT"']
        bases = [
            [(b'time_mark', b'"START"'), (b'age_ms', b'5'), (b'boot', b'9'), (b'seq', b'2'), (b't', b'7'),
             (b'dir', b'1'), (b'gap_ms', b'3'), (b'n', b'0')],
            [(b'hello', b'"TIMER"'), (b'boot', b'5'), (b'up_ms', b'1'), (b'fw', b'"1.0"'), (b'st', b'1'),
             (b'seq', b'0'), (b'pending', b'0'), (b'ms', b'5'), (b'rssi', b'-50'), (b'net', b'0')],
            [(b'ack', b'"FINISH"'), (b'boot', b'3'), (b'seq', b'1')],
        ]
        datagrams = []
        for _ in range(40000):
            # a valid message with a field dropped, a value or two replaced, a stray key added
            fields = [f for f in rng.choice(bases) if rng.random() > 0.06]
            for _ in range(rng.choice([0, 1, 1, 2])):
                if fields:
                    i = rng.randrange(len(fields))
                    fields[i] = (fields[i][0], rng.choice(strings) if rng.random() < 0.3 else rng.choice(numbers))
            if rng.random() < 0.25:
                fields.append((rng.choice(keys), rng.choice(numbers + strings)))
            rng.shuffle(fields)
            sep = rng.choice([b',', b', ', b' ,\n'])
            body = sep.join(b'"' + k + b'"' + rng.choice([b':', b' : ', b':\t']) + v for k, v in fields)
            datagrams.append(rng.choice([b'{', b' {', b'{ ']) + body + rng.choice([b'}', b'}', b'} ', b'}}', b'']))
        accepted = self.compare(datagrams)
        self.assertGreater(accepted, 5000)
        self.assertGreater(len(datagrams) - accepted, 5000)

    def test_random_bytes(self):
        rng = random.Random(99)
        datagrams = [bytes(rng.randrange(256) for _ in range(rng.randrange(1, 80))) for _ in range(20000)]
        datagrams += [b'{' + bytes(rng.choice(b'"{}:,0123456789 abtruefalsn\\-.e') for _ in range(rng.randrange(0, 60)))
                      for _ in range(20000)]
        self.compare(datagrams)

    def test_length_limits(self):
        base = b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"p":"'
        datagrams = [base + b'x' * n + b'"}' for n in range(440, 470)]
        datagrams += [b' ' * n + b'{"hello":"TIMER","boot":1}' for n in (0, 1, 485, 486, 487, 488, 600)]
        accepted = self.compare(datagrams)
        self.assertGreater(accepted, 10)
        self.assertLess(accepted, len(datagrams))


@needs_sim
class GateAgainstBackendTests(TransactionTestCase):
    def setUp(self):
        self.registry = DeviceRegistry()
        self.server = LinkServer(LOCAL, 0, registry=self.registry).start()
        self.addCleanup(self.server.stop)

    def gate(self, mode, boot, cross_ms, run_ms):
        return subprocess.Popen(
            [SIM, 'gate', '--mode', mode, '--peer', f'{LOCAL}:{self.server.port}', '--boot', str(boot),
             '--cross', str(cross_ms), '--run', str(run_ms)],
            stdout=subprocess.PIPE, text=True)

    @staticmethod
    def finish(proc):
        out, _ = proc.communicate(timeout=30)
        assert proc.returncode == 0, out
        summary = dict(kv.split('=') for kv in re.search(r'^SUMMARY (.*)$', out, re.M).group(1).split())
        edge = int(re.search(r'^EDGE epoch_ms=(\d+)$', out, re.M).group(1))
        return out, summary, edge

    def test_a_crossing_is_stored_and_acknowledged(self):
        out, summary, edge = self.finish(self.gate('START', 4242, cross_ms=300, run_ms=1500))

        # the gate saw one crossing, found the backend, and has nothing left to retry
        self.assertEqual(summary['events'], '1', out)
        self.assertEqual(summary['pending'], '0', out)
        self.assertEqual(summary['acked'], '1', out)
        self.assertEqual(summary['peers'], '1', out)
        self.assertEqual(summary['bad_datagrams'], '0', out)       # it understood every byte the backend sent
        self.assertEqual(summary['failed_sends'], '0', out)
        self.assertIn('BACKEND found at', out)
        self.assertIn('event #1 confirmed by backend', out)

        # exactly one reading, with the gate's own data
        r = TimeReading.objects.get()
        self.assertEqual((r.time_mark, r.gate_boot, r.gate_seq, r.direction), ('START', 4242, 1, 1))
        self.assertTrue(25 <= r.gap_ms <= 40, r.gap_ms)            # beam B is scripted 30 ms after beam A
        self.assertGreaterEqual(r.copies, 7)                       # the whole burst arrived (unicast + "broadcast")
        self.assertIsNone(r.competition_start)
        # ...at the right moment: the gate said when the beam was cut (its wall clock), the
        # backend reconstructed it from arrival - age. Same machine, so they must agree closely.
        self.assertLess(abs(r.sensor_time - edge), 25, (r.sensor_time, edge))
        self.assertGreaterEqual(r.server_time, r.sensor_time)

        # the backend understood every byte the gate sent
        self.assertEqual(self.server.stats['invalid'], 0)
        self.assertEqual(self.server.stats['errors'], 0)
        self.assertEqual(self.server.stats['events'], 1)
        self.assertGreaterEqual(self.server.stats['hellos'], 1)
        (dev,) = self.registry.snapshot()
        self.assertEqual((dev['role'], dev['boot'], dev['fw']), ('START', 4242, 'sim'))

    def test_start_and_finish_gates_give_the_elapsed_time(self):
        # Two gate processes started together: START is crossed 300 ms in, FINISH 1500 ms in.
        start = self.gate('START', 1001, cross_ms=300, run_ms=2600)
        finish = self.gate('FINISH', 1002, cross_ms=1500, run_ms=2600)
        _, s_summary, s_edge = self.finish(start)
        _, f_summary, f_edge = self.finish(finish)
        self.assertEqual((s_summary['acked'], f_summary['acked']), ('1', '1'))
        self.assertEqual((s_summary['pending'], f_summary['pending']), ('0', '0'))

        s = TimeReading.objects.get(time_mark='START')
        f = TimeReading.objects.get(time_mark='FINISH')
        self.assertEqual((s.gate_boot, f.gate_boot), (1001, 1002))
        elapsed_true = f_edge - s_edge                               # ~1200 ms, by the gates' own clocks
        elapsed_backend = f.sensor_time - s.sensor_time
        self.assertTrue(1100 < elapsed_true < 1400, elapsed_true)
        self.assertLess(abs(elapsed_backend - elapsed_true), 25, (elapsed_backend, elapsed_true))
        self.assertEqual({d['role'] for d in self.registry.snapshot()}, {'START', 'FINISH'})

    def test_backend_started_late_still_gets_the_event(self):
        # The gate is crossed while nothing listens on the port; the backend comes up 1 s later.
        port = self.server.port
        self.server.stop()
        proc = subprocess.Popen(
            [SIM, 'gate', '--mode', 'FINISH', '--peer', f'{LOCAL}:{port}', '--boot', '555',
             '--cross', '200', '--run', '3500'], stdout=subprocess.PIPE, text=True)
        time.sleep(1.2)
        self.assertEqual(TimeReading.objects.count(), 0)
        late = LinkServer(LOCAL, port, registry=self.registry).start()
        self.addCleanup(late.stop)
        out, summary, edge = self.finish(proc)
        self.assertEqual(summary['acked'], '1', out)
        r = TimeReading.objects.get()
        self.assertEqual((r.time_mark, r.gate_boot, r.gate_seq), ('FINISH', 555, 1))
        self.assertLess(abs(r.sensor_time - edge), 25, (r.sensor_time, edge))   # a second late, still the right time
        self.assertGreater(r.server_time - r.sensor_time, 800)
