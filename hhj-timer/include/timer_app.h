// timer_app.h — everything the display unit does, with the hardware behind an
// interface (pure C++, no Arduino). src/main.cpp only implements TimerHal and
// calls tick().
//
//   T-1  Power-up: all segments lit (8888) for selftest_ms.
//   T-2  Idle, WiFi down: four middle bars, blinking.
//   T-3  Idle, WiFi up: two glyphs per gate — START gate on the left pair,
//        FINISH gate on the right pair:
//          middle bar  the gate is online and ready
//          top bar     the gate is online but not OK (beam blocked / stuck /
//                      never aligned yet)
//          bottom bar  the gate has not been heard from for peer_ttl_ms
//   T-4  Running: the time since the START edge.
//   T-5  Finished: the result, blinking for result_blink_ms, then steady until
//        the next START.
//   T-6  The display is rewritten when the frame changes and at least every
//        refresh_ms, so a glitched shift register heals by itself.
//   T-7  Event copies are fed to the Stopwatch with the local arrival time;
//        everything else about them is the Stopwatch's business.
//   T-8  HELLO is sent every hello_ms and right after the link comes up — to
//        the broadcast address and unicast to every gate heard from — so the
//        gates learn, and keep knowing, where to unicast.
//   T-9  Serial console: s = start, f = finish, r = reset, t = lamp test.
//   T-10 Malformed datagrams are counted and otherwise ignored.
//   T-11 The log gets one line whenever an event changes what is shown, and
//        one line the first time an event is ignored. Further copies of an
//        event are silent (a gate repeats an unconfirmed event every second
//        for minutes).
//   T-12 While a backend sends SHOW datagrams the display shows what they say
//        (remote.h): 0.00, the running clock, the result (blinking like T-5),
//        or the idle picture (T-2 / T-3). The gates' events still feed the
//        own stopwatch, which takes the display back when the backend falls
//        silent. Taking over and handing back are logged.
//
// Host tests: test/test_timer_app.cpp and hhj-common/test/test_system.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "display.h"
#include "hhj/link.h"
#include "hhj/net.h"
#include "remote.h"
#include "stopwatch.h"

namespace hhj {

class TimerHal : public NetHal {
 public:
  // Shift these four bytes out (index 0 first) and latch them.
  virtual void show(const uint8_t chain[4]) = 0;
  // Next character typed on the serial console, or -1.
  virtual int read_key() = 0;

 protected:
  ~TimerHal() = default;
};

struct TimerAppConfig {
  uint32_t boot = 0;                   // random nonce, new on every power-up
  const char* fw = "1.1.0";
  Segments segments{};                 // wiring: which bit drives which segment
  bool chain_first_is_left = true;     // wiring: first chip in the chain = left digit
  StopwatchConfig stopwatch;
  RemoteConfig remote;
  WifiPlanConfig wifi;
  uint32_t hello_ms = kHelloPeriodMs;
  uint32_t peer_ttl_ms = kPeerTtlMs;
  uint32_t selftest_ms = 1000;
  uint32_t lamp_test_ms = 2000;
  uint32_t refresh_ms = 1000;
  uint32_t result_blink_ms = 3000;
  uint16_t result_blink_half_ms = 300;
  uint16_t idle_blink_half_ms = 500;
};

class TimerApp {
 public:
  TimerApp(TimerHal& hal, const TimerAppConfig& cfg)
      : hal_(hal), cfg_(cfg), sw_(cfg.stopwatch), remote_(cfg.remote), uplink_(hal, cfg.wifi) {}

  void begin() {
    t_boot_ = hal_.now_ms();
    logf(hal_, t_boot_, "hhj-timer %s boot %08lx - waiting for START/FINISH on UDP %u", cfg_.fw,
         static_cast<unsigned long>(cfg_.boot), static_cast<unsigned>(kLinkPort));
    begun_ = true;
  }

  void tick() {
    if (!begun_) begin();
    const int64_t now = hal_.now_ms();
    keys(now);
    network(now);
    if (sw_.tick(now)) logf(hal_, now, "no FINISH for %lu ms -> back to ready",
                            static_cast<unsigned long>(cfg_.stopwatch.run_timeout_ms));
    const bool remote_now = remote_.active(now);                                                  // T-12
    if (remote_now != was_remote_) {
      logf(hal_, now, remote_now ? "backend took over the display" : "backend silent -> showing the own stopwatch");
      was_remote_ = remote_now;
    }
    const Frame f = render(now);
    if (!shown_valid_ || f != shown_ || now - t_shown_ >= static_cast<int64_t>(cfg_.refresh_ms)) {   // T-6
      uint8_t chain[4];
      frame_to_chain(f, cfg_.chain_first_is_left, chain);
      hal_.show(chain);
      shown_ = f;
      shown_valid_ = true;
      t_shown_ = now;
    }
  }

  // What is on the display, whoever decides it.
  enum class View : uint8_t { Idle, Ready, Running, Result };
  struct Shown {
    View view;
    int64_t ms;            // Running: the clock; Result: the time; otherwise 0
    int64_t since_ms;      // Result: when it appeared (for the blinking)
    explicit Shown(View v, int64_t time_ms = 0, int64_t since = 0) : view(v), ms(time_ms), since_ms(since) {}
  };

