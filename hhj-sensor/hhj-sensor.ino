/*
 * Hobby Horse Jumping Sensor
 * 
 * This sensor sends timing signals when triggered and alive signals every 10 seconds.
 * 
 * CRITICAL TIMING DESIGN:
 * - Timing signals have absolute priority over alive signals
 * - Alive signals are scheduled and sent with delays to prevent blocking timing signals
 * - HTTP timeouts are shorter for alive signals (2s) vs timing signals (5s)
 * - If a timing signal is triggered while an alive signal is pending, the alive signal is cancelled
 */

#include "WiFi.h"
#include "HTTPClient.h"

#ifndef LED_IR
#define LED_IR 23
#endif

#ifndef LED_WIFI
#define LED_WIFI 21
#endif

#ifndef SIGNAL_IR
#define SIGNAL_IR 2
#endif

const char *ssid = "NotForSquares";
const char *password = "nfsWiFiPass";

// const char *server = "http://192.168.10.100:3003/timereadings/";
const char *server = "http://192.168.10.200:3003/timereadings/";

// Sensor configuration - change this for each sensor
// String sensorId = "START_SENSOR";
String sensorId = "FINISH_SENSOR";
String selectedModeLabel = "FINISH";

// Alive signal timing
unsigned long lastAliveSignal = 0;
const unsigned long aliveInterval = 10000; // 10 seconds
bool aliveSignalPending = false;
unsigned long aliveSignalStartTime = 0;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  pinMode(LED_IR, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(SIGNAL_IR, INPUT);
  
  Serial.println("Sensor starting: " + sensorId);
}

// String selectedModeLabel = "START";
String selectedModeLabel = "FINISH";

int sensorState = HIGH;
int sensorLastState = HIGH;

void sendHttpRequest(String timeMarkType, int64_t timestamp, bool isAlive = false) {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, server);
  http.addHeader("Content-Type", "application/json");
  
  // Set shorter timeout for alive signals to prevent blocking
  if (isAlive) {
    http.setTimeout(2000); // 2 second timeout for alive signals
  } else {
    http.setTimeout(5000); // 5 second timeout for timing signals
  }

  String httpRequestData = "{\"time_mark\":\"" + timeMarkType + "\", \"sensor_time\":\"" + String(timestamp) + "\", \"sensor_id\":\"" + sensorId + "\"}";
  Serial.println("Sending: " + httpRequestData);

  int httpResponseCode = http.POST(httpRequestData);
  
  if (httpResponseCode > 0) {
    Serial.println("HTTP Response: " + String(httpResponseCode));
  } else {
    Serial.println("HTTP Error: " + String(httpResponseCode));
  }
  
  http.end();
}

void sendAliveSignal() {
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  int64_t time_us = (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000L;
  
  sendHttpRequest("ALIVE", time_us, true);
  Serial.println("Alive signal sent from " + sensorId);
}

void scheduleAliveSignal() {
  aliveSignalPending = true;
  aliveSignalStartTime = millis();
}

void loop() {
  unsigned long currentTime = millis();
  
  /* 
    WiFi connection
  */
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
    
    // Schedule alive signal every 10 seconds (non-blocking)
    if (currentTime - lastAliveSignal >= aliveInterval) {
      scheduleAliveSignal();
      lastAliveSignal = currentTime;
    }
  } else {
    digitalWrite(LED_WIFI, LOW);
    aliveSignalPending = false; // Cancel pending alive signal if WiFi disconnected
  }

  /* 
    Sensor reading - HIGHEST PRIORITY
  */
  sensorState = digitalRead(SIGNAL_IR);

  if (sensorState == LOW) {     
    digitalWrite(LED_IR, LOW);  
  } else {
    digitalWrite(LED_IR, HIGH);  
  }

  if (sensorState && !sensorLastState) {
    Serial.println("Connected");
  } 
  
  // CRITICAL: Handle timing signal immediately - this takes priority over alive signals
  if (!sensorState && sensorLastState) {
    Serial.println("Disconnected - Sending timing signal");

    if (WiFi.status() == WL_CONNECTED) {
      // Cancel any pending alive signal to prioritize timing signal
      aliveSignalPending = false;
      
      struct timeval tv_now;
      gettimeofday(&tv_now, NULL);
      int64_t time_us = (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000L;
      
      sendHttpRequest(selectedModeLabel, time_us, false);
      delay(1000);
    } else {
      Serial.println("WiFi not connected - timing signal not sent");
    }
  }
  
  sensorLastState = sensorState;
  
  /* 
    Handle pending alive signal (only when no timing signal is being processed)
  */
  if (aliveSignalPending && WiFi.status() == WL_CONNECTED) {
    // Small delay to ensure alive signal doesn't interfere with timing signals
    if (currentTime - aliveSignalStartTime >= 100) { // 100ms delay
      sendAliveSignal();
      aliveSignalPending = false;
    }
  }
  
  // Cancel alive signal if it's been pending too long (timeout after 5 seconds)
  if (aliveSignalPending && (currentTime - aliveSignalStartTime >= 5000)) {
    Serial.println("Alive signal timeout - cancelling");
    aliveSignalPending = false;
  }
}
