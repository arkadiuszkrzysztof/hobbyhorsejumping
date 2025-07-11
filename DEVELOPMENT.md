# Development Guide

## Project Structure

```
hobbyhorsejumping/
├── hhj-backend/          # Django REST API backend
│   ├── adminportal/      # Main Django project
│   ├── events/           # Event, competition, participant models
│   ├── timings/          # Timing data models and WebSocket
│   └── fixtures/         # Initial test data
├── hhj-frontend/         # React display board
│   └── src/              # React components
├── hhj-sensor/           # ESP32 Arduino code
└── README.md             # Main documentation
```

## Development Workflow

### Backend Development

1. **Activate virtual environment:**
   ```bash
   cd hhj-backend
   pipenv shell
   ```

2. **Run development server:**
   ```bash
   python manage.py runserver 127.0.0.1:3003
   ```

3. **Database operations:**
   ```bash
   # Create migrations after model changes
   python manage.py makemigrations
   
   # Apply migrations
   python manage.py migrate
   
   # Load test data
   python manage.py setup_initial_data
   ```

4. **Admin interface:**
   - Create superuser: `python manage.py createsuperuser`
   - Access admin: http://127.0.0.1:3003/admin/

### Frontend Development

1. **Install dependencies:**
   ```bash
   cd hhj-frontend
   npm install
   ```

2. **Start development server:**
   ```bash
   npm start
   ```

3. **Environment configuration:**
   - Edit `.env` for API endpoints
   - Backend must be running for WebSocket connection

### Sensor Development

1. **Arduino IDE setup:**
   - Install ESP32 board package
   - Select "ESP32 Dev Module" board
   - Set upload speed to 921600

2. **Code modification:**
   - Update WiFi credentials in `hhj-sensor.ino`
   - Change server IP to your development machine
   - Modify `selectedModeLabel` for START/FINISH

## Key Components

### Backend API Endpoints

- `GET/POST /timereadings/` - Time reading data
- `ws://localhost:3003/ws/timings/` - WebSocket for live updates
- `/admin/` - Django admin interface

### Database Models

**Events App:**
- `Event` - Championship events
- `Competition` - Individual competitions within events
- `Participant` - Riders/competitors
- `CompetitionStart` - Links participants to competitions
- `HobbyHorse` - Horse information
- `Course` - Competition courses and fences

**Timings App:**
- `TimeReading` - Sensor timing data with START/FINISH marks

### WebSocket Communication

**Flow:**
1. ESP32 sensor sends HTTP POST to `/timereadings/`
2. Django view creates `TimeReading` object
3. Django broadcasts to WebSocket group `live_timings_listeners`
4. React frontend receives update and displays timing

**Message Format:**
```json
{
  "id": "uuid",
  "sensor_time": 1234567890,
  "server_time": 1234567890, 
  "time_mark": "START|FINISH",
  "competition_start": "uuid"
}
```

## Common Development Tasks

### Adding New Sensor Types

1. **Update TimeReading model:**
   ```python
   class TimeMark(models.TextChoices):
       START = 'START'
       FINISH = 'FINISH'
       CHECKPOINT = 'CHECKPOINT'  # Add new type
   ```

2. **Update frontend logic:**
   ```typescript
   if (data.time_mark === 'CHECKPOINT') {
       // Handle checkpoint logic
   }
   ```

3. **Update sensor code:**
   ```cpp
   String selectedModeLabel = "CHECKPOINT";
   ```

### Modifying Display Board

**Frontend files to edit:**
- `src/App.tsx` - Main timing display component
- `src/App.css` - Styling
- `.env` - API configuration

**Key state variables:**
- `timer` - Current elapsed time
- `startTime` - When timing started
- `messageHistory` - WebSocket message log

### Database Schema Changes

1. **Modify models in `events/models.py` or `timings/models.py`**
2. **Create migration:**
   ```bash
   python manage.py makemigrations
   ```
3. **Apply migration:**
   ```bash
   python manage.py migrate
   ```
4. **Update fixtures if needed**

### Adding Authentication

Currently the system has no authentication. To add:

1. **Django REST Framework authentication:**
   ```python
   # In settings.py
   REST_FRAMEWORK = {
       'DEFAULT_AUTHENTICATION_CLASSES': [
           'rest_framework.authentication.TokenAuthentication',
       ]
   }
   ```

2. **WebSocket authentication:**
   ```python
   # In routing.py
   from channels.auth import AuthMiddlewareStack
   
   application = ProtocolTypeRouter({
       "websocket": AuthMiddlewareStack(
           URLRouter(timings.routing.websocket_urlpatterns)
       )
   })
   ```

## Testing

### Manual Testing

1. **Start system with test scripts**
2. **Use admin panel to create test data**
3. **Simulate sensor data:**
   ```bash
   curl -X POST http://127.0.0.1:3003/timereadings/ \
        -H "Content-Type: application/json" \
        -d '{"time_mark":"START","sensor_time":"1234567890"}'
   ```

### Automated Testing

Create test files:
- `hhj-backend/timings/tests.py` - API tests
- `hhj-frontend/src/App.test.tsx` - React component tests

Run tests:
```bash
# Backend tests
python manage.py test

# Frontend tests  
npm test
```

## Production Considerations

### Security
- Change `SECRET_KEY` in settings
- Set `DEBUG = False`
- Configure `ALLOWED_HOSTS`
- Add HTTPS/SSL certificates
- Implement proper authentication

### Performance
- Use PostgreSQL instead of SQLite
- Configure Redis for WebSocket scaling
- Use nginx for static file serving
- Consider CDN for frontend assets

### Monitoring
- Add logging configuration
- Set up error reporting (Sentry)
- Monitor database performance
- Track WebSocket connection health

### Deployment
- Use Docker containers
- Set up CI/CD pipeline
- Configure backup strategy
- Plan for zero-downtime deployments

## Troubleshooting

### Common Issues

1. **CORS errors in frontend:**
   ```python
   # Add to settings.py
   CORS_ALLOWED_ORIGINS = [
       "http://localhost:3000",
       "http://127.0.0.1:3000",
   ]
   ```

2. **WebSocket connection fails:**
   - Check Django Channels configuration
   - Verify ASGI application setup
   - Test WebSocket endpoint manually

3. **Sensor data not received:**
   - Check network connectivity
   - Verify server IP in sensor code
   - Test API endpoint with curl

4. **Database migration errors:**
   - Reset migrations if in development
   - Check for circular dependencies
   - Verify foreign key relationships

### Debug Commands

```bash
# Check database schema
python manage.py dbshell

# Create database dump
python manage.py dumpdata > backup.json

# Load database from dump
python manage.py loaddata backup.json

# Interactive Django shell
python manage.py shell
```

## Contributing

1. **Fork repository**
2. **Create feature branch**
3. **Make changes with tests**
4. **Update documentation**
5. **Submit pull request**

### Code Style

- **Python**: Follow PEP 8, use Black formatter
- **JavaScript/TypeScript**: Use Prettier, ESLint
- **Arduino C++**: Follow Arduino style guide

### Commit Messages

Use conventional commits:
- `feat:` - New features
- `fix:` - Bug fixes
- `docs:` - Documentation updates
- `style:` - Code formatting
- `refactor:` - Code restructuring
- `test:` - Adding tests
