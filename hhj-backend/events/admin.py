from django.contrib import admin
from .models import Competition, CompetitionPenalty, CompetitionStart, Course, EventStartingNumber, Fence, HobbyHorse, Judge, Participant, Event, TypesTranslation

@admin.register(HobbyHorse)
class HobbyHorseAdmin(admin.ModelAdmin):
  list_display = ['name', 'description', 'is_active']
  list_filter = ['is_active']
  search_fields = ['name']

@admin.register(Participant)
class ParticipantAdmin(admin.ModelAdmin):
  list_display = ['name', 'all_hobby_horses', 'is_active']
  list_filter = ['is_active']
  search_fields = ['name']

@admin.register(Judge)
class JudgeAdmin(admin.ModelAdmin):
  list_display = ['name', 'is_active']
  list_filter = ['is_active']
  search_fields = ['name']

@admin.register(Fence)
class FenceAdmin(admin.ModelAdmin):
  list_display = ['height', 'type', 'course', 'course_order', 'is_combination']
  list_filter = ['course', 'type', 'height']
  search_fields = ['course', 'type', 'height']

@admin.register(Course)
class CourseAdmin(admin.ModelAdmin):
  list_display = ['name', 'number_of_fences']
  list_filter = ['name']
  search_fields = ['name']

@admin.register(Competition)
class CompetitionAdmin(admin.ModelAdmin):
  list_display = ['name', 'start_date_time', 'event', 'arena_size', 'height_class', 'course', 'number_of_starts']
  list_filter = ['height_class', 'course', 'event']
  search_fields = ['name', 'height_class', 'event']

@admin.register(Event)
class EventAdmin(admin.ModelAdmin):
  list_display = ['name', 'location', 'number_of_participants']
  list_filter = ['name', 'location']
  search_fields = ['name']

@admin.register(EventStartingNumber)
class EventStartingNumberAdmin(admin.ModelAdmin):
  list_display = ['event', 'participant', 'number']
  list_filter = ['event', 'participant']
  search_fields = ['event', 'participant']

@admin.register(CompetitionStart)
class CompetitionStartAdmin(admin.ModelAdmin):
  list_display = ['participant', 'competition', 'starting_order', 'has_started', 'has_finished', 'completion_time', 'penalty_points', 'is_eliminated']
  list_filter = ['participant', 'competition']
  search_fields = ['participant', 'competition']

@admin.register(CompetitionPenalty)
class CompetitionPenaltyAdmin(admin.ModelAdmin):
  list_display = ['competition', 'participant', 'fence', 'type', 'value']
  list_filter = ['competition', 'type', 'participant']
  search_fields = ['competition', 'participant']

@admin.register(TypesTranslation)
class TypesTranslationAdmin(admin.ModelAdmin):
  list_display = ['translation_key', 'language', 'translation']
  list_filter = ['language']
  search_fields = ['translation_key', 'translation']