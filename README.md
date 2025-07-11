# Hobby Horse Jumping Championship Timing System

A comprehensive timing system for hobby horse jumping championships featuring automated ESP32 sensors, Django backend, and React frontend with real-time display capabilities.

## System Overview

This system consists of three main components:

1. **ESP32 Sensors** (`hhj-sensor/`) - WiFi-enabled timing sensors that detect horse jumps
2. **Django Backend** (`hhj-backend/`) - REST API and WebSocket server for data processing
3. **React Frontend** (`hhj-frontend/`) - Real-time display board for live timing

## Features

- Real-time timing using ESP32-based IR sensors
- Automatic WiFi connectivity for sensors
- Live timing display via WebSocket connections
- Django admin interface for event management
- Support for multiple participants and competitions
- SQLite database for data persistence

## Hardware Requirements

- ESP32 microcontrollers with WiFi capability
- IR sensors for timing detection
- LEDs for status indication (WiFi and sensor status)
- Power supply for sensors

## Software Requirements

- Python 3.12+
- Node.js 16+
- Windows 10/11 (for automated startup script)

## Quick Start

### Option 1: Automated Setup (Recommended)

Run the automated startup script that will configure and start all services:

```powershell
# Run as Administrator
.\startup-script.ps1
```

This script will:

- Check and install required dependencies
- Set up Python virtual environment
- Install Python packages
- Install Node.js dependencies
- Run database migrations
- Load initial test data (competition, participant, etc.)
- Start both backend and frontend servers
- Configure services for automatic startup

### Option 2: Manual Setup

#### 1. Backend Setup

```powershell
cd hhj-backend

# Install Python dependencies using pipenv
pip install pipenv
pipenv install

# Activate virtual environment
pipenv shell

# Run database migrations
python manage.py migrate

# Load initial test data
python manage.py setup_initial_data

# Create superuser (optional)
python manage.py createsuperuser

# Start Django development server
python manage.py runserver 127.0.0.1:3003
```

#### 2. Frontend Setup

```powershell
cd hhj-frontend

# Install Node.js dependencies
npm install

# Start React development server
npm start
```

## Network Configuration

### Backend Configuration

The Django backend runs on `127.0.0.1:3003` by default. Key endpoints:

- **REST API**: `http://127.0.0.1:3003/timereadings/`
- **WebSocket**: `ws://127.0.0.1:3003/ws/timings/`
- **Admin Panel**: `http://127.0.0.1:3003/admin/`

### Frontend Configuration

The React frontend connects to the backend via environment variables in `.env`:

```properties
REACT_APP_API_HTTP_URL=http://127.0.0.1:3003
REACT_APP_API_WEBSOCKET_URL=ws://127.0.0.1:3003
```

### Sensor Configuration

Update the ESP32 sensor code with your WiFi credentials and server IP:

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *server = "http://YOUR_SERVER_IP:3003/timereadings/";
```

## Hardware Setup

### ESP32 Pin Configuration

- **LED_IR**: Pin 23 (IR sensor status LED)
- **LED_WIFI**: Pin 21 (WiFi connection status LED)
- **SIGNAL_IR**: Pin 2 (IR sensor input)

### Sensor Modes

Configure sensors as either START or FINISH timing points:

```cpp
// For start line sensor
String selectedModeLabel = "START";

