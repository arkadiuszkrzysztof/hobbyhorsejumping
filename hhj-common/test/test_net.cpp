// Tests for hhj/net.h (Peers, Uplink, logf) and for the simulated network the
// other suites rely on (sim.h) — a wrong simulator would make them meaningless.
#include <string>

#include "hhj/net.h"
#include "hhj_test.h"
#include "sim.h"

using namespace hhj;
using sim::ip;

// -------------------------------------------------------------------- Peers
TEST(net_constants) {
  CHECK_EQ(kBroadcast, 0);                           // 0 = "broadcast" when sending, "unknown" otherwise
  CHECK_EQ(Peers::kMax, 4);
}

TEST(peers_start_empty) {
  Peers p;
  CHECK_EQ(p.count(), 0);
  CHECK(!p.has_role(Role::Timer));
  for (size_t i = 0; i < Peers::kMax; i++) CHECK(!p.slot(i).used);
}

TEST(peers_touch_adds_once_and_refreshes) {
  Peers p;
  CHECK(p.touch(ip(20), Role::Timer, 100));          // new
  CHECK(!p.touch(ip(20), Role::Timer, 900));         // known: refreshed
  CHECK_EQ(p.count(), 1);
  CHECK(p.has_role(Role::Timer));
  CHECK(!p.has_role(Role::Backend));
  CHECK_EQ(p.slot(0).ip, ip(20));
  CHECK_EQ(p.slot(0).seen_ms, 900);
  CHECK(p.touch(ip(30), Role::Backend, 950));
  CHECK_EQ(p.count(), 2);
  CHECK(p.has_role(Role::Backend));
}

TEST(peers_same_address_can_change_role) {
  Peers p;
  p.touch(ip(20), Role::Timer, 0);
  CHECK(!p.touch(ip(20), Role::Backend, 10));
  CHECK_EQ(p.count(), 1);
  CHECK(p.has_role(Role::Backend));
  CHECK(!p.has_role(Role::Timer));
}

TEST(peers_reject_broadcast_address_and_no_role) {
  Peers p;
  CHECK(!p.touch(kBroadcast, Role::Timer, 0));
  CHECK(!p.touch(ip(20), Role::None, 0));
  CHECK_EQ(p.count(), 0);
}

TEST(peers_expire_boundary_is_exact) {
  Peers p;
  p.touch(ip(20), Role::Timer, 1000);
  CHECK_EQ(p.expire(8000, 7000), 0);                 // silent for exactly the ttl: still there
  CHECK_EQ(p.count(), 1);
  CHECK_EQ(p.expire(8001, 7000), 1);                 // one ms more: gone
  CHECK_EQ(p.count(), 0);
  CHECK_EQ(p.expire(9000, 7000), 0);
}

TEST(peers_expire_only_the_silent_ones) {
  Peers p;
  p.touch(ip(20), Role::Timer, 0);
  p.touch(ip(30), Role::Backend, 5000);
  CHECK_EQ(p.expire(7001, 7000), 1);
  CHECK(!p.has_role(Role::Timer));
  CHECK(p.has_role(Role::Backend));
  CHECK(p.touch(ip(21), Role::Timer, 7001));         // the freed slot is reused
  CHECK_EQ(p.count(), 2);
}

TEST(peers_full_table_replaces_the_longest_silent) {
  Peers p;
  for (uint8_t i = 0; i < Peers::kMax; i++) CHECK(p.touch(ip(static_cast<uint8_t>(20 + i)), Role::Timer, 100 + i));
  CHECK_EQ(p.count(), Peers::kMax);
  p.touch(ip(20), Role::Timer, 500);                 // refresh the first: now ip 21 is the stalest
  CHECK(p.touch(ip(99), Role::Backend, 600));
  CHECK_EQ(p.count(), Peers::kMax);
  bool has21 = false, has20 = false, has99 = false;
  for (size_t i = 0; i < Peers::kMax; i++) {
    has20 = has20 || p.slot(i).ip == ip(20);
    has21 = has21 || p.slot(i).ip == ip(21);
    has99 = has99 || p.slot(i).ip == ip(99);
  }
  CHECK(has20);
  CHECK(!has21);
  CHECK(has99);
}

TEST(peers_clear_forgets_everything) {
  Peers p;
  p.touch(ip(20), Role::Timer, 0);
  p.touch(ip(30), Role::Backend, 0);
  p.clear();
  CHECK_EQ(p.count(), 0);
  CHECK(!p.has_role(Role::Timer));
  CHECK(p.touch(ip(20), Role::Timer, 0));
}

