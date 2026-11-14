"""Publishing to WebSocket listeners from any thread.

InMemoryChannelLayer is bound to the event loop that serves the consumers.
`async_to_sync(layer.group_send)` from a plain thread (the UDP listener) runs
in a private loop and does not wake the server loop, so the message would sit
until something else woke it. We therefore remember the server's loop (see
`CaptureLoop`, wrapped around the ASGI app in adminportal/asgi.py) and submit
the send to it with run_coroutine_threadsafe.
"""
import asyncio
import json
import logging
import threading

from asgiref.sync import async_to_sync
from channels.layers import get_channel_layer

log = logging.getLogger(__name__)

LIVE_TIMINGS_LISTENERS_GROUP_NAME = 'live_timings_listeners'
LIVE_ALL_GROUP = 'live_all'                      # every operator console


def live_group(competition_id):
    return f'live_{str(competition_id).replace("-", "")}'

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
    _group_send(LIVE_TIMINGS_LISTENERS_GROUP_NAME, {'type': 'send_timings', 'message': message})


# --- operator console (/ws/live/<competition>/) --------------------------------

_watched = {}                 # competition id (str) -> number of consoles looking at it
_watched_lock = threading.Lock()


def watch(competition_id, delta):
    with _watched_lock:
        count = _watched.get(str(competition_id), 0) + delta
        if count > 0:
            _watched[str(competition_id)] = count
        else:
            _watched.pop(str(competition_id), None)


def watched():
    with _watched_lock:
        return list(_watched)


def state_text(competition):
    from . import arena
    return json.dumps({'type': 'state', 'state': arena.snapshot(competition)})


def publish_states(competition_id=None):
    """Push the fresh state of every competition a console is looking at
    (a session opening in one changes `busy` in the others)."""
    from events.models import Competition
    ids = set(watched())
    if competition_id is not None:
        ids.add(str(competition_id))
    for competition in Competition.objects.filter(pk__in=ids):
        _group_send(live_group(competition.pk), {'type': 'live.message', 'text': state_text(competition)})


def publish_devices():
    """Gate / timer status and the server clock, for every console."""
    if not watched():
        return
    from . import arena
    text = json.dumps({'type': 'devices', 'server_time': arena.now_ms(), 'devices': arena.devices()})
    _group_send(LIVE_ALL_GROUP, {'type': 'live.message', 'text': text})


def _group_send(group, event):
    layer = get_channel_layer()
    if layer is None:
        return
    loop = get_loop()
    if loop is not None and loop.is_running() and not loop.is_closed():
        try:
            current = asyncio.get_running_loop()
        except RuntimeError:
            current = None
        coro = layer.group_send(group, event)
        if current is loop:
            loop.create_task(coro).add_done_callback(_log_failure)
        else:
            asyncio.run_coroutine_threadsafe(coro, loop).add_done_callback(_log_failure)
        return
    async_to_sync(layer.group_send)(group, event)
