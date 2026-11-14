import asyncio
import threading
import time

from channels.db import database_sync_to_async
from channels.testing import WebsocketCommunicator
from django.db import connection
from django.test import TransactionTestCase

from adminportal.asgi import application
from timings import live
from timings.ingest import ingest_event
from timings.models import TimeReading
from timings.tests.helpers import event

T0 = 1_794_000_000_000


class WebsocketTests(TransactionTestCase):
    def setUp(self):
        self.addCleanup(live.set_loop, None)

    async def connect(self):
        comm = WebsocketCommunicator(application, '/ws/timings/')
        connected, _ = await comm.connect()
        self.assertTrue(connected)
        return comm

    async def test_ingest_from_another_thread_is_delivered_promptly(self):
        """Pitfall A: a plain thread (like the UDP listener) publishes into the server loop."""
        comm = await self.connect()
        self.assertIs(live.get_loop(), asyncio.get_running_loop())
        done = threading.Event()

        def udp_like_thread():
            try:
                ingest_event(event('START', boot=3, seq=1, age_ms=7), T0, '10.0.0.2')
                ingest_event(event('START', boot=3, seq=1, n=1), T0 + 5, '10.0.0.2')  # duplicate: no 2nd msg
            finally:
                connection.close()
                done.set()

        started = time.monotonic()
        threading.Thread(target=udp_like_thread, daemon=True).start()
        msg = await comm.receive_json_from(timeout=2)
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 1.0)
        self.assertEqual(msg['time_mark'], 'START')
        self.assertEqual(msg['sensor_time'], T0 - 7)
        self.assertEqual(msg['gate_boot'], 3)
        self.assertIn('server_time', msg)
        await asyncio.get_running_loop().run_in_executor(None, done.wait, 5)
        self.assertTrue(done.is_set())
        self.assertTrue(await comm.receive_nothing(timeout=0.05))  # once per event, not per copy
        await comm.disconnect()

    async def test_client_garbage_is_ignored(self):
        comm = await self.connect()
        await comm.send_to(text_data='not json at all')
        await comm.send_to(text_data='{"message": "hi"}')
        await comm.send_to(text_data='{"type": "send_timings", "message": {}}')
        await comm.send_to(bytes_data=b'\x00\xff')
        # still connected and still receiving
        await database_sync_to_async(ingest_event)(event('FINISH', boot=1, seq=1), T0, '')
        msg = await comm.receive_json_from(timeout=2)
        self.assertEqual(msg['time_mark'], 'FINISH')
        await comm.disconnect()
        self.assertEqual(await database_sync_to_async(TimeReading.objects.count)(), 1)

    async def test_two_listeners_both_receive(self):
        a = await self.connect()
        b = await self.connect()
        await database_sync_to_async(ingest_event)(event('START', boot=2, seq=2), T0, '')
        self.assertEqual((await a.receive_json_from(timeout=2))['gate_seq'], 2)
        self.assertEqual((await b.receive_json_from(timeout=2))['gate_seq'], 2)
        await a.disconnect()
        await b.disconnect()
