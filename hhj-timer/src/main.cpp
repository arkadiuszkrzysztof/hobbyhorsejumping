// hhj-timer v1.1 — 4-digit stopwatch display, XIAO ESP32-C3 + 4x TPIC6B595.
//
// This file is only the hardware glue. Everything the display decides lives in
// plain C++ that is tested on the host (make test-firmware):
//   include/display.h    digits and frames          (test/test_display.cpp)
//   include/stopwatch.h  START/FINISH -> time       (test/test_stopwatch.cpp)
//   include/timer_app.h  what to show, when, hello  (test/test_timer_app.cpp)
//   ../hhj-common        WiFi plan, protocol        (hhj-common/test)
// Keep this file free of decisions: nothing here is covered by a test.
//
// Serial console (115200): s = start, f = finish, r = reset, t = lamp test.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "config.h"
#include "display.h"
#include "hhj/wifi_config.h"
#include "timer_app.h"

#define FW_VERSION "1.1.0"

static const hhj::Segments kSegments = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G};

// Shift four bytes into the chain (index 0 first) and latch them.
static void shift_out(const uint8_t chain[4]) {
  for (int i = 0; i < 4; i++) shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, chain[i]);
  digitalWrite(PIN_LATCH, HIGH);
  digitalWrite(PIN_LATCH, LOW);
}

// The XIAO C3's USB port stalls the program when a cable is plugged in but
// nobody reads the log (the buffer fills and every write waits). So a line is
// only written when it fits; otherwise it is simply lost.
static void log_line(const char* line) {
  const int n = static_cast<int>(strlen(line));
  if (Serial.availableForWrite() >= n + 2) {
    Serial.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
    Serial.write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
  }
}

#if TEST_MODE
// --------------------------------------------------------- HARDWARE TEST
// Lights every segment of every digit in turn (the name is printed on the
// serial monitor), then 0-9 on all digits, then 1234.
//   a segment other than the one named  -> fix SEG_x in include/config.h
//   1234 reads backwards                -> CHAIN_FIRST_IS_LEFT 0
static const char* SEGN[7] = {"G (pin 4)", "F (pin 5)", "A (pin 6)", "B (pin 7)", "C (pin 14)", "D (pin 15)", "E (pin 16)"};

static void show_frame(const hhj::Frame& f) {
  uint8_t chain[4];
  hhj::frame_to_chain(f, CHAIN_FIRST_IS_LEFT != 0, chain);
  shift_out(chain);
}

void setup() {
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  show_frame(hhj::frame_fill(hhj::kBlank));
  Serial.begin(115200);
}

void loop() {
  char line[64];
  for (int d = 0; d < 4; d++) {                 // 1) each digit: one segment after another
    for (int b = 0; b < 7; b++) {
      hhj::Frame f = hhj::frame_fill(hhj::kBlank);
      f.d[d] = static_cast<uint8_t>(1 << b);
      show_frame(f);
      snprintf(line, sizeof(line), "digit %d (from the left), segment %s", d + 1, SEGN[b]);
      log_line(line);
      delay(350);
    }
  }
  for (unsigned n = 0; n < 10; n++) {           // 2) 0000..9999
    show_frame(hhj::frame_fill(hhj::glyph_digit(kSegments, n)));
    snprintf(line, sizeof(line), "all digits: %u", n);
    log_line(line);
    delay(500);
  }
  show_frame(hhj::Frame{{hhj::glyph_digit(kSegments, 1), hhj::glyph_digit(kSegments, 2),
                         hhj::glyph_digit(kSegments, 3), hhj::glyph_digit(kSegments, 4)}});
  log_line("1234 = digit order from the left");
  delay(2500);
}

#else
// ------------------------------------------------------------- STOPWATCH
class ArduinoTimerHal final : public hhj::TimerHal {
 public:
  int64_t now_ms() override { return esp_timer_get_time() / 1000; }          // 64-bit: never wraps

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
  void log(const char* line) override { log_line(line); }

  void show(const uint8_t chain[4]) override { shift_out(chain); }
  int read_key() override { return Serial.available() > 0 ? Serial.read() : -1; }

 private:
  WiFiUDP udp_;
};

static ArduinoTimerHal hal;
static hhj::TimerApp* app = nullptr;

static hhj::TimerAppConfig make_config() {
  hhj::TimerAppConfig c;
  c.boot = esp_random();
  c.fw = FW_VERSION;
  c.segments = kSegments;
  c.chain_first_is_left = CHAIN_FIRST_IS_LEFT != 0;
  c.stopwatch.min_run_ms = MIN_RUN_MS;
  c.stopwatch.run_timeout_ms = RUN_TIMEOUT_MS;
  c.result_blink_ms = RESULT_BLINK_MS;
  c.wifi.attempt_ms = HHJ_WIFI_ATTEMPT_MS;
  c.wifi.has_fallback = sizeof(HHJ_WIFI_FALLBACK_SSID) > 1;
  return c;
}

void setup() {
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  const uint8_t blank[4] = {0, 0, 0, 0};
  shift_out(blank);                       // the registers hold random data after power-up
  Serial.begin(115200);

  WiFi.persistent(false);                 // two networks are tried in turn: do not wear the flash
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                   // radio always on: an event must not wait for a power-save wake-up
  WiFi.setAutoReconnect(true);

  static hhj::TimerApp instance(hal, make_config());
  app = &instance;
  app->begin();
}

void loop() {
  app->tick();
  delay(1);
}
#endif
