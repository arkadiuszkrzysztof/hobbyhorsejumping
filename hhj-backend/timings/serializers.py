from rest_framework import serializers
from .models import TimeReading


class TimeReadingSerializer(serializers.ModelSerializer):
    class Meta:
        model = TimeReading
        fields = ['id', 'run', 'competition_start', 'time_mark', 'sensor_time', 'server_time',
                  'gate_boot', 'gate_seq', 'gate_time', 'direction', 'gap_ms', 'copies', 'source']
