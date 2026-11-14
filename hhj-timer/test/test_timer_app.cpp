// Tests for timer_app.h — the whole display unit on fake hardware. Requirement
// ids (T-n) refer to that file.
#include <string>
#include <vector>

#include "hhj_test.h"
#include "sim_devices.h"

using namespace hhj;
using sim::ip;

namespace {

constexpr uint32_t kStartBoot = 0x51A27u, kFinishBoot = 0xF1215u;

// The timer (192.168.10.20) with two pretend gates (.10 START, .11 FINISH).
// The network delay is fixed at 1 ms so that expected times are exact.
struct Rig {
  sim::World w;
  sim::TimerUnit timer;
  sim::Probe start_gate, finish_gate;

  explicit Rig(uint64_t seed = 1) : w(seed), timer(w, ip(20)), start_gate(w, ip(10)), finish_gate(w, ip(11)) {
    w.net.unicast_min_ms = w.net.unicast_max_ms = 1;
    w.net.broadcast_min_ms = w.net.broadcast_max_ms = 1;
    start_gate.join();
    finish_gate.join();
  }

  void run(int64_t ms) {
    w.run(ms, [&] {
      timer.tick();
      start_gate.listen();
      finish_gate.listen();
    });
  }
  void run_until(int64_t t) { run(t - w.t); }
  std::string text() const { return timer.text(); }

  sim::Probe& gate(Role mark) { return mark == Role::Start ? start_gate : finish_gate; }

  void hello(Role mark, uint32_t st = kStReady | kStArmed) {
    HelloMsg h;
    h.role = mark;
    h.boot = mark == Role::Start ? kStartBoot : kFinishBoot;
    h.st = st;
    snprintf(h.fw, sizeof(h.fw), "2.1.0");
    char buf[200];
    const size_t n = encode(h, buf, sizeof(buf));
    gate(mark).say(kBroadcast, std::string(buf, n));
  }
  // One copy of an event whose beam was cut at true time `edge`, sent now, unicast.
  void event(Role mark, uint32_t seq, int64_t edge) {
    EventMsg e;
    e.mark = mark;
    e.boot = mark == Role::Start ? kStartBoot : kFinishBoot;
    e.seq = seq;
    e.age_ms = static_cast<uint32_t>(w.t - edge);
    e.t_ms = edge;
    char buf[200];
    const size_t n = encode(e, buf, sizeof(buf));
    gate(mark).say(ip(20), std::string(buf, n));
  }
  // Both gates announce themselves as healthy, from now on every 2 s for `ms`.
  void run_with_gates(int64_t ms) {
    for (int64_t i = 0; i < ms; i++) {
      if (w.t % 2000 == 0) {
        hello(Role::Start);
        hello(Role::Finish);
      }
      run(1);
    }
  }
};

}  // namespace

// -------------------------------------------------------------- T-1 power-up
TEST(t1_all_segments_for_the_first_second) {
  Rig r;
  r.run(1);
  CHECK_STR(r.text(), "8888");
  r.run_until(999);
  CHECK_STR(r.text(), "8888");
  r.run_until(1000);
  CHECK(r.text() != "8888");
  CHECK(r.timer.hal.logged("hhj-timer 1.1.0 boot 00071ae5 - waiting for START/FINISH on UDP 3004"));
}

TEST(t1_selftest_counts_from_power_up_not_from_zero) {
  // The device clock does not start at 0 in the simulation.
  sim::World w;
  sim::FakeTimer hal(w, ip(20));
  hal.clock_offset_ms = 500000;
  TimerApp app(hal, sim::TimerUnit::config(1));
  w.run(999, [&] { app.tick(); });               // begin() runs on the first tick
  hhj::Frame f{};
  for (int i = 0; i < 4; i++) f.d[i] = hal.chain[3 - i];
  CHECK_STR(sim::frame_text(sim::wiring(), f), "8888");
}

// ------------------------------------------------------------------ T-2 idle
TEST(t2_no_wifi_blinks_four_dashes) {
  Rig r;
  r.w.ap[0] = false;
  r.run_until(1000);
  CHECK_STR(r.text(), "----");
  r.run_until(1499);
  CHECK_STR(r.text(), "----");
  r.run_until(1500);
  CHECK_STR(r.text(), "    ");
  r.run_until(1999);
  CHECK_STR(r.text(), "    ");
  r.run_until(2000);
  CHECK_STR(r.text(), "----");
  int dash = 0, blank = 0;
  for (int i = 0; i < 20000; i++) {
    r.run(1);
    if (r.text() == "----") dash++;
    else if (r.text() == "    ") blank++;
  }
  CHECK_EQ(dash, 10000);
  CHECK_EQ(blank, 10000);
}

// ---------------------------------------------------------- T-3 gate status
TEST(t3_wifi_up_but_no_gates_shows_bottom_bars) {
  Rig r;
  r.run_until(1500);
  CHECK(r.timer.app.uplink().up());
  CHECK_STR(r.text(), "____");
  r.run(5000);
  CHECK_STR(r.text(), "____");                    // steady, not blinking
}

TEST(t3_each_gate_has_its_own_pair_of_digits) {
  Rig r;
  r.run_until(1500);
  r.hello(Role::Start);
  r.run(10);
  CHECK_STR(r.text(), "--__");                    // START gate ready, FINISH gate missing
  CHECK(r.timer.hal.logged("START gate online"));
  r.hello(Role::Finish);
  r.run(10);
  CHECK_STR(r.text(), "----");                    // both ready
  CHECK(r.timer.app.gate_online(Role::Start, r.w.t));
  CHECK(r.timer.app.gate_online(Role::Finish, r.w.t));
}

