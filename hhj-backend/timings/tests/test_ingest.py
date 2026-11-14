import threading

from django.db import connection
from django.test import TransactionTestCase

from timings.ingest import STALE_AGE_MS, ingest_event
from timings.models import TimeReading
from timings.tests.helpers import event

T0 = 1_794_000_000_000  # epoch ms (Nov 2026) - does not fit in 32 bits


class IngestTests(TransactionTestCase):
    def test_create(self):
        ev = event('START', boot=3735928559, seq=7, age_ms=12, t=123456, dir=1, gap_ms=38)
        reading, created = ingest_event(ev, T0, '10.0.0.5')
        self.assertTrue(created)
        r = TimeReading.objects.get()
        self.assertEqual(r.pk, reading.pk)
        self.assertEqual((r.time_mark, r.gate_boot, r.gate_seq), ('START', 3735928559, 7))
        self.assertEqual(r.sensor_time, T0 - 12)
        self.assertEqual(r.server_time, T0)
        self.assertEqual((r.gate_time, r.direction, r.gap_ms, r.copies, r.source), (123456, 1, 38, 1, '10.0.0.5'))

    def test_duplicates_are_one_row_and_counted(self):
        for n in range(7):
            reading, created = ingest_event(event(seq=3, n=n), T0 + n * 10, 'x')
            self.assertEqual(created, n == 0)
        self.assertEqual(TimeReading.objects.count(), 1)
        r = TimeReading.objects.get()
        self.assertEqual(r.copies, 7)
        self.assertEqual(reading.copies, 7)
        self.assertEqual(r.server_time, T0)  # first arrival

    def test_min_filter_later_copy_better(self):
        ingest_event(event(age_ms=0), T0 + 100, '')   # delayed copy: edge T0+100
        ingest_event(event(age_ms=50), T0 + 60, '')   # better: edge T0+10
        ingest_event(event(age_ms=10), T0 + 200, '')  # worse: edge T0+190
        r = TimeReading.objects.get()
        self.assertEqual(r.sensor_time, T0 + 10)
        self.assertEqual(r.server_time, T0 + 100)
        self.assertEqual(r.copies, 3)

    def test_min_filter_first_copy_best(self):
        ingest_event(event(age_ms=50), T0 + 60, '')   # edge T0+10
        ingest_event(event(age_ms=0), T0 + 100, '')   # edge T0+100: never raises it
        self.assertEqual(TimeReading.objects.get().sensor_time, T0 + 10)

    def test_stale(self):
        self.assertEqual(ingest_event(event(age_ms=STALE_AGE_MS + 1), T0, ''), (None, False))
        self.assertEqual(ingest_event(event(age_ms=4294967295), T0, ''), (None, False))
        self.assertFalse(TimeReading.objects.exists())
        reading, created = ingest_event(event(age_ms=STALE_AGE_MS), T0, '')  # boundary is accepted
        self.assertTrue(created)

    def test_stale_copy_of_existing_event_is_not_counted(self):
        ingest_event(event(age_ms=0), T0, '')
        self.assertEqual(ingest_event(event(age_ms=STALE_AGE_MS + 1), T0, ''), (None, False))
        self.assertEqual(TimeReading.objects.get().copies, 1)

    def test_identity(self):
        self.assertTrue(ingest_event(event('START', boot=5, seq=1), T0, '')[1])
        self.assertTrue(ingest_event(event('FINISH', boot=5, seq=1), T0, '')[1])
        self.assertTrue(ingest_event(event('START', boot=6, seq=1), T0, '')[1])
        self.assertTrue(ingest_event(event('START', boot=5, seq=2), T0, '')[1])
        self.assertFalse(ingest_event(event('FINISH', boot=5, seq=1), T0, '')[1])
        self.assertEqual(TimeReading.objects.count(), 4)

    def test_large_values_round_trip(self):
        big = 2 ** 53
        ingest_event(event(boot=4294967295, seq=4294967295, t=big), big, '')
        r = TimeReading.objects.get()
        self.assertEqual((r.gate_boot, r.gate_seq, r.gate_time, r.server_time, r.sensor_time),
                         (4294967295, 4294967295, big, big, big))

    def test_created_once_per_event(self):
        made = [ingest_event(event('START', seq=1, n=n), T0, '')[1] for n in range(5)]
        made.append(ingest_event(event('FINISH', seq=1), T0, '')[1])
        self.assertEqual(made, [True, False, False, False, False, True])
        self.assertEqual(sorted(TimeReading.objects.values_list('time_mark', 'copies')), [('FINISH', 1), ('START', 5)])

    def test_concurrent_copies_one_row(self):
        barrier = threading.Barrier(4)
        errors = []

        def worker(i):
            try:
                barrier.wait()
                ingest_event(event(seq=9, age_ms=i), T0, '')
            except Exception as exc:  # pragma: no cover - reported below
                errors.append(exc)
            finally:
                connection.close()

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        r = TimeReading.objects.get()
        self.assertEqual(r.copies, 4)
        self.assertEqual(r.sensor_time, T0 - 3)