  Shown shown(int64_t now) const {
    if (remote_.active(now)) {                                                                    // T-12
      switch (remote_.state()) {
        case Show::Run:    return Shown(View::Running, remote_.shown_ms(now));
        case Show::Result: return Shown(View::Result, remote_.shown_ms(now), remote_.result_since_ms());
        case Show::Ready:  return Shown(View::Ready);
        default:           return Shown(View::Idle);
      }
    }
    switch (sw_.state()) {
      case Stopwatch::State::Running:  return Shown(View::Running, sw_.shown_ms(now));            // T-4
      case Stopwatch::State::Finished: return Shown(View::Result, sw_.shown_ms(now), sw_.finished_at_ms());
      default:                         return Shown(View::Idle);
    }
  }

  // What the digits should show right now.
  Frame render(int64_t now) const {
    const Segments& s = cfg_.segments;
    if (now - t_boot_ < static_cast<int64_t>(cfg_.selftest_ms) || now < lamp_until_) {            // T-1
      return frame_fill(glyph_digit(s, 8));
    }
    const Shown v = shown(now);
    switch (v.view) {
      case View::Running:
      case View::Ready:
        return frame_time(s, v.ms);
      case View::Result:                                                                          // T-5
        return result_blanked(now - v.since_ms) ? frame_fill(kBlank) : frame_time(s, v.ms);
      case View::Idle:
        break;
    }
    if (!uplink_.up()) {                                                                          // T-2
      const bool blank = cfg_.idle_blink_half_ms != 0 && (now / cfg_.idle_blink_half_ms) % 2 == 1;
      return frame_fill(blank ? kBlank : glyph_dash(s));
    }
    const uint8_t l = gate_glyph(start_gate_, now);                                               // T-3
    const uint8_t r = gate_glyph(finish_gate_, now);
    return Frame{{l, l, r, r}};
  }

  const Stopwatch& stopwatch() const { return sw_; }
  const Uplink& uplink() const { return uplink_; }
  const RemoteDisplay& remote() const { return remote_; }
  bool gate_online(Role mark, int64_t now) const { return online(mark == Role::Start ? start_gate_ : finish_gate_, now); }
  uint32_t rx_invalid() const { return rx_invalid_; }
  uint32_t tx_failed() const { return tx_failed_; }

 private:
  static constexpr int kRxPerTick = 8;
  static constexpr int kKeysPerTick = 8;

  struct GateSeen {
    bool have = false;
    int64_t seen_ms = 0;
    uint32_t st = 0;
  };

  // The "off" half of the blinking that announces a fresh result; `since` = how long it has been shown.
  bool result_blanked(int64_t since) const {
    return since < static_cast<int64_t>(cfg_.result_blink_ms) && cfg_.result_blink_half_ms != 0 &&
           (since / cfg_.result_blink_half_ms) % 2 == 1;
  }

  bool online(const GateSeen& g, int64_t now) const {
    return g.have && now - g.seen_ms <= static_cast<int64_t>(cfg_.peer_ttl_ms);
  }

  uint8_t gate_glyph(const GateSeen& g, int64_t now) const {
    if (!online(g, now)) return glyph_low(cfg_.segments);
    return gate_ok(g.st) ? glyph_dash(cfg_.segments) : glyph_high(cfg_.segments);
  }

  void keys(int64_t now) {                                                                        // T-9
    for (int i = 0; i < kKeysPerTick; i++) {
      const int c = hal_.read_key();
      if (c < 0) break;
      if (c == 's') {
        report(now, "START (console)", sw_.manual_start(now));
      } else if (c == 'f') {
        report(now, "FINISH (console)", sw_.manual_finish(now));
      } else if (c == 'r') {
        sw_.reset(now);
        logf(hal_, now, "reset");
      } else if (c == 't') {
        lamp_until_ = now + static_cast<int64_t>(cfg_.lamp_test_ms);
        logf(hal_, now, "lamp test");
      }
    }
  }