// --------------------------------------------------------------------- logf
TEST(logf_prefixes_time_and_never_overruns) {
  sim::World w;
  sim::Probe n(w, ip(1));
  logf(n, 1234, "hello %d", 5);
  CHECK_STR(n.logs.back(), "[   1234] hello 5");
  logf(n, 12345678, "x");
  CHECK_STR(n.logs.back(), "[12345678] x");
  const std::string huge(1000, 'y');
  logf(n, 1, "%s", huge.c_str());                    // longer than the line buffer: truncated, not overrun
  CHECK_EQ(n.logs.back().size(), 223);               // a full line is 223 characters
  CHECK(n.logs.back().compare(0, 10, "[      1] ") == 0);
}

TEST(clamp_u32_limits) {
  CHECK_EQ(clamp_u32(0), 0);
  CHECK_EQ(clamp_u32(1), 1);
  CHECK_EQ(clamp_u32(-1), 0);
  CHECK_EQ(clamp_u32(-9000000000LL), 0);
  CHECK_EQ(clamp_u32(4294967294LL), 4294967294u);
  CHECK_EQ(clamp_u32(4294967295LL), 4294967295u);
  CHECK_EQ(clamp_u32(4294967296LL), 4294967295u);
  CHECK_EQ(clamp_u32(9223372036854775807LL), 4294967295u);
}

TEST(logf_extreme_timestamps) {
  sim::World w;
  sim::Probe n(w, ip(1));
  logf(n, 9223372036854775807LL, "max");
  CHECK_STR(n.logs.back(), "[9223372036854775807] max");
  logf(n, -9223372036854775807LL - 1, "min");
  CHECK_STR(n.logs.back(), "[-9223372036854775808] min");
  logf(n, 0, "%s", "");
  CHECK_STR(n.logs.back(), "[      0] ");
}

TEST(clamp_rssi_limits) {
  CHECK_EQ(clamp_rssi(-50), -50);
  CHECK_EQ(clamp_rssi(0), 0);
  CHECK_EQ(clamp_rssi(-128), -128);
  CHECK_EQ(clamp_rssi(-129), -128);
  CHECK_EQ(clamp_rssi(-1000), -128);
  CHECK_EQ(clamp_rssi(127), 127);
  CHECK_EQ(clamp_rssi(128), 127);
  CHECK_EQ(clamp_rssi(100000), 127);
}

// ------------------------------------------------------------------- Uplink
TEST(uplink_connects_on_primary_and_opens_the_socket) {
  sim::World w;
  sim::Probe n(w, ip(1));
  n.connect_delay_ms = 300;
  Uplink up(n, WifiPlanConfig{});
  int came_up = 0;
  w.run(1000, [&] {
    up.tick(n.now_ms());
    if (up.came_up()) {
      came_up++;
      CHECK_EQ(w.t, 301);                            // first tick at t=1 asks; 300 ms later it is up
    }
  });
  CHECK(up.up());
  CHECK_EQ(came_up, 1);                              // reported in exactly one tick
  CHECK_EQ(n.opens, 1);
  CHECK_EQ(n.connect_calls.size(), 1);
  CHECK(n.connect_calls[0] == WifiPlan::Net::Primary);
  CHECK(up.net() == WifiPlan::Net::Primary);
  CHECK(n.logged("WiFi: trying primary network"));
  CHECK(n.logged("WiFi connected (primary network), rssi -50 dBm"));
}

TEST(uplink_reports_nothing_before_its_first_tick) {
  sim::World w;
  sim::Probe n(w, ip(1));
  Uplink up(n, WifiPlanConfig{});
  CHECK(!up.up());
  CHECK(!up.came_up());
  CHECK_EQ(n.logs.size(), 0);
}

TEST(uplink_falls_back_then_seeks_primary) {
  sim::World w;
  w.ap[0] = false;                                   // neither network exists
  sim::Probe n(w, ip(1));
  Uplink up(n, WifiPlanConfig{});
  w.run(45000, [&] { CHECK(!up.tick(n.now_ms())); });
  CHECK_EQ(n.connect_calls.size(), 5);               // P F P P P
  CHECK(n.connect_calls[0] == WifiPlan::Net::Primary);
  CHECK(n.connect_calls[1] == WifiPlan::Net::Fallback);
  for (size_t i = 2; i < 5; i++) CHECK(n.connect_calls[i] == WifiPlan::Net::Primary);
  CHECK_EQ(n.opens, 0);
  CHECK_EQ(n.count_logged("WiFi: trying fallback network"), 1);
  CHECK_EQ(n.count_logged("WiFi: trying primary network"), 4);
  CHECK_EQ(n.count_logged("WiFi: trying primary network (will keep trying it)"), 3);   // not the very first attempt
  CHECK(!up.up());
}

