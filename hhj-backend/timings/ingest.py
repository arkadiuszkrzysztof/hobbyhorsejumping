"""Storing the gate events received over the UDP link."""
from django.db import IntegrityError, transaction
from django.db.models import F

from . import arena
from .link import Event
from .models import TimeReading

STALE_AGE_MS = 600_000  # PROTOCOL.md: copies older than 10 minutes are dropped


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
            arena.on_reading(reading, created=True)
            return reading, True
        with transaction.atomic():
            TimeReading.objects.filter(pk=existing.pk).update(copies=F('copies') + 1)
            lowered = TimeReading.objects.filter(pk=existing.pk, sensor_time__gt=edge).update(sensor_time=edge)
        reading = TimeReading.objects.get(pk=existing.pk)
        arena.on_reading(reading, created=False, lowered=bool(lowered))
        return reading, False
    raise RuntimeError('could not store event %r' % (ev,))

