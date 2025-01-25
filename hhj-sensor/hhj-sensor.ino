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

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  pinMode(LED_IR, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(SIGNAL_IR, INPUT);
}

// String selectedModeLabel = "START";
String selectedModeLabel = "FINISH";

int sensorState = HIGH;
int sensorLastState = HIGH;

void loop() {
  /* 
    WiFi connection
  */

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
  } else {
    digitalWrite(LED_WIFI, LOW);
  }


  /* 
    Sensor reading
  */

  sensorState = digitalRead(SIGNAL_IR);
  // String reading = "Reading: " + String(sensorState);
  // Serial.println(reading);


  if (sensorState == LOW) {     
    digitalWrite(LED_IR, LOW);  
  } else {
    digitalWrite(LED_IR, HIGH);  
  }

  if (sensorState && !sensorLastState) {
    Serial.println("Connected");
  } 
  if (!sensorState && sensorLastState) {
    Serial.println("Disconnected");

    WiFiClient client;
    HTTPClient http;

    http.begin(client, server);
    http.addHeader("Content-Type", "application/json");

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000L;
    String httpRequestData = "{\"time_mark\":\"" + String(selectedModeLabel) + "\", \"sensor_time\":\"" + String(time_us) + "\"}";
    Serial.println(httpRequestData);

    int httpResponseCode = http.POST(httpRequestData);
    http.end();
    delay(1000);
  }
  sensorLastState = sensorState;
}
