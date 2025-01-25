from django.contrib import admin
from .models import TimeReading

# Register your models here.
@admin.register(TimeReading)
class TimeReadingAdmin(admin.ModelAdmin):
  list_display = ['competition_start', 'sensor_time', 'server_time', 'time_mark']
  list_filter = ['time_mark']
  ordering = ['-server_time']