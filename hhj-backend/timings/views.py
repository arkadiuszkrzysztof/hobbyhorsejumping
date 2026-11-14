"""Read-only lists for checking what the server sees (README: "Switching it on")."""
import re

from rest_framework import status
from rest_framework.response import Response
from rest_framework.views import APIView

from .devices import registry
from .models import TimeReading
from .serializers import TimeReadingSerializer
from . import udp_link

DEFAULT_LIMIT = 100
MAX_LIMIT = 1000
_DIGITS = re.compile(r'[0-9]{1,16}')


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


class DeviceList(APIView):
    def get(self, request):
        server = udp_link.get_server()
        link_info = {
            'running': bool(server and server.running),
            'port': server.port if server else None,
            'stats': dict(server.stats) if server else None,
        }
        return Response({'link': link_info, 'devices': registry.snapshot()})
