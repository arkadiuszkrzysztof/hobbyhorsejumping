import json
from channels.generic.websocket import WebsocketConsumer
from asgiref.sync import async_to_sync
from django.core.serializers.json import DjangoJSONEncoder
from django.utils import timezone
from .models import TimeReading
from events.models import CompetitionStart, Competition
from events.serializers import CompetitionStartSerializer, CompetitionSerializer

LIVE_TIMINGS_LISTENERS_GROUP_NAME = 'live_timings_listeners'
DASHBOARD_GROUP_NAME = 'dashboard_listeners'

class TimingsConsumer(WebsocketConsumer):
    def connect(self):
        print(f'Connecting to websocket {self.channel_name}')
        # Join both groups for backward compatibility
        async_to_sync(self.channel_layer.group_add)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, self.channel_name)
        async_to_sync(self.channel_layer.group_add)(DASHBOARD_GROUP_NAME, self.channel_name)
        self.accept()
        
        # Send current state on connection
        self.send_current_state()
    
    def disconnect(self, close_code):
        print(f'Disconnecting from websocket {self.channel_name}')
        async_to_sync(self.channel_layer.group_discard)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, self.channel_name)
        async_to_sync(self.channel_layer.group_discard)(DASHBOARD_GROUP_NAME, self.channel_name)
        self.close()
    
    def receive(self, text_data):
        try:
            data = json.loads(text_data)
            message_type = data.get('type', 'timing')
            
            if message_type == 'request_state':
                self.send_current_state()
            else:
                # Legacy timing message handling
                message = data.get('message', data)
                async_to_sync(self.channel_layer.group_send)(
                    LIVE_TIMINGS_LISTENERS_GROUP_NAME, 
                    {
                        "type": 'new_timing',
                        "message": message
                    }
                )
        except json.JSONDecodeError:
            print(f"Invalid JSON received: {text_data}")
    
    def send_current_state(self):
        """Send current competition state to the client"""
        try:
            # Get active competitor
            from timings.views import get_active_competitor_instance
            active_competitor = get_active_competitor_instance()
            
            # Get all competitions with their starts
            competitions = Competition.objects.prefetch_related('starts__participant__hobby_horses').all()
            
            state_data = {
                'type': 'state_update',
                'active_competitor': CompetitionStartSerializer(active_competitor).data if active_competitor else None,
                'competitions': CompetitionSerializer(competitions, many=True).data,
                'timestamp': json.dumps(timezone.now(), cls=DjangoJSONEncoder)
            }
            
            self.send(text_data=json.dumps(state_data, cls=DjangoJSONEncoder))
        except Exception as e:
            print(f"Error sending current state: {e}")
    
    def send_timings(self, event):
        """Handle timing messages"""
        message = event['message']
        self.send(text_data=json.dumps({
            'type': 'timing',
            'data': message
        }, cls=DjangoJSONEncoder))
    
    def new_timing(self, event):
        """Handle new timing messages"""
        self.send_timings(event)
    
    def state_change(self, event):
        """Handle state change broadcasts"""
        self.send(text_data=json.dumps(event['data'], cls=DjangoJSONEncoder))
    
    def competition_update(self, event):
        """Handle competition updates (competitor selection, reordering, etc.)"""
        self.send(text_data=json.dumps({
            'type': 'competition_update',
            'data': event['data']
        }, cls=DjangoJSONEncoder))

# Add utility function to broadcast state changes
def broadcast_state_change(change_type, data):
    """Broadcast state changes to all connected clients"""
    from channels.layers import get_channel_layer
    channel_layer = get_channel_layer()
    
    async_to_sync(channel_layer.group_send)(
        DASHBOARD_GROUP_NAME,
        {
            'type': 'state_change',
            'data': {
                'type': change_type,
                'data': data,
                'timestamp': json.dumps(timezone.now(), cls=DjangoJSONEncoder)
            }
        }
    )