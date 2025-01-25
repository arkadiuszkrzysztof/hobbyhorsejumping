import uuid
from django.db import models
from events.models import CompetitionStart

class TimeReading(models.Model):
  class TimeMark(models.TextChoices):
    START = 'START'
    FINISH = 'FINISH'
    COMBINED = 'COMBINED'
    FAULTY_READING = 'FAULTY_READING'
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  competition_start = models.ForeignKey(CompetitionStart, on_delete=models.CASCADE)
  sensor_time = models.IntegerField()
  server_time = models.IntegerField()
  time_mark = models.CharField(max_length=100, choices=TimeMark.choices)

  class Meta:
    ordering = ['-server_time']
    verbose_name = 'Time Reading'
    verbose_name_plural = 'Time Readings'

  def __str__(self):
    return f'{self.time_mark} - {self.sensor_time} ({self.competition_start.competition.name} - {self.competition_start.participant.name})'