import os

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "adminportal.settings")

from django.core.asgi import get_asgi_application  # noqa: E402

# Set up Django (apps, models) before importing anything that uses models.
django_asgi_app = get_asgi_application()

from channels.routing import ProtocolTypeRouter, URLRouter  # noqa: E402

import timings.routing  # noqa: E402
from timings.live import CaptureLoop  # noqa: E402

application = CaptureLoop(ProtocolTypeRouter({
    "http": django_asgi_app,
    "websocket": URLRouter(timings.routing.websocket_urlpatterns),
}))
