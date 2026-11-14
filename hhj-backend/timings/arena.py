"""The operator's competition flow (API.md): the list of competitors, the open
session, which gate signals count, and what the external display shows.

Everything that changes state is a function here. The HTTP views (api.py) and
the gate ingest (ingest.py) call them inside a transaction and then notify():
the new state goes to the browsers (live.py) and to the display (udp_link.py).
"""
import re
import threading
import time
import uuid
import zlib
from dataclasses import dataclass
from typing import List, Optional

from django.db import transaction
from django.db.models import Count, Max, Q
from django.utils import timezone

from events.models import Competition, CompetitionStart, Participant

from . import link
from .devices import registry
from .models import Run, TimeReading

START, FINISH = 'START', 'FINISH'
MAX_FENCES = 24
MAX_NAME = 100
NEARBY_MS = 120_000    # a signal this close to a session can still be picked although it was not captured
NEARBY_LIMIT = 12      # ... at most this many of them per column
KEEP = object()        # "leave this pick as it is"

_FENCE_RE = re.compile(r'([1-9][0-9]?)([ABC]?)')


class ArenaError(Exception):
    """Bad input: HTTP 400."""
    status = 400


class Conflict(ArenaError):
    """Not possible in the current state: HTTP 409."""
    status = 409


def now_ms():
    return time.time_ns() // 1_000_000


# --- small helpers --------------------------------------------------------------

def _name(value):
    if not isinstance(value, str) or not value.strip():
        raise ArenaError('A name is needed')
    value = ' '.join(value.split())
    if len(value) > MAX_NAME:
        raise ArenaError(f'The name is longer than {MAX_NAME} characters')
    return value


def _horse(value):
    """The hobby horse's name: optional, '' when there is none."""
    if value is None:
        return ''
    if not isinstance(value, str):
        raise ArenaError("The horse's name must be text")
    value = ' '.join(value.split())
    if len(value) > MAX_NAME:
        raise ArenaError(f"The horse's name is longer than {MAX_NAME} characters")
    return value


def _fence_count(value):
    if type(value) is not int or not 1 <= value <= MAX_FENCES:
        raise ArenaError(f'The number of fences must be between 1 and {MAX_FENCES}')
    return value


def _entries(competition):
    return list(competition.starts.select_related('participant').order_by('starting_order', 'pk'))


def _next_number(entry):
    last = entry.runs.aggregate(m=Max('number'))['m']
    return 1 if last is None else last + 1


def _by_time(reading):
    return (reading.sensor_time, reading.server_time, str(reading.pk))


def _require_open(run):
    if run.status != Run.Status.OPEN:
        raise Conflict('That run is saved; reopen it to change it')


def open_run():
    """The one open run (the session), or None."""
    return (Run.objects
            .select_related('entry__competition', 'entry__participant')
            .filter(status=Run.Status.OPEN).first())


# --- competitions ---------------------------------------------------------------

def list_competitions():
    rows = Competition.objects.annotate(
        n_entries=Count('starts', distinct=True),
        n_runs=Count('starts__runs', filter=Q(starts__runs__status=Run.Status.SAVED), distinct=True),
    ).order_by('-start_date_time', 'name')
    return [{'id': str(c.pk), 'name': c.name, 'fence_count': c.fence_count,
             'entries': c.n_entries, 'runs': c.n_runs} for c in rows]


def create_competition(name, fence_count=12):
    return Competition.objects.create(name=_name(name), fence_count=_fence_count(fence_count))


def update_competition(competition, name=KEEP, fence_count=KEEP):
    if name is not KEEP:
        competition.name = _name(name)
    if fence_count is not KEEP:
        competition.fence_count = _fence_count(fence_count)
    competition.save(update_fields=['name', 'fence_count'])


# --- the list of competitors ----------------------------------------------------

def add_entry(competition, name, horse=None):
    """A competitor joins, at the end of the list."""
    name = _name(name)
    horse = _horse(horse)
    # the same kid in another competition is the same Participant; a second kid
    # with the same name in THIS competition is a different one
    here = competition.starts.values_list('participant_id', flat=True)
    participant = (Participant.objects.filter(name__iexact=name).exclude(pk__in=list(here))
                   .order_by('pk').first())
    if participant is None:
        participant = Participant.objects.create(name=name)
    last = competition.starts.aggregate(m=Max('starting_order'))['m']
    entry = CompetitionStart.objects.create(competition=competition, participant=participant,
                                            horse_name=horse,
                                            starting_order=0 if last is None else last + 1)
    if last is None and competition.selected_start_id is None:
        competition.selected_start = entry                  # the first one in: selected
        competition.save(update_fields=['selected_start'])
    return entry


