import uuid
from django.db import models
from events.models import CompetitionStart


class TimeReading(models.Model):
  """One beam cut. Times are epoch milliseconds on the server clock.

  sensor_time: best estimate of the cut = min over received copies of
               (arrival - age_ms).
  server_time: arrival of the first copy.
  Gate events are identified by (time_mark, gate_boot, gate_seq); manual /
  legacy HTTP readings have no identity (gate_boot/gate_seq are NULL).
  """
  class TimeMark(models.TextChoices):
    START = 'START'
    FINISH = 'FINISH'
    COMBINED = 'COMBINED'
    FAULTY_READING = 'FAULTY_READING'
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  competition_start = models.ForeignKey(CompetitionStart, null=True, blank=True, on_delete=models.SET_NULL)
  sensor_time = models.BigIntegerField()
  server_time = models.BigIntegerField()
  time_mark = models.CharField(max_length=100, choices=TimeMark.choices)
  gate_boot = models.BigIntegerField(null=True, blank=True)
  gate_seq = models.BigIntegerField(null=True, blank=True)
  gate_time = models.BigIntegerField(null=True, blank=True)
  direction = models.SmallIntegerField(default=0)
  gap_ms = models.IntegerField(default=0)
  copies = models.IntegerField(default=1)
  source = models.CharField(max_length=45, blank=True, default='')

  class Meta:
    ordering = ['-server_time']
    verbose_name = 'Time Reading'
    verbose_name_plural = 'Time Readings'
    constraints = [
      models.UniqueConstraint(fields=['time_mark', 'gate_boot', 'gate_seq'], name='uniq_gate_event'),
    ]

  def __str__(self):
    text = f'{self.time_mark} - {self.sensor_time}'
    cs = self.competition_start
    if cs is not None:
      text += f' ({cs.competition.name} - {cs.participant.name})'
    return text
