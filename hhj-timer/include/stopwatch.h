// stopwatch.h — turns START / FINISH events into a running time and a result
// (pure C++, no Arduino, no heap).
//
// Every copy of an event says how long ago the beam was cut (age_ms), so the
// stopwatch works on its own clock: edge = arrival - age_ms. Copies can be
// lost, duplicated, delayed and reordered; the outcome must only depend on
// WHICH events happened and WHEN, not on how their datagrams travelled:
//
//   S-1  For each event the smallest edge seen over all its copies is used
//        (delay only ever makes a copy late).
//   S-2  A run is defined by the newest START. A newer START restarts the
//        clock, also while running or showing a result.
//   S-3  The run ends at the EARLIEST FINISH that lies at least min_run_ms
//        after the START. Later FINISHes do not change the result.
//   S-4  A FINISH that arrives before its START is known is remembered and
//        applied when the START arrives.
//   S-5  A copy older than max_age_ms is ignored (stale).
//   S-6  A START that is not newer than what is already known is ignored:
//        same gate boot and a lower sequence number, or an edge that is not
//        after the last START / the last reset.
//   S-7  A run without a FINISH is abandoned after run_timeout_ms.
//   S-8  reset() returns to idle; nothing that happened before it can start
//        or end a run afterwards.
//   S-9  A result stays until the next START or reset.
//
// Host tests: test/test_stopwatch.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "hhj/link.h"

namespace hhj {

struct StopwatchConfig {
  uint32_t min_run_ms = 1000;          // a FINISH sooner than this after START is not the finish
  uint32_t run_timeout_ms = 600000;    // no FINISH for this long: back to idle
  uint32_t max_age_ms = kMaxAgeMs;     // older copies are stale
};

class Stopwatch {
 public:
  enum class State : uint8_t { Idle, Running, Finished };

  enum class Outcome : uint8_t {
    Started,     // idle / result -> running
    Restarted,   // running -> running from a newer START
    Finished,    // the run got its result (or an earlier FINISH replaced it)
    Refined,     // a better edge for an event already in use
    Duplicate,   // copy of a known event, nothing new
    Buffered,    // a FINISH that does not end the current run (kept for S-4)
    Old,         // S-6
    Stale,       // S-5
    Invalid      // not a START / FINISH
  };

  explicit Stopwatch(const StopwatchConfig& c = StopwatchConfig{}) : cfg_(c) {}

  // One received copy of a gate event. arrival_ms = local clock when it arrived.
  Outcome on_event(Role mark, uint32_t boot, uint32_t seq, uint32_t age_ms, int64_t arrival_ms) {
    if (!is_gate(mark)) return Outcome::Invalid;
    if (age_ms > cfg_.max_age_ms) return Outcome::Stale;                       // S-5
    const int64_t edge = arrival_ms - static_cast<int64_t>(age_ms);
    const Key key{true, boot, seq};
    return mark == Role::Start ? on_start(key, edge, arrival_ms) : on_finish(key, edge, arrival_ms);
  }

  // Operator pressed "start" / "finish" (serial console).
  Outcome manual_start(int64_t now_ms) { return on_start(Key{}, now_ms, now_ms); }
  Outcome manual_finish(int64_t now_ms) { return on_finish(Key{}, now_ms, now_ms); }

  void reset(int64_t now_ms) {                                                 // S-8
    run_ = false;
    for (Fin& f : fins_) f = Fin{};
    choose(now_ms);                    // no run: nothing is chosen
    if (now_ms > floor_) floor_ = now_ms;
  }

  // Call regularly. Returns true when a run was just abandoned (S-7).
  bool tick(int64_t now_ms) {
    if (run_ && chosen_ == kNone && now_ms - t_start_ > static_cast<int64_t>(cfg_.run_timeout_ms)) {
      run_ = false;
      return true;
    }
    return false;
  }

  State state() const { return !run_ ? State::Idle : (chosen_ == kNone ? State::Running : State::Finished); }

  // Time on the clock right now (Running), the result (Finished), or 0 (Idle).
  int64_t shown_ms(int64_t now_ms) const {
    if (!run_) return 0;
    const int64_t ms = (chosen_ == kNone ? now_ms : fins_[chosen_].edge) - t_start_;
    return ms < 0 ? 0 : ms;
  }

