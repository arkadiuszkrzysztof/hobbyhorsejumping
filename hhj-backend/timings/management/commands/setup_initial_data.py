from django.core.management.base import BaseCommand
from django.core.management import call_command
import os
from django.conf import settings

class Command(BaseCommand):
    help = 'Sets up initial data for the hobby horse jumping timing system'

    def handle(self, *args, **options):
        self.stdout.write(self.style.SUCCESS('Setting up initial data for HHJ timing system...'))
        
        # Load the fixtures
        fixtures_path = os.path.join(settings.BASE_DIR, 'fixtures', 'initial_data.json')
        
        if os.path.exists(fixtures_path):
            try:
                call_command('loaddata', fixtures_path)
                self.stdout.write(
                    self.style.SUCCESS('✅ Successfully loaded initial data!')
                )
                self.stdout.write('Created:')
                self.stdout.write('  - Default Event: "Default Championship"')
                self.stdout.write('  - Default Competition: "Speed Round"') 
                self.stdout.write('  - Default Participant: "Test Rider"')
                self.stdout.write('  - Default Course: "Default Course"')
                self.stdout.write('  - Default Competition Start for timing')
                self.stdout.write('')
                self.stdout.write('You can now:')
                self.stdout.write('  1. Access admin panel: http://127.0.0.1:3003/admin/')
                self.stdout.write('  2. Start receiving sensor data: http://127.0.0.1:3003/timereadings/')
                self.stdout.write('  3. View live timing: http://localhost:3000')
                
            except Exception as e:
                self.stdout.write(
                    self.style.ERROR(f'❌ Failed to load initial data: {e}')
                )
                self.stdout.write(
                    self.style.WARNING('You may need to create competitions manually in the admin panel.')
                )
        else:
            self.stdout.write(
                self.style.ERROR(f'❌ Fixtures file not found: {fixtures_path}')
            )
