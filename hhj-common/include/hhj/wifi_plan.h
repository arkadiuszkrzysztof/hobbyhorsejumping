// wifi_plan.h — which WiFi network to try, and when (pure C++, no Arduino).
//
// Two networks: the PRIMARY one (the competition router) and an optional
// FALLBACK (home / workshop). The plan, restarted from the top at power-up and
// every time an established connection is lost:
//
//   W-1  first try PRIMARY for one attempt;
//   W-2  if that did not connect, try FALLBACK for one attempt;
//   W-3  if that did not connect either, keep trying PRIMARY forever
//        (one attempt after another) — the fallback is not tried again;
//   W-4  without a fallback configured, W-2 is skipped;
//   W-5  the moment the link is up the plan is CONNECTED and asks for nothing;
//   W-6  losing the link in CONNECTED restarts the plan at W-1 immediately.
//
// The plan only decides; the glue performs "start associating with network X"
// whenever update() returns an action.
//
// Host tests: hhj-common/test/test_wifi_plan.cpp
#pragma once
#include <stdint.h>

namespace hhj {

struct WifiPlanConfig {
  uint32_t attempt_ms = 10000;   // how long one attempt may take before moving on
  bool has_fallback = true;      // false when no fallback network is configured
};

class WifiPlan {
 public:
  enum class Net : uint8_t { Primary = 0, Fallback = 1 };
  enum class Phase : uint8_t { Boot, TryPrimary, TryFallback, SeekPrimary, Connected };

  struct Action {
    bool connect;   // true: (re)start associating with `net` now
    Net net;
  };

  explicit WifiPlan(const WifiPlanConfig& c = WifiPlanConfig{}) : cfg_(c) {}

  // Call often. `link_up` = associated and has an IP address.
  Action update(bool link_up, int64_t now_ms) {
    if (link_up) {
      if (phase_ != Phase::Connected) {
        if (phase_ == Phase::Boot) net_ = Net::Primary;   // link came up without us asking
        phase_ = Phase::Connected;
        connects_++;
      }
      return Action{false, net_};
    }
    switch (phase_) {
      case Phase::Boot:
        return begin(Phase::TryPrimary, now_ms);
      case Phase::Connected:
        losses_++;
        return begin(Phase::TryPrimary, now_ms);
      case Phase::TryPrimary:
        if (!expired(now_ms)) break;
        return begin(cfg_.has_fallback ? Phase::TryFallback : Phase::SeekPrimary, now_ms);
      case Phase::TryFallback:
      case Phase::SeekPrimary:
        if (!expired(now_ms)) break;
        return begin(Phase::SeekPrimary, now_ms);
    }
    return Action{false, net_};
  }

  Phase phase() const { return phase_; }
  bool connected() const { return phase_ == Phase::Connected; }
  // The network being tried, or the one we are connected to.
  Net net() const { return net_; }

  uint32_t attempts() const { return attempts_; }   // "connect" actions issued
  uint32_t connects() const { return connects_; }   // times the link came up
  uint32_t losses() const { return losses_; }       // times an established link dropped

 private:
  bool expired(int64_t now_ms) const { return now_ms - t_attempt_ >= static_cast<int64_t>(cfg_.attempt_ms); }

  Action begin(Phase p, int64_t now_ms) {
    phase_ = p;
    net_ = (p == Phase::TryFallback) ? Net::Fallback : Net::Primary;
    t_attempt_ = now_ms;
    attempts_++;
    return Action{true, net_};
  }

  WifiPlanConfig cfg_;
  Phase phase_ = Phase::Boot;
  Net net_ = Net::Primary;
  int64_t t_attempt_ = 0;
  uint32_t attempts_ = 0, connects_ = 0, losses_ = 0;
};

inline const char* wifi_net_name(WifiPlan::Net n) { return n == WifiPlan::Net::Primary ? "primary" : "fallback"; }

}  // namespace hhj
