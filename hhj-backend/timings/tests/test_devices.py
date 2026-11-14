from django.test import SimpleTestCase

from timings.devices import DeviceRegistry
from timings.link import Hello


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class DeviceRegistryTests(SimpleTestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.reg = DeviceRegistry(clock=self.clock)

    def one(self):
        (entry,) = self.reg.snapshot()
        return entry

    def test_online_offline_by_age(self):
        self.reg.seen(Hello(role='START', boot=1, fw='2.1.0', st=24, seq=3, rssi=-60), '10.0.0.2')
        e = self.one()
        self.assertTrue(e['online'])
        self.assertEqual(e['age_s'], 0)
        self.assertEqual((e['role'], e['ip'], e['boot'], e['fw'], e['seq'], e['rssi']),
                         ('START', '10.0.0.2', 1, '2.1.0', 3, -60))
        self.clock.now += 6.9
        self.assertTrue(self.one()['online'])
        self.clock.now += 0.1
        e = self.one()
        self.assertFalse(e['online'])
        self.assertAlmostEqual(e['age_s'], 7.0)
        self.reg.seen(Hello(role='START', boot=1), '10.0.0.2')
        self.assertTrue(self.one()['online'])

    def test_reboot_detection(self):
        self.reg.seen(Hello(role='FINISH', boot=1), '10.0.0.3')
        self.reg.seen(Hello(role='FINISH', boot=1), '10.0.0.3')
        self.assertEqual(self.one()['reboots'], 0)
        self.reg.seen(Hello(role='FINISH', boot=2), '10.0.0.3')
        self.reg.seen(Hello(role='FINISH', boot=3), '10.0.0.3')
        e = self.one()
        self.assertEqual((e['reboots'], e['boot'], e['hellos']), (2, 3, 4))

    def test_key_is_role_and_ip(self):
        self.reg.seen(Hello(role='START', boot=1), '10.0.0.2')
        self.reg.seen(Hello(role='FINISH', boot=1), '10.0.0.2')
        self.reg.seen(Hello(role='START', boot=9), '10.0.0.4')
        self.assertEqual(len(self.reg.snapshot()), 3)
        self.assertEqual(sum(e['reboots'] for e in self.reg.snapshot()), 0)

    def test_gate_flags(self):
        cases = {
            24: dict(beam_a=False, beam_b=False, stuck=False, ready=True, armed=True, ok=True),
            8: dict(beam_a=False, beam_b=False, stuck=False, ready=True, armed=False, ok=True),
            9: dict(beam_a=True, beam_b=False, stuck=False, ready=True, armed=False, ok=False),
            30: dict(beam_a=False, beam_b=True, stuck=True, ready=True, armed=True, ok=False),
            0: dict(beam_a=False, beam_b=False, stuck=False, ready=False, armed=False, ok=False),
        }
        for st, flags in cases.items():
            with self.subTest(st=st):
                reg = DeviceRegistry(clock=self.clock)
                reg.seen(Hello(role='START', boot=1, st=st), '1.1.1.1')
                (e,) = reg.snapshot()
                self.assertEqual({k: e[k] for k in flags}, flags)

    def test_timer_has_no_gate_flags(self):
        self.reg.seen(Hello(role='TIMER', boot=1, st=1, ms=12340), '10.0.0.9')
        e = self.one()
        self.assertEqual(e['ms'], 12340)
        self.assertNotIn('ok', e)