TEST(t3_gate_that_is_not_ok_shows_top_bars) {
  Rig r;
  r.run_until(1500);
  r.hello(Role::Start);
  const uint32_t not_ok[] = {kStReady | kStBeamA, kStReady | kStBeamB, kStReady | kStStuck | kStBeamA, kStArmed, 0};
  for (uint32_t st : not_ok) {
    r.hello(Role::Finish, st);
    r.run(10);
    CHECK_STR(r.text(), "--^^");
  }
  r.hello(Role::Finish, kStReady);                // quiet period after an event is fine
  r.run(10);
  CHECK_STR(r.text(), "----");
  r.hello(Role::Start, kStReady | kStBeamA);
  r.run(10);
  CHECK_STR(r.text(), "^^--");
}

TEST(t3_gate_goes_back_to_bottom_bars_after_7s_of_silence) {
  Rig r;
  r.run_until(1500);
  r.hello(Role::Start);
  r.hello(Role::Finish);
  r.run(10);
  const int64_t heard = 1501;
  r.run_until(heard + 6990);
  CHECK_STR(r.text(), "----");
  for (int i = 0; i < 4; i++) {                   // FINISH keeps talking, START fell silent
    r.hello(Role::Finish);
    r.run(5);
  }
  r.run_until(heard + 7010);
  CHECK_STR(r.text(), "__--");
  CHECK(!r.timer.app.gate_online(Role::Start, r.w.t));
  r.hello(Role::Start);
  r.run(10);
  CHECK_STR(r.text(), "----");
  CHECK_EQ(r.timer.hal.count_logged("START gate online"), 2);
}

TEST(t3_losing_wifi_voids_what_was_known_about_the_gates) {
  Rig r;
  r.run_until(1500);
  r.hello(Role::Start);
  r.hello(Role::Finish);
  r.run(10);
  CHECK_STR(r.text(), "----");
  r.w.ap[0] = false;
  r.run_until(2100);                              // blink phase "on"
  CHECK_STR(r.text(), "----");
  r.run_until(2600);
  CHECK_STR(r.text(), "    ");                    // blinking again: no WiFi
  r.w.ap[0] = true;
  r.run(25000);
  CHECK(r.timer.app.uplink().up());
  CHECK_STR(r.text(), "____");                    // and the gates have to announce themselves again
}

TEST(hello_from_timer_or_backend_is_not_a_gate) {
  Rig r;
  r.run_until(1500);
  r.start_gate.say(ip(20), "{\"hello\":\"TIMER\",\"boot\":1}");
  r.start_gate.say(ip(20), "{\"hello\":\"BACKEND\",\"boot\":1}");
  r.run(10);
  CHECK_STR(r.text(), "____");
}

// ----------------------------------------------------------- T-4 running
TEST(t4_running_time_counts_from_the_start_edge) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);                  // cut right now; arrives 1 ms later
  r.run_until(2010);
  CHECK_STR(r.text(), " 000");                    // 9 ms on the clock
  r.run_until(2011);
  CHECK_STR(r.text(), " 001");
  r.run_until(3234);
  CHECK_STR(r.text(), " 123");
  r.run_until(14569);
  CHECK_STR(r.text(), "1256");
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
  CHECK(r.timer.hal.logged("START #1 (copy 0, age 0 ms) -> clock started"));
}

TEST(t7_a_late_start_shows_the_true_running_time_at_once) {
  Rig r;
  r.run_until(7500);
  r.event(Role::Start, 1, 2000);                  // the beam was cut 5.5 s ago
  r.run_until(7505);
  CHECK_STR(r.text(), " 550");
}

TEST(t4_long_runs_switch_to_minutes) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(2001 + 99999);
  CHECK_STR(r.text(), "9999");
  r.run_until(2001 + 100000);
  CHECK_STR(r.text(), " 140");
  r.run_until(2001 + 124999);
  CHECK_STR(r.text(), " 204");
  r.run_until(2001 + 125000);
  CHECK_STR(r.text(), " 205");
}

TEST(t4_clock_keeps_running_without_wifi) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(3000);
  r.w.ap[0] = false;
  r.run_until(9001);
  CHECK_STR(r.text(), " 700");
}

// ----------------------------------------------------------- T-5 result
TEST(t5_result_blinks_three_seconds_then_stays) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(14340);
  r.event(Role::Finish, 1, 14340);                // 12.34 s
  r.run(5);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Finished);
  const int64_t shown_at = r.timer.app.stopwatch().finished_at_ms();
  CHECK_EQ(shown_at, 14341);
  CHECK(r.timer.hal.logged("FINISH #1 (copy 0, age 0 ms) -> RESULT 12.34 s"));
  // 300 ms on, 300 ms off, ... for 3 s
  for (int64_t t = shown_at + 5; t < shown_at + 3000; t++) {
    r.run_until(t);
    const bool off = ((t - shown_at) / 300) % 2 == 1;
    CHECK_STR(r.text(), off ? "    " : "1234");
  }
  for (int64_t t = shown_at + 3000; t < shown_at + 9000; t += 7) {
    r.run_until(t);
    CHECK_STR(r.text(), "1234");                  // steady
  }
  r.run(600000);                                  // ten minutes later: still there (S-9)
  CHECK_STR(r.text(), "1234");
}

TEST(t5_next_start_replaces_the_result) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(9000);
  r.event(Role::Finish, 1, 9000);
  r.run_until(20000);
  CHECK_STR(r.text(), " 700");
  r.event(Role::Start, 2, 20000);
  r.run_until(21001);
  CHECK_STR(r.text(), " 100");
}

