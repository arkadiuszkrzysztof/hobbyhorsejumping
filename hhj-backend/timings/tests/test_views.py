import json
from unittest import mock

from django.test import TestCase

from timings import udp_link
from timings.devices import registry
from timings.ingest import ingest_event
from timings.link import Hello
from timings.models import TimeReading
from timings.tests.helpers import LOCAL, event

T0 = 1_794_000_000_000


class TimeReadingListGetTests(TestCase):
    @classmethod
    def setUpTestData(cls):
        TimeReading.objects.bulk_create([
            TimeReading(time_mark='START', sensor_time=T0 + i, server_time=T0 + i) for i in range(1005)
        ])

    def get(self, query=''):
        return self.client.get('/timereadings/' + query)

    def test_default_limit_and_order(self):
        r = self.get()
        self.assertEqual(r.status_code, 200)
        data = r.json()
        self.assertEqual(len(data), 100)
        self.assertEqual(data[0]['server_time'], T0 + 1004)  # latest first
        self.assertEqual(data[1]['server_time'], T0 + 1003)
        expected = {'id', 'competition_start', 'time_mark', 'sensor_time', 'server_time', 'gate_boot',
                    'gate_seq', 'gate_time', 'direction', 'gap_ms', 'copies', 'source'}
        self.assertEqual(set(data[0]), expected)

    def test_limit_bounds(self):
        self.assertEqual(len(self.get('?limit=1').json()), 1)
        self.assertEqual(len(self.get('?limit=1000').json()), 1000)
        self.assertEqual(len(self.get('?limit=1001').json()), 1000)
        self.assertEqual(len(self.get('?limit=99999999').json()), 1000)
        for bad in ('0', '-1', 'abc', '1.5', '', ' 5'):
            with self.subTest(limit=bad):
                r = self.get('?limit=' + bad)
                self.assertEqual(r.status_code, 400)
                self.assertIn('error', r.json())


@mock.patch('timings.live.publish')
class TimeReadingListPostTests(TestCase):
    def post(self, body, content_type='application/json'):
        if not isinstance(body, (str, bytes)):
            body = json.dumps(body)
        return self.client.post('/timereadings/', body, content_type=content_type)

    def test_v1_string_sensor_time(self, publish):
        with self.captureOnCommitCallbacks(execute=True):
            r = self.post({'time_mark': 'FINISH', 'sensor_time': str(T0)})
        self.assertEqual(r.status_code, 201, r.content)
        self.assertEqual(r.json()['sensor_time'], T0)
        self.assertEqual(r.json()['source'], 'http')
        self.assertEqual(TimeReading.objects.get().sensor_time, T0)
        publish.assert_called_once()
        self.assertEqual(publish.call_args.args[0]['time_mark'], 'FINISH')

    def test_int_sensor_time(self, publish):
        r = self.post({'time_mark': 'START', 'sensor_time': T0})
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()['sensor_time'], T0)

    def test_absent_or_zero_means_now(self, publish):
        with mock.patch('timings.ingest.now_ms', return_value=T0):
            a = self.post({'time_mark': 'START'})
            b = self.post({'time_mark': 'START', 'sensor_time': 0})
            c = self.post({'time_mark': 'START', 'sensor_time': '0'})
        for r in (a, b, c):
            self.assertEqual(r.status_code, 201)
            self.assertEqual((r.json()['sensor_time'], r.json()['server_time']), (T0, T0))

    def test_age_ms(self, publish):
        with mock.patch('timings.ingest.now_ms', return_value=T0):
            r = self.post({'time_mark': 'START', 'age_ms': 250})
            s = self.post({'time_mark': 'START', 'age_ms': '250', 'sensor_time': '0'})
        self.assertEqual(r.json()['sensor_time'], T0 - 250)
        self.assertEqual(s.json()['sensor_time'], T0 - 250)

    def test_form_encoded(self, publish):
        r = self.client.post('/timereadings/', {'time_mark': 'FINISH', 'sensor_time': str(T0)})
        self.assertEqual(r.status_code, 201, r.content)
        self.assertEqual(r.json()['sensor_time'], T0)

    def test_json_without_content_type(self, publish):
        r = self.post({'time_mark': 'FINISH', 'sensor_time': str(T0)}, content_type='text/plain')
        self.assertEqual(r.status_code, 201, r.content)

    def test_bad_input(self, publish):
        cases = [
            b'', b'not json', b'[]', b'"FINISH"', b'null', b'{"time_mark":',
            {}, {'time_mark': 'BOGUS'}, {'time_mark': 'finish'}, {'time_mark': None}, {'time_mark': 5},
            {'time_mark': 'TIMER'}, {'time_mark': 'COMBINED'},
            {'time_mark': 'START', 'sensor_time': 'abc'},
            {'time_mark': 'START', 'sensor_time': '-5'},
            {'time_mark': 'START', 'sensor_time': -5},
            {'time_mark': 'START', 'sensor_time': 1.5},
            {'time_mark': 'START', 'sensor_time': '1e12'},
            {'time_mark': 'START', 'sensor_time': True},
            {'time_mark': 'START', 'sensor_time': [1]},
            {'time_mark': 'START', 'sensor_time': 2 ** 53 + 1},
            {'time_mark': 'START', 'sensor_time': '1' * 17},
            {'time_mark': 'START', 'age_ms': -1},
            {'time_mark': 'START', 'age_ms': 600_001},
            {'time_mark': 'START', 'age_ms': 'x'},
            {'time_mark': 'START', 'sensor_time': T0, 'age_ms': 5},
        ]
        for body in cases:
            with self.subTest(body=body):
                r = self.post(body)
                self.assertEqual(r.status_code, 400)
                self.assertIsInstance(r.json()['error'], str)
        self.assertFalse(TimeReading.objects.exists())
        publish.assert_not_called()


