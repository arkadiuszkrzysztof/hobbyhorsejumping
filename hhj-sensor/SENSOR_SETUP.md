# ESP32 Sensor Configuration Guide

## Hardware Setup

### Required Components
- ESP32 development board (ESP32-WROOM-32 or similar)
- IR sensor module (photoelectric sensor)
- 2 LEDs (different colors recommended)
- 220Ω resistors for LEDs
- Breadboard and jumper wires
- 5V power supply or USB power

### Wiring Diagram

```
ESP32 Pin   →   Component
GPIO 23     →   LED_IR (Sensor Status LED)
GPIO 21     →   LED_WIFI (WiFi Status LED)
GPIO 2      →   SIGNAL_IR (IR Sensor Input)
3.3V        →   IR Sensor VCC
GND         →   IR Sensor GND & LED cathodes
```

### Circuit Notes
- Connect LEDs with 220Ω current limiting resistors
- IR sensor should be a digital output type (HIGH when clear, LOW when blocked)
- Ensure stable 3.3V or 5V power supply depending on sensor requirements

## Software Configuration

### 1. WiFi Settings
Update these values in the sensor code:

```cpp
const char *ssid = "YOUR_WIFI_NETWORK_NAME";
const char *password = "YOUR_WIFI_PASSWORD";
```

### 2. Server Configuration
Update the server URL to point to your computer running the backend:

```cpp
// Replace YOUR_PC_IP with your computer's local IP address
const char *server = "http://YOUR_PC_IP:3003/timereadings/";
```

To find your PC's IP address:
- Windows: Run `ipconfig` in Command Prompt
- Look for "IPv4 Address" under your active network adapter
- Usually starts with 192.168.x.x or 10.x.x.x

### 3. Sensor Mode Configuration
Set the sensor as either START or FINISH timing point:

```cpp
// For start line sensor
String selectedModeLabel = "START";

// For finish line sensor  
String selectedModeLabel = "FINISH";
```

## Programming the ESP32

### Using Arduino IDE

1. **Install ESP32 Board Package:**
   - File → Preferences
   - Add to Additional Board Manager URLs: 
     `https://dl.espressif.com/dl/package_esp32_index.json`
   - Tools → Board → Board Manager
   - Search and install "esp32"

2. **Select Board:**
   - Tools → Board → ESP32 Arduino → ESP32 Dev Module

3. **Configure Upload Settings:**
   - Port: Select your ESP32's COM port
   - Upload Speed: 921600
   - Flash Frequency: 80MHz

4. **Upload Code:**
   - Open `hhj-sensor.ino`
   - Update WiFi credentials and server IP
   - Click Upload button

### Using PlatformIO (Alternative)

1. Create `platformio.ini` in sensor directory:
```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

2. Place code in `src/main.cpp`
3. Run: `pio run --target upload`

## Testing and Deployment

### 1. Initial Testing

1. **Upload code** with your WiFi credentials
2. **Open Serial Monitor** (115200 baud rate)
3. **Power on ESP32** and check output:
   - Should see WiFi connection attempts
   - WiFi LED should turn on when connected
   - Sensor readings should be displayed

### 2. Sensor Testing

1. **Block the IR sensor** (wave hand in front)
2. **Check Serial Monitor** for "Disconnected" message
3. **Verify HTTP request** is sent to server
4. **Check backend** receives timing data

### 3. LED Status Indicators

- **WiFi LED (GPIO 21):**
  - ON: Connected to WiFi
  - OFF: Not connected to WiFi

- **Sensor LED (GPIO 23):**
  - ON: IR beam is clear (normal state)
  - OFF: IR beam is blocked (triggered state)

## Physical Installation

### Sensor Placement

1. **START Sensor:**
   - Place at starting line
   - IR beam should cross the path at horse chest height
   - Ensure stable mounting (avoid vibration)

2. **FINISH Sensor:**
   - Place at finish line
   - Same height as start sensor
   - Clear line of sight across full track width

### Mounting Tips

- Use weatherproof enclosures for outdoor use
- Secure cables to prevent disconnection
- Test range and reliability before competition
- Have backup power source (battery pack)
- Position away from direct sunlight (can interfere with IR)

## Troubleshooting

### Common Issues

1. **WiFi Not Connecting:**
   - Check SSID and password spelling
   - Verify ESP32 is in range of WiFi
   - Check if network is 2.4GHz (ESP32 doesn't support 5GHz)

2. **No Data Received by Server:**
   - Verify server IP address is correct
   - Check if backend server is running
   - Test with browser: `http://YOUR_PC_IP:3003/timereadings/`
   - Check firewall settings on PC

3. **False Triggers:**
   - Adjust sensor sensitivity
   - Check for environmental interference
   - Ensure stable power supply
   - Add debounce delay if needed

4. **Sensor Not Detecting:**
   - Check IR sensor wiring
   - Verify sensor is receiving power
   - Test sensor output with multimeter
   - Clean sensor lens

### Debug Commands

Add these to your code for debugging:

```cpp
// In loop(), add periodic status
static unsigned long lastStatus = 0;
if (millis() - lastStatus > 5000) {
    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    lastStatus = millis();
}
```

## Advanced Configuration

### Power Management
```cpp
// Add to setup() for better power efficiency
WiFi.setSleep(false);  // Disable WiFi sleep
setCpuFrequencyMhz(80); // Reduce CPU frequency
```

### Network Reliability
```cpp
// Add reconnection logic in loop()
if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(1000);
}
```

### Custom Timing Precision
```cpp
// Use hardware timer for microsecond precision
hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;

void IRAM_ATTR onTimer() {
    xSemaphoreGiveFromISR(timerSemaphore, NULL);
}
```

## Network Setup

### Static IP Configuration (Optional)
```cpp
IPAddress local_IP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
}
```

### Server Discovery (Advanced)
```cpp
// Use mDNS to find server automatically
#include <ESPmDNS.h>

void setup() {
    // ... other setup code ...
    if (MDNS.begin("hhj-sensor")) {
        Serial.println("MDNS responder started");
    }
}
```

## Safety and Best Practices

1. **Always test thoroughly** before live events
2. **Have backup timing methods** available
3. **Secure all connections** and mounting points
4. **Document your setup** for future reference
5. **Keep spare ESP32s** programmed and ready
6. **Use quality cables** and connectors
7. **Test in actual competition conditions**

## Maintenance

### Regular Checks
- Clean sensor lenses monthly
- Check cable connections
- Verify LED functionality  
- Test WiFi range and stability
- Update firmware if needed

### Competition Day
- Arrive early for setup and testing
- Bring backup sensors and cables
- Test full system before first competitor
- Monitor for any interference or issues
- Have technical support contact ready
