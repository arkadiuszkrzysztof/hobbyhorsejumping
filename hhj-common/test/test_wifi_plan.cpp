// Tests for hhj/wifi_plan.h — requirement ids (W-n) refer to that file.
#include <string>

#include "hhj/wifi_plan.h"
#include "hhj_test.h"

using hhj::WifiPlan;
using hhj::WifiPlanConfig;
using Net = hhj::WifiPlan::Net;
using Phase = hhj::WifiPlan::Phase;

namespace {

// Runs the plan millisecond by millisecond against a scripted "is the link up"
// function and records every connect action as "<time>:<P|F>".
template <class LinkUp>
std::string trace(WifiPlan& plan, int64_t from, int64_t to, LinkUp link_up) {
  std::string out;
  for (int64_t t = from; t <= to; t++) {
    const WifiPlan::Action a = plan.update(link_up(t, plan), t);
    if (a.connect) out += std::to_string(t) + (a.net == Net::Primary ? ":P " : ":F ");
  }
  return out;
}

bool never(int64_t, const WifiPlan&) { return false; }

}  // namespace

TEST(w1_first_action_is_primary_immediately) {
  WifiPlan plan;
  CHECK(plan.phase() == Phase::Boot);
  CHECK(!plan.connected());
  const WifiPlan::Action a = plan.update(false, 0);
  CHECK(a.connect);
  CHECK(a.net == Net::Primary);
  CHECK(plan.phase() == Phase::TryPrimary);
  CHECK_EQ(plan.attempts(), 1);
}

TEST(w1_w2_w3_full_sequence_when_nothing_is_there) {
  WifiPlan plan;                                   // 10 s attempts, fallback configured
  const std::string got = trace(plan, 0, 65000, never);
  // primary, fallback once, then primary forever
  CHECK_STR(got, "0:P 10000:F 20000:P 30000:P 40000:P 50000:P 60000:P ");
  CHECK(plan.phase() == Phase::SeekPrimary);
  CHECK(plan.net() == Net::Primary);
  CHECK_EQ(plan.attempts(), 7);
  CHECK_EQ(plan.connects(), 0);
  CHECK_EQ(plan.losses(), 0);
}

TEST(w3_fallback_is_never_tried_again_while_seeking) {
  WifiPlan plan;
  int fallback = 0;
  for (int64_t t = 0; t <= 3600000; t += 7) {      // an hour, irregular polling
    const WifiPlan::Action a = plan.update(false, t);
    if (a.connect && a.net == Net::Fallback) fallback++;
  }
  CHECK_EQ(fallback, 1);
  CHECK(plan.phase() == Phase::SeekPrimary);
}

TEST(attempt_boundary_is_exact) {
  WifiPlan plan;
  CHECK(plan.update(false, 100).connect);          // attempt starts at 100
  CHECK(!plan.update(false, 10099).connect);       // 9999 ms in: still the primary attempt
  CHECK(plan.phase() == Phase::TryPrimary);
  const WifiPlan::Action a = plan.update(false, 10100);
  CHECK(a.connect);
  CHECK(a.net == Net::Fallback);
  CHECK(!plan.update(false, 20099).connect);
  const WifiPlan::Action b = plan.update(false, 20100);
  CHECK(b.connect);
  CHECK(b.net == Net::Primary);
  CHECK(plan.phase() == Phase::SeekPrimary);
  CHECK(!plan.update(false, 30099).connect);
  CHECK(plan.update(false, 30100).connect);
}

TEST(custom_attempt_length) {
  WifiPlanConfig c;
  c.attempt_ms = 3000;
  WifiPlan plan(c);
  CHECK_STR(trace(plan, 0, 10000, never), "0:P 3000:F 6000:P 9000:P ");
}

TEST(w4_no_fallback_configured_means_primary_only) {
  WifiPlanConfig c;
  c.has_fallback = false;
  WifiPlan plan(c);
  CHECK_STR(trace(plan, 0, 35000, never), "0:P 10000:P 20000:P 30000:P ");
  CHECK(plan.phase() == Phase::SeekPrimary);
}

TEST(w5_connecting_on_primary_stops_all_actions) {
  WifiPlan plan;
  const std::string got = trace(plan, 0, 100000, [](int64_t t, const WifiPlan&) { return t >= 2500; });
  CHECK_STR(got, "0:P ");
  CHECK(plan.connected());
  CHECK(plan.phase() == Phase::Connected);
  CHECK(plan.net() == Net::Primary);
  CHECK_EQ(plan.connects(), 1);
}

TEST(w2_w5_home_scenario_connects_on_fallback) {
  // Only the fallback network exists: the link comes up 2 s into the fallback attempt.
  WifiPlan plan;
  const std::string got = trace(plan, 0, 100000, [](int64_t t, const WifiPlan& p) {
    return p.net() == Net::Fallback && t >= 12000;
  });
  CHECK_STR(got, "0:P 10000:F ");
  CHECK(plan.connected());
  CHECK(plan.net() == Net::Fallback);
}

TEST(w3_w5_late_router_is_found_while_seeking) {
  // The competition router is switched on 47 s after the devices.
  WifiPlan plan;
  const std::string got = trace(plan, 0, 100000, [](int64_t t, const WifiPlan& p) {
    return p.net() == Net::Primary && t >= 52000;       // comes up 2 s into the attempt started at 50 s
  });
  CHECK_STR(got, "0:P 10000:F 20000:P 30000:P 40000:P 50000:P ");
  CHECK(plan.connected());
  CHECK(plan.net() == Net::Primary);
}