TEST(uplink_home_network_only) {
  sim::World w;
  w.ap[0] = false;
  w.ap[1] = true;
  sim::Probe n(w, ip(1));
  Uplink up(n, WifiPlanConfig{});
  w.run(12000, [&] { up.tick(n.now_ms()); });
  CHECK(up.up());
  CHECK(up.net() == WifiPlan::Net::Fallback);
  CHECK(n.logged("WiFi connected (fallback network)"));
  CHECK_EQ(n.opens, 1);
}

TEST(uplink_loss_is_logged_and_socket_reopened_on_return) {
  sim::World w;
  sim::Probe n(w, ip(1));
  Uplink up(n, WifiPlanConfig{});
  w.run(1000, [&] { up.tick(n.now_ms()); });
  CHECK(up.up());
  w.ap[0] = false;                                   // router unplugged
  w.run(5000, [&] { up.tick(n.now_ms()); });
  CHECK(!up.up());
  CHECK_EQ(n.count_logged("WiFi lost"), 1);
  w.ap[0] = true;                                    // and back
  w.run(40000, [&] { up.tick(n.now_ms()); });
  CHECK(up.up());
  CHECK(up.net() == WifiPlan::Net::Primary);
  CHECK_EQ(n.opens, 2);                              // socket re-opened after the link returned
  CHECK_EQ(up.plan().losses(), 1);
  CHECK_EQ(up.plan().connects(), 2);
}

// ---------------------------------------------------------------- simulator
TEST(sim_ip_layout_matches_arduino) {
  CHECK_EQ(ip(5) & 0xFFu, 192);
  CHECK_EQ((ip(5) >> 8) & 0xFFu, 168);
  CHECK_EQ((ip(5) >> 16) & 0xFFu, 10);
  CHECK_EQ((ip(5) >> 24) & 0xFFu, 5);
}

TEST(sim_unicast_reaches_only_its_target_broadcast_reaches_all_others) {
  sim::World w;
  sim::Probe a(w, ip(1)), b(w, ip(2)), c(w, ip(3));
  a.join(); b.join(); c.join();
  w.run(5, [] {});
  CHECK(a.say(ip(2), "to-b"));
  CHECK(a.say(kBroadcast, "to-all"));
  w.run(10, [&] { a.listen(); b.listen(); c.listen(); });
  CHECK_EQ(a.heard.size(), 0);                       // never hears itself
  CHECK_EQ(b.heard.size(), 2);
  CHECK_EQ(c.heard.size(), 1);
  CHECK_STR(c.heard[0].data, "to-all");
  CHECK_EQ(c.heard[0].from, ip(1));
}

TEST(sim_no_delivery_before_latency_and_none_when_offline) {
  sim::World w;
  w.net.unicast_min_ms = w.net.unicast_max_ms = 5;
  sim::Probe a(w, ip(1)), b(w, ip(2));
  a.join(); b.join();
  w.run(1, [] {});
  const int64_t t0 = w.t;
  a.say(ip(2), "x");
  w.run(4, [&] { b.listen(); });
  CHECK_EQ(b.heard.size(), 0);
  w.run(1, [&] { b.listen(); });
  CHECK_EQ(b.heard.size(), 1);
  CHECK_EQ(b.heard[0].at, t0 + 5);
  // a node that is not on the network cannot send, and hears nothing
  sim::Probe off(w, ip(9));
  CHECK(!off.say(ip(2), "nope"));
  a.say(ip(9), "lost");
  w.run(20, [&] { off.listen(); });
  CHECK_EQ(off.heard.size(), 0);
}

TEST(sim_different_networks_do_not_hear_each_other) {
  sim::World w;
  w.ap[1] = true;
  sim::Probe a(w, ip(1)), b(w, ip(2));
  a.join(WifiPlan::Net::Primary);
  b.join(WifiPlan::Net::Fallback);
  w.run(2, [] {});
  a.say(kBroadcast, "x");
  a.say(ip(2), "y");
  w.run(20, [&] { b.listen(); });
  CHECK_EQ(b.heard.size(), 0);
}

