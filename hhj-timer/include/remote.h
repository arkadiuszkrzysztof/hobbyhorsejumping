// remote.h — the display following the backend (pure C++, no Arduino, no heap).
//
// With a backend present the operator decides which rider is being timed and
// which START / FINISH count, so the backend tells the display what to show
// (SHOW datagrams, hhj-common/PROTOCOL.md) and the gates' own announcements
// are not shown:
//
//   R-1  A SHOW takes the display over, and keeps it for timeout_ms after the
//        last one.
//   R-2  Without a SHOW for longer than timeout_ms the display is no longer
//        remote (the timer shows its idle picture).
//   R-3  RUN: the clock runs from arrival - age_ms. Over the copies of the
//        same thing (same rev, still RUN) the smallest such start is kept —
//        delay only ever makes a copy late. Anything else starts afresh.
//   R-4  RESULT: the given ms. It counts as new — the display blinks — when it
//        appears, or when its ms or rev changes; repeats do not restart that.
//   R-5  READY and IDLE carry nothing: 0.00 and the idle picture.
//   R-6  After a silence longer than timeout_ms the next SHOW is taken as new:
//        nothing is refined against what was shown before, a RESULT blinks again.
//
// Host tests: test/test_remote.cpp
#pragma once
#include <stdint.h>

#include "hhj/link.h"

namespace hhj {

struct RemoteConfig {
  uint32_t timeout_ms = 5000;      // the backend sends a SHOW every second
};

class RemoteDisplay {
 public:
  explicit RemoteDisplay(const RemoteConfig& c = RemoteConfig{}) : cfg_(c) {}

  // One received SHOW. arrival_ms = local clock when it arrived.
  void on_show(const ShowMsg& m, int64_t arrival_ms) {
    if (m.state == Show::None) return;
    const bool same = active(arrival_ms) && state_ == m.state && rev_ == m.rev;   // R-6
    if (m.state == Show::Run) {
      const int64_t start = arrival_ms - static_cast<int64_t>(m.age_ms);
      if (!same || start < t_start_) t_start_ = start;                            // R-3
    } else if (m.state == Show::Result) {
      if (!same || ms_ != static_cast<int64_t>(m.ms)) t_result_ = arrival_ms;     // R-4
      ms_ = m.ms;
    }
    state_ = m.state;
    rev_ = m.rev;
    t_heard_ = arrival_ms;
    heard_ = true;
  }

  // R-1 / R-2
  bool active(int64_t now_ms) const { return heard_ && now_ms - t_heard_ <= static_cast<int64_t>(cfg_.timeout_ms); }

  // What to show. Only meaningful while active().
  Show state() const { return state_; }

  // The time on the clock: running (Run), the result (Result), otherwise 0.
  int64_t shown_ms(int64_t now_ms) const {
    if (state_ == Show::Run) {
      const int64_t ms = now_ms - t_start_;
      return ms < 0 ? 0 : ms;
    }
    return state_ == Show::Result ? ms_ : 0;
  }

  // Local time at which the current result appeared or last changed (Result only).
  int64_t result_since_ms() const { return t_result_; }

 private:
  RemoteConfig cfg_;
  bool heard_ = false;
  Show state_ = Show::Idle;
  uint32_t rev_ = 0;
  int64_t t_heard_ = 0;
  int64_t t_start_ = 0;
  int64_t t_result_ = 0;
  int64_t ms_ = 0;
};

}  // namespace hhj
