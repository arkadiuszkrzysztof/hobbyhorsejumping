// gate_app.h — everything a gate does, with the hardware behind an interface
// (pure C++, no Arduino). hhj-sensor.ino only implements GateHal and calls tick().
//
//   A-1  Beams are sampled on every tick(); nothing on the measuring path
//        waits for the network (UDP sends return immediately, there is no
//        HTTP and no blocking reconnect).
//   A-2  Every crossing reported by GateLogic is queued in the Outbox and
//        logged, whether or not WiFi is up; it is transmitted with its true
//        age as soon as the link allows.
//   A-3  Each copy goes unicast to every known TIMER / BACKEND and once to the
//        subnet broadcast address (PROTOCOL.md).
//   A-4  HELLO is sent every hello_ms and right after the link comes up: to
//        the broadcast address (discovery) and unicast to every known peer
//        (broadcasts get lost far more easily than unicasts).
//   A-5  Peers are learned from TIMER / BACKEND HELLOs, forgotten after
//        peer_ttl_ms of silence and when the link drops.
//   A-6  Only an ACK carrying this gate's own mark and boot nonce confirms an event.
//   A-7  Malformed or foreign datagrams are counted and otherwise ignored.
//   A-8  LEDs — WiFi LED: on = connected, slow blink = not connected.
//        State LED: slow blink until both beams have been free once; then
//        on = both beams free, off = a beam is broken, fast blink = stuck.
//
// Host tests: test/test_gate_app.cpp and hhj-common/test/test_system.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "beam_logic.h"
#include "hhj/link.h"
#include "hhj/net.h"
#include "outbox.h"

namespace hhj {

enum class Led : uint8_t { State, Wifi };

class GateHal : public NetHal {
 public:
  // beam 0 = A, 1 = B. True = the beam is physically interrupted.
  virtual bool beam_broken(uint8_t beam) = 0;
  virtual void set_led(Led led, bool on) = 0;

 protected:
  ~GateHal() = default;
};

struct GateAppConfig {
  Role mark = Role::Finish;          // Role::Start or Role::Finish
  uint32_t boot = 0;                 // random nonce, new on every power-up
  const char* fw = "2.1.0";
  GateConfig gate;
  OutboxConfig outbox;
  WifiPlanConfig wifi;
  uint32_t hello_ms = kHelloPeriodMs;
  uint32_t peer_ttl_ms = kPeerTtlMs;
  uint32_t status_log_ms = 30000;    // period of the one-line health summary
  uint16_t blink_slow_ms = 500;
  uint16_t blink_fast_ms = 100;
};

class GateApp {
 public:
  GateApp(GateHal& hal, const GateAppConfig& cfg)
      : hal_(hal), cfg_(cfg), gate_(cfg.gate), outbox_(cfg.outbox), uplink_(hal, cfg.wifi) {}

  void begin() {
    const int64_t now = hal_.now_ms();
    const GateConfig& c = gate_.config();
    logf(hal_, now, "hhj-sensor %s [%s] boot %08lx", cfg_.fw, role_name(cfg_.mark),
         static_cast<unsigned long>(cfg_.boot));
    logf(hal_, now, "gate: min_pulse %u ms, window %u ms, refractory %u ms, stuck %lu ms",
         static_cast<unsigned>(c.min_pulse_ms), static_cast<unsigned>(c.window_ms),
         static_cast<unsigned>(c.refractory_ms), static_cast<unsigned long>(c.stuck_ms));
    next_status_ms_ = now + static_cast<int64_t>(cfg_.status_log_ms);
  }

  void tick() {
    const int64_t now = hal_.now_ms();
    if (ticked_ && now - last_tick_ > max_gap_ms_) max_gap_ms_ = now - last_tick_;
    measure(now);                                             // A-1
    leds(now);
    if (!ticked_ || now != last_tick_) network(now);          // at most once per millisecond
    last_tick_ = now;
    ticked_ = true;
  }

  // HELLO "st" (PROTOCOL.md)
  uint32_t status(int64_t now) const {
    return (a_ ? kStBeamA : 0u) | (b_ ? kStBeamB : 0u) | (gate_.stuck(now) ? kStStuck : 0u) |
           (ready_ ? kStReady : 0u) | (gate_.armed() ? kStArmed : 0u);
  }

  const GateLogic& gate() const { return gate_; }
  const Outbox& outbox() const { return outbox_; }
  const Peers& peers() const { return peers_; }
  const Uplink& uplink() const { return uplink_; }
  bool ready() const { return ready_; }
  int64_t max_gap_ms() const { return max_gap_ms_; }   // longest pause between two tick() calls
  uint32_t rx_invalid() const { return rx_invalid_; }
  uint32_t tx_failed() const { return tx_failed_; }

 private:
  static constexpr int kRxPerTick = 8;
  static constexpr int kTxPerTick = 2;

