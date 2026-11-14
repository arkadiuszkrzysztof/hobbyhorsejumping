"""The operator's competition flow (timings/arena.py): the list, the session,
which signals count, what the display shows."""
from unittest import mock

from django.test import TestCase

from events.models import Competition, CompetitionStart, Participant
from timings import arena, link
from timings.ingest import ingest_event
from timings.models import Run, TimeReading
from timings.tests.helpers import event

T0 = 1_794_000_000_000      # "now" in these tests, epoch ms


class ArenaCase(TestCase):
    def setUp(self):
        self.clock = T0
        patcher = mock.patch('timings.arena.now_ms', side_effect=lambda: self.clock)
        patcher.start()
        self.addCleanup(patcher.stop)
        for target in ('timings.live.publish_states', 'timings.udp_link.kick_display'):
            p = mock.patch(target)
            setattr(self, target.rsplit('.', 1)[1], p.start())
            self.addCleanup(p.stop)
        arena._set_last_result(None)
        self.addCleanup(arena._set_last_result, None)
        self.c = arena.create_competition('Saturday open')

    def add(self, *names):
        return [arena.add_entry(self.c, n) for n in names]

    def names(self):
        return [e['name'] for e in arena.snapshot(self.c)['entries']]

    def signal(self, mark, at, seq=None):
        """A gate signal whose beam was cut at `at`, arriving 5 ms later."""
        self._seq = getattr(self, '_seq', 0) + 1
        reading, created = ingest_event(event(mark, boot=1 if mark == 'START' else 2,
                                              seq=seq or self._seq, age_ms=5), at + 5, '10.0.0.1')
        self.assertTrue(created)
        return reading

    def session(self):
        return arena.snapshot(self.c)['session']

    def open_session(self, entry):
        arena.select(self.c, entry)
        return arena.set_capture(self.c, True)


class CompetitionTests(ArenaCase):
    def test_a_name_is_all_a_competition_needs(self):
        c = Competition.objects.get(pk=self.c.pk)
        self.assertEqual((c.name, c.fence_count), ('Saturday open', 12))
        self.assertEqual(str(c)[:13], 'Saturday open')

    def test_name_and_fence_count_are_validated(self):
        for bad in (None, '', '   ', 5, 'x' * 101):
            with self.assertRaises(arena.ArenaError):
                arena.create_competition(bad)
        for bad in (0, 25, -1, '12', 12.0, None, True):
            with self.assertRaises(arena.ArenaError):
                arena.create_competition('ok', bad)
        self.assertEqual(arena.create_competition('  Two   words ', 24).name, 'Two words')
        self.assertEqual(arena.create_competition('one fence', 1).fence_count, 1)

    def test_update(self):
        arena.update_competition(self.c, name='Sunday', fence_count=18)
        self.c.refresh_from_db()
        self.assertEqual((self.c.name, self.c.fence_count), ('Sunday', 18))
        arena.update_competition(self.c, fence_count=6)
        self.assertEqual((self.c.name, self.c.fence_count), ('Sunday', 6))
        with self.assertRaises(arena.ArenaError):
            arena.update_competition(self.c, name='')

    def test_list_newest_first_with_counts(self):
        a, b = self.add('Ala', 'Bea')
        self.open_session(a)
        arena.save_run(arena.open_run())
        newer = arena.create_competition('Later one')
        rows = arena.list_competitions()
        self.assertEqual([r['name'] for r in rows], ['Later one', 'Saturday open'])
        self.assertEqual(rows[1], {'id': str(self.c.pk), 'name': 'Saturday open', 'fence_count': 12,
                                   'entries': 2, 'runs': 1})
        self.assertEqual((rows[0]['entries'], rows[0]['runs'], rows[0]['id']), (0, 0, str(newer.pk)))


