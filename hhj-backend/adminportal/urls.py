from django.contrib import admin
from django.urls import include, path

import timings.urls

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', include(timings.urls)),
]