  // ---------------------------------------------------------------- measuring
  void measure(int64_t now) {
    a_ = hal_.beam_broken(0);
    b_ = hal_.beam_broken(1);
    GateEvent ev;
    const bool got = gate_.update(a_, b_, now, &ev);
    log_beam(now, 'A', a_, &prev_a_, &t_break_a_);
    log_beam(now, 'B', b_, &prev_b_, &t_break_b_);
    log_lone(now, 'A', 'B', gate_.lone_a, &seen_lone_a_);
    log_lone(now, 'B', 'A', gate_.lone_b, &seen_lone_b_);
    if (got) {                                                // A-2
      const uint32_t evicted = outbox_.evicted;
      const uint32_t seq = outbox_.push(ev, now);
      logf(hal_, now, "EVENT #%lu t=%lld dir=%s gap=%u ms (lone A/B %lu/%lu, short %lu)",
           static_cast<unsigned long>(seq), static_cast<long long>(ev.t_ms), ev.dir > 0 ? "A->B" : "B->A",
           static_cast<unsigned>(ev.gap_ms), static_cast<unsigned long>(gate_.lone_a),
           static_cast<unsigned long>(gate_.lone_b), static_cast<unsigned long>(gate_.rejected_short));
      if (outbox_.evicted != evicted) logf(hal_, now, "outbox full -> oldest unconfirmed event discarded");
    }
    const bool armed = gate_.armed();
    if (armed && !prev_armed_) logf(hal_, now, "gate armed");
    prev_armed_ = armed;

    const bool stuck = gate_.stuck(now);
    if (stuck != prev_stuck_) {
      if (stuck) {
        logf(hal_, now, "STUCK: a beam has been broken > %lu ms (obstacle / misaligned?)",
             static_cast<unsigned long>(gate_.config().stuck_ms));
      } else {
        logf(hal_, now, "stuck cleared");
      }
      prev_stuck_ = stuck;
    }
    if (!ready_ && !a_ && !b_) {
      ready_ = true;
      logf(hal_, now, "both beams free -> gate ready");
    }
  }

  void log_beam(int64_t now, char name, bool broken, bool* prev, int64_t* t_break) {
    if (broken == *prev) return;
    if (broken) {
      *t_break = now;
      logf(hal_, now, "%c broken", name);
    } else {
      const int64_t d = now - *t_break;
      logf(hal_, now, "%c free after %lld ms%s", name, static_cast<long long>(d),
           d < gate_.config().min_pulse_ms ? " -> too short, ignored" : "");
    }
    *prev = broken;
  }

  void log_lone(int64_t now, char name, char other, uint32_t count, uint32_t* seen) {
    if (count == *seen) return;
    *seen = count;
    logf(hal_, now, "%c alone: no %c within %u ms -> ignored (total %lu)", name, other,
         static_cast<unsigned>(gate_.config().window_ms), static_cast<unsigned long>(count));
  }

  // --------------------------------------------------------------------- LEDs
  void leds(int64_t now) {                                    // A-8
    const bool slow = cfg_.blink_slow_ms != 0 && (now / cfg_.blink_slow_ms) % 2 != 0;
    const bool fast = cfg_.blink_fast_ms != 0 && (now / cfg_.blink_fast_ms) % 2 != 0;
    hal_.set_led(Led::Wifi, uplink_.up() ? true : slow);
    bool state = slow;
    if (ready_) state = gate_.stuck(now) ? fast : !(a_ || b_);
    hal_.set_led(Led::State, state);
  }

  // ------------------------------------------------------------------ network
  void network(int64_t now) {
    const bool up = uplink_.tick(now);
    if (now >= next_status_ms_) {
      next_status_ms_ = now + static_cast<int64_t>(cfg_.status_log_ms);
      logf(hal_, now, "status: wifi %s, peers %u, events %lu, unconfirmed %u, longest loop pause %lld ms, "
                      "bad datagrams %lu, failed sends %lu",
           up ? "up" : "down", static_cast<unsigned>(peers_.count()),
           static_cast<unsigned long>(gate_.events), static_cast<unsigned>(outbox_.pending()),
           static_cast<long long>(max_gap_ms_), static_cast<unsigned long>(rx_invalid_),
           static_cast<unsigned long>(tx_failed_));
    }
    if (!up) {
      if (peers_.count() != 0) peers_.clear();                // A-5
      return;
    }
    if (uplink_.came_up()) next_hello_ms_ = now;              // A-4
    receive(now);
    const size_t gone = peers_.expire(now, cfg_.peer_ttl_ms); // A-5
    if (gone != 0) logf(hal_, now, "%u peer(s) silent -> forgotten", static_cast<unsigned>(gone));
    transmit(now);
    if (now >= next_hello_ms_) {
      next_hello_ms_ = now + static_cast<int64_t>(cfg_.hello_ms);
      send_hello(now);
    }
  }

