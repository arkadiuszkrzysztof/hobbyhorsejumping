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
        expected = {'id', 'run', 'time_mark', 'sensor_time', 'server_time', 'gate_boot',
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
        ingest_event(event('FINISH', boot=4294967295, seq=12, age_ms=10, dir=1, gap_ms=40, t=2 ** 53), T0, '10.0.0.9')
        (row,) = self.client.get('/timereadings/').json()
        self.assertEqual(row['gate_boot'], 4294967295)
        self.assertEqual(row['gate_time'], 2 ** 53)
        self.assertEqual(row['sensor_time'], T0 - 10)
        self.assertEqual((row['direction'], row['gap_ms'], row['copies'], row['source']), (1, 40, 1, '10.0.0.9'))

    def test_the_list_is_read_only(self):
        r = self.client.post('/timereadings/', {'time_mark': 'START'}, content_type='application/json')
        self.assertEqual(r.status_code, 405)
        self.assertFalse(TimeReading.objects.exists())