TEST(t7_copies_refine_the_result) {
  // The first FINISH copy was delayed 80 ms by the network; a later copy was not.
  Rig r;
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 1;
  r.run_until(2000);
  r.event(Role::Start, 1, 1999);                  // arrives at 2001 with age 1: edge 2000 on the timer's clock
  r.run_until(12000);
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 81;
  r.event(Role::Finish, 1, 11999);                // implies 12080
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 1;
  r.run_until(12090);
  CHECK_EQ(r.timer.app.stopwatch().shown_ms(r.w.t), 10080);
  r.event(Role::Finish, 1, 11999);                // same event, quick copy: implies 12000
  r.run_until(12100);
  CHECK_EQ(r.timer.app.stopwatch().shown_ms(r.w.t), 10000);
  CHECK_EQ(r.timer.hal.count_logged("RESULT"), 1);              // refinements are not logged as new results
}

TEST(s7_abandoned_run_goes_back_to_ready) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_with_gates(600100 - 2000 + 2000);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  CHECK_STR(r.text(), "----");
  CHECK_EQ(r.timer.hal.count_logged("no FINISH for 600000 ms -> back to ready"), 1);
}

// ------------------------------------------------------------- T-6 refresh
TEST(t6_display_is_written_on_change_and_at_least_every_second) {
  Rig r;
  r.run_until(1500);                              // idle, WiFi up: "____", static
  const int before = r.timer.hal.shows;
  r.run(10000);
  const int writes = r.timer.hal.shows - before;
  CHECK(writes >= 10 && writes <= 11);            // only the 1 s refresh
  // and never more than refresh_ms between two writes, from power-up on
  for (size_t i = 1; i < r.timer.hal.show_times.size(); i++) {
    CHECK(r.timer.hal.show_times[i] - r.timer.hal.show_times[i - 1] <= 1000);
  }
  CHECK_EQ(r.timer.hal.show_times[0], 1);         // written on the very first tick
}

TEST(t6_running_clock_is_written_once_per_hundredth) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(3000);
  const int before = r.timer.hal.shows;
  r.run(1000);
  CHECK_EQ(r.timer.hal.shows - before, 100);
}

TEST(t6_refresh_rewrites_the_same_content) {
  Rig r;
  r.run_until(1500);
  r.timer.hal.chain[0] = 0xFF;                    // a shift register picked up a glitch
  r.timer.hal.chain[2] = 0x00;
  r.run(1001);
  CHECK_STR(r.text(), "____");                    // healed within a second
}

TEST(d5_both_chain_orders) {
  for (bool first_is_left : {true, false}) {
    sim::World w;
    TimerAppConfig c = sim::TimerUnit::config(1);
    c.chain_first_is_left = first_is_left;
    sim::TimerUnit t(w, ip(20), c);
    sim::Probe g(w, ip(10));
    g.join();
    w.run(1500, [&] { t.tick(); });
    g.say(kBroadcast, "{\"hello\":\"START\",\"boot\":1,\"st\":8}");
    w.run(10, [&] { t.tick(); });
    // frame is  - - _ _  (left to right)
    const uint8_t dash = glyph_dash(sim::wiring()), low = glyph_low(sim::wiring());
    if (first_is_left) {
      CHECK_EQ(t.hal.chain[0], low);              // rightmost digit is shifted out first
      CHECK_EQ(t.hal.chain[3], dash);
    } else {
      CHECK_EQ(t.hal.chain[0], dash);
      CHECK_EQ(t.hal.chain[3], low);
    }
  }
}

// --------------------------------------------------------------- T-8 hello
TEST(t8_hello_is_broadcast_on_link_up_and_every_two_seconds) {
  Rig r;
  r.run(10000);
  std::vector<sim::Datagram> hellos;
  for (const sim::Datagram& d : r.start_gate.heard) {
    Msg m;
    if (decode(d.data.data(), d.data.size(), &m) && m.kind == Msg::Kind::Hello) hellos.push_back(d);
  }
  CHECK_EQ(hellos.size(), 5);
  for (size_t i = 0; i < hellos.size(); i++) {
    CHECK(hellos[i].broadcast);
    if (i > 0) CHECK(hellos[i].at - hellos[i - 1].at >= 1998 && hellos[i].at - hellos[i - 1].at <= 2002);
  }
  Msg m;
  decode(hellos[0].data.data(), hellos[0].data.size(), &m);
  CHECK(m.hello.role == Role::Timer);
  CHECK_EQ(m.hello.boot, 0x71AE5u);
  CHECK_STR(m.hello.fw, "1.1.0");
  CHECK_EQ(m.hello.st, kStIdle);
  CHECK_EQ(m.hello.ms, 0);
  CHECK_EQ(m.hello.rssi, -50);
  CHECK_EQ(m.hello.net, 0);
}

TEST(t8_hello_carries_state_and_time) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(6100);
  auto last_hello = [&] {
    Msg m, out;
    for (const sim::Datagram& d : r.start_gate.heard) {
      if (decode(d.data.data(), d.data.size(), &m) && m.kind == Msg::Kind::Hello) out = m;
    }
    return out.hello;
  };
  CHECK_EQ(last_hello().st, kStRunning);
  CHECK(last_hello().ms >= 4000 && last_hello().ms <= 4100);    // sent at ~6051: 4.05 s into the run
  r.event(Role::Finish, 1, 6100);
  r.run_until(8200);
  CHECK_EQ(last_hello().st, kStFinished);
  CHECK_EQ(last_hello().ms, 4100);
}

