from django.urls import path
from . import consumers

websocket_urlpatterns = [
  path('ws/live/<uuid:competition_id>/', consumers.LiveConsumer.as_asgi()),
]
