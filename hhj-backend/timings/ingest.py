"""Storing timing events (UDP link and the legacy/manual HTTP path)."""
import json
import time

from django.core.serializers.json import DjangoJSONEncoder
from django.db import IntegrityError, transaction
from django.db.models import F

from . import arena, live
from .link import Event
from .models import TimeReading

STALE_AGE_MS = 600_000  # PROTOCOL.md: copies older than 10 minutes are dropped


def now_ms():
    return time.time_ns() // 1_000_000


def serialize(reading):
    from .serializers import TimeReadingSerializer
    # Plain JSON types, so the message is safe for any channel layer.
    return json.loads(json.dumps(TimeReadingSerializer(reading).data, cls=DjangoJSONEncoder))


def _publish_on_commit(reading):
    message = serialize(reading)
    transaction.on_commit(lambda: live.publish(message))


def ingest_event(ev: Event, recv_epoch_ms: int, source_ip: str = ''):
    """Store one received EVENT copy.

    Returns (reading, created); (None, False) for a stale copy (nothing stored).
    On an existing event `copies` is incremented and `sensor_time` is lowered
    if this copy gives a smaller estimate (it is never raised).
    """
    if ev.age_ms > STALE_AGE_MS:
        return None, False
    edge = recv_epoch_ms - ev.age_ms
    identity = {'time_mark': ev.mark, 'gate_boot': ev.boot, 'gate_seq': ev.seq}

    for _ in range(2):
        existing = TimeReading.objects.filter(**identity).only('pk').first()
        if existing is None:
            try:
                with transaction.atomic():
                    reading = TimeReading.objects.create(
                        **identity,
                        sensor_time=edge,
                        server_time=recv_epoch_ms,
                        gate_time=ev.t,
                        direction=ev.dir,
                        gap_ms=ev.gap_ms,
                        copies=1,
                        source=(source_ip or '')[:45],
                    )
            except IntegrityError:
                continue  # another writer created it first: count as a copy
            _publish_on_commit(reading)
            arena.on_reading(reading, created=True)
            return reading, True
        with transaction.atomic():
            TimeReading.objects.filter(pk=existing.pk).update(copies=F('copies') + 1)
            lowered = TimeReading.objects.filter(pk=existing.pk, sensor_time__gt=edge).update(sensor_time=edge)
        reading = TimeReading.objects.get(pk=existing.pk)
        arena.on_reading(reading, created=False, lowered=bool(lowered))
        return reading, False
    raise RuntimeError('could not store event %r' % (ev,))


def ingest_legacy(time_mark, sensor_time=None, age_ms=None, source='http'):
    """Store a manual / v1 reading (no gate identity; always a new row).

    sensor_time: epoch ms of the cut; None or 0 means "now" (minus age_ms).
    """
    received = now_ms()
    if sensor_time:
        edge = int(sensor_time)
    else:
        edge = received - int(age_ms or 0)
    reading = TimeReading.objects.create(
        time_mark=time_mark,
        sensor_time=edge,
        server_time=received,
        source=(source or '')[:45],
    )
    _publish_on_commit(reading)
    arena.on_reading(reading, created=True)
    return reading
