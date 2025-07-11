from django.shortcuts import render
from django.utils import timezone
from rest_framework import status
from rest_framework.response import Response
from rest_framework.views import APIView
from rest_framework import generics
from .models import Competition, CompetitionStart, Participant, Event
from .serializers import CompetitionSerializer, CompetitionStartSerializer, ParticipantSerializer

class CompetitionListView(generics.ListAPIView):
    queryset = Competition.objects.filter(start_date_time__date__gte='2024-01-01')
    serializer_class = CompetitionSerializer

class CompetitionStartListView(generics.ListAPIView):
    serializer_class = CompetitionStartSerializer
    
    def get_queryset(self):
        competition_id = self.request.query_params.get('competition_id')
        if competition_id:
            return CompetitionStart.objects.filter(competition_id=competition_id)
        return CompetitionStart.objects.all()

class ActiveCompetitionStartView(APIView):
    """Get or set the currently active competition start"""
    
    def get(self, request):
        # Get the currently active competition start (the one that's started but not finished)
        active_start = CompetitionStart.objects.filter(
            has_started=True, 
            has_finished=False
        ).first()
        
        if active_start:
            serializer = CompetitionStartSerializer(active_start)
            return Response(serializer.data)
        else:
            return Response({'active_competitor': None})
    
    def post(self, request):
        # Set a competition start as active
        competition_start_id = request.data.get('competition_start_id')
        
        try:
            # Reset any currently active starts
            CompetitionStart.objects.filter(has_started=True, has_finished=False).update(
                has_started=False
            )
            
            # Set the new active start
            active_start = CompetitionStart.objects.get(id=competition_start_id)
            active_start.has_started = True
            active_start.has_finished = False
            active_start.save()
            
            serializer = CompetitionStartSerializer(active_start)
            
            # Broadcast the active competitor change to all connected clients
            from timings.consumers import broadcast_state_change
            broadcast_state_change('active_competitor_change', {
                'active_competitor': serializer.data
            })
            
            return Response(serializer.data)
            
        except CompetitionStart.DoesNotExist:
            return Response(
                {'error': 'Competition start not found'}, 
                status=status.HTTP_404_NOT_FOUND
            )

class ReorderCompetitorsView(APIView):
    """Reorder competitors in a competition starting list"""
    
    def post(self, request):
        competition_id = request.data.get('competition_id')
        competitor_orders = request.data.get('competitor_orders')  # List of {id, starting_order}
        
        try:
            competition = Competition.objects.get(id=competition_id)
            
            # Update starting orders
            for item in competitor_orders:
                CompetitionStart.objects.filter(
                    id=item['id'],
                    competition=competition
                ).update(starting_order=item['starting_order'])
            
            # Get updated competition starts
            updated_starts = CompetitionStart.objects.filter(
                competition=competition
            ).order_by('starting_order')
            
            serializer = CompetitionStartSerializer(updated_starts, many=True)
            
            # Broadcast the reordering to all connected clients
            from timings.consumers import broadcast_state_change
            broadcast_state_change('competitor_reorder', {
                'competition_id': competition_id,
                'competitors': serializer.data
            })
            
            return Response(serializer.data)
            
        except Competition.DoesNotExist:
            return Response(
                {'error': 'Competition not found'}, 
                status=status.HTTP_404_NOT_FOUND
            )
        except Exception as e:
            return Response(
                {'error': str(e)}, 
                status=status.HTTP_400_BAD_REQUEST
            )

class PublicDisplayView(APIView):
    """Get public display data (rankings, current competitor, etc.)"""
    
    def get(self, request):
        # Get active competitor
        active_start = CompetitionStart.objects.filter(
            has_started=True, 
            has_finished=False
        ).first()
        
        # Get all competitions with their results ordered by completion time and penalties
        competitions_data = []
        
        for competition in Competition.objects.all():
            # Get finished competitors ordered by completion time and penalties
            finished_competitors = CompetitionStart.objects.filter(
                competition=competition,
                has_finished=True
            ).order_by('completion_time', 'penalty_points')
            
            # Get all competitors ordered by starting order
            all_competitors = CompetitionStart.objects.filter(
                competition=competition
            ).order_by('starting_order')
            
            competitions_data.append({
                'competition': CompetitionSerializer(competition).data,
                'rankings': CompetitionStartSerializer(finished_competitors, many=True).data,
                'all_competitors': CompetitionStartSerializer(all_competitors, many=True).data
            })
        
        return Response({
            'active_competitor': CompetitionStartSerializer(active_start).data if active_start else None,
            'competitions': competitions_data,
            'timestamp': timezone.now()
        })

class ReconcileSignalsView(APIView):
    """Reconcile timing signals for a competitor"""
    
    def post(self, request):
        competitor_id = request.data.get('competitor_id')
        start_signal = request.data.get('start_signal')
        finish_signal = request.data.get('finish_signal')
        
        try:
            # Get the competitor
            competitor_start = CompetitionStart.objects.get(id=competitor_id)
            
            # Calculate the final time
            start_time = start_signal['sensor_time']
            finish_time = finish_signal['sensor_time']
            completion_time = finish_time - start_time
            
            # Update the competitor record
            competitor_start.completion_time = completion_time
            competitor_start.has_started = True
            competitor_start.has_finished = True
            competitor_start.save()
            
            # Store the reconciliation record for audit trail
            # (You might want to create a separate model for this)
            
            # Broadcast the update
            from timings.consumers import broadcast_state_change
            broadcast_state_change('timing_reconciliation', {
                'competitor_id': competitor_id,
                'start_time': start_time,
                'finish_time': finish_time,
                'completion_time': completion_time,
                'reconciliation_timestamp': timezone.now()
            })
            
            serializer = CompetitionStartSerializer(competitor_start)
            return Response({
                'competitor': serializer.data,
                'reconciliation': {
                    'start_time': start_time,
                    'finish_time': finish_time,
                    'completion_time': completion_time
                }
            })
            
        except CompetitionStart.DoesNotExist:
            return Response(
                {'error': 'Competitor not found'}, 
                status=status.HTTP_404_NOT_FOUND
            )
        except Exception as e:
            return Response(
                {'error': str(e)}, 
                status=status.HTTP_400_BAD_REQUEST
            )