class ListTests(ArenaCase):
    def test_add_appends_and_the_first_one_is_selected(self):
        self.add('Ala', 'Bea', 'Cela')
        snap = arena.snapshot(self.c)
        self.assertEqual(self.names(), ['Ala', 'Bea', 'Cela'])
        self.assertEqual(snap['selected_entry'], snap['entries'][0]['id'])
        self.assertEqual([e['runs'] for e in snap['entries']], [[], [], []])

    def test_same_kid_in_two_competitions_is_one_participant_two_kids_one_name_are_two(self):
        (ala,) = self.add('Ala')
        other = arena.create_competition('Other')
        again = arena.add_entry(other, 'ala')                      # same kid, other competition
        self.assertEqual(again.participant_id, ala.participant_id)
        twin = arena.add_entry(self.c, 'Ala')                      # a second Ala here
        self.assertNotEqual(twin.participant_id, ala.participant_id)
        self.assertEqual(Participant.objects.count(), 2)

    def test_names_are_validated(self):
        for bad in (None, '', '  ', 7, 'x' * 101):
            with self.assertRaises(arena.ArenaError):
                arena.add_entry(self.c, bad)
        self.assertEqual(self.names(), [])

    def test_rename_does_not_rename_the_kid_in_other_competitions(self):
        (ala,) = self.add('Ala')
        other = arena.create_competition('Other')
        there = arena.add_entry(other, 'Ala')
        arena.rename_entry(ala, 'Alicja')
        self.assertEqual(self.names(), ['Alicja'])
        self.assertEqual(arena.snapshot(other)['entries'][0]['name'], 'Ala')
        arena.rename_entry(there, 'Ala K.')                        # now only entered there: renamed in place
        self.assertEqual(Participant.objects.count(), 2)
        self.assertEqual(arena.snapshot(other)['entries'][0]['name'], 'Ala K.')

    def horses(self, competition=None):
        return [e['horse'] for e in arena.snapshot(competition or self.c)['entries']]

    def test_horse_name_is_optional_and_tidied(self):
        arena.add_entry(self.c, 'Ala')
        arena.add_entry(self.c, 'Bea', None)
        arena.add_entry(self.c, 'Cela', '   ')
        arena.add_entry(self.c, 'Dora', '  Storm   Chaser ')
        arena.add_entry(self.c, 'Ewa', 'x' * 100)
        self.assertEqual(self.horses(), ['', '', '', 'Storm Chaser', 'x' * 100])

    def test_a_bad_horse_name_adds_nobody(self):
        for bad in (7, ['Stormy'], {'name': 'Stormy'}, True, 'x' * 101):
            with self.assertRaises(arena.ArenaError):
                arena.add_entry(self.c, 'Ala', bad)
        self.assertEqual(self.names(), [])

    def test_set_horse_names_renames_and_removes(self):
        ala, bea = self.add('Ala', 'Bea')
        arena.set_horse(ala, ' Stormy ')
        self.assertEqual(self.horses(), ['Stormy', ''])
        arena.set_horse(ala, 'Thunder')
        arena.set_horse(bea, 'Stormy')                             # two kids may ride a Stormy each
        self.assertEqual(self.horses(), ['Thunder', 'Stormy'])
        arena.set_horse(ala, '')
        arena.set_horse(bea, None)
        self.assertEqual(self.horses(), ['', ''])
        for bad in (7, 'x' * 101):
            with self.assertRaises(arena.ArenaError):
                arena.set_horse(ala, bad)
        self.assertEqual(self.horses(), ['', ''])

    def test_the_horse_belongs_to_the_entry_not_to_the_kid(self):
        ala = arena.add_entry(self.c, 'Ala', 'Stormy')
        other = arena.create_competition('Other')
        there = arena.add_entry(other, 'Ala', 'Thunder')            # same kid, another horse there
        self.assertEqual(there.participant_id, ala.participant_id)
        self.assertEqual((self.horses(), self.horses(other)), (['Stormy'], ['Thunder']))
        arena.rename_entry(ala, 'Alicja')                          # renaming the kid keeps the horse
        arena.set_horse(there, '')
        self.assertEqual((self.names(), self.horses(), self.horses(other)), (['Alicja'], ['Stormy'], ['']))

    def test_the_horse_survives_reorder_and_a_run(self):
        ala = arena.add_entry(self.c, 'Ala', 'Stormy')
        bea = arena.add_entry(self.c, 'Bea')
        arena.reorder(self.c, [str(bea.pk), str(ala.pk)])
        arena.select(self.c, ala)
        arena.set_capture(self.c, True)
        arena.save_run(arena.open_run())
        self.assertEqual(self.horses(), ['', 'Stormy'])

    def test_reorder(self):
        a, b, c = self.add('Ala', 'Bea', 'Cela')
        arena.reorder(self.c, [str(c.pk), str(a.pk), str(b.pk)])
        self.assertEqual(self.names(), ['Cela', 'Ala', 'Bea'])
        arena.reorder(self.c, [c.pk, b.pk, a.pk])                  # UUID objects work too
        self.assertEqual(self.names(), ['Cela', 'Bea', 'Ala'])

    def test_reorder_must_name_everyone_exactly_once(self):
        a, b, c = self.add('Ala', 'Bea', 'Cela')
        stranger = arena.add_entry(arena.create_competition('Other'), 'Zed')
        for bad in ([a.pk, b.pk], [a.pk, b.pk, c.pk, c.pk], [a.pk, b.pk, b.pk], [a.pk, b.pk, stranger.pk],
                    None, 'abc', [], [a.pk, b.pk, c.pk, stranger.pk]):
            with self.assertRaises(arena.ArenaError):
                arena.reorder(self.c, bad)
        self.assertEqual(self.names(), ['Ala', 'Bea', 'Cela'])

    def test_remove_closes_the_gap_and_selects_who_moved_up(self):
        a, b, c = self.add('Ala', 'Bea', 'Cela')
        arena.select(self.c, b)
        arena.remove_entry(b)
        self.assertEqual(self.names(), ['Ala', 'Cela'])
        self.assertEqual(self.c.selected_start_id, c.pk)
        self.assertEqual(list(CompetitionStart.objects.filter(competition=self.c)
                              .values_list('starting_order', flat=True)), [0, 1])
        arena.remove_entry(c)                                      # the last one: nobody moves up
        self.assertIsNone(self.c.selected_start_id)
        arena.remove_entry(a)
        self.assertEqual(self.names(), [])

    def test_remove_keeps_the_selection_when_somebody_else_leaves(self):
        a, b, c = self.add('Ala', 'Bea', 'Cela')
        arena.select(self.c, c)
        arena.remove_entry(a)
        self.assertEqual(self.c.selected_start_id, c.pk)

    def test_remove_takes_saved_runs_along_but_never_the_signals(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(a)
        self.signal('START', T0 + 1000)
        arena.save_run(run)
        arena.remove_entry(a)
        self.assertEqual(Run.objects.count(), 0)
        self.assertEqual(TimeReading.objects.count(), 1)           # raw measurements are never deleted
        self.assertIsNone(TimeReading.objects.get().run)

    def test_cannot_remove_the_competitor_whose_session_is_open(self):
        a, b = self.add('Ala', 'Bea')
        self.open_session(a)
        with self.assertRaises(arena.Conflict):
            arena.remove_entry(a)
        arena.remove_entry(b)                                      # others can go
        self.assertEqual(self.names(), ['Ala'])


class SelectionTests(ArenaCase):
    def test_select_and_unselect(self):
        a, b = self.add('Ala', 'Bea')
        arena.select(self.c, b)
        self.assertEqual(arena.snapshot(self.c)['selected_entry'], str(b.pk))
        arena.select(self.c, None)
        self.assertIsNone(arena.snapshot(self.c)['selected_entry'])

    def test_selecting_somebody_else_during_a_session_is_refused(self):
        a, b = self.add('Ala', 'Bea')
        self.open_session(a)
        with self.assertRaises(arena.Conflict) as ctx:
            arena.select(self.c, b)
        self.assertIn('Ala', str(ctx.exception))
        with self.assertRaises(arena.Conflict):
            arena.select(self.c, None)
        arena.select(self.c, a)                                    # re-selecting the same one is fine
        self.assertEqual(self.session()['entry_id'], str(a.pk))

    def test_move_hands_the_open_session_to_the_right_kid(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(b)
        arena.save_run(run)                                        # Bea already has one try
        run = self.open_session(a)                                 # started for Ala by mistake
        start = self.signal('START', T0 + 1000)
        arena.toggle_penalty(run, '3')
        arena.select(self.c, b, move=True)
        s = self.session()
        self.assertEqual((s['entry_id'], s['number'], s['penalties']), (str(b.pk), 2, ['3']))
        self.assertEqual(s['start_id'], str(start.pk))
        snap = arena.snapshot(self.c)
        self.assertEqual(snap['selected_entry'], str(b.pk))
        self.assertEqual([len(e['runs']) for e in snap['entries']], [0, 2])

    def test_a_session_elsewhere_does_not_block_selecting_here(self):
        (a,) = self.add('Ala')
        other = arena.create_competition('Other')
        z = arena.add_entry(other, 'Zed')
        self.open_session(a)
        arena.select(other, z)
        arena.select(other, None)
        self.assertEqual(arena.snapshot(other)['busy'],
                         {'competition_id': str(self.c.pk), 'competition_name': 'Saturday open'})
        self.assertIsNone(arena.snapshot(other)['session'])
        self.assertIsNone(arena.snapshot(self.c)['busy'])


class CaptureTests(ArenaCase):
    def test_start_registering_opens_a_session_for_the_selected_competitor(self):
        a, b = self.add('Ala', 'Bea')
        arena.select(self.c, b)
        self.assertIsNone(self.session())
        arena.set_capture(self.c, True)
        s = self.session()
        self.assertEqual((s['entry_id'], s['number'], s['capturing']), (str(b.pk), 1, True))
        self.assertEqual((s['starts'], s['finishes'], s['penalties'], s['time_ms']), ([], [], [], None))
        self.assertEqual((s['start_id'], s['finish_id'], s['start_auto'], s['finish_auto']), (None, None, True, True))
        runs = arena.snapshot(self.c)['entries'][1]['runs']
        self.assertEqual(runs, [{'id': s['run_id'], 'number': 1, 'status': 'OPEN', 'time_ms': None, 'penalties': []}])

    def test_needs_a_selected_competitor(self):
        with self.assertRaises(arena.Conflict):
            arena.set_capture(self.c, True)
        self.assertEqual(Run.objects.count(), 0)

    def test_stop_and_resume_keep_the_same_session(self):
        (a,) = self.add('Ala')
        run = self.open_session(a)
        self.clock += 5000
        arena.set_capture(self.c, False)
        self.assertFalse(self.session()['capturing'])
        self.clock += 5000
        arena.set_capture(self.c, True)
        self.assertEqual(self.session()['run_id'], str(run.pk))
        self.assertTrue(self.session()['capturing'])
        self.assertEqual(Run.objects.count(), 1)

    def test_stopping_with_no_session_is_a_no_op(self):
        self.add('Ala')
        self.assertIsNone(arena.set_capture(self.c, False))
        self.assertEqual(Run.objects.count(), 0)

    def test_only_one_session_in_the_whole_system(self):
        (a,) = self.add('Ala')
        other = arena.create_competition('Other')
        arena.add_entry(other, 'Zed')
        self.open_session(a)
        for on in (True, False):
            with self.assertRaises(arena.Conflict) as ctx:
                arena.set_capture(other, on)
            self.assertIn('Saturday open', str(ctx.exception))
        self.assertEqual(Run.objects.filter(status='OPEN').count(), 1)

    def test_signals_are_attached_only_while_registering(self):
        (a,) = self.add('Ala')
        before = self.signal('START', T0 - 10_000)                 # a kid ran through before the session
        self.clock = T0
        run = self.open_session(a)
        inside = self.signal('START', T0 + 1000)
        self.clock = T0 + 5000
        arena.set_capture(self.c, False)
        paused = self.signal('FINISH', T0 + 6000)                  # crossed while registering was off
        self.clock = T0 + 8000
        arena.set_capture(self.c, True)
        again = self.signal('FINISH', T0 + 9000)
        attached = set(TimeReading.objects.filter(run=run).values_list('pk', flat=True))
        self.assertEqual(attached, {inside.pk, again.pk})
        s = self.session()
        self.assertEqual([(r['id'], r['in_session']) for r in s['starts']],
                         [(str(before.pk), False), (str(inside.pk), True)])
        self.assertEqual([(r['id'], r['in_session']) for r in s['finishes']],
                         [(str(paused.pk), False), (str(again.pk), True)])

    def test_capture_window_boundaries_are_exact(self):
        (a,) = self.add('Ala')
        run = self.open_session(a)                                 # registering from T0
        just_before = self.signal('START', T0 - 1)
        at_start = self.signal('START', T0)
        self.clock = T0 + 5000
        arena.set_capture(self.c, False)                           # ...to T0 + 5000
        at_stop = self.signal('FINISH', T0 + 5000)                 # cut in the last instant, delivered late
        just_after = self.signal('FINISH', T0 + 5001)
        got = {r.pk: r.run_id for r in TimeReading.objects.all()}
        self.assertEqual(got, {just_before.pk: None, at_start.pk: run.pk, at_stop.pk: run.pk, just_after.pk: None})

    def test_a_signal_delivered_late_still_lands_in_the_session_it_was_cut_in(self):
        (a,) = self.add('Ala')
        run = self.open_session(a)
        self.clock = T0 + 60_000
        late, _ = ingest_event(event('START', boot=1, seq=9, age_ms=58_000), T0 + 60_000, '10.0.0.1')
        late.refresh_from_db()
        self.assertEqual((late.run_id, late.sensor_time), (run.pk, T0 + 2000))

    def test_nearby_signals_outside_the_session_are_offered_but_limited(self):
        (a,) = self.add('Ala')
        too_old = self.signal('START', T0 - 120_001)
        oldest_offered = self.signal('START', T0 - 120_000)
        self.open_session(a)
        ids = [r['id'] for r in self.session()['starts']]
        self.assertEqual(ids, [str(oldest_offered.pk)])
        self.assertNotIn(str(too_old.pk), ids)
        for i in range(20):                                        # a crowd before the session: only the latest 12
            self.signal('FINISH', T0 - 60_000 + i * 1000)
        finishes = self.session()['finishes']
        self.assertEqual(len(finishes), 12)
        self.assertEqual(finishes[0]['sensor_time'], T0 - 60_000 + 8 * 1000)
        self.assertTrue(all(not f['in_session'] for f in finishes))

    def test_after_registering_stops_new_strays_are_offered_for_two_minutes_only(self):
        (a,) = self.add('Ala')
        self.open_session(a)
        self.clock = T0 + 10_000
        arena.set_capture(self.c, False)
        soon = self.signal('FINISH', T0 + 10_000 + 120_000)
        late = self.signal('FINISH', T0 + 10_000 + 120_001)
        self.clock = T0 + 500_000
        ids = [f['id'] for f in self.session()['finishes']]
        self.assertEqual(ids, [str(soon.pk)])
        self.assertNotIn(str(late.pk), ids)


class ChoiceTests(ArenaCase):
    def setUp(self):
        super().setUp()
        (self.a,) = self.add('Ala')
        self.run = self.open_session(self.a)

    def test_first_start_and_last_finish_count_by_default(self):
        s1 = self.signal('START', T0 + 1000)
        s2 = self.signal('START', T0 + 3000)                       # another kid through the start gate
        f1 = self.signal('FINISH', T0 + 20_000)
        f2 = self.signal('FINISH', T0 + 24_450)
        s = self.session()
        self.assertEqual((s['start_id'], s['finish_id']), (str(s1.pk), str(f2.pk)))
        self.assertEqual((s['start_auto'], s['finish_auto'], s['time_ms']), (True, True, 23_450))
        self.assertEqual([f['time_ms'] for f in s['finishes']], [19_000, 23_450])
        self.assertEqual([r['id'] for r in s['starts']], [str(s1.pk), str(s2.pk)])
        self.assertEqual(arena.snapshot(self.c)['entries'][0]['runs'][0]['time_ms'], 23_450)

    def test_arrival_order_does_not_matter(self):
        f = self.signal('FINISH', T0 + 20_000)
        late_start = self.signal('START', T0 + 2000)
        first_start = self.signal('START', T0 + 1000)
        s = self.session()
        self.assertEqual((s['start_id'], s['finish_id'], s['time_ms']), (str(first_start.pk), str(f.pk), 19_000))
        self.assertEqual([r['id'] for r in s['starts']], [str(first_start.pk), str(late_start.pk)])

    def test_a_finish_only_counts_after_the_start(self):
        self.signal('FINISH', T0 + 500)                            # before anybody started
        s = self.session()
        self.assertEqual((s['finish_id'], s['time_ms']), (None, None))
        start = self.signal('START', T0 + 1000)
        same_ms = self.signal('FINISH', T0 + 1000)
        s = self.session()
        self.assertEqual((s['start_id'], s['finish_id'], s['time_ms']), (str(start.pk), None, None))
        self.assertEqual([f['time_ms'] for f in s['finishes']], [None, None])
        after = self.signal('FINISH', T0 + 1001)
        s = self.session()
        self.assertEqual((s['finish_id'], s['time_ms']), (str(after.pk), 1))
        self.assertIsNotNone(same_ms)

    def test_picking_another_start_and_finish(self):
        s1 = self.signal('START', T0 + 1000)
        s2 = self.signal('START', T0 + 3000)
        f1 = self.signal('FINISH', T0 + 20_000)
        f2 = self.signal('FINISH', T0 + 24_450)
        arena.pick(self.run, start=str(s2.pk))
        s = self.session()
        self.assertEqual((s['start_id'], s['start_auto'], s['finish_id'], s['finish_auto'], s['time_ms']),
                         (str(s2.pk), False, str(f2.pk), True, 21_450))
        arena.pick(self.run, finish=f1.pk)
        s = self.session()
        self.assertEqual((s['start_id'], s['finish_id'], s['finish_auto'], s['time_ms']),
                         (str(s2.pk), str(f1.pk), False, 17_000))
        arena.pick(self.run, start=None)                           # back to automatic; the finish pick stays
        s = self.session()
        self.assertEqual((s['start_id'], s['start_auto'], s['finish_id'], s['time_ms']),
                         (str(s1.pk), True, str(f1.pk), 19_000))
        arena.pick(self.run, finish=None)
        self.assertEqual((self.session()['finish_id'], self.session()['finish_auto']), (str(f2.pk), True))

    def test_a_picked_finish_stays_when_later_ones_arrive(self):
        self.signal('START', T0 + 1000)
        f1 = self.signal('FINISH', T0 + 20_000)
        arena.pick(self.run, finish=str(f1.pk))
        self.signal('FINISH', T0 + 30_000)
        self.assertEqual((self.session()['finish_id'], self.session()['time_ms']), (str(f1.pk), 19_000))

    def test_a_picked_finish_before_the_picked_start_does_not_count(self):
        s1 = self.signal('START', T0 + 1000)
        s2 = self.signal('START', T0 + 25_000)
        f1 = self.signal('FINISH', T0 + 20_000)
        f2 = self.signal('FINISH', T0 + 40_000)
        arena.pick(self.run, finish=str(f1.pk))
        arena.pick(self.run, start=str(s2.pk))                     # now the picked finish lies before the start
        s = self.session()
        self.assertEqual((s['finish_id'], s['finish_auto'], s['time_ms']), (str(f2.pk), True, 15_000))
        arena.pick(self.run, start=str(s1.pk))                     # ...and it counts again once it is after it
        self.assertEqual((self.session()['finish_id'], self.session()['finish_auto']), (str(f1.pk), False))

    def test_picking_a_signal_from_outside_takes_it_into_the_session(self):
        arena.set_capture(self.c, False)
        stray = self.signal('START', T0 + 1000)                    # crossed while registering was off
        self.assertFalse(self.session()['starts'][0]['in_session'])
        arena.pick(self.run, start=str(stray.pk))
        s = self.session()
        self.assertEqual((s['start_id'], s['start_auto'], s['starts'][0]['in_session']), (str(stray.pk), False, True))
        stray.refresh_from_db()
        self.assertEqual(stray.run_id, self.run.pk)

    def test_pick_refuses_what_is_not_pickable(self):
        s1 = self.signal('START', T0 + 1000)
        f1 = self.signal('FINISH', T0 + 20_000)
        for kwargs in ({'start': str(f1.pk)}, {'finish': str(s1.pk)}, {'start': 'not-an-id'}, {'finish': 12345},
                       {'start': '00000000-0000-0000-0000-000000000000'}):
            with self.assertRaises(arena.ArenaError):
                arena.pick(self.run, **kwargs)
        self.assertTrue(self.session()['start_auto'] and self.session()['finish_auto'])

    def test_a_signal_of_another_run_cannot_be_picked(self):
        mine = self.signal('START', T0 + 1000)
        arena.save_run(self.run)
        b = arena.add_entry(self.c, 'Bea')
        other = self.open_session(b)
        with self.assertRaises(arena.Conflict):
            arena.pick(other, start=str(mine.pk))

    def test_better_estimate_of_a_signal_updates_the_time(self):
        self.signal('START', T0 + 1000, seq=1)
        first, _ = ingest_event(event('FINISH', boot=2, seq=1, age_ms=0), T0 + 20_030, '')    # 30 ms late copy
        self.assertEqual(self.session()['time_ms'], 19_030)
        self.publish_states.reset_mock()
        with self.captureOnCommitCallbacks(execute=True):
            ingest_event(event('FINISH', boot=2, seq=1, age_ms=50, n=1), T0 + 20_051, '')     # better copy
        self.assertEqual(self.session()['time_ms'], 19_001)
        self.assertEqual(self.publish_states.call_count, 1)        # the consoles are told
        self.publish_states.reset_mock()
        with self.captureOnCommitCallbacks(execute=True):
            ingest_event(event('FINISH', boot=2, seq=1, age_ms=60, n=2), T0 + 20_090, '')     # worse copy: nothing new
        self.assertEqual(self.publish_states.call_count, 0)


class PenaltyTests(ArenaCase):
    def setUp(self):
        super().setUp()
        (self.a,) = self.add('Ala')
        self.run = self.open_session(self.a)

    def test_toggle_and_fence_order(self):
        for fence in ('9B', '7', '10', '2A', '9'):
            arena.toggle_penalty(self.run, fence)
        self.assertEqual(self.session()['penalties'], ['2A', '7', '9', '9B', '10'])
        arena.toggle_penalty(self.run, '9')                        # again: off
        arena.toggle_penalty(self.run, '2A')
        self.assertEqual(self.session()['penalties'], ['7', '9B', '10'])

    def test_only_fences_of_this_competition(self):
        for bad in ('0', '13', '7D', 'B', '', '7b', '07', ' 7', '7 ', 7, None, '1AB', '100'):
            with self.assertRaises(arena.ArenaError):
                arena.toggle_penalty(self.run, bad)
        self.assertEqual(self.session()['penalties'], [])
        arena.toggle_penalty(self.run, '12C')                      # the last fence exists
        arena.update_competition(self.c, fence_count=24)
        self.run = Run.objects.select_related('entry__competition').get(pk=self.run.pk)
        arena.toggle_penalty(self.run, '24')
        with self.assertRaises(arena.ArenaError):
            arena.toggle_penalty(self.run, '25')
        self.assertEqual(self.session()['penalties'], ['12C', '24'])


class SaveTests(ArenaCase):
    def test_save_freezes_the_run_and_moves_to_the_next_competitor(self):
        a, b, c = self.add('Ala', 'Bea', 'Cela')
        run = self.open_session(a)
        s1 = self.signal('START', T0 + 1000)
        self.signal('START', T0 + 2000)
        f1 = self.signal('FINISH', T0 + 24_450)
        arena.toggle_penalty(run, '7')
        self.clock = T0 + 30_000
        arena.save_run(run)
        snap = arena.snapshot(self.c)
        self.assertIsNone(snap['session'])
        self.assertEqual(snap['selected_entry'], str(b.pk))
        self.assertEqual(snap['entries'][0]['runs'],
                         [{'id': str(run.pk), 'number': 1, 'status': 'SAVED', 'time_ms': 23_450, 'penalties': ['7']}])
        run.refresh_from_db()
        self.assertEqual((run.start_pick_id, run.finish_pick_id, run.capturing, run.capture_to),
                         (s1.pk, f1.pk, False, T0 + 30_000))
        self.assertIsNotNone(run.saved_at)
        # later strays change nothing about a saved run
        stray = self.signal('FINISH', T0 + 40_000)
        self.assertIsNone(TimeReading.objects.get(pk=stray.pk).run_id)
        self.assertEqual(arena.snapshot(self.c)['entries'][0]['runs'][0]['time_ms'], 23_450)

    def test_saving_without_a_time_is_possible(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(a)
        arena.toggle_penalty(run, '3')
        arena.save_run(run)
        self.assertEqual(arena.snapshot(self.c)['entries'][0]['runs'][0],
                         {'id': str(run.pk), 'number': 1, 'status': 'SAVED', 'time_ms': None, 'penalties': ['3']})

    def test_after_the_last_competitor_nobody_is_selected(self):
        a, b = self.add('Ala', 'Bea')
        arena.save_run(self.open_session(b))
        self.assertIsNone(arena.snapshot(self.c)['selected_entry'])

    def test_several_tries_for_the_same_competitor(self):
        a, b = self.add('Ala', 'Bea')
        for i in range(3):
            self.clock = T0 + i * 100_000
            run = self.open_session(a)                             # select Ala again and again
            self.signal('START', self.clock + 1000)
            self.signal('FINISH', self.clock + 1000 + 20_000 + i)
            arena.save_run(run)
        runs = arena.snapshot(self.c)['entries'][0]['runs']
        self.assertEqual([(r['number'], r['time_ms']) for r in runs], [(1, 20_000), (2, 20_001), (3, 20_002)])
        self.assertEqual(arena.snapshot(self.c)['selected_entry'], str(b.pk))

    def test_saved_runs_cannot_be_changed_without_reopening(self):
        (a,) = self.add('Ala')
        run = self.open_session(a)
        arena.save_run(run)
        for change in (lambda: arena.toggle_penalty(run, '1'), lambda: arena.pick(run, start=None),
                       lambda: arena.save_run(run), lambda: arena.discard_run(run)):
            with self.assertRaises(arena.Conflict):
                change()

    def test_discard_throws_the_session_away_but_keeps_the_signals(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(a)
        self.signal('START', T0 + 1000)
        arena.discard_run(run)
        snap = arena.snapshot(self.c)
        self.assertIsNone(snap['session'])
        self.assertEqual(snap['entries'][0]['runs'], [])
        self.assertEqual(snap['selected_entry'], str(a.pk))        # still on the same kid
        self.assertEqual(TimeReading.objects.filter(run__isnull=True).count(), 1)

    def test_reopen_to_correct_and_save_again(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(a)
        s1 = self.signal('START', T0 + 1000)
        f1 = self.signal('FINISH', T0 + 20_000)
        f2 = self.signal('FINISH', T0 + 24_000)
        arena.save_run(run)                                        # saved with the last finish: 23.000
        arena.reopen_run(run)
        s = self.session()
        self.assertEqual((s['run_id'], s['capturing'], s['time_ms']), (str(run.pk), False, 23_000))
        self.assertEqual((s['start_auto'], s['finish_auto']), (False, False))   # what was saved is kept as picks
        self.assertEqual(arena.snapshot(self.c)['selected_entry'], str(a.pk))
        arena.pick(run, finish=str(f1.pk))
        arena.toggle_penalty(run, '5')
        arena.save_run(run)
        self.assertEqual(arena.snapshot(self.c)['entries'][0]['runs'][0],
                         {'id': str(run.pk), 'number': 1, 'status': 'SAVED', 'time_ms': 19_000, 'penalties': ['5']})
        self.assertIsNotNone(s1)
        self.assertIsNotNone(f2)

    def test_reopen_needs_no_other_session(self):
        a, b = self.add('Ala', 'Bea')
        first = self.open_session(a)
        arena.save_run(first)
        second = self.open_session(b)
        with self.assertRaises(arena.Conflict):
            arena.reopen_run(first)
        with self.assertRaises(arena.Conflict):
            arena.reopen_run(second)                               # it is open already
        self.assertEqual(Run.objects.filter(status='OPEN').count(), 1)

    def test_delete_a_saved_run(self):
        (a,) = self.add('Ala')
        run = self.open_session(a)
        self.signal('START', T0 + 1000)
        with self.assertRaises(arena.Conflict):
            arena.delete_run(run)                                  # open: discard instead
        arena.save_run(run)
        arena.delete_run(run)
        self.assertEqual(arena.snapshot(self.c)['entries'][0]['runs'], [])
        self.assertEqual(TimeReading.objects.count(), 1)

    def test_try_numbers_stay_1_2_3_after_a_delete(self):
        (a,) = self.add('Ala')
        runs = []
        for _ in range(3):
            run = self.open_session(a)
            arena.save_run(run)
            runs.append(run)
        arena.delete_run(runs[1])
        self.assertEqual([r['number'] for r in arena.snapshot(self.c)['entries'][0]['runs']], [1, 2])
        self.open_session(a)
        self.assertEqual(self.session()['number'], 3)


class DisplayTests(ArenaCase):
    """What the external display is told to show (PROTOCOL.md, SHOW)."""

    def test_idle_without_anything(self):
        self.assertEqual(arena.display(T0), link.Show('IDLE'))

    def test_follows_the_session(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(a)
        self.assertEqual(arena.display(T0 + 500), link.Show('READY'))          # 0.00: about to start
        s1 = self.signal('START', T0 + 1000)
        running = arena.display(T0 + 5000)
        self.assertEqual((running.state, running.age_ms, running.ms), ('RUN', 4000, 0))
        self.assertEqual(arena.display(T0 + 9000).rev, running.rev)             # same run, same start: same rev
        f1 = self.signal('FINISH', T0 + 24_450)
        result = arena.display(T0 + 25_000)
        self.assertEqual((result.state, result.ms, result.age_ms), ('RESULT', 23_450, 0))
        self.assertNotEqual(result.rev, running.rev)
        # the operator stops registering, marks a penalty: the result stays up
        arena.set_capture(self.c, False)
        arena.toggle_penalty(run, '4')
        self.assertEqual(arena.display(T0 + 26_000), result)
        # saved: the same result stays (same rev: the display does not blink again) until the next session
        arena.save_run(run)
        self.assertEqual(arena.display(T0 + 30_000), result)
        self.open_session(b)
        self.assertEqual(arena.display(T0 + 31_000), link.Show('READY'))
        self.assertIsNotNone(s1)
        self.assertIsNotNone(f1)

    def test_picking_other_signals_changes_what_is_shown(self):
        (a,) = self.add('Ala')
        run = self.open_session(a)
        self.signal('START', T0 + 1000)
        s2 = self.signal('START', T0 + 3000)
        first = arena.display(T0 + 10_000)
        arena.pick(run, start=str(s2.pk))
        second = arena.display(T0 + 10_000)
        self.assertEqual((first.age_ms, second.age_ms), (9000, 7000))
        self.assertNotEqual(first.rev, second.rev)                 # the timer must not mix the two up
        f1 = self.signal('FINISH', T0 + 20_000)
        self.signal('FINISH', T0 + 24_000)
        last = arena.display(T0 + 30_000)
        arena.pick(run, finish=str(f1.pk))
        picked = arena.display(T0 + 30_000)
        self.assertEqual((last.ms, picked.ms), (21_000, 17_000))
        self.assertNotEqual(last.rev, picked.rev)

    def test_clock_never_runs_backwards_or_past_the_field(self):
        (a,) = self.add('Ala')
        self.open_session(a)
        self.signal('START', T0 + 1000)
        self.assertEqual(arena.display(T0 + 900).age_ms, 0)        # asked just before the cut: not negative
        self.assertEqual(arena.display(T0 + 1000 + 2 ** 33).age_ms, 2 ** 32 - 1)

    def test_a_run_saved_without_time_leaves_the_display_idle(self):
        (a,) = self.add('Ala')
        arena.save_run(self.open_session(a))
        self.assertEqual(arena.display(T0), link.Show('IDLE'))

    def test_deleted_or_reopened_last_run_is_not_shown_any_more(self):
        a, b = self.add('Ala', 'Bea')
        run = self.open_session(a)
        self.signal('START', T0 + 1000)
        self.signal('FINISH', T0 + 20_000)
        arena.save_run(run)
        self.assertEqual(arena.display(T0 + 30_000).state, 'RESULT')
        arena.reopen_run(run)
        self.assertEqual(arena.display(T0 + 30_000).state, 'RESULT')            # as the open session again
        arena.discard_run(run)
        self.assertEqual(arena.display(T0 + 30_000), link.Show('IDLE'))

    def test_show_is_a_valid_protocol_message_in_every_state(self):
        (a,) = self.add('Ala')
        self.open_session(a)
        states = [arena.display(T0)]
        self.signal('START', T0 + 1000)
        states.append(arena.display(T0 + 2000))
        self.signal('FINISH', T0 + 9000)
        states.append(arena.display(T0 + 9500))
        self.assertEqual([s.state for s in states], ['READY', 'RUN', 'RESULT'])
        for s in states:
            self.assertEqual(link.decode(link.encode(s)), s)


class NotifyTests(ArenaCase):
    def test_devices_always_lists_the_three_roles(self):
        self.assertEqual(arena.snapshot(self.c)['devices'],
                         [{'role': 'START', 'online': False, 'ok': None},
                          {'role': 'FINISH', 'online': False, 'ok': None},
                          {'role': 'TIMER', 'online': False, 'ok': None}])

    def test_devices_reflect_the_registry(self):
        fake = [
            {'role': 'START', 'online': True, 'ok': True, 'age_s': 1.0},
            {'role': 'START', 'online': True, 'ok': False, 'age_s': 0.2},     # the fresher one wins
            {'role': 'FINISH', 'online': False, 'ok': True, 'age_s': 30.0},
            {'role': 'TIMER', 'online': True, 'age_s': 0.5},
        ]
        with mock.patch.object(arena.registry, 'snapshot', return_value=fake):
            self.assertEqual(arena.devices(), [{'role': 'START', 'online': True, 'ok': False},
                                               {'role': 'FINISH', 'online': False, 'ok': None},
                                               {'role': 'TIMER', 'online': True, 'ok': None}])

    def test_a_new_signal_tells_consoles_and_display_only_during_a_session(self):
        (a,) = self.add('Ala')
        with self.captureOnCommitCallbacks(execute=True):
            self.signal('START', T0 + 1000)                        # nobody is being timed
        self.assertEqual(self.publish_states.call_count, 0)
        self.open_session(a)
        self.publish_states.reset_mock()
        self.kick_display.reset_mock()
        with self.captureOnCommitCallbacks(execute=True):
            self.signal('START', T0 + 2000)
        self.publish_states.assert_called_once_with(self.c.pk)
        self.kick_display.assert_called_once_with()

    def test_snapshot_has_server_time_and_competition(self):
        self.clock = T0 + 123
        snap = arena.snapshot(self.c)
        self.assertEqual(snap['server_time'], T0 + 123)
        self.assertEqual(snap['competition'], {'id': str(self.c.pk), 'name': 'Saturday open', 'fence_count': 12})
        self.assertEqual(set(snap), {'server_time', 'competition', 'entries', 'selected_entry', 'session',
                                     'busy', 'devices'})
