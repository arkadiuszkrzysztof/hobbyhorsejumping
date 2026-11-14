"""Publishing to WebSocket listeners from any thread.

InMemoryChannelLayer is bound to the event loop that serves the consumers.
`async_to_sync(layer.group_send)` from a plain thread (the UDP listener) runs
in a private loop and does not wake the server loop, so the message would sit
until something else woke it. We therefore remember the server's loop (see
`CaptureLoop`, wrapped around the ASGI app in adminportal/asgi.py) and submit
the send to it with run_coroutine_threadsafe.
"""
import asyncio
import logging
import threading

from asgiref.sync import async_to_sync
from channels.layers import get_channel_layer

log = logging.getLogger(__name__)

LIVE_TIMINGS_LISTENERS_GROUP_NAME = 'live_timings_listeners'

_loop = None
_loop_lock = threading.Lock()


def set_loop(loop):
    global _loop
    with _loop_lock:
        _loop = loop


def get_loop():
    return _loop


class CaptureLoop:
    """ASGI wrapper that records the running loop on every call."""

    def __init__(self, app):
        self.app = app

    async def __call__(self, scope, receive, send):
        loop = asyncio.get_running_loop()
        if _loop is not loop:
            set_loop(loop)
        return await self.app(scope, receive, send)


def _log_failure(fut):
    if not fut.cancelled() and fut.exception() is not None:
        log.error('live publish failed: %r', fut.exception())


def publish(message):
    """Send one serialized TimeReading to every live-timings listener."""
    layer = get_channel_layer()
    if layer is None:
        return
    event = {'type': 'send_timings', 'message': message}
    loop = get_loop()
    if loop is not None and loop.is_running() and not loop.is_closed():
        try:
            current = asyncio.get_running_loop()
        except RuntimeError:
            current = None
        coro = layer.group_send(LIVE_TIMINGS_LISTENERS_GROUP_NAME, event)
        if current is loop:
            loop.create_task(coro).add_done_callback(_log_failure)
        else:
            asyncio.run_coroutine_threadsafe(coro, loop).add_done_callback(_log_failure)
        return
    async_to_sync(layer.group_send)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, event)