TEST(sim_datagrams_arriving_while_link_is_down_are_lost) {
  sim::World w;
  w.net.unicast_min_ms = w.net.unicast_max_ms = 10;
  sim::Probe a(w, ip(1)), b(w, ip(2));
  a.join(); b.join();
  w.run(2, [] {});
  a.say(ip(2), "x");
  w.run(3, [] {});
  w.ap[0] = false;                                   // b's link drops before the datagram lands
  w.run(20, [] {});
  w.ap[0] = true;
  b.join();
  w.run(20, [&] { b.listen(); });
  CHECK_EQ(b.heard.size(), 0);
}

TEST(sim_dtim_holds_broadcasts_but_not_unicasts) {
  sim::World w;
  w.net.dtim_ms = 100;
  sim::Probe a(w, ip(1)), b(w, ip(2));
  a.join(); b.join();
  w.run(110, [] {});                                 // t = 110
  a.say(kBroadcast, "bc");
  a.say(ip(2), "uc");
  w.run(60, [&] { b.listen(); });                    // t = 170
  CHECK_EQ(b.heard.size(), 1);
  CHECK_STR(b.heard[0].data, "uc");
  w.run(40, [&] { b.listen(); });                    // t = 210
  CHECK_EQ(b.heard.size(), 2);
  CHECK_EQ(b.heard[1].at, 200);                      // released at the DTIM boundary
}

TEST(sim_loss_and_duplication_rates_are_plausible) {
  sim::World w(5);
  w.net.unicast_loss = 0.3;
  w.net.duplicate = 0.2;
  sim::Probe a(w, ip(1)), b(w, ip(2));
  a.join(); b.join();
  w.run(2, [] {});
  for (int i = 0; i < 2000; i++) {
    a.say(ip(2), "x");
    w.run(1, [&] { b.listen(); });
  }
  w.run(10, [&] { b.listen(); });
  // expectation: 2000 * 0.7 * 1.2 = 1680
  CHECK(b.heard.size() > 1550);
  CHECK(b.heard.size() < 1800);
  CHECK(w.lost > 500 && w.lost < 700);
}

TEST(sim_clock_offset_and_drift) {
  sim::World w;
  sim::Probe n(w, ip(1));
  n.clock_offset_ms = 5000;
  n.drift_ppm = 100;
  w.run(1000000, [] {});
  CHECK_EQ(n.now_ms(), 5000 + 1000000 + 100);
}

TEST(sim_model_backend_follows_the_protocol) {
  sim::World w;
  sim::Probe gate(w, ip(10));
  sim::ModelBackend be(w, ip(30));
  gate.join();
  be.join(WifiPlan::Net::Primary);
  w.run(2, [&] { be.tick(); });
  gate.say(kBroadcast, "{\"hello\":\"START\",\"boot\":1}");
  gate.say(ip(30), "{\"time_mark\":\"START\",\"age_ms\":100,\"boot\":1,\"seq\":1}");
  gate.say(ip(30), "{\"time_mark\":\"START\",\"age_ms\":101,\"boot\":1,\"seq\":1}");
  gate.say(ip(30), "{\"time_mark\":\"START\",\"age_ms\":600001,\"boot\":1,\"seq\":2}");   // stale
  gate.say(ip(30), "garbage");
  gate.say(ip(30), "{\"hello\":\"BACKEND\",\"boot\":9}");                                 // not answered
  w.run(20, [&] { be.tick(); gate.listen(); });
  CHECK_EQ(gate.heard_kind(Msg::Kind::Hello).size(), 1);
  CHECK(gate.heard_kind(Msg::Kind::Hello)[0].hello.role == Role::Backend);
  CHECK_EQ(gate.heard_kind(Msg::Kind::Ack).size(), 2);                                    // every copy acknowledged
  CHECK_EQ(be.readings.size(), 1);
  CHECK_EQ(be.find(Role::Start, 1, 1)->copies, 2);
  CHECK_EQ(be.stale, 1);
  CHECK_EQ(be.invalid, 1);
  be.power(false);
  gate.say(ip(30), "{\"time_mark\":\"START\",\"age_ms\":1,\"boot\":1,\"seq\":3}");
  w.run(20, [&] { be.tick(); gate.listen(); });
  CHECK_EQ(be.readings.size(), 1);                                                        // offline: hears nothing
  CHECK_EQ(gate.heard_kind(Msg::Kind::Ack).size(), 2);
}
