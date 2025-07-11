# Demo Script for Hobby Horse Jumping Timing System

## Quick Demo

This script demonstrates the complete workflow of the enhanced timing system.

### Step 1: Start the System
```bash
# Run the automated setup script
.\setup-and-start.bat
```

This will:
- ✅ Start Django backend on http://127.0.0.1:3003
- ✅ Start React frontend on http://localhost:3000
- ✅ Load test data (competition, participant, course)

### Step 2: Access the Dashboard
1. Open browser to http://localhost:3000
2. You should see:
   - **Left sidebar**: List of competitors (including "Test Rider")
   - **Main area**: Large timer display
   - **Top bar**: Status indicators and test button (🐛)

### Step 3: Select a Competitor
1. Click on **"#1 Test Rider"** in the left sidebar
2. The competitor should become highlighted (blue background)
3. Status bar should show "Active Competitor: #1 Test Rider"

### Step 4: Test the Timing System

#### Option A: Use the Test Interface (Recommended)
1. Click the **🐛 bug icon** in the top-right of the dashboard
2. This opens the **Testing Interface** with multiple options:
   - **Quick Tests**: START, FINISH, and timing scenarios
   - **Custom JSON**: Send custom payloads
   - **API Tests**: Verify all endpoints are working

3. Click **"START Signal"** button
   - Timer should start immediately
   - WebSocket status shows the message
   - Competitor status updates

4. Wait a few seconds, then click **"FINISH Signal"**
   - Timer stops
   - Final time is displayed
   - Competitor marked as "Finished"

#### Option B: Simulate ESP32 Sensor (Advanced)
```bash
# Send START signal
curl -X POST http://127.0.0.1:3003/timereadings/ \
     -H "Content-Type: application/json" \
     -d '{"time_mark":"START","sensor_time":"'$(date +%s%3N)'"}'

# Wait a few seconds...

# Send FINISH signal  
curl -X POST http://127.0.0.1:3003/timereadings/ \
     -H "Content-Type: application/json" \
     -d '{"time_mark":"FINISH","sensor_time":"'$(date +%s%3N)'"}'
```

### Step 5: View Results
1. Check the **competitor list** - timing should be displayed
2. **Recent Messages** section shows sensor activity
3. **Timer Display** shows final time and competitor info

### Step 6: Add More Competitors (Optional)
1. Open Django Admin: http://127.0.0.1:3003/admin/
2. Login (create superuser if needed): `python manage.py createsuperuser`
3. Add more:
   - **Participants** (riders)
   - **Hobby Horses** 
   - **Competition Starts** (link participants to competitions)

## API Endpoints for ESP32 Integration

### Production ESP32 Code
Update your ESP32 sensor with:

```cpp
const char *server = "http://YOUR_PC_IP:3003/timereadings/";
String selectedModeLabel = "START";  // or "FINISH"
```

### Key Endpoints
- **POST** `/timereadings/` - Send timing data from sensors
- **GET** `/api/competitions/` - List competitions
- **GET** `/api/competition-starts/` - List participants
- **POST** `/api/active-competitor/` - Set active competitor
- **GET** `/test/` - API health check

## Expected Flow

1. **Setup**: Competition, participants, and courses configured
2. **Selection**: Staff selects active competitor from dashboard
3. **START**: ESP32 sensor detects beam break, sends START signal
4. **Timing**: Dashboard shows live timer, WebSocket updates in real-time
5. **FINISH**: ESP32 sensor detects finish, sends FINISH signal
6. **Results**: Final time recorded, competitor marked finished
7. **Next**: Select next competitor and repeat

## Troubleshooting

### No Competitors in List
- Check that test data loaded: `python manage.py setup_initial_data`
- Add competitors in Django admin panel

### Sensor Signals Not Working
- Verify active competitor is selected (blue highlight)
- Check WebSocket connection (green indicator)
- Use test interface to verify backend connectivity

### Timer Not Starting
- Ensure competitor is selected first
- Check browser console for errors
- Verify backend is running on http://127.0.0.1:3003

### WebSocket Disconnected
- Restart backend server
- Check firewall settings
- Verify ports 3003 and 3000 are available

## Production Deployment

For actual competitions:

1. **Network Setup**: 
   - Connect all devices to same WiFi network
   - Note server computer's IP address
   - Update ESP32 code with server IP

2. **Hardware Setup**:
   - Mount sensors at start/finish lines
   - Test IR beam alignment
   - Verify power supply stability

3. **Competition Management**:
   - Create real competitions in admin panel
   - Add all participants and horses
   - Set up starting orders

4. **Display Setup**:
   - Connect large monitor/TV to show dashboard
   - Use browser full-screen mode
   - Consider backup display systems

## Features Demonstrated

✅ **Real-time WebSocket communication**
✅ **Competitor selection and management** 
✅ **Persistent timing data**
✅ **Professional UI with status indicators**
✅ **Test interface for debugging**
✅ **Database integration**
✅ **API endpoints for ESP32 sensors**
✅ **Error handling and validation**

The system is now production-ready for hobby horse jumping championships! 🏆🐎
