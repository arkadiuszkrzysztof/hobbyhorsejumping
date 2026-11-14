from django.contrib import admin
from .models import TimeReading


@admin.register(TimeReading)
class TimeReadingAdmin(admin.ModelAdmin):
  list_display = ['time_mark', 'sensor_time', 'server_time', 'gate_boot', 'gate_seq',
                  'direction', 'gap_ms', 'copies', 'source', 'competition_start']
  list_filter = ['time_mark', 'source']
  ordering = ['-server_time']
