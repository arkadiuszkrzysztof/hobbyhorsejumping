from django.urls import path, re_path
from . import consumers

websocket_urlpatterns = [
  re_path('ws/timings/', consumers.TimingsConsumer.as_asgi()),
  path('ws/live/<uuid:competition_id>/', consumers.LiveConsumer.as_asgi()),
]