  // Local time at which the current result appeared (valid in Finished).
  int64_t finished_at_ms() const { return t_shown_; }

 private:
  struct Key {
    bool gate = false;          // false: operator command, never matches another event
    uint32_t boot = 0, seq = 0;
    bool same(const Key& o) const { return gate && o.gate && boot == o.boot && seq == o.seq; }
  };
  struct Fin {
    bool used = false;
    Key key;
    int64_t edge = 0;
  };

  static constexpr size_t kFins = 4;
  static constexpr size_t kNone = kFins;

  Outcome on_start(const Key& key, int64_t edge, int64_t now_ms) {
    if (key.gate && last_start_.gate && key.boot == last_start_.boot) {
      if (key.seq == last_start_.seq) {
        if (run_ && run_by_gate_ && edge < t_start_) {                         // S-1
          t_start_ = edge;
          choose(now_ms);
          return Outcome::Refined;
        }
        return Outcome::Duplicate;
      }
      if (key.seq < last_start_.seq) return Outcome::Old;                      // S-6
    }
    if (edge < floor_) return Outcome::Old;                                    // S-6, S-8
    const bool was_running = state() == State::Running;
    if (key.gate) last_start_ = key;
    run_ = true;                                                               // S-2
    run_by_gate_ = key.gate;
    t_start_ = edge;
    floor_ = edge + 1;
    chosen_ = kNone;
    choose(now_ms);                                                            // S-4
    return was_running ? Outcome::Restarted : Outcome::Started;
  }

  Outcome on_finish(const Key& key, int64_t edge, int64_t now_ms) {
    size_t idx = kNone;
    for (size_t i = 0; i < kFins; i++) {
      if (fins_[i].used && fins_[i].key.same(key)) idx = i;
    }
    const size_t before = chosen_;
    if (idx != kNone) {
      if (edge >= fins_[idx].edge) return Outcome::Duplicate;
      fins_[idx].edge = edge;                                                  // S-1
      choose(now_ms);
      return (chosen_ == idx && before != idx) ? Outcome::Finished : Outcome::Refined;
    }
    idx = insert(key, edge);
    if (idx == kNone) return Outcome::Old;
    choose(now_ms);
    return chosen_ == idx ? Outcome::Finished : Outcome::Buffered;
  }

  // Remember a FINISH. When full, the oldest one makes room — but never the
  // one that defines the result. A newcomer older than everything kept is
  // dropped, unless it is the one that ends the current run (S-3).
  size_t insert(const Key& key, int64_t edge) {
    size_t slot = kNone;
    for (size_t i = 0; i < kFins; i++) {
      if (!fins_[i].used) { slot = i; break; }
    }
    if (slot == kNone) {
      for (size_t i = 0; i < kFins; i++) {
        if (i == chosen_) continue;
        if (slot == kNone || fins_[i].edge < fins_[slot].edge) slot = i;
      }
      const bool ends_run = run_ && edge - t_start_ >= static_cast<int64_t>(cfg_.min_run_ms) &&
                            (chosen_ == kNone || edge < fins_[chosen_].edge);
      if (edge <= fins_[slot].edge && !ends_run) return kNone;
    }
    fins_[slot].used = true;
    fins_[slot].key = key;
    fins_[slot].edge = edge;
    return slot;
  }

  // S-3: the earliest FINISH at least min_run_ms after the START.
  void choose(int64_t now_ms) {
    size_t best = kNone;
    if (run_) {
      for (size_t i = 0; i < kFins; i++) {
        if (!fins_[i].used || fins_[i].edge - t_start_ < static_cast<int64_t>(cfg_.min_run_ms)) continue;
        if (best == kNone || fins_[i].edge < fins_[best].edge) best = i;
      }
    }
    if (best != chosen_) {
      chosen_ = best;
      if (best != kNone) t_shown_ = now_ms;
    }
  }

  StopwatchConfig cfg_;
  bool run_ = false;              // a START defines a run (Running or Finished)
  bool run_by_gate_ = false;      // ... and it is last_start_ (not an operator start)
  Key last_start_;                // newest gate START ever accepted
  int64_t t_start_ = 0;
  int64_t floor_ = INT64_MIN;     // a START before this instant cannot define a run
  Fin fins_[kFins];
  size_t chosen_ = kNone;         // index of the FINISH that ends the run
  int64_t t_shown_ = 0;
};

}  // namespace hhj