// ------------------------------------------------------------- T-9 console
TEST(t9_console_start_finish_reset) {
  Rig r;
  r.run_until(2000);
  r.timer.hal.type("s");
  r.run(1);                                       // t = 2001
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
  CHECK(r.timer.hal.logged("START (console) -> clock started"));
  r.run_until(4501);
  r.timer.hal.type("f");
  r.run(1);                                       // t = 4502
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Finished);
  CHECK(r.timer.hal.logged("FINISH (console) -> RESULT 2.50 s"));
  CHECK_STR(r.text(), " 250");
  r.timer.hal.type("r");
  r.run(1);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  CHECK(r.timer.hal.logged("reset"));
  CHECK_STR(r.text(), "____");
}

TEST(t9_lamp_test_shows_8888_for_two_seconds_without_blocking) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(3000);
  r.timer.hal.type("t");
  r.run(1);                                       // key read at t = 3001
  CHECK_STR(r.text(), "8888");
  r.run_until(5000);
  CHECK_STR(r.text(), "8888");
  r.event(Role::Finish, 1, 4990);                 // events are still handled during the lamp test
  r.run_until(5000);
  CHECK_STR(r.text(), "8888");
  r.run_until(5010);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Finished);
  r.run_until(5100);
  CHECK_STR(r.text(), " 299");                    // 4991 - 2001
}

TEST(t9_unknown_keys_are_ignored_and_several_keys_work_in_one_go) {
  Rig r;
  r.run_until(2000);
  r.timer.hal.type("xyz\r\n?S F");
  r.run(5);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  r.timer.hal.type("sr");                         // start and reset within one tick
  r.run(1);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  r.timer.hal.type("s");
  r.run(1);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
  r.timer.hal.type("ssssssssssss");               // more keys than one tick handles: none is lost
  r.run(3);
  CHECK(r.timer.hal.keys.empty());
}

// ------------------------------------------------------------ T-10 bad input
TEST(t10_malformed_datagrams_are_counted_and_ignored) {
  Rig r;
  r.run_until(1500);
  r.start_gate.say(ip(20), "garbage");
  r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":0,\"boot\":1}");          // seq missing
  r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":0,\"boot\":1,\"seq\":0}");
  r.start_gate.say(ip(20), std::string(700, '{'));                                          // too long: dropped by the socket layer
  r.start_gate.say(ip(20), "{\"ack\":\"START\",\"boot\":1,\"seq\":1}");                     // valid, but not for a timer
  r.run(10);
  CHECK_EQ(r.timer.app.rx_invalid(), 3);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  CHECK_STR(r.text(), "____");
}

TEST(stale_and_old_events_are_logged_once_and_ignored) {
  Rig r;
  r.run_until(2000);
  r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":600001,\"boot\":1,\"seq\":9}");
  r.run(10);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  CHECK(r.timer.hal.logged("START #9 (copy 0, age 600001 ms) -> stale, ignored"));
  r.event(Role::Start, 5, 2010);
  r.run(10);
  r.event(Role::Start, 4, 1000);
  r.run(10);
  CHECK(r.timer.hal.logged("START #4 (copy 0, age 1020 ms) -> older than what is shown, ignored"));
  r.run_until(2500);
  r.event(Role::Finish, 1, 2500);                 // 490 ms after the start: not a finish
  r.run(10);
  CHECK(r.timer.hal.logged("-> does not end a run, noted"));
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
}

TEST(event_flood_is_drained) {
  // 7 copies x unicast + broadcast from each gate at once must all be consumed.
  Rig r;
  r.run_until(2000);
  for (int i = 0; i < 40; i++) r.event(Role::Start, 1, 2000);
  r.run(20);
  CHECK(r.timer.hal.inbox.empty());
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
  CHECK_EQ(r.timer.hal.count_logged("clock started"), 1);
}

TEST(send_failures_are_counted_not_fatal) {
  Rig r;
  r.timer.hal.radio_ok = false;
  r.run(5000);
  CHECK(r.timer.app.tx_failed() >= 2);
  CHECK_STR(r.text(), "____");
}

TEST(timer_on_the_fallback_network) {
  Rig r;
  r.w.ap[0] = false;
  r.w.ap[1] = true;
  sim::Probe home(r.w, ip(50));
  home.join(WifiPlan::Net::Fallback);
  r.w.run(13000, [&] { r.timer.tick(); home.listen(); });
  CHECK(r.timer.app.uplink().up());
  CHECK(r.timer.app.uplink().net() == WifiPlan::Net::Fallback);
  Msg m, last;
  for (const sim::Datagram& d : home.heard) {
    if (decode(d.data.data(), d.data.size(), &m) && m.kind == Msg::Kind::Hello) last = m;
  }
  CHECK(last.hello.role == Role::Timer);
  CHECK_EQ(last.hello.net, 1);
  CHECK_STR(r.text(), "____");
}

