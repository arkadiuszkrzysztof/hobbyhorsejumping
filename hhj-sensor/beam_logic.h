// beam_logic.h — two-beam gate: decides when a rider crossed (pure C++, no Arduino).
//
// Two IR beams, A and B, a few centimetres apart along the direction of travel.
// A crossing is reported once, when both beams are broken "together":
//
//   G-1  A break shorter than min_pulse_ms is noise: it is counted
//        (rejected_short) and has no other effect.
//   G-2  An EVENT is reported when both beams have each been broken for at
//        least min_pulse_ms, are both still broken, and their break edges are
//        at most window_ms apart.
//   G-3  The event time is the raw break edge of the beam that broke first —
//        not the moment of detection.
//   G-4  dir = +1 if beam A broke first (or both in the same millisecond),
//        -1 if beam B broke first. gap_ms = time between the two edges.
//   G-5  A real break (>= min_pulse_ms) of an armed gate that never becomes
//        part of an event is "lone": counted once (lone_a / lone_b), no event.
//        It is written off as soon as it cannot pair any more — the window
//        has passed without a partner, or it is released — and stays spent
//        until the beam is released and broken again.
//   G-6  After an event the gate is quiet (not armed). It re-arms once both
//        beams are free and refractory_ms have passed since the last real
//        break (>= min_pulse_ms) ended. One rider = one event, however long
//        they stand in the gate. Noise shorter than min_pulse_ms does not
//        prolong the quiet period.
//   G-7  stuck(): a beam has been broken for longer than stuck_ms
//        (obstacle, misaligned emitter). Diagnostic only.
//   G-8  The clock is expected to be monotonic; a step backwards is treated as
//        "no time has passed" and can never create an event.
//
// Host tests: test/test_beam_logic.cpp
#pragma once
#include <stdint.h>

namespace hhj {

struct GateConfig {
  uint16_t min_pulse_ms  = 20;     // shorter breaks are noise, not a rider
  uint16_t window_ms     = 150;    // max time between the two beams of one crossing
  uint16_t refractory_ms = 1500;   // quiet time after an event
  uint32_t stuck_ms      = 5000;   // a beam broken this long = fault / obstacle
};

struct GateEvent {
  int64_t  t_ms = 0;     // raw break edge of the FIRST beam (the sporting timestamp)
  int8_t   dir = 0;      // +1 = A first, -1 = B first
  uint16_t gap_ms = 0;   // time between the two beams
};

class GateLogic {
 public:
  explicit GateLogic(const GateConfig& c = GateConfig{}) : cfg_(c) {}

  // Call every loop with the physical state of both beams (true = broken).
  // Returns true exactly once per crossing and fills *out (if not null).
  bool update(bool a_broken, bool b_broken, int64_t now_ms, GateEvent* out) {
    if (now_ms < now_) now_ms = now_;                      // G-8
    now_ = now_ms;
    step(a_, a_broken, now_ms, &lone_a);
    step(b_, b_broken, now_ms, &lone_b);

    if (quiet_) {                                          // G-6
      // t_release_ is always later than the event: its two beams were real
      // breaks and have to be released before both can be free.
      if (!a_broken && !b_broken && now_ms - t_release_ >= cfg_.refractory_ms) quiet_ = false;
      return false;
    }

    if (a_.confirmed && b_.confirmed) {
      const bool a_first = a_.t_edge <= b_.t_edge;         // G-4: a tie counts as A first
      const int64_t first = a_first ? a_.t_edge : b_.t_edge;
      const int64_t gap = (a_first ? b_.t_edge : a_.t_edge) - first;
      if (gap <= cfg_.window_ms) {                         // G-2
        if (out != nullptr) {
          out->t_ms = first;                               // G-3
          out->dir = a_first ? int8_t{1} : int8_t{-1};
          out->gap_ms = static_cast<uint16_t>(gap);
        }
        spend(a_);
        spend(b_);
        quiet_ = true;
        events++;
        return true;
      }
      // both real, but too far apart: the older one was alone (G-5)
      if (a_first) { lone_a++; spend(a_); } else { lone_b++; spend(b_); }
    }

    // real break, window over, partner silent (G-5)
    if (a_.confirmed && !b_.candidate && now_ms - a_.t_edge > cfg_.window_ms) { lone_a++; spend(a_); }
    if (b_.confirmed && !a_.candidate && now_ms - b_.t_edge > cfg_.window_ms) { lone_b++; spend(b_); }
    return false;
  }

  // G-7
  bool stuck(int64_t now_ms) const {
    return (a_.raw && now_ms - a_.t_edge > cfg_.stuck_ms) || (b_.raw && now_ms - b_.t_edge > cfg_.stuck_ms);
  }

  // false during the quiet period after an event
  bool armed() const { return !quiet_; }
  const GateConfig& config() const { return cfg_; }

  // diagnostic counters
  uint32_t events = 0, lone_a = 0, lone_b = 0, rejected_short = 0;

 private:
  struct Beam {
    bool raw = false;         // last physical state
    bool candidate = false;   // broken, and not yet used for an event / written off
    bool confirmed = false;   // candidate that has lasted min_pulse_ms
    int64_t t_edge = 0;       // time of the last break edge
  };

  void step(Beam& bm, bool raw, int64_t now_ms, uint32_t* lone) {
    if (raw && !bm.raw) {                                  // break edge
      bm.t_edge = now_ms;
      bm.candidate = true;
    } else if (!raw && bm.raw) {                           // release edge
      if (now_ms - bm.t_edge < cfg_.min_pulse_ms) {
        rejected_short++;                                  // G-1
      } else {
        t_release_ = now_ms;                               // G-6: the quiet time counts from here
        if (!quiet_ && bm.candidate) (*lone)++;            // G-5: real, released unpaired
      }
      spend(bm);
    }
    if (bm.candidate && !bm.confirmed && now_ms - bm.t_edge >= cfg_.min_pulse_ms) bm.confirmed = true;
    bm.raw = raw;
  }

  static void spend(Beam& bm) {
    bm.candidate = false;
    bm.confirmed = false;
  }

  GateConfig cfg_;
  Beam a_, b_;
  bool quiet_ = false;
  int64_t t_release_ = 0;        // when the last real break (>= min_pulse_ms) ended
  int64_t now_ = INT64_MIN;      // last time seen, for G-8
};

}  // namespace hhj