// For finish line sensor
String selectedModeLabel = "FINISH";
```

## Usage

### Initial Setup

The system includes default test data for immediate testing:

- **Event**: "Default Championship"
- **Competition**: "Speed Round"
- **Participant**: "Test Rider"
- **Course**: "Default Course"

This allows sensors to send timing data immediately without manual setup.

### Running a Competition

1. **Setup Event**: Use Django admin panel to create competitions, participants, and hobby horses
2. **Deploy Sensors**: Place START sensor at the beginning and FINISH sensor at the end of the course
3. **Start Display**: Open the React frontend on a display monitor for live timing
4. **Begin Competition**: Sensors automatically detect crossings and send timing data

## API Endpoints

### Time Readings

- **GET** `/timereadings/` - List all time readings
- **POST** `/timereadings/` - Create new time reading (used by sensors)

### WebSocket

- **Connection**: `/ws/timings/` - Real-time timing updates

## Data Flow

1. ESP32 sensor detects IR beam interruption
2. Sensor sends HTTP POST to Django backend with timing data
3. Backend stores data and broadcasts via WebSocket
4. Frontend receives WebSocket message and updates display
5. Timer starts on START signal and stops on FINISH signal

## Database Schema

### TimeReading Model

- `id`: UUID primary key
- `competition_start`: Foreign key to CompetitionStart
- `sensor_time`: Sensor timestamp (milliseconds)
- `server_time`: Server timestamp (milliseconds)
- `time_mark`: START, FINISH, COMBINED, or FAULTY_READING

### Events Models

- `HobbyHorse`: Horse information
- `Participant`: Competitor information
- `Competition`: Event details
- `CompetitionStart`: Links participants to competitions

## Development

### Backend Development

```powershell
cd hhj-backend
pipenv shell
python manage.py runserver 127.0.0.1:3003
```

### Frontend Development

```powershell
cd hhj-frontend
npm start
```

### Sensor Development

Use Arduino IDE or PlatformIO with ESP32 board package installed.

## Troubleshooting

### Common Issues

1. **Sensor not connecting to WiFi**
   - Check WiFi credentials in sensor code
   - Verify WiFi network is accessible
   - Check WiFi LED status on sensor

2. **No timing data received**
   - Verify server IP address in sensor configuration
   - Check if backend server is running on correct port
   - Ensure network connectivity between sensor and server

3. **Display not updating**
   - Check WebSocket connection status in browser console
   - Verify frontend environment variables
   - Ensure backend WebSocket endpoint is accessible

4. **Database errors**
   - Run `python manage.py migrate` to apply pending migrations
   - Check if CompetitionStart object exists (hardcoded UUID in views.py)

## Production Deployment

For production use:

1. Update Django settings for production
2. Use a proper database (PostgreSQL recommended)
3. Configure proper WebSocket backend (Redis recommended)
4. Set up proper web server (nginx + gunicorn)
5. Use environment variables for sensitive configuration

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

See LICENSE file for details.

## Support

For issues and questions, please check the troubleshooting section or create an issue in the repository.

## Testing the System

### Using the Built-in Test Page

The frontend includes a comprehensive testing interface that allows you to simulate ESP32 sensor signals without physical hardware:

1. **Access the Test Page**:
   - Open the React frontend (<http://localhost:3000>)
   - Click the 🐛 (bug) icon in the top-right corner of the navigation bar

2. **Select a Competitor First**:
   - Before testing, make sure to select an active competitor from the main dashboard
   - The test page will show the active competitor status

3. **Available Test Options**:
   - **🟢 START Signal**: Sends a START timing signal
   - **🔴 FINISH Signal**: Sends a FINISH timing signal  
   - **🏃‍♂️ Complete Run**: Simulates a full run (START → 3 seconds → FINISH)
   - **⏰ START (5s ago)**: Sends a backdated START signal
   - **⏱️ FINISH (2s ago)**: Sends a backdated FINISH signal

4. **Advanced Testing**:
   - **Custom Timing**: Enter specific timestamps for testing edge cases
   - **Custom JSON**: Send custom payloads to test API validation
   - **API Endpoint Testing**: Test all backend endpoints for connectivity

5. **View Results**:
   - Real-time server responses are displayed
   - Test history shows success/failure status and response times
   - WebSocket updates appear instantly on the main dashboard

### API Testing Endpoints

You can also test the system manually using these API endpoints:

```bash
# Send START signal
curl -X POST http://localhost:3003/timereadings/ \
  -H "Content-Type: application/json" \
  -d '{"time_mark": "START", "sensor_time": "1234567890"}'

# Send FINISH signal  
curl -X POST http://localhost:3003/timereadings/ \
  -H "Content-Type: application/json" \
  -d '{"time_mark": "FINISH", "sensor_time": "1234567891"}'

# Get active competitor
curl http://localhost:3003/api/active-competitor/

# Set active competitor (replace ID)
curl -X POST http://localhost:3003/api/active-competitor/ \
  -H "Content-Type: application/json" \
  -d '{"competition_start_id": "your-competitor-id"}'
```

### Command Line Testing Script

For quick API testing without the web interface, use the PowerShell testing script:

```powershell
# Interactive mode (recommended)
.\test-api.ps1

# Send specific signals
.\test-api.ps1 -Command START
.\test-api.ps1 -Command FINISH
.\test-api.ps1 -Command RUN    # Complete run simulation

# Use different API URL
.\test-api.ps1 -ApiUrl "http://192.168.1.100:3003"
```

The script provides:

- Automatic API connectivity testing
- Active competitor status checking  
- Individual START/FINISH signal sending
- Complete run simulation (START → wait → FINISH)
- Interactive mode for repeated testing

## ✨ Enhanced Features (Latest Update)

### 🔄 Real-Time State Synchronization
- **Multi-Screen Support**: Open multiple dashboard and public displays simultaneously
- **WebSocket Broadcasting**: All state changes (competitor selection, reordering) sync instantly across screens
- **Persistent State**: Selected competition and active competitor persist across browser refreshes

### 🔢 Competitor Reordering
- **Drag & Drop Interface**: Intuitive reordering of starting list
- **Button Controls**: Alternative arrow buttons for precise positioning
- **Real-Time Updates**: Changes broadcast to all connected displays immediately
- **Access**: Click the 🔢 (reorder) icon in the toolbar

### 📺 Public Display System
- **Dedicated URL**: `/public` route for public-facing screens
- **Live Rankings**: Real-time leaderboard with completion times and penalties
- **Active Competitor Spotlight**: Prominent display of current rider
- **Professional Presentation**: Large, clear typography suitable for arena displays

### 🔗 URL Structure
- **Internal Dashboard**: `http://localhost:3000/` - Competition management and timing
- **Public Display**: `http://localhost:3000/public` - Public-facing results screen

### 🎮 Usage Workflow
1. **Setup**: Start system with `.\enhanced-startup.ps1`
2. **Internal Dashboard**: Select competition and manage competitors
3. **Reordering**: Use 🔢 icon to adjust starting order via drag & drop
4. **Public Display**: Open `/public` on arena screens for live results
5. **Real-Time**: All changes sync automatically between screens
