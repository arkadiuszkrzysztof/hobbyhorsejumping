from rest_framework import serializers
from .models import TimeReading

class TimeReadingSerializer(serializers.ModelSerializer):
    class Meta:
        model = TimeReading
        fields = '__all__'
