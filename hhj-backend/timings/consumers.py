import json

from asgiref.sync import async_to_sync
from channels.generic.websocket import WebsocketConsumer
from django.core.serializers.json import DjangoJSONEncoder

from .live import LIVE_TIMINGS_LISTENERS_GROUP_NAME


class TimingsConsumer(WebsocketConsumer):
    """Pushes every new TimeReading (flat serialized object) to the client."""

    def connect(self):
        async_to_sync(self.channel_layer.group_add)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, self.channel_name)
        self.accept()

    def disconnect(self, close_code):
        async_to_sync(self.channel_layer.group_discard)(LIVE_TIMINGS_LISTENERS_GROUP_NAME, self.channel_name)

    def receive(self, text_data=None, bytes_data=None):
        # The channel is server -> client only; client messages are ignored.
        pass

    def send_timings(self, event):
        self.send(text_data=json.dumps(event['message'], cls=DjangoJSONEncoder))