// ------------------------------------------------------ remaining corners
TEST(t8_known_gates_get_the_hello_unicast_as_well) {
  Rig r;
  sim::Probe bystander(r.w, ip(40));
  bystander.join();
  r.run_until(1000);
  r.hello(Role::Start);
  auto count = [](const sim::Probe& p, bool broadcast) {
    int n = 0;
    for (const sim::Datagram& d : p.heard) {
      Msg m;
      if (decode(d.data.data(), d.data.size(), &m) && m.kind == Msg::Kind::Hello && d.broadcast == broadcast) n++;
    }
    return n;
  };
  r.w.run(1000, [&] { r.timer.tick(); r.start_gate.listen(); bystander.listen(); });   // t = 2000
  r.start_gate.heard.clear();
  bystander.heard.clear();
  r.w.run(4100, [&] { r.timer.tick(); r.start_gate.listen(); bystander.listen(); });   // HELLOs at 2051, 4051, 6051
  CHECK_EQ(count(r.start_gate, true), 3);
  CHECK_EQ(count(r.start_gate, false), 3);        // the gate that announced itself also gets unicast copies
  CHECK_EQ(count(bystander, true), 3);
  CHECK_EQ(count(bystander, false), 0);
  // ...until it has been silent for 7 s
  r.start_gate.heard.clear();
  r.w.run(6000, [&] { r.timer.tick(); r.start_gate.listen(); });                       // t = 12100; gate last heard at 1001
  r.start_gate.heard.clear();
  r.w.run(4000, [&] { r.timer.tick(); r.start_gate.listen(); });
  CHECK_EQ(count(r.start_gate, false), 0);
  CHECK_EQ(count(r.start_gate, true), 2);
}

TEST(t5_t2_blink_periods_of_zero_mean_no_blinking) {
  TimerAppConfig c = sim::TimerUnit::config(1);
  c.result_blink_half_ms = 0;
  c.idle_blink_half_ms = 0;
  sim::World w;
  w.ap[0] = false;
  sim::TimerUnit t(w, ip(20), c);
  w.run(1000, [&] { t.tick(); });
  for (int i = 0; i < 3000; i++) {
    w.run(1, [&] { t.tick(); });
    CHECK_STR(t.text(), "----");                  // no WiFi, steady
  }
  t.hal.type("s");
  w.run(2500, [&] { t.tick(); });
  t.hal.type("f");
  for (int i = 0; i < 4000; i++) {
    w.run(1, [&] { t.tick(); });
    CHECK_STR(t.text(), " 250");                  // result, steady from the first moment
  }
}

TEST(t8_a_version_string_the_protocol_cannot_carry_is_not_sent) {
  TimerAppConfig c = sim::TimerUnit::config(1);
  c.fw = "not a version";
  sim::World w;
  sim::TimerUnit t(w, ip(20), c);
  sim::Probe listener(w, ip(40));
  listener.join();
  w.run(5000, [&] { t.tick(); listener.listen(); });
  CHECK_EQ(listener.heard.size(), 0);
  CHECK(t.app.tx_failed() >= 2);
  CHECK_STR(t.text(), "____");                    // otherwise alive
}

TEST(t8_failed_unicast_hellos_are_counted) {
  Rig r;
  r.run_until(1000);
  r.hello(Role::Start);
  r.run(100);
  CHECK_EQ(r.timer.app.tx_failed(), 0);
  r.timer.hal.radio_ok = false;
  r.run(2000);                                    // one HELLO round: unicast to the gate + broadcast
  CHECK_EQ(r.timer.app.tx_failed(), 2);
}

// ------------------------------------------------------------- T-11 the log
TEST(t11_an_ignored_event_is_logged_once_not_once_per_copy) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 2, 2000);
  r.run(10);
  // START #1 belongs to an earlier run; its gate keeps repeating it every second
  for (int i = 0; i < 30; i++) {
    r.event(Role::Start, 1, 500);
    r.run(1000);
  }
  CHECK_EQ(r.timer.hal.count_logged("START #1"), 1);
  CHECK_EQ(r.timer.hal.count_logged("older than what is shown, ignored"), 1);
  CHECK_EQ(r.timer.hal.count_logged("clock started"), 1);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
}

TEST(t11_copies_of_the_current_events_are_silent) {
  Rig r;
  r.run_until(2000);
  for (int i = 0; i < 7; i++) {
    r.event(Role::Start, 1, 2000);
    r.run(10);
  }
  r.run_until(9000);
  for (int i = 0; i < 7; i++) {
    r.event(Role::Finish, 1, 9000);
    r.run(10);
  }
  CHECK_EQ(r.timer.hal.count_logged("START #1"), 1);
  CHECK_EQ(r.timer.hal.count_logged("FINISH #1"), 1);
  CHECK_EQ(r.timer.hal.count_logged("RESULT 7.00 s"), 1);
}

TEST(t11_many_different_ignored_events_each_get_their_line_once) {
  // Sixteen stale events repeated in rotation (two gates with full outboxes).
  Rig r;
  r.run_until(2000);
  for (int round = 0; round < 5; round++) {
    for (uint32_t seq = 1; seq <= 16; seq++) {
      r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":1,\"seq\":" + std::to_string(seq) + "}");
      r.run(3);
    }
  }
  CHECK_EQ(r.timer.hal.count_logged("stale, ignored"), 16);
  // a 17th pushes the oldest out of that memory: both are then reported once more
  r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":1,\"seq\":17}");
  r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":1,\"seq\":1}");
  r.start_gate.say(ip(20), "{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":1,\"seq\":3}");
  r.run(10);
  CHECK_EQ(r.timer.hal.count_logged("stale, ignored"), 18);
  CHECK_EQ(r.timer.hal.count_logged("START #3 "), 1);
}

TEST(t11_same_number_from_another_gate_or_boot_is_a_different_event) {
  Rig r;
  r.run_until(2000);
  const char* variants[] = {"{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":1,\"seq\":5}",
                            "{\"time_mark\":\"FINISH\",\"age_ms\":700000,\"boot\":1,\"seq\":5}",
                            "{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":2,\"seq\":5}",
                            "{\"time_mark\":\"START\",\"age_ms\":700000,\"boot\":1,\"seq\":6}"};
  for (int round = 0; round < 3; round++) {
    for (const char* v : variants) r.start_gate.say(ip(20), v);
    r.run(5);
  }
  CHECK_EQ(r.timer.hal.count_logged("stale, ignored"), 4);
}

