import uuid
from django.db import models
from django.utils import timezone


class Participant(models.Model):
  """A kid. The same Participant can be entered in several competitions."""
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)

  class Meta:
    ordering = ['name']

  def __str__(self):
    return self.name


class Competition(models.Model):
  """The master record of one competition.

  Its competitors are the CompetitionStart rows (`starts`), in
  `starting_order`; each of them can have several timings.Run.
  """
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  start_date_time = models.DateTimeField(default=timezone.now)
  fence_count = models.PositiveSmallIntegerField(default=12)
  # the competitor highlighted in the operator console: who rides next
  selected_start = models.ForeignKey('CompetitionStart', on_delete=models.SET_NULL, null=True, blank=True,
                                     related_name='+')

  class Meta:
    ordering = ['-start_date_time']

  def number_of_starts(self):
    return self.starts.count()

  def __str__(self):
    return self.name + ' - ' + self.start_date_time.strftime('%Y-%m-%d %H:%M')


class CompetitionStart(models.Model):
  """One competitor signed up for one competition (an "entry" in the console)."""
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  competition = models.ForeignKey(Competition, on_delete=models.CASCADE, related_name='starts')
  participant = models.ForeignKey(Participant, on_delete=models.CASCADE, related_name='starts')
  # the hobby horse ridden in this competition, as typed at sign-up; optional
  horse_name = models.CharField(max_length=100, blank=True, default='')
  starting_order = models.IntegerField(default=0)

  class Meta:
    ordering = ['starting_order']
    verbose_name = 'Competitor'

  def __str__(self):
    return f'{self.participant.name} - {self.competition.name}'
