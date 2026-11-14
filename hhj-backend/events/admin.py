from django.contrib import admin
from .models import Competition, CompetitionStart, Participant


@admin.register(Participant)
class ParticipantAdmin(admin.ModelAdmin):
  list_display = ['name']
  search_fields = ['name']


@admin.register(Competition)
class CompetitionAdmin(admin.ModelAdmin):
  list_display = ['name', 'start_date_time', 'fence_count', 'time_allowed', 'number_of_starts']
  search_fields = ['name']


@admin.register(CompetitionStart)
class CompetitionStartAdmin(admin.ModelAdmin):
  list_display = ['participant', 'horse_name', 'competition', 'starting_order']
  list_filter = ['competition']
  search_fields = ['participant__name', 'horse_name', 'competition__name']
