"""The operator console's WebSocket (/ws/live/<competition>/)."""
import asyncio
import json
import threading
import uuid

from channels.db import database_sync_to_async
from channels.testing import WebsocketCommunicator
from django.db import connection
from django.test import TransactionTestCase

from adminportal.asgi import application
from timings import arena, live
from timings.ingest import ingest_event
from timings.tests.helpers import event


class LiveSocketTests(TransactionTestCase):
    def setUp(self):
        self.addCleanup(live.set_loop, None)
        self.addCleanup(arena._set_last_result, None)
        self.competition = arena.create_competition('Live test')
        self.entry = arena.add_entry(self.competition, 'Ala')

    async def connect(self, competition_id=None):
        comm = WebsocketCommunicator(application, f'/ws/live/{competition_id or self.competition.pk}/')
        connected, _ = await comm.connect()
        return comm, connected

    async def test_state_on_connect_and_after_every_change(self):
        comm, connected = await self.connect()
        self.assertTrue(connected)
        first = await comm.receive_json_from(timeout=2)
        self.assertEqual(first['type'], 'state')
        self.assertEqual(first['state']['competition']['name'], 'Live test')
        self.assertEqual([e['name'] for e in first['state']['entries']], ['Ala'])

        @database_sync_to_async
        def add():
            from django.db import transaction
            with transaction.atomic():
                arena.add_entry(self.competition, 'Bea')
                arena.notify(self.competition.pk)
        await add()
        pushed = await comm.receive_json_from(timeout=2)
        self.assertEqual([e['name'] for e in pushed['state']['entries']], ['Ala', 'Bea'])
        self.assertGreaterEqual(pushed['state']['server_time'], first['state']['server_time'])
        await comm.disconnect()

    async def test_a_gate_signal_from_the_udp_thread_reaches_the_console(self):
        await database_sync_to_async(arena.select)(self.competition, self.entry)
        await database_sync_to_async(arena.set_capture)(self.competition, True)
        comm, _ = await self.connect()
        opened = await comm.receive_json_from(timeout=2)
        self.assertTrue(opened['state']['session']['capturing'])
        done = threading.Event()

        def udp_like_thread():
            try:
                ingest_event(event('START', boot=3, seq=1, age_ms=0), arena.now_ms() + 50, '10.0.0.2')
            finally:
                connection.close()
                done.set()

        threading.Thread(target=udp_like_thread, daemon=True).start()
        pushed = await comm.receive_json_from(timeout=2)
        self.assertEqual(len(pushed['state']['session']['starts']), 1)
        self.assertTrue(pushed['state']['session']['starts'][0]['in_session'])
        await asyncio.get_running_loop().run_in_executor(None, done.wait, 5)
        await comm.disconnect()

    async def test_devices_message(self):
        comm, _ = await self.connect()
        await comm.receive_json_from(timeout=2)
        await database_sync_to_async(live.publish_devices)()
        msg = await comm.receive_json_from(timeout=2)
        self.assertEqual(msg['type'], 'devices')
        self.assertEqual([d['role'] for d in msg['devices']], ['START', 'FINISH', 'TIMER'])
        self.assertIsInstance(msg['server_time'], int)
        await comm.disconnect()

    async def test_consoles_of_other_competitions_learn_that_a_session_is_open(self):
        other = await database_sync_to_async(arena.create_competition)('Other')
        watcher, _ = await self.connect(other.pk)
        self.assertIsNone((await watcher.receive_json_from(timeout=2))['state']['busy'])

        @database_sync_to_async
        def open_session():
            from django.db import transaction
            with transaction.atomic():
                arena.select(self.competition, self.entry)
                arena.set_capture(self.competition, True)
                arena.notify(self.competition.pk)
        await open_session()
        pushed = await watcher.receive_json_from(timeout=2)
        self.assertEqual(pushed['state']['busy']['competition_name'], 'Live test')
        await watcher.disconnect()

    async def test_unknown_competition_is_refused(self):
        comm, connected = await self.connect(uuid.uuid4())
        self.assertFalse(connected)

    async def test_client_messages_are_ignored_and_watch_count_is_released(self):
        comm, _ = await self.connect()
        await comm.receive_json_from(timeout=2)
        self.assertEqual(live.watched(), [str(self.competition.pk)])
        await comm.send_to(text_data='garbage')
        await comm.send_to(text_data=json.dumps({'type': 'live.message', 'text': 'x'}))
        self.assertTrue(await comm.receive_nothing(timeout=0.05))
        await comm.disconnect()
        self.assertEqual(live.watched(), [])

    async def test_nothing_is_published_while_nobody_watches(self):
        self.assertEqual(live.watched(), [])
        await database_sync_to_async(live.publish_devices)()      # must simply do nothing
        await database_sync_to_async(live.publish_states)()
