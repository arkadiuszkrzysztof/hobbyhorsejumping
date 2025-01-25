# Generated by Django 5.1.2 on 2024-11-05 11:10

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    initial = True

    dependencies = [
    ]

    operations = [
        migrations.CreateModel(
            name='Course',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=100)),
                ('description', models.TextField()),
                ('plan', models.FileField(upload_to='')),
            ],
        ),
        migrations.CreateModel(
            name='Fence',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('height', models.IntegerField()),
                ('type', models.CharField(choices=[('VERTICAL', 'Vertical'), ('LENGTH', 'Length'), ('OXER', 'Oxer'), ('TRIPLE', 'Triple'), ('WALL', 'Wall'), ('WATER_FENCE', 'Water Fence'), ('WATER_GRAVE', 'Water Grave'), ('OTHER', 'Other')], max_length=100)),
                ('is_combination', models.BooleanField()),
                ('parkour_order', models.CharField(max_length=100)),
            ],
        ),
        migrations.CreateModel(
            name='HobbyHorse',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=100)),
                ('description', models.TextField()),
                ('is_active', models.BooleanField()),
            ],
        ),
        migrations.CreateModel(
            name='Judge',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=100)),
                ('is_active', models.BooleanField()),
            ],
        ),
        migrations.CreateModel(
            name='TypesTranslation',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('translation', models.TextField()),
                ('language', models.CharField(choices=[('ENGLISH', 'English'), ('POLISH', 'Polish')], max_length=100)),
                ('translation_key', models.CharField(choices=[('VERTICAL', 'Vertical'), ('LENGTH', 'Length'), ('OXER', 'Oxer'), ('TRIPLE', 'Triple'), ('WALL', 'Wall'), ('WATER_FENCE', 'Water Fence'), ('WATER_GRAVE', 'Water Grave'), ('OTHER', 'Other'), ('SECOND_REFUSAL', 'Second Refusal'), ('FALLING_OFF', 'Falling Off'), ('FALSTART', 'Falstart'), ('EXCEEDING_TIME_TWICE', 'Exceeding Time Twice'), ('HAND_OFF_REINS', 'Hand Off Reins'), ('HITTING_GROUND', 'Hitting Ground'), ('CONSTANT_TROTTING', 'Constant Trotting'), ('WRONG_OBSTACLE', 'Wrong Obstacle'), ('DANGEROUS_BEHAVIOUR', 'Dangerous Behaviour'), ('DISREGARDING_JUDGES', 'Disregarding Judges'), ('UNAUTHORIZED_JUMP', 'Unauthorized Jump'), ('LEAVING_ARENA', 'Leaving Arena'), ('FAILURE_TO_ARRIVE', 'Failure To Arrive'), ('OTHER', 'Other'), ('DISOBEDIENCE', 'Disobedience'), ('DROPPING', 'Dropping'), ('FALLING', 'Falling'), ('TIME', 'Time'), ('OTHER', 'Other')], max_length=100)),
            ],
        ),
        migrations.CreateModel(
            name='Competition',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=100)),
                ('time', models.TimeField()),
                ('arena_size', models.CharField(max_length=100)),
                ('height_class', models.CharField(max_length=100)),
                ('time_limit', models.IntegerField()),
                ('description', models.TextField()),
                ('course', models.ForeignKey(on_delete=django.db.models.deletion.DO_NOTHING, to='events.course')),
                ('judges', models.ManyToManyField(to='events.judge')),
            ],
        ),
        migrations.CreateModel(
            name='Event',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=100)),
                ('date', models.DateField()),
                ('location', models.CharField(max_length=100)),
                ('competitions', models.ManyToManyField(to='events.competition')),
            ],
        ),
        migrations.AddField(
            model_name='course',
            name='fences',
            field=models.ManyToManyField(to='events.fence'),
        ),
        migrations.CreateModel(
            name='Participant',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('name', models.CharField(max_length=100)),
                ('is_active', models.BooleanField()),
                ('hobby_horses', models.ManyToManyField(to='events.hobbyhorse')),
            ],
        ),
        migrations.CreateModel(
            name='EventStartingNumber',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('number', models.IntegerField()),
                ('event', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.event')),
                ('participant', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.participant')),
            ],
        ),
        migrations.AddField(
            model_name='event',
            name='participants',
            field=models.ManyToManyField(to='events.participant'),
        ),
        migrations.CreateModel(
            name='CompetitionStartingList',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('starting_order', models.IntegerField()),
                ('has_started', models.BooleanField()),
                ('has_finished', models.BooleanField()),
                ('completion_time', models.TimeField()),
                ('is_elimination', models.BooleanField()),
                ('elimination_type', models.CharField(choices=[('SECOND_REFUSAL', 'Second Refusal'), ('FALLING_OFF', 'Falling Off'), ('FALSTART', 'Falstart'), ('EXCEEDING_TIME_TWICE', 'Exceeding Time Twice'), ('HAND_OFF_REINS', 'Hand Off Reins'), ('HITTING_GROUND', 'Hitting Ground'), ('CONSTANT_TROTTING', 'Constant Trotting'), ('WRONG_OBSTACLE', 'Wrong Obstacle'), ('DANGEROUS_BEHAVIOUR', 'Dangerous Behaviour'), ('DISREGARDING_JUDGES', 'Disregarding Judges'), ('UNAUTHORIZED_JUMP', 'Unauthorized Jump'), ('LEAVING_ARENA', 'Leaving Arena'), ('FAILURE_TO_ARRIVE', 'Failure To Arrive'), ('OTHER', 'Other')], max_length=100)),
                ('notes', models.TextField()),
                ('competition', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.competition')),
                ('participant', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.participant')),
            ],
        ),
        migrations.CreateModel(
            name='CompetitionPenalty',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('type', models.CharField(choices=[('DISOBEDIENCE', 'Disobedience'), ('DROPPING', 'Dropping'), ('FALLING', 'Falling'), ('TIME', 'Time'), ('OTHER', 'Other')], max_length=100)),
                ('value', models.IntegerField()),
                ('competition', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.competition')),
                ('fence', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.fence')),
                ('participant', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, to='events.participant')),
            ],
        ),
    ]