def rename_entry(entry, name):
    name = _name(name)
    participant = entry.participant
    if participant.starts.exclude(pk=entry.pk).exists():
        # entered elsewhere too: do not rename the kid in the other competitions
        entry.participant = Participant.objects.create(name=name)
        entry.save(update_fields=['participant'])
    else:
        participant.name = name
        participant.save(update_fields=['name'])


def set_horse(entry, horse):
    """Name, rename or (None / '') take away the horse of this entry."""
    entry.horse_name = _horse(horse)
    entry.save(update_fields=['horse_name'])


def remove_entry(entry):
    """A competitor leaves the list, with their saved runs."""
    run = open_run()
    if run is not None and run.entry_id == entry.pk:
        raise Conflict('Save or discard the open session of this competitor first')
    competition = entry.competition
    before = _entries(competition)
    index = [e.pk for e in before].index(entry.pk)
    was_selected = competition.selected_start_id == entry.pk
    entry.delete()
    after = before[:index] + before[index + 1:]
    for position, e in enumerate(after):
        if e.starting_order != position:
            e.starting_order = position
            e.save(update_fields=['starting_order'])
    competition.refresh_from_db(fields=['selected_start'])
    if was_selected:                                        # the one who moved up into its place
        competition.selected_start = after[index] if index < len(after) else None
        competition.save(update_fields=['selected_start'])


def reorder(competition, ids):
    entries = {str(e.pk): e for e in _entries(competition)}
    if not isinstance(ids, list) or sorted(str(i) for i in ids) != sorted(entries):
        raise ArenaError('The order must name every competitor exactly once')
    for position, eid in enumerate(str(i) for i in ids):
        entry = entries[eid]
        if entry.starting_order != position:
            entry.starting_order = position
            entry.save(update_fields=['starting_order'])


def select(competition, entry, move=False):
    """Make `entry` (or nobody) the selected competitor.

    With a session open for somebody else this is refused, unless `move`:
    then the open session is handed over to `entry` (it was started for the
    wrong kid).
    """
    run = open_run()
    if run is not None and run.entry.competition_id == competition.pk:
        if entry is None:
            raise Conflict('Save or discard the open session first')
        if run.entry_id != entry.pk:
            if not move:
                raise Conflict(f'A session is open for {run.entry.participant.name}')
            run.number = _next_number(entry)
            run.entry = entry
            run.save(update_fields=['entry', 'number'])
    competition.selected_start = entry
    competition.save(update_fields=['selected_start'])


# --- the session ----------------------------------------------------------------

def set_capture(competition, on):
    """Start / stop registering gate signals for the selected competitor."""
    run = open_run()
    if run is not None and run.entry.competition_id != competition.pk:
        raise Conflict(f'A session is open in "{run.entry.competition.name}"')
    now = now_ms()
    if on:
        if run is None:
            entry = competition.selected_start
            if entry is None:
                raise Conflict('Select a competitor first')
            run = Run.objects.create(entry=entry, number=_next_number(entry), opened_ms=now)
        if not run.capturing:
            run.capturing = True
            run.capture_from = now
            run.capture_to = None
            run.save(update_fields=['capturing', 'capture_from', 'capture_to'])
    elif run is not None and run.capturing:
        run.capturing = False
        run.capture_to = now
        run.save(update_fields=['capturing', 'capture_to'])
    return run


def _fence_key(label):
    m = _FENCE_RE.fullmatch(label)
    return (int(m.group(1)), m.group(2))


def toggle_penalty(run, fence):
    _require_open(run)
    m = _FENCE_RE.fullmatch(fence) if isinstance(fence, str) else None
    if m is None or not 1 <= int(m.group(1)) <= run.entry.competition.fence_count:
        raise ArenaError('No such fence')
    penalties = [p for p in run.penalties if p != fence]
    if len(penalties) == len(run.penalties):
        penalties.append(fence)
    run.penalties = sorted(penalties, key=_fence_key)
    run.save(update_fields=['penalties'])


def pick(run, start=KEEP, finish=KEEP):
    """Make a signal the one that counts; None = back to the automatic choice."""
    _require_open(run)
    for mark, value, field in ((START, start, 'start_pick'), (FINISH, finish, 'finish_pick')):
        if value is KEEP:
            continue
        if value is None:
            setattr(run, field, None)
            continue
        try:
            reading = TimeReading.objects.filter(pk=uuid.UUID(str(value)), time_mark=mark).first()
        except ValueError:
            reading = None
        if reading is None:
            raise ArenaError(f'No such {mark} signal')
        if reading.run_id is None:
            reading.run = run                               # it was outside the session: take it in
            reading.save(update_fields=['run'])
        elif reading.run_id != run.pk:
            raise Conflict('That signal belongs to another run')
        setattr(run, field, reading)
    run.save(update_fields=['start_pick', 'finish_pick'])


