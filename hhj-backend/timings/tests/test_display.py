"""The backend drives the external display over the real UDP link (PROTOCOL.md, SHOW)."""
import time
from unittest import mock

from django.test import TransactionTestCase

from timings import arena, link, udp_link
from timings.devices import DeviceRegistry
from timings.tests.helpers import LOCAL, Client, event
from timings.udp_link import LinkServer


class DisplayLinkTests(TransactionTestCase):
    def setUp(self):
        for target in ('timings.live.publish_states', 'timings.live.publish_devices'):
            p = mock.patch(target)
            p.start()
            self.addCleanup(p.stop)
        arena._set_last_result(None)
        self.registry = DeviceRegistry()
        self.server = LinkServer(LOCAL, 0, registry=self.registry).start()
        self.addCleanup(self.server.stop)
        self.timer = Client(self.server.port)
        self.addCleanup(self.timer.close)
        self.competition = arena.create_competition('Display test')
        self.entry = arena.add_entry(self.competition, 'Ala')

    def next_show(self, timeout=5.0):
        """The next SHOW datagram the timer receives (other datagrams are skipped)."""
        while True:
            msg, raw, addr = self.timer.recv(timeout)
            if isinstance(msg, link.Show):
                self.assertEqual(addr, (LOCAL, self.server.port))
                self.assertEqual(raw, link.encode(msg))            # canonical bytes
                return msg

    def announce(self):
        self.timer.send(link.Hello(role='TIMER', boot=5, fw='1.1.0'))

    def test_a_timer_that_says_hello_is_told_what_to_show_at_once_and_then_every_second(self):
        self.announce()
        self.assertEqual(self.next_show(), link.Show('IDLE'))
        self.assertEqual(self.next_show(timeout=3.0), link.Show('IDLE'))       # the 1 Hz repeat
        self.assertGreaterEqual(self.server.stats['shows_sent'], 2)

    def test_nothing_is_sent_while_no_timer_is_known(self):
        gate = Client(self.server.port)
        self.addCleanup(gate.close)
        gate.send(link.Hello(role='START', boot=1, fw='2.1.0', st=24))
        msg, _, _ = gate.recv()
        self.assertEqual(msg.role, 'BACKEND')                      # gates get the HELLO answer only
        self.assertEqual(gate.barrier(), [])                       # barrier() itself announces a TIMER...
        self.assertIsInstance(self.next_show_for(gate), link.Show)  # ...which is then told what to show

    def next_show_for(self, client, timeout=5.0):
        while True:
            msg, _, _ = client.recv(timeout)
            if isinstance(msg, link.Show):
                return msg

    def test_display_follows_the_session_through_a_whole_run(self):
        self.announce()
        self.assertEqual(self.next_show().state, 'IDLE')
        gate = Client(self.server.port)
        self.addCleanup(gate.close)

        arena.select(self.competition, self.entry)
        arena.set_capture(self.competition, True)
        udp_link_kick(self.server)
        self.assertEqual(self.wait_for_state('READY').state, 'READY')

        gate.send(event('START', boot=1, seq=1, age_ms=0))         # the rider cuts the start beam now
        self.assertIsInstance(gate.recv()[0], link.Ack)
        running = self.wait_for_state('RUN')
        self.assertLess(running.age_ms, 3000)

        time.sleep(0.05)                                           # a (very) short ride
        gate.send(event('FINISH', boot=2, seq=1, age_ms=0))
        self.assertIsInstance(gate.recv()[0], link.Ack)
        result = self.wait_for_state('RESULT')
        self.assertTrue(40 <= result.ms < 3000, result.ms)
        session = arena.snapshot(self.competition)['session']
        self.assertEqual(result.ms, session['time_ms'])            # exactly the number the console shows

        arena.save_run(arena.open_run())
        udp_link_kick(self.server)
        self.assertEqual(self.next_show(), result)                 # the saved result stays up, same rev

    def wait_for_state(self, state, tries=20):
        for _ in range(tries):
            self.announce()                                        # a real timer keeps saying hello
            msg = self.next_show()
            if msg.state == state:
                return msg
        self.fail(f'the display was never told {state}')

    def test_shows_stop_when_the_timer_falls_silent(self):
        clock = [1000.0]
        registry = DeviceRegistry(clock=lambda: clock[0])
        registry.seen(link.Hello(role='TIMER', boot=1), LOCAL, 4321)
        self.assertEqual(registry.addresses('TIMER'), [(LOCAL, 4321)])
        clock[0] += 6.9
        self.assertEqual(registry.addresses('TIMER'), [(LOCAL, 4321)])
        clock[0] += 0.2                                            # 7.1 s of silence
        self.assertEqual(registry.addresses('TIMER'), [])
        self.assertEqual(registry.addresses('START'), [])

    def test_a_show_from_somebody_else_is_ignored(self):
        self.timer.send(link.Show('RESULT', rev=1, ms=5))
        self.assertEqual(self.timer.barrier(), [])                 # no reply to it
        self.assertEqual(self.server.stats['invalid'], 0)
        self.assertGreaterEqual(self.server.stats['ignored'], 1)

    def test_kick_display_without_a_server_is_harmless(self):
        with mock.patch.object(udp_link, '_server', None):
            udp_link.kick_display()


def udp_link_kick(server):
    server.kick()
