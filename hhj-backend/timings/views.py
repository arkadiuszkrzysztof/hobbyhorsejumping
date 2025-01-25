from asgiref.sync import async_to_sync
from channels.layers import get_channel_layer
from rest_framework import status
from rest_framework.response import Response
from rest_framework.views import APIView

from .models import TimeReading
from .serializers import TimeReadingSerializer
from events.models import CompetitionStart

from .consumers import LIVE_TIMINGS_LISTENERS_GROUP_NAME

import json
import time

class TimeReadingList(APIView):
    def get(self, request):
        timings = TimeReading.objects.all()
        serializer = TimeReadingSerializer(timings, many=True)
        return Response(serializer.data, status=status.HTTP_200_OK)

    def post(self, request):
        input_data = json.loads(request.body)
        new_timing = TimeReading.objects.create(
            sensor_time=input_data['sensor_time'],
            time_mark=input_data['time_mark'],
            server_time=int(time.time() * 1000),
            competition_start=CompetitionStart.objects.get(pk='001073d1-a357-40a4-b6f9-8e2a633ceb23'),
        )

        serializer = TimeReadingSerializer(new_timing)
        channel_layer = get_channel_layer()
        async_to_sync(channel_layer.group_send)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, {
            'type': 'send_timings',
            'message': serializer.data
        })
        return Response(serializer.data, status=status.HTTP_201_CREATED)