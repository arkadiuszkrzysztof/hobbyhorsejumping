import json

from asgiref.sync import async_to_sync
from channels.generic.websocket import WebsocketConsumer
from django.core.serializers.json import DjangoJSONEncoder

from . import live
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


class LiveConsumer(WebsocketConsumer):
    """Operator console: the state of one competition on connect and after
    every change, device status every 2 s (API.md). Server to client only."""

    def connect(self):
        from events.models import Competition
        self.competition_id = str(self.scope['url_route']['kwargs']['competition_id'])
        competition = Competition.objects.filter(pk=self.competition_id).first()
        if competition is None:
            self.close(code=4404)
            return
        async_to_sync(self.channel_layer.group_add)(live.live_group(self.competition_id), self.channel_name)
        async_to_sync(self.channel_layer.group_add)(live.LIVE_ALL_GROUP, self.channel_name)
        live.watch(self.competition_id, +1)
        self.watching = True
        self.accept()
        self.send(text_data=live.state_text(competition))

    def disconnect(self, close_code):
        if getattr(self, 'watching', False):
            live.watch(self.competition_id, -1)
            async_to_sync(self.channel_layer.group_discard)(live.live_group(self.competition_id), self.channel_name)
            async_to_sync(self.channel_layer.group_discard)(live.LIVE_ALL_GROUP, self.channel_name)

    def receive(self, text_data=None, bytes_data=None):
        pass

    def live_message(self, event):
        self.send(text_data=event['text'])
