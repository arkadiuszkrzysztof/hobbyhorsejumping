// hhj-sensor v2.1 — two-beam gate (START or FINISH), ESP32.
//
// This file is only the hardware glue. Everything the gate decides lives in
// plain C++ that is tested on the host (make test-firmware):
//   beam_logic.h  when did a rider cross           (test/test_beam_logic.cpp)
//   outbox.h      repeating events until delivered  (test/test_outbox.cpp)
//   gate_app.h    LEDs, log, network conversation   (test/test_gate_app.cpp)
//   ../hhj-common WiFi plan, protocol               (hhj-common/test)
// Keep this file free of decisions: nothing here is covered by a test.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "gate_app.h"
#include "hhj/wifi_config.h"

// ---------------- CONFIG (override with -D in platformio.ini) ----------
#ifndef GATE_MODE
#define GATE_MODE        "FINISH"        // "START" / "FINISH"
#endif
#ifndef PIN_BEAM_A
#define PIN_BEAM_A        2              // receiver A: D2 (SIGNAL_IR in v1), open collector
#endif
#ifndef PIN_BEAM_B
#define PIN_BEAM_B        4              // receiver B: D4
#endif
#ifndef PIN_LED_STATE
#define PIN_LED_STATE     23             // red: beam state (LED_IR in v1)
#endif
#ifndef PIN_LED_WIFI
#define PIN_LED_WIFI      21             // blue: WiFi (LED_WIFI in v1)
#endif
#define SERIAL_BAUD       115200         // = monitor_speed in platformio.ini
#define BEAM_BROKEN_LEVEL LOW            // the open collector pulls the line low when the beam is cut
#define FW_VERSION        "2.1.0"

constexpr bool str_eq(const char* a, const char* b) { return *a == *b && (*a == '\0' || str_eq(a + 1, b + 1)); }
static_assert(str_eq(GATE_MODE, "START") || str_eq(GATE_MODE, "FINISH"), "GATE_MODE must be \"START\" or \"FINISH\"");

// ---------------------------------------------------------------------
class ArduinoGateHal final : public hhj::GateHal {
 public:
  int64_t now_ms() override { return esp_timer_get_time() / 1000; }          // 64-bit: never wraps

  bool beam_broken(uint8_t beam) override {
    return digitalRead(beam == 0 ? PIN_BEAM_A : PIN_BEAM_B) == BEAM_BROKEN_LEVEL;
  }
  void set_led(hhj::Led led, bool on) override {
    digitalWrite(led == hhj::Led::State ? PIN_LED_STATE : PIN_LED_WIFI, on ? HIGH : LOW);
  }

  bool wifi_up() override { return WiFi.status() == WL_CONNECTED; }
  void wifi_connect(hhj::WifiPlan::Net net) override {
    const bool primary = net == hhj::WifiPlan::Net::Primary;
    WiFi.disconnectAsync();                                                  // never the blocking disconnect()
    WiFi.begin(primary ? HHJ_WIFI_PRIMARY_SSID : HHJ_WIFI_FALLBACK_SSID,
               primary ? HHJ_WIFI_PRIMARY_PASS : HHJ_WIFI_FALLBACK_PASS);
  }

  void net_open() override {
    udp_.stop();
    udp_.begin(hhj::kLinkPort);
  }
  bool net_send(uint32_t ip, const char* data, size_t len) override {
    const IPAddress to = ip == hhj::kBroadcast ? WiFi.broadcastIP() : IPAddress(ip);
    if (!udp_.beginPacket(to, hhj::kLinkPort)) return false;
    udp_.write(reinterpret_cast<const uint8_t*>(data), len);
    return udp_.endPacket() == 1;
  }
  size_t net_recv(char* buf, size_t cap, uint32_t* from_ip) override {
    const int n = udp_.parsePacket();
    if (n <= 0) return 0;
    size_t got = 0;
    if (static_cast<size_t>(n) <= cap) {
      const int r = udp_.read(buf, cap);
      got = r > 0 ? static_cast<size_t>(r) : 0;
      *from_ip = static_cast<uint32_t>(udp_.remoteIP());
    }
    udp_.clear();                                                            // too long, or done: drop the rest
    return got;
  }

  int rssi() override { return WiFi.RSSI(); }

  void log(const char* line) override {
    // Never wait for the UART: a line that does not fit the TX buffer is dropped.
    if (Serial.availableForWrite() >= static_cast<int>(strlen(line)) + 2) Serial.println(line);
  }

 private:
  WiFiUDP udp_;
};

static ArduinoGateHal hal;
static hhj::GateApp* app = nullptr;

static hhj::GateAppConfig make_config() {
  hhj::GateAppConfig c;
  c.mark = str_eq(GATE_MODE, "START") ? hhj::Role::Start : hhj::Role::Finish;
  c.boot = esp_random();
  c.fw = FW_VERSION;
  c.wifi.attempt_ms = HHJ_WIFI_ATTEMPT_MS;
  c.wifi.has_fallback = sizeof(HHJ_WIFI_FALLBACK_SSID) > 1;
  return c;
}

void setup() {
  Serial.setTxBufferSize(2048);           // before begin(): log lines are queued, never waited for
  Serial.begin(SERIAL_BAUD);
  pinMode(PIN_BEAM_A, INPUT_PULLUP);      // an open collector floats without a pull-up
  pinMode(PIN_BEAM_B, INPUT_PULLUP);
  pinMode(PIN_LED_STATE, OUTPUT);
  pinMode(PIN_LED_WIFI, OUTPUT);

  WiFi.persistent(false);                 // two networks are tried in turn: do not wear the flash
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                   // radio always on: ACKs and HELLOs arrive without power-save delay
  WiFi.setAutoReconnect(true);

  static hhj::GateApp instance(hal, make_config());
  app = &instance;
  app->begin();
  Serial.printf("wifi: primary \"%s\", fallback %s%s%s\n", HHJ_WIFI_PRIMARY_SSID,
                sizeof(HHJ_WIFI_FALLBACK_SSID) > 1 ? "\"" : "", sizeof(HHJ_WIFI_FALLBACK_SSID) > 1 ? HHJ_WIFI_FALLBACK_SSID : "none",
                sizeof(HHJ_WIFI_FALLBACK_SSID) > 1 ? "\"" : "");
  Serial.printf("pins: beams A=GPIO%d B=GPIO%d (broken=%s), leds state=GPIO%d wifi=GPIO%d\n", PIN_BEAM_A, PIN_BEAM_B,
                BEAM_BROKEN_LEVEL == LOW ? "LOW" : "HIGH", PIN_LED_STATE, PIN_LED_WIFI);
}

void loop() { app->tick(); }
