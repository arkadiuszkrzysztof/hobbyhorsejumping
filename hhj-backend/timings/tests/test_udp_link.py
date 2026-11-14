import io
import socket
from unittest import mock

from django.core.management import call_command
from django.db import DatabaseError
from django.test import TransactionTestCase

from timings import link, udp_link
from timings.devices import DeviceRegistry
from timings.models import TimeReading
from timings.tests.helpers import LOCAL, Client, event, wait_for
from timings.udp_link import LinkServer


class LinkServerTests(TransactionTestCase):
    def setUp(self):
        self.registry = DeviceRegistry()
        # drive_displays=False: these tests are about the gate side of the link; the
        # display side (SHOW datagrams to timers) has its own tests in test_display.py
        self.server = LinkServer(LOCAL, 0, registry=self.registry, drive_displays=False).start()
        self.addCleanup(self.server.stop)
        self.client = Client(self.server.port)
        self.addCleanup(self.client.close)

    def test_binds_ephemeral_port(self):
        self.assertTrue(self.server.running)
        self.assertNotEqual(self.server.port, 0)
        self.assertNotEqual(self.server.port, 3004)

    def test_hello_gets_backend_hello_unicast_to_source(self):
        self.client.send(link.Hello(role='START', boot=11, fw='2.1.0', st=24, seq=0, rssi=-50))
        msg, raw, addr = self.client.recv()
        self.assertEqual(addr, (LOCAL, self.server.port))  # came from the link socket
        self.assertIsInstance(msg, link.Hello)
        self.assertEqual(msg.role, 'BACKEND')
        self.assertEqual(msg.boot, udp_link.PROCESS_BOOT)
        self.assertEqual(msg.fw, udp_link.BACKEND_FW)
        self.assertEqual(raw, link.encode(msg))  # canonical
        (dev,) = self.registry.snapshot()
        self.assertEqual((dev['role'], dev['ip'], dev['boot'], dev['ok']), ('START', LOCAL, 11, True))
        self.assertEqual(self.server.stats['hellos'], 1)

    def test_timer_hello_answered(self):
        self.client.send(link.Hello(role='TIMER', boot=1, st=1, ms=500))
        msg, _, _ = self.client.recv()
        self.assertEqual(msg.role, 'BACKEND')

    def test_event_stored_then_acked(self):
        self.client.send(event('FINISH', boot=77, seq=3, age_ms=20, dir=-1, gap_ms=12, t=999))
        msg, raw, _ = self.client.recv()
        self.assertEqual(msg, link.Ack(mark='FINISH', boot=77, seq=3))
        self.assertEqual(raw, b'{"ack":"FINISH","boot":77,"seq":3}')
        r = TimeReading.objects.get()  # stored before the ACK was sent
        self.assertEqual((r.time_mark, r.gate_boot, r.gate_seq, r.direction, r.gap_ms, r.gate_time, r.source),
                         ('FINISH', 77, 3, -1, 12, 999, LOCAL))
        self.assertLessEqual(r.sensor_time, r.server_time - 20)
        self.assertEqual(self.server.stats['events'], 1)
        wait_for(lambda: self.server.stats['acks_sent'] == 1, msg='acks_sent')  # counted after sendto

    def test_duplicate_acked_one_row(self):
        for n in range(3):
            self.client.send(event(boot=5, seq=1, n=n))
            msg, _, _ = self.client.recv()
            self.assertEqual(msg, link.Ack(mark='START', boot=5, seq=1))
        self.assertEqual(TimeReading.objects.count(), 1)
        self.assertEqual(TimeReading.objects.get().copies, 3)
        self.assertEqual((self.server.stats['events'], self.server.stats['duplicates']), (1, 2))

    def test_stale_not_acked_not_stored(self):
        self.client.send(event(age_ms=600_001))
        self.assertEqual(self.client.barrier(), [])
        self.assertFalse(TimeReading.objects.exists())
        self.assertEqual(self.server.stats['stale'], 1)

    def test_malformed_ignored_and_thread_survives(self):
        bad = [b'', b' ', b'garbage', b'\xff\x00\xfe', b'{"time_mark":"START"}',
               b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"seq":3}',
               b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"p":"' + b'x' * 600 + b'"}']
        for payload in bad:
            self.client.send(payload)
        self.assertEqual(self.client.barrier(), [])
        self.assertEqual(self.server.stats['invalid'], len(bad))
        self.assertTrue(self.server.running)
        self.client.send(event(seq=4))
        self.assertIsInstance(self.client.recv()[0], link.Ack)

    def test_oversize_datagram(self):
        self.client.send(b'{"time_mark":"START","age_ms":5,"boot":9,"seq":2,"p":"' + b'x' * 9000 + b'"}')
        self.assertEqual(self.client.barrier(), [])
        self.assertEqual(self.server.stats['invalid'], 1)
        self.assertTrue(self.server.running)

    def test_ack_and_backend_hello_ignored(self):
        self.client.send(link.Ack(mark='START', boot=1, seq=1))
        self.client.send(link.Hello(role='BACKEND', boot=1, up_ms=5, fw='1.0.0'))
        self.assertEqual(self.client.barrier(), [])
        self.assertEqual(self.server.stats['ignored'], 2)
        self.assertEqual([d['role'] for d in self.registry.snapshot()], ['TIMER'])  # only the barrier

    def test_db_error_does_not_kill_thread(self):
        with mock.patch('timings.ingest.ingest_event', side_effect=DatabaseError('boom')):
            with self.assertLogs('timings.udp_link', 'ERROR'):
                self.client.send(event(seq=1))
                self.assertEqual(self.client.barrier(), [])  # no ACK for an unstored event
        self.assertEqual(self.server.stats['errors'], 1)
        self.assertTrue(self.server.running)
        self.client.send(event(seq=1))
        self.assertIsInstance(self.client.recv()[0], link.Ack)

    def test_burst_of_distinct_events(self):
        count = 100
        for seq in range(1, count + 1):
            self.client.send(event('START' if seq % 2 else 'FINISH', boot=42, seq=seq))
        acks = set()
        while len(acks) < count:
            msg, _, _ = self.client.recv()
            self.assertIsInstance(msg, link.Ack)
            acks.add((msg.mark, msg.seq))
        self.assertEqual(TimeReading.objects.filter(gate_boot=42).count(), count)
        wait_for(lambda: self.server.stats['acks_sent'] == count, msg='acks_sent')

    def test_stats_count_datagrams(self):
        self.client.send(b'x')
        self.client.barrier()
        self.assertEqual(self.server.stats['datagrams'], 2)