TEST(t11_a_late_start_that_completes_a_run_logs_the_result) {
  Rig r;
  r.run_until(20000);
  r.event(Role::Finish, 1, 20000);
  r.run(10);
  CHECK(r.timer.hal.logged("FINISH #1 (copy 0, age 0 ms) -> does not end a run, noted"));
  r.run_until(25000);
  r.event(Role::Start, 1, 8000);                  // the START gate was offline for 17 s
  r.run(10);
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Finished);
  CHECK(r.timer.hal.logged("START #1 (copy 0, age 17000 ms) -> its FINISH was already known: RESULT 12.00 s"));
  CHECK(!r.timer.hal.logged("clock started"));
  CHECK_STR(r.text(), "1200");
}

TEST(t11_restart_is_logged_as_such) {
  Rig r;
  r.run_until(2000);
  r.event(Role::Start, 1, 2000);
  r.run_until(5000);
  r.event(Role::Start, 2, 5000);
  r.run(10);
  CHECK(r.timer.hal.logged("START #2 (copy 0, age 0 ms) -> clock restarted"));
  CHECK_EQ(r.timer.hal.count_logged("clock started"), 1);
  // the longest possible description still fits the log line
  r.start_gate.say(ip(20), "{\"time_mark\":\"FINISH\",\"age_ms\":4294967295,\"boot\":4294967295,\"seq\":4294967295,\"n\":65535}");
  r.run(10);
  CHECK(r.timer.hal.logged("FINISH #4294967295 (copy 65535, age 4294967295 ms) -> stale, ignored"));
}

// ------------------------------------------------- found by mutation testing
TEST(default_settings) {
  const TimerAppConfig c;
  CHECK_EQ(c.boot, 0);
  CHECK_STR(c.fw, "1.1.0");
  CHECK(c.chain_first_is_left);
  CHECK_EQ(c.hello_ms, 2000);
  CHECK_EQ(c.peer_ttl_ms, 7000);
  CHECK_EQ(c.selftest_ms, 1000);
  CHECK_EQ(c.lamp_test_ms, 2000);
  CHECK_EQ(c.refresh_ms, 1000);
  CHECK_EQ(c.result_blink_ms, 3000);
  CHECK_EQ(c.result_blink_half_ms, 300);
  CHECK_EQ(c.idle_blink_half_ms, 500);
}

TEST(t9_lamp_test_ends_exactly_on_time) {
  Rig r;
  r.run_until(1500);
  r.timer.hal.type("t");
  r.run(1);                                       // key read at 1501: lamp until 3501
  r.run_until(3500);
  CHECK_STR(r.text(), "8888");
  r.run_until(3501);
  CHECK_STR(r.text(), "____");
}

TEST(t3_gate_online_boundary_is_exact) {
  Rig r;
  r.run_until(1500);
  r.hello(Role::Start);                           // heard at 1501
  r.run_until(1501 + 7000);
  CHECK_STR(r.text(), "--__");                    // silent for exactly 7000 ms: still online
  r.run(1);
  CHECK_STR(r.text(), "____");
}

TEST(t10_at_most_eight_datagrams_are_read_per_tick) {
  Rig r;
  r.run_until(1500);
  for (int i = 0; i < 20; i++) r.start_gate.say(ip(20), "{\"hello\":\"BACKEND\",\"boot\":1}");
  r.run(1);                                       // all 20 arrive in this millisecond
  CHECK_EQ(r.timer.hal.inbox.size(), 12);
  r.run(1);
  CHECK_EQ(r.timer.hal.inbox.size(), 4);
  r.run(1);
  CHECK_EQ(r.timer.hal.inbox.size(), 0);
}

TEST(t9_at_most_eight_keys_are_read_per_tick) {
  Rig r;
  r.run_until(1500);
  r.timer.hal.type("xxxxxxxxxxxxxxxxxxxx");       // 20 keys
  r.run(1);
  CHECK_EQ(r.timer.hal.keys.size(), 12);
  r.run(1);
  CHECK_EQ(r.timer.hal.keys.size(), 4);
  r.run(1);
  CHECK_EQ(r.timer.hal.keys.size(), 0);
}

TEST(t3_a_link_loss_voids_gate_knowledge_even_within_the_silence_limit) {
  // With a long silence limit the gates would still count as "heard recently"
  // after a short WiFi outage; they must announce themselves again all the same.
  TimerAppConfig c = sim::TimerUnit::config(1);
  c.peer_ttl_ms = 120000;
  sim::World w;
  w.net.unicast_min_ms = w.net.unicast_max_ms = 1;
  w.net.broadcast_min_ms = w.net.broadcast_max_ms = 1;
  sim::TimerUnit t(w, ip(20), c);
  sim::Probe gate(w, ip(10));
  gate.join();
  auto each = [&] { t.tick(); gate.listen(); };
  w.run(1500, each);
  gate.say(kBroadcast, "{\"hello\":\"START\",\"boot\":1,\"st\":8}");
  gate.say(kBroadcast, "{\"hello\":\"FINISH\",\"boot\":2,\"st\":8}");
  w.run(10, each);
  CHECK_STR(t.text(), "----");
  w.ap[0] = false;
  w.run(5, each);
  w.ap[0] = true;
  gate.join();
  w.run(25000, each);                             // the timer is back on the network
  CHECK(t.app.uplink().up());
  CHECK_STR(t.text(), "____");                    // not "----": nothing is known about the gates any more
  // and it no longer unicasts to where the gates used to be
  gate.heard.clear();
  w.run(4100, each);
  int unicast = 0, broadcast = 0;
  for (const sim::Datagram& d : gate.heard) (d.broadcast ? broadcast : unicast)++;
  CHECK_EQ(unicast, 0);
  CHECK(broadcast >= 2);
}

