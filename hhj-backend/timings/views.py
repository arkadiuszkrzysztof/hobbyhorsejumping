import json
import re

from rest_framework import status
from rest_framework.response import Response
from rest_framework.views import APIView

from .devices import registry
from .ingest import STALE_AGE_MS, ingest_legacy, serialize
from .link import GATE_ROLES, U53
from .models import TimeReading
from .serializers import TimeReadingSerializer
from . import udp_link

DEFAULT_LIMIT = 100
MAX_LIMIT = 1000
_DIGITS = re.compile(r'[0-9]{1,16}')


class BadInput(ValueError):
    pass


def _int_field(data, key, lo, hi):
    """Optional non-negative integer; a JSON int or a decimal string."""
    if key not in data or data[key] is None or data[key] == '':
        return None
    v = data[key]
    if isinstance(v, str) and _DIGITS.fullmatch(v):
        v = int(v)
    if type(v) is not int:
        raise BadInput(f'{key} must be an integer (or a decimal string)')
    if not lo <= v <= hi:
        raise BadInput(f'{key} must be between {lo} and {hi}')
    return v


def _body(request):
    ctype = request.content_type or ''
    if ctype.startswith(('application/x-www-form-urlencoded', 'multipart/form-data')):
        return {k: request.data.get(k) for k in request.data}
    try:
        data = json.loads(request.body or b'')
    except (ValueError, UnicodeDecodeError):
        raise BadInput('body must be a JSON object')
    if not isinstance(data, dict):
        raise BadInput('body must be a JSON object')
    return data


class TimeReadingList(APIView):
    def get(self, request):
        raw = request.query_params.get('limit')
        limit = DEFAULT_LIMIT
        if raw is not None:
            if not _DIGITS.fullmatch(raw) or int(raw) < 1:
                return Response({'error': 'limit must be a positive integer'}, status=status.HTTP_400_BAD_REQUEST)
            limit = min(int(raw), MAX_LIMIT)
        readings = TimeReading.objects.order_by('-server_time', '-sensor_time')[:limit]
        return Response(TimeReadingSerializer(readings, many=True).data, status=status.HTTP_200_OK)

    def post(self, request):
        """Manual / v1 path: {"time_mark": "FINISH", "sensor_time": "<epoch_ms>", "age_ms": n}."""
        try:
            data = _body(request)
            mark = data.get('time_mark')
            if mark not in GATE_ROLES:
                raise BadInput('time_mark must be "START" or "FINISH"')
            sensor_time = _int_field(data, 'sensor_time', 0, U53)
            age_ms = _int_field(data, 'age_ms', 0, STALE_AGE_MS)
            if sensor_time and age_ms:
                raise BadInput('give sensor_time or age_ms, not both')
        except BadInput as exc:
            return Response({'error': str(exc)}, status=status.HTTP_400_BAD_REQUEST)
        reading = ingest_legacy(mark, sensor_time=sensor_time, age_ms=age_ms, source='http')
        return Response(serialize(reading), status=status.HTTP_201_CREATED)


class DeviceList(APIView):
    def get(self, request):
        server = udp_link.get_server()
        link_info = {
            'running': bool(server and server.running),
            'port': server.port if server else None,
            'stats': dict(server.stats) if server else None,
        }
        return Response({'link': link_info, 'devices': registry.snapshot()})
