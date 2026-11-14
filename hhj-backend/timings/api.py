"""HTTP side of the operator console — the contract is API.md.

Every view runs its change in one transaction (arena.py does the work),
tells the browsers and the display, and answers with the complete new state.
"""
import functools
import uuid

from django.db import transaction
from django.http import Http404
from rest_framework.decorators import api_view
from rest_framework.response import Response

from events.models import Competition, CompetitionStart

from . import arena
from .models import Run


def _body(request):
    return request.data if isinstance(request.data, dict) else {}


def _get(model, pk, what):
    obj = model.objects.filter(pk=pk).first()
    if obj is None:
        raise Http404(what)
    return obj


def _competition(cid):
    return _get(Competition, cid, 'No such competition')


def _run(rid):
    run = Run.objects.select_related('entry__competition', 'entry__participant').filter(pk=rid).first()
    if run is None:
        raise Http404('No such run')
    return run


def console_view(methods):
    """The wrapped function makes the change and returns the competition whose
    state is the answer."""
    def decorate(fn):
        @api_view(methods)
        @functools.wraps(fn)
        def view(request, *args, **kwargs):
            try:
                with transaction.atomic():
                    competition = fn(request, *args, **kwargs)
                    if request.method != 'GET':
                        arena.notify(competition.pk)
            except arena.ArenaError as exc:
                return Response({'error': str(exc)}, status=exc.status)
            except Http404 as exc:
                return Response({'error': str(exc) or 'Not found'}, status=404)
            competition.refresh_from_db()
            return Response(arena.snapshot(competition))
        return view
    return decorate


@api_view(['GET', 'POST'])
def competitions(request):
    if request.method == 'GET':
        return Response(arena.list_competitions())
    try:
        with transaction.atomic():
            data = _body(request)
            created = arena.create_competition(data.get('name'), data.get('fence_count', 12), **_rules(data))
            arena.notify(created.pk)
    except arena.ArenaError as exc:
        return Response({'error': str(exc)}, status=exc.status)
    return Response(arena.snapshot(created), status=201)


@console_view(['GET'])
def state(request, cid):
    return _competition(cid)


@console_view(['PATCH'])
def competition(request, cid):
    c = _competition(cid)
    data = _body(request)
    arena.update_competition(c, data.get('name', arena.KEEP), data.get('fence_count', arena.KEEP), **_rules(data))
    return c


@console_view(['POST'])
def entries(request, cid):
    c = _competition(cid)
    data = _body(request)
    arena.add_entry(c, data.get('name'), data.get('horse'))
    return c


@console_view(['PATCH', 'DELETE'])
def entry(request, eid):
    e = CompetitionStart.objects.select_related('competition', 'participant').filter(pk=eid).first()
    if e is None:
        raise Http404('No such competitor')
    c = e.competition
    if request.method == 'DELETE':
        arena.remove_entry(e)
        return c
    data = _body(request)
    if 'name' not in data and 'horse' not in data:
        raise arena.ArenaError('Nothing to change: give "name" and / or "horse"')
    if 'name' in data:
        arena.rename_entry(e, data['name'])
    if 'horse' in data:
        arena.set_horse(e, data['horse'])
    return c


@console_view(['POST'])
def order(request, cid):
    c = _competition(cid)
    arena.reorder(c, _body(request).get('entries'))
    return c


@console_view(['POST'])
def select(request, cid):
    c = _competition(cid)
    data = _body(request)
    eid = data.get('entry')
    e = None
    if eid is not None:
        e = c.starts.select_related('participant').filter(pk=_uuid(eid)).first()
        if e is None:
            raise arena.ArenaError('No such competitor in this competition')
    arena.select(c, e, move=data.get('move') is True)
    return c


@console_view(['POST'])
def capture(request, cid):
    c = _competition(cid)
    on = _body(request).get('on')
    if type(on) is not bool:
        raise arena.ArenaError('"on" must be true or false')
    arena.set_capture(c, on)
    return c


@console_view(['POST'])
def penalty(request, rid):
    run = _run(rid)
    arena.toggle_penalty(run, _body(request).get('fence'))
    return run.entry.competition


@console_view(['POST'])
def refusal(request, rid):
    run = _run(rid)
    arena.cycle_refusal(run, _body(request).get('fence'))
    return run.entry.competition


@console_view(['POST'])
def pick(request, rid):
    run = _run(rid)
    data = _body(request)
    arena.pick(run, data.get('start', arena.KEEP), data.get('finish', arena.KEEP))
    return run.entry.competition


@console_view(['POST'])
def save(request, rid):
    run = _run(rid)
    data = _body(request)
    arena.save_run(run, data.get('outcome', 'FINISHED'), data.get('fence'))
    return run.entry.competition


@console_view(['POST'])
def discard(request, rid):
    run = _run(rid)
    c = run.entry.competition
    arena.discard_run(run)
    return c


@console_view(['POST'])
def reopen(request, rid):
    run = _run(rid)
    arena.reopen_run(run)
    return run.entry.competition


@console_view(['DELETE'])
def run(request, rid):
    r = _run(rid)
    c = r.entry.competition
    arena.delete_run(r)
    return c


def _rules(data):
    return {key: data[key] for key in arena.RULES if key in data}


def _uuid(value):
    try:
        return uuid.UUID(str(value))
    except ValueError:
        raise arena.ArenaError('Not a valid id')
