from django.urls import path
from . import views


urlpatterns = [
    path('timereadings/', views.TimeReadingList.as_view(), name='time_reading_list'),
    path('test/', views.TestAPIView.as_view(), name='test_api'),
    path('sensor-status/', views.SensorStatusView.as_view(), name='sensor_status'),
]