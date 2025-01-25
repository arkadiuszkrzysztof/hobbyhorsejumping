from django.urls import path
from . import views


urlpatterns = [
    path('timereadings/', views.TimeReadingList.as_view(), name='time_reading_list'),
]