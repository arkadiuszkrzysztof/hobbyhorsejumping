from asgiref.sync import async_to_sync
from channels.layers import get_channel_layer
from rest_framework import status
from rest_framework.response import Response
from rest_framework.views import APIView
from django.utils import timezone

from .models import TimeReading, SensorStatus
from .serializers import TimeReadingSerializer
from events.models import CompetitionStart
from events.serializers import CompetitionStartSerializer

from .consumers import LIVE_TIMINGS_LISTENERS_GROUP_NAME

import json
import time

class TimeReadingList(APIView):
    def get(self, request):
        timings = TimeReading.objects.all().order_by('-server_time')[:50]  # Get latest 50
        serializer = TimeReadingSerializer(timings, many=True)
        return Response(serializer.data, status=status.HTTP_200_OK)

    def post(self, request):
        input_data = json.loads(request.body)
        sensor_id = input_data.get('sensor_id', 'unknown')
        time_mark = input_data['time_mark']
        
        # Handle ALIVE signals separately
        if time_mark == 'ALIVE':
            return self.handle_alive_signal(sensor_id, input_data)
        
        # Get the currently active competition start
        active_start = CompetitionStart.objects.filter(
            has_started=True, 
            has_finished=False
        ).first()
        
        if not active_start:
            return Response(
                {'error': 'No active competitor selected. Please select a competitor first.'}, 
                status=status.HTTP_400_BAD_REQUEST
            )
        
        # If this is a FINISH signal, mark the competition start as finished
        if time_mark == 'FINISH':
            active_start.has_finished = True
            active_start.save()
        
        new_timing = TimeReading.objects.create(
            sensor_time=input_data['sensor_time'],
            time_mark=time_mark,
            server_time=int(time.time() * 1000),
            competition_start=active_start,
            sensor_id=sensor_id,
        )

        serializer = TimeReadingSerializer(new_timing)
        
        # Broadcast the timing update to all connected clients
        from .consumers import broadcast_state_change
        broadcast_state_change('timing_update', {
            'timing': serializer.data,
            'active_competitor': CompetitionStartSerializer(active_start).data,
            'time_mark': time_mark
        })
        
        channel_layer = get_channel_layer()
        async_to_sync(channel_layer.group_send)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, {
            'type': 'send_timings',
            'message': serializer.data
        })
        return Response(serializer.data, status=status.HTTP_201_CREATED)
    
    def handle_alive_signal(self, sensor_id, input_data):
        """Handle ALIVE signals from sensors"""
        try:
            # Determine sensor type from sensor_id
            sensor_type = SensorStatus.SensorType.START_SENSOR if 'START' in sensor_id.upper() else SensorStatus.SensorType.FINISH_SENSOR
            
            # Update or create sensor status
            sensor_status, created = SensorStatus.objects.update_or_create(
                sensor_id=sensor_id,
                defaults={
                    'sensor_type': sensor_type,
                    'last_alive_signal': timezone.now(),
                    'is_online': True,
                }
            )
            
            # Create ALIVE time reading for logging
            TimeReading.objects.create(
                sensor_time=input_data['sensor_time'],
                time_mark='ALIVE',
                server_time=int(time.time() * 1000),
                sensor_id=sensor_id,
                competition_start=None,  # ALIVE signals don't belong to any specific competition
            )
            
            # Broadcast sensor status update
            from .consumers import broadcast_state_change
            broadcast_state_change('sensor_status_update', {
                'sensor_id': sensor_id,
                'sensor_type': sensor_type,
                'is_online': True,
                'last_alive': timezone.now().isoformat(),
            })
            
            return Response({
                'message': f'Alive signal received from {sensor_id}',
                'sensor_type': sensor_type,
                'timestamp': timezone.now().isoformat()
            }, status=status.HTTP_200_OK)
            
        except Exception as e:
            return Response(
                {'error': f'Failed to process alive signal: {str(e)}'}, 
                status=status.HTTP_400_BAD_REQUEST
            )

class TestAPIView(APIView):
    """Test endpoint for debugging"""
    
    def get(self, request):
        return Response({
            'message': 'API is working!',
            'timestamp': int(time.time() * 1000),
            'active_competitors': CompetitionStart.objects.filter(
                has_started=True, 
                has_finished=False
            ).count(),
            'total_timings': TimeReading.objects.count()
        }, status=status.HTTP_200_OK)

class SensorStatusView(APIView):
    """Get current status of all sensors"""
    
    def get(self, request):
        # Update sensor status based on recent alive signals
        current_time = timezone.now()
        sensors = SensorStatus.objects.all()
        
        sensor_data = []
        for sensor in sensors:
            is_alive = sensor.is_alive
            # Update is_online status if it has changed
            if sensor.is_online != is_alive:
                sensor.is_online = is_alive
                sensor.save()
            
            sensor_data.append({
                'sensor_id': sensor.sensor_id,
                'sensor_type': sensor.sensor_type,
                'is_online': is_alive,
                'last_alive_signal': sensor.last_alive_signal.isoformat() if sensor.last_alive_signal else None,
                'seconds_since_last_signal': (current_time - sensor.last_alive_signal).total_seconds() if sensor.last_alive_signal else None,
            })
        
        return Response({
            'sensors': sensor_data,
            'total_sensors': len(sensor_data),
            'online_sensors': len([s for s in sensor_data if s['is_online']]),
            'timestamp': current_time.isoformat()
        }, status=status.HTTP_200_OK)