@dataclass
class Chosen:
    starts: List[TimeReading]
    finishes: List[TimeReading]
    start: Optional[TimeReading]
    finish: Optional[TimeReading]
    start_auto: bool
    finish_auto: bool
    time_ms: Optional[int]


def chosen(run) -> Chosen:
    """Which START and FINISH of the run count.

    The operator's pick if there is one; otherwise the first START, and the
    last FINISH after the START that counts. A FINISH that is not after that
    START never counts, picked or not.
    """
    readings = list(run.readings.all())
    starts = sorted((r for r in readings if r.time_mark == START), key=_by_time)
    finishes = sorted((r for r in readings if r.time_mark == FINISH), key=_by_time)
    picked = next((r for r in starts if r.pk == run.start_pick_id), None)
    start = picked or (starts[0] if starts else None)
    finish, finish_auto = None, True
    if start is not None:
        after = [f for f in finishes if f.sensor_time > start.sensor_time]
        picked_finish = next((f for f in after if f.pk == run.finish_pick_id), None)
        if picked_finish is not None:
            finish, finish_auto = picked_finish, False
        elif after:
            finish = after[-1]
    time_ms = finish.sensor_time - start.sensor_time if finish is not None else None
    return Chosen(starts, finishes, start, finish, picked is None, finish_auto, time_ms)


def save_run(run):
    """Freeze the session and move on to the next competitor in the list."""
    _require_open(run)
    c = chosen(run)
    if run.capturing:
        run.capturing = False
        run.capture_to = now_ms()
    run.start_pick = c.start
    run.finish_pick = c.finish
    run.time_ms = c.time_ms
    run.status = Run.Status.SAVED
    run.saved_at = timezone.now()
    run.save()
    competition = run.entry.competition
    entries = _entries(competition)
    index = [e.pk for e in entries].index(run.entry_id)
    competition.selected_start = entries[index + 1] if index + 1 < len(entries) else None
    competition.save(update_fields=['selected_start'])
    _set_last_result(run.pk)


def discard_run(run):
    _require_open(run)
    run.delete()                                            # its signals stay stored, unattached


def reopen_run(run):
    """Make a saved run the session again, to correct it."""
    if run.status != Run.Status.SAVED:
        raise Conflict('That run is already open')
    if open_run() is not None:
        raise Conflict('Save or discard the open session first')
    run.status = Run.Status.OPEN
    run.capturing = False
    run.time_ms = None
    run.saved_at = None
    run.save(update_fields=['status', 'capturing', 'time_ms', 'saved_at'])
    competition = run.entry.competition
    competition.selected_start = run.entry
    competition.save(update_fields=['selected_start'])


def delete_run(run):
    if run.status == Run.Status.OPEN:
        raise Conflict('Discard the open session instead')
    run.delete()


def _in_capture(run, t):
    return (run.capture_from is not None and t >= run.capture_from
            and (run.capture_to is None or t <= run.capture_to))


def on_reading(reading, created, lowered=False):
    """A gate signal was stored (`created`) or its time estimate improved (`lowered`).

    A new signal whose beam was cut while the session was registering is
    attached to the session — also when it arrives late.
    """
    if not created and not lowered:
        return
    with transaction.atomic():
        run = open_run()
        if run is None:
            return
        if created and reading.run_id is None and _in_capture(run, reading.sensor_time):
            reading.run = run
            reading.save(update_fields=['run'])
        competition_id = run.entry.competition_id
    notify(competition_id)


# --- the state the console shows (API.md) --------------------------------------

def devices():
    seen = registry.snapshot()
    out = []
    for role in (START, FINISH, 'TIMER'):
        online = [d for d in seen if d['role'] == role and d['online']]
        newest = min(online, key=lambda d: d['age_s']) if online else None
        out.append({'role': role, 'online': newest is not None,
                    'ok': newest['ok'] if newest is not None and role != 'TIMER' else None})
    return out