  void network(int64_t now) {
    const bool up = uplink_.tick(now);
    if (!up) {
      start_gate_.have = false;        // what we knew about the gates is void without a link
      finish_gate_.have = false;
      if (gates_.count() != 0) gates_.clear();
      return;
    }
    if (uplink_.came_up()) next_hello_ms_ = now;                                                  // T-8
    char buf[kMaxDatagram];
    for (int i = 0; i < kRxPerTick; i++) {
      uint32_t from = 0;
      const size_t n = hal_.net_recv(buf, sizeof(buf), &from);
      if (n == 0) break;
      Msg m;
      if (!decode(buf, n, &m)) {                                                                  // T-10
        rx_invalid_++;
        continue;
      }
      if (m.kind == Msg::Kind::Event) {
        const EventMsg& e = m.event;
        const Stopwatch::Outcome o = sw_.on_event(e.mark, e.boot, e.seq, e.age_ms, now);          // T-7
        const bool news = o == Stopwatch::Outcome::Started || o == Stopwatch::Outcome::Restarted ||
                          o == Stopwatch::Outcome::Finished;
        if (news || first_time_reported(e)) {                                                     // T-11
          char what[64];
          snprintf(what, sizeof(what), "%s #%lu (copy %u, age %lu ms)", role_name(e.mark),
                   static_cast<unsigned long>(e.seq), static_cast<unsigned>(e.n),
                   static_cast<unsigned long>(e.age_ms));
          report(now, what, o);
        }
      } else if (m.kind == Msg::Kind::Show) {
        remote_.on_show(m.show, now);                                                             // T-12
      } else if (m.kind == Msg::Kind::Hello && is_gate(m.hello.role)) {
        GateSeen& g = m.hello.role == Role::Start ? start_gate_ : finish_gate_;
        if (!online(g, now)) logf(hal_, now, "%s gate online", role_name(m.hello.role));
        g.have = true;
        g.seen_ms = now;
        g.st = m.hello.st;
        gates_.touch(from, m.hello.role, now);
      }
    }
    gates_.expire(now, cfg_.peer_ttl_ms);
    if (now >= next_hello_ms_) {
      next_hello_ms_ = now + static_cast<int64_t>(cfg_.hello_ms);
      send_hello(now);
    }
  }

  // True the first time this event is seen here; remembers the last kSeen events.
  bool first_time_reported(const EventMsg& e) {
    for (const SeenEvent& s : seen_) {
      if (s.used && s.mark == e.mark && s.boot == e.boot && s.seq == e.seq) return false;
    }
    seen_[seen_next_] = SeenEvent{true, e.mark, e.boot, e.seq};
    seen_next_ = (seen_next_ + 1) % kSeen;
    return true;
  }

  // One log line for an outcome a person would want to know about; a further
  // copy of a known event that changes nothing (Duplicate / Refined) is silent.
  void report(int64_t now, const char* what, Stopwatch::Outcome o) {
    switch (o) {
      case Stopwatch::Outcome::Finished:
        log_result(now, what, "");
        break;
      case Stopwatch::Outcome::Started:
      case Stopwatch::Outcome::Restarted:
        if (sw_.state() == Stopwatch::State::Finished) {
          log_result(now, what, "its FINISH was already known: ");
        } else {
          logf(hal_, now, "%s -> %s", what, o == Stopwatch::Outcome::Started ? "clock started" : "clock restarted");
        }
        break;
      case Stopwatch::Outcome::Buffered: logf(hal_, now, "%s -> does not end a run, noted", what); break;
      case Stopwatch::Outcome::Old:      logf(hal_, now, "%s -> older than what is shown, ignored", what); break;
      case Stopwatch::Outcome::Stale:    logf(hal_, now, "%s -> stale, ignored", what); break;
      default: break;
    }
  }

  void log_result(int64_t now, const char* what, const char* note) {
    const int64_t ms = sw_.shown_ms(now);
    logf(hal_, now, "%s -> %sRESULT %lld.%02lld s", what, note, static_cast<long long>(ms / 1000),
         static_cast<long long>((ms % 1000) / 10));
  }

  void send_hello(int64_t now) {
    HelloMsg h;
    h.role = Role::Timer;
    h.boot = cfg_.boot;
    h.up_ms = now;
    snprintf(h.fw, sizeof(h.fw), "%s", cfg_.fw);
    const Shown v = shown(now);
    h.st = v.view == View::Running ? kStRunning : (v.view == View::Result ? kStFinished : kStIdle);
    h.ms = clamp_u32(v.ms);
    h.rssi = clamp_rssi(hal_.rssi());
    h.net = uplink_.net() == WifiPlan::Net::Fallback ? 1 : 0;
    char buf[200];
    const size_t len = encode(h, buf, sizeof(buf));
    if (len == 0) {
      tx_failed_++;
      return;
    }
    for (size_t i = 0; i < Peers::kMax; i++) {
      const Peer& p = gates_.slot(i);
      if (p.used && !hal_.net_send(p.ip, buf, len)) tx_failed_++;
    }
    if (!hal_.net_send(kBroadcast, buf, len)) tx_failed_++;
  }

  TimerHal& hal_;
  TimerAppConfig cfg_;
  Stopwatch sw_;
  RemoteDisplay remote_;
  Uplink uplink_;

  bool begun_ = false;
  bool was_remote_ = false;
  int64_t t_boot_ = 0;
  int64_t lamp_until_ = 0;
  int64_t next_hello_ms_ = 0;
  GateSeen start_gate_, finish_gate_;
  Peers gates_;                        // where the gates are, for unicast HELLOs

  static constexpr size_t kSeen = 16;  // two gates, eight events each in their outboxes
  struct SeenEvent {
    bool used = false;
    Role mark = Role::None;
    uint32_t boot = 0, seq = 0;
  };
  SeenEvent seen_[kSeen];
  size_t seen_next_ = 0;
  Frame shown_{};
  bool shown_valid_ = false;
  int64_t t_shown_ = 0;
  uint32_t rx_invalid_ = 0, tx_failed_ = 0;
};

}  // namespace hhj
