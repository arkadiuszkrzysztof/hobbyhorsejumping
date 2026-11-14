import uuid
from django.db import models
from events.models import CompetitionStart


class TimeReading(models.Model):
  """One beam cut. Times are epoch milliseconds on the server clock.

  sensor_time: best estimate of the cut = min over received copies of
               (arrival - age_ms).
  server_time: arrival of the first copy.
  A gate event is identified by (time_mark, gate_boot, gate_seq). Those two
  are NULL only in rows stored by the first version of the system.
  """
  class TimeMark(models.TextChoices):
    START = 'START'
    FINISH = 'FINISH'
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  # the run this signal was captured for (operator console); null = nobody was being timed
  run = models.ForeignKey('Run', null=True, blank=True, on_delete=models.SET_NULL, related_name='readings')
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
    return f'{self.time_mark} - {self.sensor_time}'


class Run(models.Model):
  """One try of one competitor (an entry = events.CompetitionStart).

  OPEN while the operator works on it - that is "the session"; there is at
  most one OPEN run in the whole system. Gate signals are attached to it
  (TimeReading.run) while `capturing` is on. Of those signals one START and
  one FINISH count: the operator's picks if set, otherwise the first START
  and the last FINISH after it (timings/arena.py). Saving freezes the picks
  and `time_ms`.
  """
  class Status(models.TextChoices):
    OPEN = 'OPEN'
    SAVED = 'SAVED'

  class Outcome(models.TextChoices):
    FINISHED = 'FINISHED'
    RETIRED = 'RETIRED'        # the competitor gave up on the way
    ELIMINATED = 'ELIMINATED'  # the round was ended for them, e.g. after a fall
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  entry = models.ForeignKey(CompetitionStart, on_delete=models.CASCADE, related_name='runs')
  number = models.PositiveSmallIntegerField()   # 1 = first try of this competitor, 2 = second ...
  status = models.CharField(max_length=10, choices=Status.choices, default=Status.OPEN)
  capturing = models.BooleanField(default=False)
  opened_ms = models.BigIntegerField()                          # epoch ms when the session was opened
  capture_from = models.BigIntegerField(null=True, blank=True)  # epoch ms, latest "start registering"
  capture_to = models.BigIntegerField(null=True, blank=True)    # epoch ms, latest "stop registering"; null while on
  start_pick = models.ForeignKey(TimeReading, null=True, blank=True, on_delete=models.SET_NULL, related_name='+')
  finish_pick = models.ForeignKey(TimeReading, null=True, blank=True, on_delete=models.SET_NULL, related_name='+')
  penalties = models.JSONField(default=list, blank=True)        # knocked-down fences, by label: ["7", "9B"]
  refusals = models.JSONField(default=dict, blank=True)         # refusals per fence number: {"5": 2}
  # how a saved run ended; a retired or eliminated one has no time, and may say at which fence
  outcome = models.CharField(max_length=12, choices=Outcome.choices, default=Outcome.FINISHED)
  outcome_fence = models.CharField(max_length=3, blank=True, default='')
  time_ms = models.BigIntegerField(null=True, blank=True)       # the result; set when saved
  saved_at = models.DateTimeField(null=True, blank=True)

  class Meta:
    ordering = ['entry', 'number']
    constraints = [
      models.UniqueConstraint(fields=['status'], condition=models.Q(status='OPEN'), name='one_open_run'),
    ]

  def __str__(self):
    time = '-' if self.time_ms is None else f'{self.time_ms / 1000:.2f} s'
    return f'{self.entry.participant.name} #{self.number}: {time} ({self.status})'