def _session(run, position, now):
    c = chosen(run)
    lo = run.opened_ms - NEARBY_MS
    hi = now if run.capturing or run.capture_to is None else run.capture_to + NEARBY_MS
    nearby = {START: [], FINISH: []}
    for mark in (START, FINISH):
        rows = (TimeReading.objects
                .filter(run__isnull=True, time_mark=mark, sensor_time__gte=lo, sensor_time__lte=hi)
                .order_by('-sensor_time')[:NEARBY_LIMIT])
        nearby[mark] = list(rows)

    def rows(mark, inside):
        out = [(r, True) for r in inside] + [(r, False) for r in nearby[mark]]
        out.sort(key=lambda pair: _by_time(pair[0]))
        return out

    starts = [{'id': str(r.pk), 'sensor_time': r.sensor_time, 'in_session': inside}
              for r, inside in rows(START, c.starts)]
    finishes = []
    for r, inside in rows(FINISH, c.finishes):
        after = c.start is not None and r.sensor_time > c.start.sensor_time
        finishes.append({'id': str(r.pk), 'sensor_time': r.sensor_time, 'in_session': inside,
                         'time_ms': r.sensor_time - c.start.sensor_time if after else None})
    return {
        'run_id': str(run.pk), 'entry_id': str(run.entry_id), 'number': position,
        'capturing': run.capturing,
        'penalties': list(run.penalties),
        'starts': starts, 'finishes': finishes,
        'start_id': str(c.start.pk) if c.start else None,
        'finish_id': str(c.finish.pk) if c.finish else None,
        'start_auto': c.start_auto, 'finish_auto': c.finish_auto,
        'time_ms': c.time_ms,
    }


def snapshot(competition):
    """The complete state of one competition, as the console needs it."""
    now = now_ms()
    runs = {}
    for run in Run.objects.filter(entry__competition=competition).order_by('number', 'opened_ms'):
        runs.setdefault(run.entry_id, []).append(run)
    current = open_run()
    session, busy = None, None
    if current is not None:
        if current.entry.competition_id == competition.pk:
            position = [r.pk for r in runs[current.entry_id]].index(current.pk) + 1
            session = _session(current, position, now)
        else:
            busy = {'competition_id': str(current.entry.competition_id),
                    'competition_name': current.entry.competition.name}
    entries = []
    for entry in _entries(competition):
        entries.append({
            'id': str(entry.pk), 'name': entry.participant.name, 'horse': entry.horse_name,
            'runs': [{'id': str(r.pk), 'number': i + 1, 'status': r.status,
                      'time_ms': session['time_ms'] if session and r.pk == current.pk else r.time_ms,
                      'penalties': list(r.penalties)}
                     for i, r in enumerate(runs.get(entry.pk, []))],
        })
    return {
        'server_time': now,
        'competition': {'id': str(competition.pk), 'name': competition.name,
                        'fence_count': competition.fence_count},
        'entries': entries,
        'selected_entry': str(competition.selected_start_id) if competition.selected_start_id else None,
        'session': session,
        'busy': busy,
        'devices': devices(),
    }


# --- the external display (PROTOCOL.md, SHOW) ----------------------------------

_last_result = None            # pk of the run whose result stays on the display between sessions
_last_result_lock = threading.Lock()


def _set_last_result(run_pk):
    global _last_result
    with _last_result_lock:
        _last_result = run_pk


def _rev(run_pk, start_pk, finish_pk):
    """Identifies what is being timed: changes with the run and with the signals that count."""
    return zlib.crc32(f'{run_pk}:{start_pk}:{finish_pk or ""}'.encode())


def display(now=None) -> link.Show:
    """What the external display should show right now.

    An open session: 0.00 until a START counts, then the running clock, then
    the result. No session: the result of the run saved last (until the next
    session opens), otherwise the idle picture.
    """
    now = now_ms() if now is None else now
    run = open_run()
    if run is not None:
        c = chosen(run)
        if c.start is None:
            return link.Show('READY')
        rev = _rev(run.pk, c.start.pk, c.finish.pk if c.finish else None)
        if c.finish is None:
            return link.Show('RUN', rev=rev, age_ms=min(max(now - c.start.sensor_time, 0), link.U32))
        return link.Show('RESULT', rev=rev, ms=min(c.time_ms, link.U32))
    with _last_result_lock:
        last_pk = _last_result
    last = Run.objects.filter(pk=last_pk, status=Run.Status.SAVED).first() if last_pk else None
    if last is not None and last.time_ms is not None:
        return link.Show('RESULT', rev=_rev(last.pk, last.start_pick_id, last.finish_pick_id),
                         ms=min(last.time_ms, link.U32))
    return link.Show('IDLE')


# --- telling everybody ----------------------------------------------------------

def notify(competition_id=None):
    """Once the current transaction is committed: push the state to the
    browsers and what to show to the display."""
    def push():
        from . import live, udp_link
        live.publish_states(competition_id)
        udp_link.kick_display()
    transaction.on_commit(push)
