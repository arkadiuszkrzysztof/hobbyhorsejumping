// sim_devices.h — the real gate and timer applications running on fake
// hardware inside the simulated network of sim.h.
#pragma once
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "config.h"      // hhj-timer wiring: SEG_x, CHAIN_FIRST_IS_LEFT
#include "gate_app.h"
#include "sim.h"
#include "timer_app.h"

namespace sim {

// The production segment wiring of the display.
inline hhj::Segments wiring() { return hhj::Segments{SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G}; }

// ---------------------------------------------------------------- gate
class FakeGate final : public NodeHal<hhj::GateHal> {
 public:
  FakeGate(World& w, uint32_t address) : NodeHal<hhj::GateHal>(w, address) {}

  bool beam[2] = {false, false};       // true = broken
  bool led[2] = {false, false};        // [Led::State, Led::Wifi]

  // Break / free a beam at true time `at`.
  void at(int64_t when, int which, bool broken) {
    script_.push_back(Edge{when, which, broken});
    std::stable_sort(script_.begin() + static_cast<long>(next_), script_.end(),
                     [](const Edge& x, const Edge& y) { return x.when < y.when; });
  }
  // A rider: first beam at `when`, the second `gap` ms later, each broken for `len` ms.
  void crossing(int64_t when, int64_t gap = 40, int64_t len = 250, bool a_first = true) {
    const int first = a_first ? 0 : 1;
    at(when, first, true);
    at(when + gap, 1 - first, true);
    at(when + len, first, false);
    at(when + gap + len, 1 - first, false);
  }

  bool beam_broken(uint8_t idx) override {
    while (next_ < script_.size() && script_[next_].when <= world.t) {
      beam[script_[next_].which] = script_[next_].broken;
      next_++;
    }
    return beam[idx];
  }
  void set_led(hhj::Led which, bool on) override { led[static_cast<int>(which)] = on; }

 private:
  struct Edge {
    int64_t when;
    int which;
    bool broken;
  };
  std::vector<Edge> script_;
  size_t next_ = 0;
};

struct GateUnit {
  FakeGate hal;
  hhj::GateApp app;

  static hhj::GateAppConfig config(hhj::Role mark, uint32_t boot) {
    hhj::GateAppConfig c;
    c.mark = mark;
    c.boot = boot;
    return c;
  }
  GateUnit(World& w, uint32_t address, hhj::Role mark, uint32_t boot) : hal(w, address), app(hal, config(mark, boot)) {
    app.begin();
  }
  GateUnit(World& w, uint32_t address, const hhj::GateAppConfig& c) : hal(w, address), app(hal, c) { app.begin(); }
  void tick() { app.tick(); }
};

// --------------------------------------------------------------- timer
class FakeTimer final : public NodeHal<hhj::TimerHal> {
 public:
  FakeTimer(World& w, uint32_t address) : NodeHal<hhj::TimerHal>(w, address) {}

  uint8_t chain[4] = {0, 0, 0, 0};     // the bytes last shifted out, in shift order
  int shows = 0;
  std::vector<int64_t> show_times;
  std::deque<int> keys;

  void show(const uint8_t c[4]) override {
    memcpy(chain, c, 4);
    shows++;
    show_times.push_back(world.t);
  }
  int read_key() override {
    if (keys.empty()) return -1;
    const int c = keys.front();
    keys.pop_front();
    return c;
  }
  void type(const char* s) {
    for (; *s != '\0'; s++) keys.push_back(*s);
  }
};

// One glyph as a character: digit, '-' middle bar, '_' bottom bar, '^' top bar,
// ' ' blank, '?' anything else.
inline char glyph_char(const hhj::Segments& s, uint8_t g) {
  for (unsigned d = 0; d < 10; d++) {
    if (g == hhj::glyph_digit(s, d)) return static_cast<char>('0' + d);
  }
  if (g == hhj::glyph_dash(s)) return '-';
  if (g == hhj::glyph_low(s)) return '_';
  if (g == hhj::glyph_high(s)) return '^';
  if (g == hhj::kBlank) return ' ';
  return '?';
}

inline std::string frame_text(const hhj::Segments& s, const hhj::Frame& f) {
  std::string out;
  for (uint8_t g : f.d) out += glyph_char(s, g);
  return out;
}

struct TimerUnit {
  FakeTimer hal;
  hhj::TimerApp app;

  static hhj::TimerAppConfig config(uint32_t boot) {
    hhj::TimerAppConfig c;
    c.boot = boot;
    c.segments = wiring();
    c.chain_first_is_left = CHAIN_FIRST_IS_LEFT != 0;
    return c;
  }
  TimerUnit(World& w, uint32_t address, uint32_t boot = 0x71AE5u) : hal(w, address), app(hal, config(boot)) { app.begin(); }
  TimerUnit(World& w, uint32_t address, const hhj::TimerAppConfig& c) : hal(w, address), app(hal, c) { app.begin(); }
  void tick() { app.tick(); }

  // What a person sees on the four digits, left to right.
  std::string text() const {
    hhj::Frame f{};
    for (int i = 0; i < 4; i++) f.d[i] = (CHAIN_FIRST_IS_LEFT != 0) ? hal.chain[3 - i] : hal.chain[i];
    return frame_text(wiring(), f);
  }
  // The same as a number of milliseconds, or -1 if it is not a time below 100 s.
  int64_t shown_ms() const {
    const std::string t = text();
    if (t[0] != ' ' && (t[0] < '0' || t[0] > '9')) return -1;
    for (int i = 1; i < 4; i++) {
      if (t[static_cast<size_t>(i)] < '0' || t[static_cast<size_t>(i)] > '9') return -1;
    }
    const int tens = t[0] == ' ' ? 0 : t[0] - '0';
    return (tens * 10 + (t[1] - '0')) * 1000 + (t[2] - '0') * 100 + (t[3] - '0') * 10;
  }
};

}  // namespace sim
