from django.conf import settings
from django.contrib import admin
from django.urls import include, path

import timings.urls

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', include(timings.urls)),
]

if not settings.TESTING:
    from debug_toolbar.toolbar import debug_toolbar_urls
    urlpatterns += debug_toolbar_urls()
