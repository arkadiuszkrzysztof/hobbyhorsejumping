# Generated by Django 5.1.3 on 2024-11-13 10:47

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('events', '0005_remove_course_fences_fence_course'),
    ]

    operations = [
        migrations.AlterField(
            model_name='course',
            name='description',
            field=models.TextField(blank=True, null=True),
        ),
        migrations.AlterField(
            model_name='course',
            name='plan',
            field=models.FileField(blank=True, null=True, upload_to=''),
        ),
    ]