TEST(t8_hello_reports_uptime) {
  Rig r;
  r.timer.hal.clock_offset_ms = 0;
  r.run(4100);
  Msg m, last;
  for (const sim::Datagram& d : r.start_gate.heard) {
    if (decode(d.data.data(), d.data.size(), &m) && m.kind == Msg::Kind::Hello) last = m;
  }
  CHECK_EQ(last.hello.up_ms, 4051);               // sent at 4051 by a timer whose clock started at 0
}

TEST(t6_first_frame_is_written_even_if_it_is_blank) {
  // No self-test and a blank first frame: the registers still hold power-up garbage.
  TimerAppConfig c = sim::TimerUnit::config(1);
  c.selftest_ms = 0;
  c.idle_blink_half_ms = 1;                       // odd milliseconds are blank
  sim::World w;
  w.ap[0] = false;
  sim::TimerUnit t(w, ip(20), c);
  t.hal.chain[0] = t.hal.chain[1] = t.hal.chain[2] = t.hal.chain[3] = 0xFF;
  w.run(1, [&] { t.tick(); });                    // t = 1: blank frame
  CHECK_EQ(t.hal.shows, 1);
  CHECK_STR(t.text(), "    ");
}

TEST(t5_blinking_stops_exactly_at_the_end_of_the_blink_time) {
  // 900 ms of blinking in 300 ms halves: on, off, on - and at 900 ms it would be "off" if it went on.
  TimerAppConfig c = sim::TimerUnit::config(1);
  c.result_blink_ms = 900;
  sim::World w;
  sim::TimerUnit t(w, ip(20), c);
  w.run(1500, [&] { t.tick(); });
  t.hal.type("s");
  w.run(2000, [&] { t.tick(); });                 // started at 1501
  t.hal.type("f");
  w.run(1, [&] { t.tick(); });                    // finished at 3501: result 2.00 s
  const int64_t shown_at = 3501;
  const char* want[] = {" 200", "    ", " 200", " 200", " 200"};   // 0-299, 300-599, 600-899, 900-1199, ...
  for (int64_t since = 0; since < 1500; since++) {
    CHECK_STR(t.text(), want[since / 300]);
    w.run(1, [&] { t.tick(); });
  }
  CHECK_EQ(t.app.stopwatch().finished_at_ms(), shown_at);
}

TEST(t9_a_nul_byte_on_the_console_is_just_an_unknown_key) {
  Rig r;
  r.run_until(1500);
  r.timer.hal.keys.push_back(0);
  r.timer.hal.keys.push_back('s');
  r.run(1);                                       // both are read in the same tick
  CHECK(r.timer.hal.keys.empty());
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Running);
}

TEST(t11_result_log_shows_seconds_and_hundredths) {
  Rig r;
  r.run_until(1500);
  r.timer.hal.type("s");
  r.run(1);                                       // started at 1501
  r.run_until(1501 + 2996);
  r.timer.hal.type("f");
  r.run(1);                                       // finished at 1501 + 2997
  CHECK(r.timer.hal.logged("FINISH (console) -> RESULT 2.99 s"));
  r.timer.hal.type("s");
  r.run(1);
  r.run(61049);
  r.timer.hal.type("f");
  r.run(1);                                       // 61.050 s
  CHECK(r.timer.hal.logged("FINISH (console) -> RESULT 61.05 s"));
}

// ------------------------------------------------ T-12 the backend's display
namespace {

// A pretend backend at 192.168.10.30 that tells the timer what to show.
struct Director {
  Rig& r;
  sim::Probe node;
  explicit Director(Rig& rig) : r(rig), node(rig.w, ip(30)) { node.join(); }
  void say(Show state, uint32_t rev = 0, uint32_t age_ms = 0, uint32_t ms = 0) {
    ShowMsg s;
    s.state = state;
    s.rev = rev;
    s.age_ms = age_ms;
    s.ms = ms;
    char buf[120];
    const size_t n = encode(s, buf, sizeof(buf));
    node.say(ip(20), std::string(buf, n));
  }
};

}  // namespace

TEST(t12_backend_takes_the_display_over_and_hands_it_back) {
  Rig r;
  Director d(r);
  r.run_until(2000);
  CHECK(!r.timer.app.remote().active(r.w.t));
  d.say(Show::Ready);
  r.run(2);                                       // arrives at 2001
  CHECK(r.timer.app.remote().active(r.w.t));
  CHECK_STR(r.text(), " 000");                    // 0.00: a rider is about to start
  CHECK_EQ(r.timer.hal.count_logged("backend took over the display"), 1);
  r.run_until(2001 + 5000);
  CHECK_STR(r.text(), " 000");                    // 5000 ms after the last SHOW: still the backend's
  r.run(1);
  CHECK_STR(r.text(), "____");                    // 5001 ms: back to the own (idle) picture
  CHECK_EQ(r.timer.hal.count_logged("backend silent -> showing the own stopwatch"), 1);
  d.say(Show::Ready);
  r.run(2);
  CHECK_EQ(r.timer.hal.count_logged("backend took over the display"), 2);
}

