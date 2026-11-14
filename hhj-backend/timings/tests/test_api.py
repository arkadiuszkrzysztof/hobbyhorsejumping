"""The operator console's HTTP contract (API.md)."""
import uuid
from unittest import mock

from django.contrib.auth import get_user_model
from django.test import TestCase
from rest_framework.test import APIClient

from timings import arena
from timings.ingest import ingest_event
from timings.models import Run
from timings.tests.helpers import USUAL_RULES, event, run_row

T0 = 1_794_000_000_000
STATE_KEYS = {'server_time', 'competition', 'entries', 'selected_entry', 'session', 'busy', 'devices'}


class ApiCase(TestCase):
    def setUp(self):
        self.api = APIClient()
        for target in ('timings.live.publish_states', 'timings.udp_link.kick_display'):
            p = mock.patch(target)
            setattr(self, target.rsplit('.', 1)[1], p.start())
            self.addCleanup(p.stop)
        arena._set_last_result(None)

    def call(self, method, path, body=None, status=200):
        with self.captureOnCommitCallbacks(execute=True):
            r = getattr(self.api, method)(path, body, format='json')
        self.assertEqual(r.status_code, status, r.content)
        return r.json() if r.content else None

    def new_competition(self, name='Saturday open', **extra):
        return self.call('post', '/api/competitions/', {'name': name, **extra}, 201)

    def start_session(self, cid):
        return self.call('post', f'/api/competitions/{cid}/capture/', {'on': True})


class CompetitionApiTests(ApiCase):
    def test_create_list_state_patch(self):
        state = self.new_competition('Saturday open', fence_count=18)
        self.assertEqual(set(state), STATE_KEYS)
        cid = state['competition']['id']
        self.assertEqual(state['competition'], {'id': cid, 'name': 'Saturday open', 'fence_count': 18, **USUAL_RULES})
        self.assertEqual((state['entries'], state['selected_entry'], state['session'], state['busy']),
                         ([], None, None, None))
        self.assertEqual(self.call('get', '/api/competitions/'),
                         [{'id': cid, 'name': 'Saturday open', 'fence_count': 18, **USUAL_RULES, 'entries': 0, 'runs': 0}])
        self.assertEqual(self.call('get', f'/api/competitions/{cid}/state/')['competition']['name'], 'Saturday open')
        patched = self.call('patch', f'/api/competitions/{cid}/', {'name': 'Sunday', 'fence_count': 12})
        self.assertEqual(patched['competition'], {'id': cid, 'name': 'Sunday', 'fence_count': 12, **USUAL_RULES})
        only_name = self.call('patch', f'/api/competitions/{cid}/', {'name': 'Monday'})
        self.assertEqual(only_name['competition'], {'id': cid, 'name': 'Monday', 'fence_count': 12, **USUAL_RULES})

    def test_default_fence_count(self):
        self.assertEqual(self.new_competition()['competition']['fence_count'], 12)

    def test_bad_input_is_400_with_a_message(self):
        for body in ({}, {'name': ''}, {'name': 'x', 'fence_count': 0}, {'name': 'x', 'fence_count': '12'},
                     ['a list'], 'a string'):
            r = self.call('post', '/api/competitions/', body, 400)
            self.assertIsInstance(r['error'], str)
        self.assertEqual(self.call('get', '/api/competitions/'), [])

    def test_unknown_ids_are_404_with_a_message(self):
        ghost = uuid.uuid4()
        for method, path in (('get', f'/api/competitions/{ghost}/state/'), ('patch', f'/api/competitions/{ghost}/'),
                             ('post', f'/api/competitions/{ghost}/entries/'), ('delete', f'/api/entries/{ghost}/'),
                             ('post', f'/api/runs/{ghost}/save/'), ('delete', f'/api/runs/{ghost}/')):
            r = self.call(method, path, {'name': 'x'}, 404)
            self.assertIsInstance(r['error'], str)

    def test_a_get_does_not_notify_anybody(self):
        cid = self.new_competition()['competition']['id']
        self.publish_states.reset_mock()
        self.call('get', f'/api/competitions/{cid}/state/')
        self.call('get', '/api/competitions/')
        self.assertEqual(self.publish_states.call_count, 0)

    def test_every_change_notifies_consoles_and_display(self):
        cid = self.new_competition()['competition']['id']
        self.publish_states.reset_mock()
        self.kick_display.reset_mock()
        self.call('post', f'/api/competitions/{cid}/entries/', {'name': 'Ala'})
        self.assertEqual(self.publish_states.call_count, 1)
        self.assertEqual(str(self.publish_states.call_args[0][0]), cid)
        self.assertEqual(self.kick_display.call_count, 1)

    def test_a_refused_change_notifies_nobody_and_changes_nothing(self):
        cid = self.new_competition()['competition']['id']
        self.publish_states.reset_mock()
        self.call('post', f'/api/competitions/{cid}/capture/', {'on': True}, 409)      # nobody selected
        self.assertEqual(self.publish_states.call_count, 0)
        self.assertEqual(Run.objects.count(), 0)

    def test_being_logged_into_the_admin_does_not_break_the_console(self):
        # same browser, session cookie present: no CSRF token must be needed
        user = get_user_model().objects.create_superuser('admin', 'a@example.com', 'pw')
        strict = APIClient(enforce_csrf_checks=True)
        strict.force_login(user)
        r = strict.post('/api/competitions/', {'name': 'With cookie'}, format='json')
        self.assertEqual(r.status_code, 201, r.content)


