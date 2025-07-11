import uuid
from django.db import models
from django.utils import timezone
from events.models import CompetitionStart

class TimeReading(models.Model):
  class TimeMark(models.TextChoices):
    START = 'START'
    FINISH = 'FINISH'
    COMBINED = 'COMBINED'
    FAULTY_READING = 'FAULTY_READING'
    ALIVE = 'ALIVE'
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  competition_start = models.ForeignKey(CompetitionStart, on_delete=models.CASCADE, null=True, blank=True)
  sensor_time = models.IntegerField()
  server_time = models.IntegerField()
  time_mark = models.CharField(max_length=100, choices=TimeMark.choices)
  sensor_id = models.CharField(max_length=100, null=True, blank=True)

  class Meta:
    ordering = ['-server_time']
    verbose_name = 'Time Reading'
    verbose_name_plural = 'Time Readings'

  def __str__(self):
    if self.competition_start:
      return f'{self.time_mark} - {self.sensor_time} ({self.competition_start.competition.name} - {self.competition_start.participant.name})'
    return f'{self.time_mark} - {self.sensor_time} (Sensor: {self.sensor_id})'

class SensorStatus(models.Model):
  class SensorType(models.TextChoices):
    START_SENSOR = 'START_SENSOR'
    FINISH_SENSOR = 'FINISH_SENSOR'
  
  sensor_id = models.CharField(max_length=100, unique=True)
  sensor_type = models.CharField(max_length=20, choices=SensorType.choices)
  last_alive_signal = models.DateTimeField()
  is_online = models.BooleanField(default=False)
  created_at = models.DateTimeField(auto_now_add=True)
  updated_at = models.DateTimeField(auto_now=True)

  class Meta:
    ordering = ['sensor_type', 'sensor_id']
    verbose_name = 'Sensor Status'
    verbose_name_plural = 'Sensor Statuses'

  def __str__(self):
    status = "Online" if self.is_online else "Offline"
    return f'{self.sensor_id} ({self.sensor_type}) - {status}'

  @property
  def is_alive(self):
    """Check if sensor is considered alive (received signal in last 30 seconds)"""
    if not self.last_alive_signal:
      return False
    return (timezone.now() - self.last_alive_signal).total_seconds() < 30