  void receive(int64_t now) {
    char buf[kMaxDatagram];
    for (int i = 0; i < kRxPerTick; i++) {
      uint32_t from = 0;
      const size_t n = hal_.net_recv(buf, sizeof(buf), &from);
      if (n == 0) break;
      Msg m;
      if (!decode(buf, n, &m)) {                              // A-7
        rx_invalid_++;
        continue;
      }
      if (m.kind == Msg::Kind::Hello) {
        if (m.hello.role == Role::Timer || m.hello.role == Role::Backend) {
          if (peers_.touch(from, m.hello.role, now)) {
            logf(hal_, now, "%s found at %u.%u.%u.%u", role_name(m.hello.role),
                 static_cast<unsigned>(from & 0xFFu), static_cast<unsigned>((from >> 8) & 0xFFu),
                 static_cast<unsigned>((from >> 16) & 0xFFu), static_cast<unsigned>((from >> 24) & 0xFFu));
          }
        }
      } else if (m.kind == Msg::Kind::Ack) {
        if (m.ack.mark == cfg_.mark && m.ack.boot == cfg_.boot && outbox_.ack(m.ack.seq)) {   // A-6
          logf(hal_, now, "event #%lu confirmed by backend", static_cast<unsigned long>(m.ack.seq));
        }
      }
    }
  }

  void transmit(int64_t now) {
    const uint32_t expired = outbox_.expired;
    Outbox::Tx tx;
    for (int i = 0; i < kTxPerTick && outbox_.poll(now, &tx); i++) send_event(tx, now);
    if (outbox_.expired != expired) {
      logf(hal_, now, "%lu event(s) never confirmed by a backend -> given up",
           static_cast<unsigned long>(outbox_.expired - expired));
    }
  }

  void send_event(const Outbox::Tx& tx, int64_t now) {        // A-3
    EventMsg e;
    e.mark = cfg_.mark;
    e.boot = cfg_.boot;
    e.seq = tx.seq;
    e.t_ms = tx.t_ms;
    e.dir = tx.dir;
    e.gap_ms = tx.gap_ms;
    e.n = tx.n;
    char buf[160];
    unsigned unicast = 0;
    for (size_t i = 0; i <= Peers::kMax; i++) {               // every peer, then the broadcast
      uint32_t ip = kBroadcast;
      if (i < Peers::kMax) {
        const Peer& p = peers_.slot(i);
        if (!p.used) continue;
        ip = p.ip;
        unicast++;
      }
      e.age_ms = event_age_ms(hal_.now_ms(), tx.t_ms);        // fresh for every datagram
      const size_t len = encode(e, buf, sizeof(buf));
      if (len == 0 || !hal_.net_send(ip, buf, len)) tx_failed_++;
    }
    if (tx.n == 0) {
      logf(hal_, now, "event #%lu sent, %lu ms after the cut, to %u peer(s) + broadcast",
           static_cast<unsigned long>(tx.seq), static_cast<unsigned long>(tx.age_ms), unicast);
    }
  }

  void send_hello(int64_t now) {
    HelloMsg h;
    h.role = cfg_.mark;
    h.boot = cfg_.boot;
    h.up_ms = now;
    snprintf(h.fw, sizeof(h.fw), "%s", cfg_.fw);
    h.st = status(now);
    h.seq = outbox_.last_seq();
    h.pending = outbox_.pending();
    h.rssi = clamp_rssi(hal_.rssi());
    h.net = uplink_.net() == WifiPlan::Net::Fallback ? 1 : 0;
    char buf[200];
    const size_t len = encode(h, buf, sizeof(buf));
    if (len == 0) {
      tx_failed_++;
      return;
    }
    for (size_t i = 0; i < Peers::kMax; i++) {
      const Peer& p = peers_.slot(i);
      if (p.used && !hal_.net_send(p.ip, buf, len)) tx_failed_++;
    }
    if (!hal_.net_send(kBroadcast, buf, len)) tx_failed_++;
  }

  GateHal& hal_;
  GateAppConfig cfg_;
  GateLogic gate_;
  Outbox outbox_;
  Uplink uplink_;
  Peers peers_;

  bool ticked_ = false;
  int64_t last_tick_ = 0;
  int64_t max_gap_ms_ = 0;
  int64_t next_hello_ms_ = 0;
  int64_t next_status_ms_ = 0;

  bool a_ = false, b_ = false;       // current physical beam state
  bool ready_ = false;               // both beams have been free at least once
  bool prev_a_ = false, prev_b_ = false, prev_armed_ = true, prev_stuck_ = false;
  int64_t t_break_a_ = 0, t_break_b_ = 0;
  uint32_t seen_lone_a_ = 0, seen_lone_b_ = 0;
  uint32_t rx_invalid_ = 0, tx_failed_ = 0;
};

}  // namespace hhj
