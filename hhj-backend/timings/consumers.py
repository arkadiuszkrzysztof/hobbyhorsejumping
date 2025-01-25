import json
from channels.generic.websocket import WebsocketConsumer

from asgiref.sync import async_to_sync

from django.core.serializers.json import DjangoJSONEncoder
from .models import TimeReading

LIVE_TIMINGS_LISTENERS_GROUP_NAME = 'live_timings_listeners'

class TimingsConsumer(WebsocketConsumer):
    def connect(self):
        print('Connecting to websocket ' + self.channel_name)
        async_to_sync(self.channel_layer.group_add)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, self.channel_name)
        self.accept()
    def disconnect(self, close_code):
        print('Disconnecting from websocket ' + self.channel_name)
        async_to_sync(self.channel_layer.group_discard)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, self.channel_name)
        self.close()
    def receive(self, text_data):
        data = json.loads(text_data)
        message = data['message']
        async_to_sync(self.channel_layer.group_send)(
            LIVE_TIMINGS_LISTENERS_GROUP_NAME, 
            {
                "type": 'new_timing',
                "message": message
            }
        )
    def send_timings(self, event, type='send_timings'):
        message = event['message']
        self.send(text_data=json.dumps(message, cls=DjangoJSONEncoder))