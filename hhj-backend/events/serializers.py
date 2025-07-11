from rest_framework import serializers
from .models import (
    Competition, CompetitionStart, Participant, Event, 
    HobbyHorse, Judge, Course
)

class HobbyHorseSerializer(serializers.ModelSerializer):
    class Meta:
        model = HobbyHorse
        fields = ['id', 'name', 'description']

class ParticipantSerializer(serializers.ModelSerializer):
    hobby_horses = HobbyHorseSerializer(many=True, read_only=True)
    
    class Meta:
        model = Participant
        fields = ['id', 'name', 'hobby_horses']

class JudgeSerializer(serializers.ModelSerializer):
    class Meta:
        model = Judge
        fields = ['id', 'name']

class CourseSerializer(serializers.ModelSerializer):
    class Meta:
        model = Course
        fields = ['id', 'name', 'description']

class EventSerializer(serializers.ModelSerializer):
    class Meta:
        model = Event
        fields = ['id', 'name', 'start_date', 'end_date', 'location']

class CompetitionSerializer(serializers.ModelSerializer):
    event = EventSerializer(read_only=True)
    judges = JudgeSerializer(many=True, read_only=True)
    course = CourseSerializer(read_only=True)
    
    class Meta:
        model = Competition
        fields = [
            'id', 'name', 'event', 'start_date_time', 'arena_size', 
            'height_class', 'time_limit', 'description', 'course', 'judges'
        ]

class CompetitionStartSerializer(serializers.ModelSerializer):
    participant = ParticipantSerializer(read_only=True)
    competition = CompetitionSerializer(read_only=True)
    
    class Meta:
        model = CompetitionStart
        fields = [
            'id', 'competition', 'participant', 'starting_order',
            'has_started', 'has_finished', 'completion_time', 
            'penalty_points', 'is_eliminated', 'elimination_type', 'notes'
        ]