TEST(w6_losing_the_link_restarts_from_primary_at_once) {
  WifiPlan plan;
  // up from 1 s to 60 s on primary, then gone for good
  const std::string got = trace(plan, 0, 95000, [](int64_t t, const WifiPlan&) { return t >= 1000 && t < 60000; });
  CHECK_STR(got, "0:P 60000:P 70000:F 80000:P 90000:P ");
  CHECK_EQ(plan.losses(), 1);
  CHECK_EQ(plan.connects(), 1);
  CHECK(plan.phase() == Phase::SeekPrimary);
}

TEST(w6_losing_the_fallback_also_restarts_from_primary) {
  WifiPlan plan;
  // fallback link between 11 s and 50 s, then nothing
  const std::string got = trace(plan, 0, 75000, [](int64_t t, const WifiPlan& p) {
    return p.net() == Net::Fallback && t >= 11000 && t < 50000;
  });
  CHECK_STR(got, "0:P 10000:F 50000:P 60000:F 70000:P ");
  CHECK_EQ(plan.losses(), 1);
}

TEST(w6_reconnect_after_loss_and_lose_again) {
  WifiPlan plan;
  auto link = [](int64_t t, const WifiPlan& p) {
    if (p.net() != Net::Primary) return false;
    return (t >= 1000 && t < 20000) || (t >= 21000 && t < 40000);
  };
  const std::string got = trace(plan, 0, 61000, link);
  // lost at 20 s -> primary again at once (reconnects at 21 s); lost at 40 s -> full plan again
  CHECK_STR(got, "0:P 20000:P 40000:P 50000:F 60000:P ");
  CHECK_EQ(plan.connects(), 2);
  CHECK_EQ(plan.losses(), 2);
}

TEST(link_up_before_any_action_counts_as_primary) {
  WifiPlan plan;
  const WifiPlan::Action a = plan.update(true, 0);
  CHECK(!a.connect);
  CHECK(plan.connected());
  CHECK(plan.net() == Net::Primary);
  CHECK_EQ(plan.attempts(), 0);
  CHECK_EQ(plan.connects(), 1);
}

TEST(connected_reports_the_network_it_is_on) {
  WifiPlan plan;
  plan.update(false, 0);
  plan.update(false, 10000);                       // now trying fallback
  CHECK(plan.net() == Net::Fallback);
  const WifiPlan::Action a = plan.update(true, 10500);
  CHECK(!a.connect);
  CHECK(a.net == Net::Fallback);
  CHECK(plan.connected());
  for (int64_t t = 10501; t < 200000; t += 1000) {
    CHECK(!plan.update(true, t).connect);          // W-5: nothing is asked while connected
    CHECK(plan.net() == Net::Fallback);
  }
  CHECK_EQ(plan.connects(), 1);
}

TEST(sparse_polling_still_advances_one_phase_per_call) {
  // The plan is only called every 25 s: each call moves on by exactly one step.
  WifiPlan plan;
  CHECK(plan.update(false, 0).net == Net::Primary);
  WifiPlan::Action a = plan.update(false, 25000);
  CHECK(a.connect);
  CHECK(a.net == Net::Fallback);
  a = plan.update(false, 50000);
  CHECK(a.connect);
  CHECK(a.net == Net::Primary);
  CHECK(plan.phase() == Phase::SeekPrimary);
}

TEST(net_numbers_are_the_protocol_values) {
  // HELLO "net": 0 = primary, 1 = fallback (PROTOCOL.md); also used as an index.
  CHECK_EQ(static_cast<int>(Net::Primary), 0);
  CHECK_EQ(static_cast<int>(Net::Fallback), 1);
}

TEST(net_names) {
  CHECK_STR(hhj::wifi_net_name(Net::Primary), "primary");
  CHECK_STR(hhj::wifi_net_name(Net::Fallback), "fallback");
}

// Random link behaviour, checked against the rules themselves.
TEST(fuzz_plan_invariants) {
  hhj_test::Rng rng(42);
  const int runs = hhj_test::iterations(400, 30);
  for (int r = 0; r < runs; r++) {
    WifiPlanConfig c;
    c.attempt_ms = static_cast<uint32_t>(rng.range(1, 3000));
    c.has_fallback = rng.chance(0.7);
    WifiPlan plan(c);
    bool up = false;
    int64_t last_action = -1;
    bool fallback_since_restart = false;
    Net last_target = Net::Primary;
    for (int64_t t = 0; t < 30000; t += rng.range(1, 40)) {
      if (rng.chance(0.01)) up = !up;
      const bool was_connected = plan.connected();
      const WifiPlan::Action a = plan.update(up, t);
      if (up) {
        CHECK(!a.connect);                                   // W-5
        CHECK(plan.connected());
        fallback_since_restart = false;
        last_action = -1;
        continue;
      }
      CHECK(!plan.connected());
      if (was_connected) {                                   // W-6
        CHECK(a.connect);
        CHECK(a.net == Net::Primary);
      }
      if (a.connect) {
        if (last_action >= 0 && !was_connected) CHECK(t - last_action >= c.attempt_ms);   // one attempt at a time
        if (a.net == Net::Fallback) {
          CHECK(c.has_fallback);                             // W-4
          CHECK(!fallback_since_restart);                    // W-3: at most once per restart
          CHECK(last_target == Net::Primary);                // W-2: only right after a primary attempt
          fallback_since_restart = true;
        }
        if (was_connected) fallback_since_restart = false;
        last_action = t;
        last_target = a.net;
      }
    }
  }
}
