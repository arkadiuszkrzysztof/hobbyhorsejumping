from django.urls import path
from . import api, views


urlpatterns = [
    path('timereadings/', views.TimeReadingList.as_view(), name='time_reading_list'),
    path('devices/', views.DeviceList.as_view(), name='device_list'),

    # operator console (API.md)
    path('api/competitions/', api.competitions),
    path('api/competitions/<uuid:cid>/', api.competition),
    path('api/competitions/<uuid:cid>/state/', api.state),
    path('api/competitions/<uuid:cid>/entries/', api.entries),
    path('api/competitions/<uuid:cid>/order/', api.order),
    path('api/competitions/<uuid:cid>/select/', api.select),
    path('api/competitions/<uuid:cid>/capture/', api.capture),
    path('api/entries/<uuid:eid>/', api.entry),
    path('api/runs/<uuid:rid>/', api.run),
    path('api/runs/<uuid:rid>/penalty/', api.penalty),
    path('api/runs/<uuid:rid>/pick/', api.pick),
    path('api/runs/<uuid:rid>/save/', api.save),
    path('api/runs/<uuid:rid>/discard/', api.discard),
    path('api/runs/<uuid:rid>/reopen/', api.reopen),
]
