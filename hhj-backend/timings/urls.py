from django.urls import path
from . import views


urlpatterns = [
    path('timereadings/', views.TimeReadingList.as_view(), name='time_reading_list'),
    path('devices/', views.DeviceList.as_view(), name='device_list'),
]
