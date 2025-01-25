# Generated by Django 5.1.3 on 2024-11-11 15:36

import django.db.models.deletion
import uuid
from django.db import migrations, models


class Migration(migrations.Migration):

    initial = True

    dependencies = [
        ('events', '0002_fence_angle_fence_coordinates_competitionstart_and_more'),
    ]

    operations = [
        migrations.CreateModel(
            name='TimeReading',
            fields=[
                ('id', models.UUIDField(default=uuid.uuid4, editable=False, primary_key=True, serialize=False)),
                ('sensor_time', models.TimeField()),
                ('server_time', models.TimeField()),
                ('time_mark', models.CharField(choices=[('START', 'Start'), ('FINISH', 'Finish'), ('FAULTY_READING', 'Faulty Reading')], max_length=100)),
                ('competition_start', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.competitionstart')),
            ],
        ),
    ]
