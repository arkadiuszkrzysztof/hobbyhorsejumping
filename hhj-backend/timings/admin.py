from django.contrib import admin
from .models import Run, TimeReading


@admin.register(TimeReading)
class TimeReadingAdmin(admin.ModelAdmin):
  list_display = ['time_mark', 'sensor_time', 'server_time', 'run', 'gate_boot', 'gate_seq',
                  'direction', 'gap_ms', 'copies', 'source']
  list_filter = ['time_mark', 'source']
  ordering = ['-server_time']


@admin.register(Run)
class RunAdmin(admin.ModelAdmin):
  list_display = ['entry', 'number', 'status', 'outcome', 'time_ms', 'penalties', 'refusals', 'capturing', 'saved_at']
  list_filter = ['status', 'entry__competition']
  ordering = ['-opened_ms']