TEST(t12_gate_crossings_are_not_shown_while_the_backend_decides) {
  // Kids run through the gates between sessions: the display must not start counting.
  Rig r;
  Director d(r);
  r.run_until(2000);
  r.hello(Role::Start);
  r.hello(Role::Finish);
  for (int64_t t = 2000; t < 12000; t += 1000) {
    r.run_until(t);
    d.say(Show::Idle);
    if (t == 4000) r.event(Role::Start, 1, 4000);
    if (t == 7000) r.event(Role::Finish, 1, 7000);
    if (t % 2000 == 0) {
      r.hello(Role::Start);
      r.hello(Role::Finish);
    }
    r.run(500);
    CHECK_STR(r.text(), "----");                  // the idle picture throughout
  }
  // ...but the own stopwatch did follow them, ready to take over
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Finished);
  r.run_until(11000 + 5010);
  CHECK_STR(r.text(), " 300");                    // backend silent: the own result (7001 - 4001) shows
}

TEST(t12_running_clock_from_the_backend) {
  Rig r;
  Director d(r);
  r.run_until(10000);
  d.say(Show::Run, 77, 4000);                     // the counting START was cut 4 s ago; arrives at 10001
  r.run(2);
  CHECK_STR(r.text(), " 400");
  for (int64_t t = 11000; t <= 18000; t += 1000) {          // the backend repeats it every second
    r.run_until(t);
    d.say(Show::Run, 77, static_cast<uint32_t>(t - 6000));
  }
  r.run_until(6001 + 12340);
  CHECK_STR(r.text(), "1234");                    // 12.34 s after the START as the timer placed it (6001)
  CHECK(r.timer.app.stopwatch().state() == Stopwatch::State::Idle);   // the own stopwatch saw no gate event
}

TEST(t12_copies_refine_the_backend_clock) {
  Rig r;
  Director d(r);
  r.run_until(10000);
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 41;     // the first copy is 41 ms late
  d.say(Show::Run, 5, 1000);                      // true start: 9000
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 1;
  r.run_until(10100);
  CHECK_EQ(r.timer.app.remote().shown_ms(10100), 1059);     // start estimated 9041
  d.say(Show::Run, 5, 1100);                      // 1 ms on the wire: start 9001
  r.run_until(10200);
  CHECK_EQ(r.timer.app.remote().shown_ms(10200), 1199);
}

TEST(t12_operator_picks_another_start_and_the_clock_jumps) {
  Rig r;
  Director d(r);
  r.run_until(10000);
  d.say(Show::Run, 1, 6000);                      // first START counts: 6 s on the clock
  r.run(2);
  CHECK_STR(r.text(), " 600");
  d.say(Show::Run, 2, 2000);                      // the operator picked a later START: 2 s
  r.run(2);
  CHECK_STR(r.text(), " 200");
}

TEST(t12_result_from_the_backend_blinks_then_stays) {
  Rig r;
  Director d(r);
  r.run_until(10000);
  d.say(Show::Result, 9, 0, 23450);               // arrives at 10001
  for (int64_t since = 1; since < 3000; since++) {
    r.run_until(10001 + since);
    const bool off = (since / 300) % 2 == 1;
    CHECK_STR(r.text(), off ? "    " : "2345");
    if (since % 1000 == 0) d.say(Show::Result, 9, 0, 23450);   // the 1 Hz repeats do not restart the blinking
  }
  r.run_until(10001 + 3000);
  CHECK_STR(r.text(), "2345");
  // the operator picks another FINISH: new result, blinking again
  d.say(Show::Result, 10, 0, 21000);
  r.run_until(13002 + 299);
  CHECK_STR(r.text(), "2100");
  r.run_until(13002 + 300);
  CHECK_STR(r.text(), "    ");
}

TEST(t12_idle_from_the_backend_shows_gate_status_and_no_wifi_still_blinks) {
  Rig r;
  Director d(r);
  r.run_until(2000);
  d.say(Show::Idle);
  r.hello(Role::Start);
  r.run(5);
  CHECK_STR(r.text(), "--__");
  r.w.ap[0] = false;                              // WiFi drops; the last SHOW still counts for 5 s
  r.run_until(2600);
  CHECK(r.timer.app.remote().active(r.w.t));
  CHECK_STR(r.text(), "    ");                    // 2600 / 500 = 5: the "no WiFi" blink, off phase
  r.run_until(3100);
  CHECK_STR(r.text(), "----");
}

TEST(t12_hello_reports_what_is_shown) {
  Rig r;
  Director d(r);
  auto last_hello = [&] {
    Msg m, out;
    for (const sim::Datagram& dg : r.start_gate.heard) {
      if (decode(dg.data.data(), dg.data.size(), &m) && m.kind == Msg::Kind::Hello) out = m;
    }
    return out.hello;
  };
  r.run_until(3900);
  d.say(Show::Run, 1, 1000);                      // arrives 3901: start 2901
  r.run_until(4100);                              // HELLO at 4051
  CHECK_EQ(last_hello().st, kStRunning);
  CHECK_EQ(last_hello().ms, 1150);
  d.say(Show::Result, 2, 0, 9870);
  r.run_until(6100);
  CHECK_EQ(last_hello().st, kStFinished);
  CHECK_EQ(last_hello().ms, 9870);
  d.say(Show::Ready);
  r.run_until(8100);
  CHECK_EQ(last_hello().st, kStIdle);
  CHECK_EQ(last_hello().ms, 0);
  d.say(Show::Idle, 3, 500, 600);                 // idle: whatever else the datagram carries
  r.run_until(10100);
  CHECK_EQ(last_hello().st, kStIdle);
  CHECK_EQ(last_hello().ms, 0);
}

TEST(t12_default_remote_timeout) {
  CHECK_EQ(TimerAppConfig{}.remote.timeout_ms, 5000);
}