class PauseResumeTimerView(APIView):
    """Pause or resume the timer for the active competitor"""
    
    def post(self, request):
        action = request.data.get('action')  # 'pause' or 'resume'
        current_time = request.data.get('current_time')  # Current server time in milliseconds
        
        try:
            # Get the currently active competition start
            active_start = CompetitionStart.objects.filter(
                has_started=True, 
                has_finished=False
            ).first()
            
            if not active_start:
                return Response(
                    {'error': 'No active competitor found'}, 
                    status=status.HTTP_400_BAD_REQUEST
                )
            
            if action == 'pause':
                if not active_start.is_paused:
                    active_start.is_paused = True
                    active_start.pause_start_time = current_time
                    active_start.save()
                    
                    # Broadcast pause state
                    from timings.consumers import broadcast_state_change
                    broadcast_state_change('timer_paused', {
                        'active_competitor': CompetitionStartSerializer(active_start).data,
                        'pause_start_time': current_time
                    })
                    
                    return Response({'status': 'paused'})
                else:
                    return Response({'error': 'Timer is already paused'}, status=status.HTTP_400_BAD_REQUEST)
                    
            elif action == 'resume':
                if active_start.is_paused and active_start.pause_start_time:
                    # Calculate pause duration and add to total
                    pause_duration = current_time - active_start.pause_start_time
                    active_start.total_pause_time += pause_duration
                    active_start.is_paused = False
                    active_start.pause_start_time = None
                    active_start.save()
                    
                    # Broadcast resume state
                    from timings.consumers import broadcast_state_change
                    broadcast_state_change('timer_resumed', {
                        'active_competitor': CompetitionStartSerializer(active_start).data,
                        'total_pause_time': active_start.total_pause_time
                    })
                    
                    return Response({'status': 'resumed', 'total_pause_time': active_start.total_pause_time})
                else:
                    return Response({'error': 'Timer is not paused'}, status=status.HTTP_400_BAD_REQUEST)
            else:
                return Response({'error': 'Invalid action'}, status=status.HTTP_400_BAD_REQUEST)
                
        except Exception as e:
            return Response(
                {'error': str(e)}, 
                status=status.HTTP_500_INTERNAL_SERVER_ERROR
            )

class ManualTimingView(APIView):
    """Manually trigger start or finish timing via keyboard"""
    
    def post(self, request):
        action = request.data.get('action')  # 'start' or 'finish'
        current_time = request.data.get('current_time')  # Current server time in milliseconds
        
        try:
            if action == 'start':
                # Get the currently active competition start
                active_start = CompetitionStart.objects.filter(
                    has_started=True, 
                    has_finished=False
                ).first()
                
                if not active_start:
                    return Response(
                        {'error': 'No active competitor selected'}, 
                        status=status.HTTP_400_BAD_REQUEST
                    )
                
                # Create a manual START time reading
                from timings.models import TimeReading
                time_reading = TimeReading.objects.create(
                    competition_start=active_start,
                    sensor_time=current_time,
                    server_time=current_time,
                    time_mark=TimeReading.TimeMark.START,
                    sensor_id='MANUAL_KEYBOARD'
                )
                
                # Broadcast the timing signal
                from timings.consumers import broadcast_timing_data
                broadcast_timing_data({
                    'time_mark': 'START',
                    'sensor_time': current_time,
                    'server_time': current_time,
                    'sensor_id': 'MANUAL_KEYBOARD',
                    'competition_start_id': str(active_start.id)
                })
                
                return Response({'status': 'start_triggered', 'time': current_time})
                
            elif action == 'finish':
                # Get the currently active competition start
                active_start = CompetitionStart.objects.filter(
                    has_started=True, 
                    has_finished=False
                ).first()
                
                if not active_start:
                    return Response(
                        {'error': 'No active competitor found'}, 
                        status=status.HTTP_400_BAD_REQUEST
                    )
                
                # Create a manual FINISH time reading
                from timings.models import TimeReading
                time_reading = TimeReading.objects.create(
                    competition_start=active_start,
                    sensor_time=current_time,
                    server_time=current_time,
                    time_mark=TimeReading.TimeMark.FINISH,
                    sensor_id='MANUAL_KEYBOARD'
                )
                
                # Broadcast the timing signal
                from timings.consumers import broadcast_timing_data
                broadcast_timing_data({
                    'time_mark': 'FINISH',
                    'sensor_time': current_time,
                    'server_time': current_time,
                    'sensor_id': 'MANUAL_KEYBOARD',
                    'competition_start_id': str(active_start.id)
                })
                
                return Response({'status': 'finish_triggered', 'time': current_time})
            else:
                return Response({'error': 'Invalid action'}, status=status.HTTP_400_BAD_REQUEST)
                
        except Exception as e:
            return Response(
                {'error': str(e)}, 
                status=status.HTTP_500_INTERNAL_SERVER_ERROR
            )

# Create your views here.
