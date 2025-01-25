import uuid
from django.db import models

class HobbyHorse(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  description = models.TextField(null=True, blank=True)
  is_active = models.BooleanField(default=True)

  class Meta:
    ordering = ['name']
    verbose_name = 'Hobby Horse'
    verbose_name_plural = 'Hobby Horses'

  def __str__(self):
    return self.name

class Participant(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  hobby_horses = models.ManyToManyField(HobbyHorse, null=True, blank=True)
  is_active = models.BooleanField(default=True)

  class Meta:
    ordering = ['name']
    verbose_name = 'Participant'
    verbose_name_plural = 'Participants'

  def all_hobby_horses(self):
    return '(' + ', '.join([hobby_horse.name for hobby_horse in self.hobby_horses.all()]) + ')'

  def __str__(self):
    return self.name + ' (' + ', '.join([hobby_horse.name for hobby_horse in self.hobby_horses.all()]) + ')'

class Judge(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  is_active = models.BooleanField(default=True)

  class Meta:
    ordering = ['name']
    verbose_name = 'Judge'
    verbose_name_plural = 'Judges'

  def __str__(self):
    return self.name

class FenceType(models.TextChoices):
  VERTICAL = 'VERTICAL'
  LENGTH = 'LENGTH'
  OXER = 'OXER'
  TRIPLE = 'TRIPLE'
  WALL = 'WALL'
  WATER_FENCE = 'WATER_FENCE'
  WATER_GRAVE = 'WATER_GRAVE'
  OTHER = 'OTHER'

class Fence(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  height = models.IntegerField()
  type = models.CharField(max_length=100, choices=FenceType.choices)
  course = models.ForeignKey('Course', on_delete=models.CASCADE, related_name='fences')
  is_combination = models.BooleanField(default=False)
  course_order = models.CharField(max_length=100, default='0')
  coordinates = models.CharField(max_length=100, default='0,0')
  angle = models.IntegerField(default=0)

  class Meta:
    ordering = ['height']
    verbose_name = 'Fence'
    verbose_name_plural = 'Fences'

  def __str__(self):
    return f'{self.type} - {self.height}cm'

class Course(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  description = models.TextField(null=True, blank=True)
  plan = models.FileField(null=True, blank=True)

  class Meta:
    ordering = ['name']
    verbose_name = 'Course'
    verbose_name_plural = 'Courses'

  def number_of_fences(self):
    return self.fences.count()

  def __str__(self):
    return self.name

class Competition(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  event = models.ForeignKey('Event', on_delete=models.CASCADE)
  start_date_time = models.DateTimeField()
  arena_size = models.CharField(max_length=100)
  height_class = models.CharField(max_length=100)
  time_limit = models.IntegerField()
  description = models.TextField(null=True, blank=True)
  course = models.ForeignKey(Course, on_delete=models.DO_NOTHING)
  judges = models.ManyToManyField(Judge)

  class Meta:
    ordering = ['-start_date_time']
    verbose_name = 'Competition'
    verbose_name_plural = 'Competitions'

  def number_of_starts(self):
    return self.starts.count()

  def __str__(self):
    return self.name + ' - ' + self.start_date_time.strftime('%Y-%m-%d %H:%M')

class Event(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  name = models.CharField(max_length=100)
  participants = models.ManyToManyField(Participant, null=True, blank=True)
  start_date = models.DateField()
  end_date = models.DateField()
  location = models.CharField(max_length=100)

  class Meta:
    ordering = ['-start_date']
    verbose_name = 'Event'
    verbose_name_plural = 'Events'

  def number_of_participants(self):
    return self.participants.count()
  
  def __str__(self):
    return self.name + ' - ' + self.start_date.strftime('%Y-%m-%d')

class EventStartingNumber(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  number = models.IntegerField()
  participant = models.ForeignKey(Participant, on_delete=models.CASCADE)
  event = models.ForeignKey(Event, on_delete=models.CASCADE)

  class Meta:
    ordering = ['number']
    verbose_name = 'Event Starting Number'
    verbose_name_plural = 'Event Starting Numbers'

  def __str__(self):
    return f'({self.event.name}) {self.number} - {self.participant.name}'

class EliminationType(models.TextChoices):
  SECOND_REFUSAL = 'SECOND_REFUSAL'
  FALLING_OFF = 'FALLING_OFF'
  FALSTART = 'FALSTART'
  EXCEEDING_TIME_TWICE = 'EXCEEDING_TIME_TWICE'
  HAND_OFF_REINS = 'HAND_OFF_REINS'
  HITTING_GROUND = 'HITTING_GROUND'
  CONSTANT_TROTTING = 'CONSTANT_TROTTING'
  WRONG_OBSTACLE = 'WRONG_OBSTACLE'
  DANGEROUS_BEHAVIOUR = 'DANGEROUS_BEHAVIOUR'
  DISREGARDING_JUDGES = 'DISREGARDING_JUDGES'
  UNAUTHORIZED_JUMP = 'UNAUTHORIZED_JUMP'
  LEAVING_ARENA = 'LEAVING_ARENA'
  FAILURE_TO_ARRIVE = 'FAILURE_TO_ARRIVE'
  OTHER = 'OTHER'

class CompetitionStart(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  competition = models.ForeignKey(Competition, on_delete=models.CASCADE, related_name='starts')
  participant = models.ForeignKey(Participant, on_delete=models.CASCADE, related_name='starts')
  starting_order = models.IntegerField(default=0)
  has_started = models.BooleanField(default=False)
  has_finished = models.BooleanField(default=False)
  completion_time = models.TimeField(null=True, blank=True)
  penalty_points = models.IntegerField(default=0)
  is_eliminated = models.BooleanField(default=False)
  elimination_type = models.CharField(max_length=100, choices=EliminationType.choices, null=True, blank=True)
  notes = models.TextField(null=True, blank=True)

  class Meta:
    ordering = ['starting_order']
    verbose_name = 'Competition Start'
    verbose_name_plural = 'Competition Starts'

  def __str__(self):
    return f'{self.participant.name} - {self.competition.name}'

class CompetitionPenaltyType(models.TextChoices):
  DISOBEDIENCE = 'DISOBEDIENCE'
  DROPPING = 'DROPPING'
  FALLING = 'FALLING'
  TIME = 'TIME'
  OTHER = 'OTHER'

class CompetitionPenalty(models.Model):
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  competition = models.ForeignKey(Competition, on_delete=models.CASCADE)
  participant = models.ForeignKey(Participant, on_delete=models.CASCADE)
  fence = models.ForeignKey(Fence, on_delete=models.CASCADE)
  type = models.CharField(max_length=100, choices=CompetitionPenaltyType.choices)
  value = models.IntegerField()

  class Meta:
    ordering = ['fence', 'type']
    verbose_name = 'Competition Penalty'
    verbose_name_plural = 'Competition Penalties'

  def __str__(self):
    return f'{self.participant.name} - {self.competition.name} - {self.fence.type} - {self.type} - {self.value}'

class TypesTranslation(models.Model):
  class Language(models.TextChoices):
    ENGLISH = 'ENGLISH'
    POLISH = 'POLISH'
  id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
  translation = models.TextField()
  language = models.CharField(max_length=100, choices=Language.choices)
  translation_key = models.CharField(max_length=100, choices=FenceType.choices + EliminationType.choices + CompetitionPenaltyType.choices)

  class Meta:
    ordering = ['translation_key', 'language']
    verbose_name = 'Types Translation'
    verbose_name_plural = 'Types Translations'

  def __str__(self):
    return f'{self.translation_key} - {self.language} - {self.translation}'