class DevicesViewTests(TestCase):
    def setUp(self):
        registry.clear()
        self.addCleanup(registry.clear)

    def test_without_link(self):
        with mock.patch.object(udp_link, '_server', None):
            r = self.client.get('/devices/')
        self.assertEqual(r.status_code, 200)
        self.assertEqual(r.json(), {'link': {'running': False, 'port': None, 'stats': None}, 'devices': []})

    def test_with_link_and_devices(self):
        server = udp_link.LinkServer(LOCAL, 0).start()
        self.addCleanup(server.stop)
        registry.seen(Hello(role='START', boot=7, fw='2.1.0', st=24, seq=2), '10.0.0.2')
        registry.seen(Hello(role='TIMER', boot=8, st=1, ms=1234), '10.0.0.3')
        with mock.patch.object(udp_link, '_server', server):
            data = self.client.get('/devices/').json()
        self.assertEqual(data['link']['running'], True)
        self.assertEqual(data['link']['port'], server.port)
        self.assertEqual(set(data['link']['stats']), set(udp_link.STAT_KEYS))
        start, timer = data['devices']
        self.assertEqual((start['role'], start['ip'], start['boot'], start['online'], start['ok']),
                         ('START', '10.0.0.2', 7, True, True))
        for key in ('beam_a', 'beam_b', 'stuck', 'ready', 'armed', 'age_s', 'reboots', 'rssi', 'net',
                    'up_ms', 'pending', 'fw', 'st', 'seq', 'ms'):
            self.assertIn(key, start)
        self.assertEqual((timer['role'], timer['ms']), ('TIMER', 1234))


class GateEventInListTests(TestCase):
    def test_gate_event_fields_in_get(self):
        with mock.patch('timings.live.publish'):
            ingest_event(event('FINISH', boot=4294967295, seq=12, age_ms=10, dir=1, gap_ms=40, t=2 ** 53), T0, '10.0.0.9')
        (row,) = self.client.get('/timereadings/').json()
        self.assertEqual(row['gate_boot'], 4294967295)
        self.assertEqual(row['gate_time'], 2 ** 53)
        self.assertEqual(row['sensor_time'], T0 - 10)
        self.assertEqual((row['direction'], row['gap_ms'], row['copies'], row['source']), (1, 40, 1, '10.0.0.9'))
        self.assertIsNone(row['competition_start'])