class LinkServerLifecycleTests(TransactionTestCase):
    def test_stop_is_clean(self):
        server = LinkServer(LOCAL, 0, registry=DeviceRegistry()).start()
        port = server.port
        server.stop()
        self.assertFalse(server.running)
        self.assertEqual(server._sock.fileno(), -1)
        # the port is free again
        again = LinkServer(LOCAL, port, registry=DeviceRegistry()).start()
        again.stop()

    def test_second_server_on_same_port_fails_cleanly(self):
        first = LinkServer(LOCAL, 0, registry=DeviceRegistry()).start()
        self.addCleanup(first.stop)
        second = LinkServer(LOCAL, first.port, registry=DeviceRegistry())
        with self.assertRaises(OSError):
            second.start()
        self.assertFalse(second.running)
        self.assertTrue(first.running)
        # the first one still works
        client = Client(first.port)
        self.addCleanup(client.close)
        client.send(link.Hello(role='TIMER', boot=1))
        self.assertEqual(client.recv()[0].role, 'BACKEND')

    def test_start_server_logs_and_returns_none_on_bind_failure(self):
        blocker = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        blocker.bind((LOCAL, 0))
        self.addCleanup(blocker.close)
        with mock.patch.object(udp_link, '_server', None):
            with self.assertLogs('timings.udp_link', 'ERROR') as logs:
                self.assertIsNone(udp_link.start_server(LOCAL, blocker.getsockname()[1]))
            self.assertIn('cannot bind', logs.output[0])
            self.assertIsNone(udp_link.get_server())

    def test_start_server_is_a_singleton(self):
        with mock.patch.object(udp_link, '_server', None):
            server = udp_link.start_server(LOCAL, 0)
            try:
                self.assertIs(udp_link.start_server(LOCAL, 0), server)
                self.assertIs(udp_link.get_server(), server)
            finally:
                server.stop()


class HhjSendCommandTests(TransactionTestCase):
    def test_plays_a_gate(self):
        server = LinkServer(LOCAL, 0, registry=DeviceRegistry()).start()
        self.addCleanup(server.stop)
        out = io.StringIO()
        call_command('hhj_send', 'FINISH', '--to', LOCAL, '--port', str(server.port),
                     '--boot', '1234', '--seq', '5', '--age', '40', '--wait', '0.3', stdout=out)
        r = TimeReading.objects.get()
        self.assertEqual((r.time_mark, r.gate_boot, r.gate_seq, r.copies), ('FINISH', 1234, 5, 7))
        self.assertLessEqual(r.sensor_time, r.server_time - 40)
        text = out.getvalue()
        self.assertIn('HELLO {"hello":"BACKEND"', text)
        self.assertIn('ACK {"ack":"FINISH","boot":1234,"seq":5}', text)
        self.assertEqual(server.stats['hellos'], 1)
        wait_for(lambda: server.stats['acks_sent'] == 7, msg='7 acks')