class FlowApiTests(ApiCase):
    """The whole operator flow, endpoint by endpoint."""

    def setUp(self):
        super().setUp()
        self.cid = self.new_competition()['competition']['id']
        for name in ('Ala', 'Bea', 'Cela'):
            state = self.call('post', f'/api/competitions/{self.cid}/entries/', {'name': name})
        self.ids = [e['id'] for e in state['entries']]

    def signal(self, mark, at, seq):
        with self.captureOnCommitCallbacks(execute=True):
            reading, _ = ingest_event(event(mark, boot=1 if mark == 'START' else 2, seq=seq, age_ms=0), at, '')
        return str(reading.pk)

    def test_entries_add_rename_order_remove(self):
        base = f'/api/competitions/{self.cid}'
        self.call('post', f'{base}/entries/', {'name': ''}, 400)
        self.call('post', f'{base}/entries/', {}, 400)
        state = self.call('patch', f'/api/entries/{self.ids[1]}/', {'name': 'Beata'})
        self.assertEqual([e['name'] for e in state['entries']], ['Ala', 'Beata', 'Cela'])
        self.call('patch', f'/api/entries/{self.ids[1]}/', {'name': ' '}, 400)
        state = self.call('post', f'{base}/order/', {'entries': [self.ids[2], self.ids[0], self.ids[1]]})
        self.assertEqual([e['name'] for e in state['entries']], ['Cela', 'Ala', 'Beata'])
        self.call('post', f'{base}/order/', {'entries': self.ids[:2]}, 400)
        self.call('post', f'{base}/order/', {}, 400)
        state = self.call('delete', f'/api/entries/{self.ids[0]}/')
        self.assertEqual([e['name'] for e in state['entries']], ['Cela', 'Beata'])

    def test_horse_names(self):
        base = f'/api/competitions/{self.cid}'
        horses = lambda state: [e['horse'] for e in state['entries']]
        state = self.call('post', f'{base}/entries/', {'name': 'Dora', 'horse': ' Storm  Chaser '})
        self.assertEqual(horses(state), ['', '', '', 'Storm Chaser'])
        dora = state['entries'][3]['id']
        # a bad horse name adds nobody
        self.call('post', f'{base}/entries/', {'name': 'Ewa', 'horse': 7}, 400)
        self.call('post', f'{base}/entries/', {'name': 'Ewa', 'horse': 'x' * 101}, 400)
        self.assertEqual(len(self.call('get', f'{base}/state/')['entries']), 4)

        state = self.call('patch', f'/api/entries/{self.ids[0]}/', {'horse': 'Stormy'})        # horse only
        self.assertEqual([(e['name'], e['horse']) for e in state['entries']][0], ('Ala', 'Stormy'))
        state = self.call('patch', f'/api/entries/{self.ids[0]}/', {'name': 'Alicja'})         # name only
        self.assertEqual([(e['name'], e['horse']) for e in state['entries']][0], ('Alicja', 'Stormy'))
        state = self.call('patch', f'/api/entries/{dora}/', {'name': 'Dorota', 'horse': 'Thunder'})
        self.assertEqual([(e['name'], e['horse']) for e in state['entries']][3], ('Dorota', 'Thunder'))
        self.assertEqual(horses(self.call('patch', f'/api/entries/{dora}/', {'horse': ''})), ['Stormy', '', '', ''])
        self.assertEqual(horses(self.call('patch', f'/api/entries/{self.ids[0]}/', {'horse': None})), [''] * 4)

    def test_a_bad_entry_patch_changes_nothing(self):
        base = f'/api/competitions/{self.cid}'
        self.call('patch', f'/api/entries/{self.ids[0]}/', {'horse': 'Stormy'})
        self.assertIn('Nothing to change', self.call('patch', f'/api/entries/{self.ids[0]}/', {}, 400)['error'])
        self.call('patch', f'/api/entries/{self.ids[0]}/', {'horse': 7}, 400)
        # one good and one bad field: neither is applied
        self.call('patch', f'/api/entries/{self.ids[0]}/', {'name': 'Alicja', 'horse': 'x' * 101}, 400)
        self.call('patch', f'/api/entries/{self.ids[0]}/', {'name': '', 'horse': 'Thunder'}, 400)
        first = self.call('get', f'{base}/state/')['entries'][0]
        self.assertEqual((first['name'], first['horse']), ('Ala', 'Stormy'))

    def test_scoring_rules(self):
        made = self.call('post', '/api/competitions/', {'name': 'With rules', 'fence_count': 10, 'time_allowed': 45,
                                                        'knockdown_points': 3, 'refusal_points': 2.5,
                                                        'overtime_points': 0.25}, 201)['competition']
        self.assertEqual({key: made[key] for key in arena.RULES},
                         {'time_allowed': 45, 'knockdown_points': 3.0, 'refusal_points': 2.5, 'overtime_points': 0.25})
        changed = self.call('patch', f"/api/competitions/{made['id']}/", {'time_allowed': None, 'overtime_points': 1})
        self.assertEqual({key: changed['competition'][key] for key in arena.RULES},
                         {'time_allowed': None, 'knockdown_points': 3.0, 'refusal_points': 2.5, 'overtime_points': 1.0})
        for bad in ({'time_allowed': 0}, {'time_allowed': '45'}, {'knockdown_points': -1}, {'refusal_points': 'x'},
                    {'overtime_points': 0.123}, {'name': 'ok', 'time_allowed': 45.5}):
            self.call('patch', f"/api/competitions/{made['id']}/", bad, 400)
            self.call('post', '/api/competitions/', {'name': 'Bad', **bad}, 400)
        self.assertEqual(len(self.call('get', '/api/competitions/')), 2)    # no "Bad" one was created
        self.assertEqual(self.call('get', f"/api/competitions/{made['id']}/state/")['competition'], changed['competition'])

    def test_refusals_and_points(self):
        self.call('patch', f'/api/competitions/{self.cid}/', {'time_allowed': 20})
        with mock.patch('timings.arena.now_ms', return_value=T0):
            rid = self.start_session(self.cid)['session']['run_id']
        self.signal('START', T0 + 1000, 1)
        self.signal('FINISH', T0 + 25_500, 1)
        self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '3'})
        self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '7B'})
        s = self.call('post', f'/api/runs/{rid}/refusal/', {'fence': '5'})['session']
        self.assertEqual((s['refusals'], s['points'], s['fence_points'], s['time_points']), ({'5': 1}, 13.6, 12.0, 1.6))
        s = self.call('post', f'/api/runs/{rid}/refusal/', {'fence': '5'})['session']
        self.assertEqual((s['refusals'], s['points']), ({'5': 2}, 17.6))
        s = self.call('post', f'/api/runs/{rid}/refusal/', {'fence': '5'})['session']
        self.assertEqual((s['refusals'], s['points']), ({}, 9.6))
        for bad in ({'fence': '7B'}, {'fence': '13'}, {'fence': 5}, {}):
            self.call('post', f'/api/runs/{rid}/refusal/', bad, 400)
        saved = self.call('post', f'/api/runs/{rid}/save/')
        self.assertEqual(saved['entries'][0]['runs'], [run_row(9.6, 1.6, id=rid, number=1, status='SAVED',
                                                               time_ms=24_500, penalties=['3', '7B'])])
        self.call('post', f'/api/runs/{rid}/refusal/', {'fence': '5'}, 409)
        self.call('post', f'/api/runs/{uuid.uuid4()}/refusal/', {'fence': '5'}, 404)

    def test_retire_and_eliminate(self):
        rid = self.start_session(self.cid)['session']['run_id']
        self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '2'})
        for bad in ({'outcome': 'DNF'}, {'outcome': 'RETIRED', 'fence': '13'}, {'outcome': 'FINISHED', 'fence': '2'},
                    {'outcome': None}):
            self.call('post', f'/api/runs/{rid}/save/', bad, 400)
        retired = self.call('post', f'/api/runs/{rid}/save/', {'outcome': 'RETIRED', 'fence': '4'})
        self.assertIsNone(retired['session'])
        self.assertEqual(retired['selected_entry'], self.ids[1])
        self.assertEqual(retired['entries'][0]['runs'], [run_row(4.0, id=rid, number=1, status='SAVED', time_ms=None,
                                                                 penalties=['2'], outcome='RETIRED', outcome_fence='4')])
        rid2 = self.start_session(self.cid)['session']['run_id']
        eliminated = self.call('post', f'/api/runs/{rid2}/save/', {'outcome': 'ELIMINATED'})
        row = eliminated['entries'][1]['runs'][0]
        self.assertEqual((row['outcome'], row['outcome_fence'], row['time_ms']), ('ELIMINATED', '', None))
        reopened = self.call('post', f'/api/runs/{rid}/reopen/')
        self.assertEqual(reopened['entries'][0]['runs'][0]['outcome'], 'FINISHED')

    def test_select(self):
        base = f'/api/competitions/{self.cid}'
        self.assertEqual(self.call('post', f'{base}/select/', {'entry': self.ids[2]})['selected_entry'], self.ids[2])
        self.assertIsNone(self.call('post', f'{base}/select/', {'entry': None})['selected_entry'])
        self.call('post', f'{base}/select/', {'entry': str(uuid.uuid4())}, 400)
        self.call('post', f'{base}/select/', {'entry': 'not-an-id'}, 400)
        other = self.new_competition('Other')['competition']['id']
        stranger = self.call('post', f'/api/competitions/{other}/entries/', {'name': 'Zed'})['entries'][0]['id']
        self.call('post', f'{base}/select/', {'entry': stranger}, 400)          # not in this competition

    def test_session_move_flow(self):
        base = f'/api/competitions/{self.cid}'
        self.start_session(self.cid)                                            # Ala (first) is selected
        r = self.call('post', f'{base}/select/', {'entry': self.ids[1]}, 409)
        self.assertIn('Ala', r['error'])
        self.call('post', f'{base}/select/', {'entry': self.ids[1], 'move': 'yes'}, 409)   # only a real true moves
        state = self.call('post', f'{base}/select/', {'entry': self.ids[1], 'move': True})
        self.assertEqual((state['selected_entry'], state['session']['entry_id']), (self.ids[1], self.ids[1]))

    def test_capture_needs_a_real_boolean(self):
        base = f'/api/competitions/{self.cid}'
        for body in ({}, {'on': 'true'}, {'on': 1}, {'on': None}):
            self.call('post', f'{base}/capture/', body, 400)
        self.assertTrue(self.start_session(self.cid)['session']['capturing'])
        self.assertFalse(self.call('post', f'{base}/capture/', {'on': False})['session']['capturing'])

    def test_a_whole_run(self):
        base = f'/api/competitions/{self.cid}'
        with mock.patch('timings.arena.now_ms', return_value=T0):
            rid = self.start_session(self.cid)['session']['run_id']
        with mock.patch('timings.arena.now_ms', return_value=T0 + 60_000):
            s1 = self.signal('START', T0 + 1000, 1)
            s2 = self.signal('START', T0 + 3000, 2)
            f1 = self.signal('FINISH', T0 + 20_000, 1)
            f2 = self.signal('FINISH', T0 + 24_450, 2)
            state = self.call('get', f'{base}/state/')
            self.assertEqual((state['session']['start_id'], state['session']['finish_id'],
                              state['session']['time_ms']), (s1, f2, 23_450))
            self.assertEqual(self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '7'})['session']['penalties'],
                             ['7'])
            self.assertEqual(self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '9B'})['session']['penalties'],
                             ['7', '9B'])
            self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '13'}, 400)
            self.call('post', f'/api/runs/{rid}/penalty/', {}, 400)
            picked = self.call('post', f'/api/runs/{rid}/pick/', {'start': s2, 'finish': f1})['session']
            self.assertEqual((picked['start_id'], picked['finish_id'], picked['time_ms'],
                              picked['start_auto'], picked['finish_auto']), (s2, f1, 17_000, False, False))
            back = self.call('post', f'/api/runs/{rid}/pick/', {'start': None})['session']
            self.assertEqual((back['start_id'], back['start_auto'], back['finish_id']), (s1, True, f1))
            untouched = self.call('post', f'/api/runs/{rid}/pick/', {})['session']
            self.assertEqual((untouched['start_id'], untouched['finish_id']), (s1, f1))
            self.call('post', f'/api/runs/{rid}/pick/', {'start': f1}, 400)
            saved = self.call('post', f'/api/runs/{rid}/save/')
        self.assertIsNone(saved['session'])
        self.assertEqual(saved['selected_entry'], self.ids[1])                  # on to Bea
        self.assertEqual(saved['entries'][0]['runs'],
                         [run_row(8.0, id=rid, number=1, status='SAVED', time_ms=19_000, penalties=['7', '9B'])])
        # a saved run is closed for changes ...
        self.call('post', f'/api/runs/{rid}/penalty/', {'fence': '1'}, 409)
        self.call('post', f'/api/runs/{rid}/pick/', {'start': None}, 409)
        self.call('post', f'/api/runs/{rid}/save/', None, 409)
        self.call('post', f'/api/runs/{rid}/discard/', None, 409)
        # ... until it is reopened
        reopened = self.call('post', f'/api/runs/{rid}/reopen/')
        self.assertEqual((reopened['session']['run_id'], reopened['selected_entry']), (rid, self.ids[0]))
        self.call('post', f'/api/runs/{rid}/reopen/', None, 409)
        self.call('delete', f'/api/runs/{rid}/', None, 409)                     # open: discard instead
        gone = self.call('post', f'/api/runs/{rid}/discard/')
        self.assertEqual((gone['session'], gone['entries'][0]['runs']), (None, []))

    def test_delete_a_saved_run(self):
        rid = self.start_session(self.cid)['session']['run_id']
        self.call('post', f'/api/runs/{rid}/save/')
        state = self.call('delete', f'/api/runs/{rid}/')
        self.assertEqual(state['entries'][0]['runs'], [])

    def test_busy_in_another_competition(self):
        self.start_session(self.cid)
        other = self.new_competition('Other')['competition']['id']
        self.call('post', f'/api/competitions/{other}/entries/', {'name': 'Zed'})
        r = self.call('post', f'/api/competitions/{other}/capture/', {'on': True}, 409)
        self.assertIn('Saturday open', r['error'])
        self.assertEqual(self.call('get', f'/api/competitions/{other}/state/')['busy'],
                         {'competition_id': self.cid, 'competition_name': 'Saturday open'})

    def test_cannot_remove_the_competitor_in_session(self):
        self.start_session(self.cid)
        self.call('delete', f'/api/entries/{self.ids[0]}/', None, 409)

    def test_wrong_methods_are_rejected(self):
        self.assertEqual(self.api.get(f'/api/competitions/{self.cid}/capture/').status_code, 405)
        self.assertEqual(self.api.post(f'/api/competitions/{self.cid}/state/', {}, format='json').status_code, 405)
        self.assertEqual(self.api.get(f'/api/entries/{self.ids[0]}/').status_code, 405)
        self.assertEqual(self.api.get('/api/competitions/not-a-uuid/state/').status_code, 404)
