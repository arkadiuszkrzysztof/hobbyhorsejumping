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

// The timer (192.168.10.20) with two pretend gates (.10 START, .11 FINISH)
// and a pretend backend (.30) that tells it what to show.
// The network delay is fixed at 1 ms so that expected times are exact.
struct Rig {
  sim::World w;
  sim::TimerUnit timer;
  sim::Probe start_gate, finish_gate, backend;

  explicit Rig(const TimerAppConfig& c = sim::TimerUnit::config(0x71AE5u))
      : timer(w, ip(20), c), start_gate(w, ip(10)), finish_gate(w, ip(11)), backend(w, ip(30)) {
    w.net.unicast_min_ms = w.net.unicast_max_ms = 1;
    w.net.broadcast_min_ms = w.net.broadcast_max_ms = 1;
    start_gate.join();
    finish_gate.join();
    backend.join();
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
  // One copy of an event whose beam was cut at true time `edge`, sent now.
  void event(Role mark, uint32_t seq, int64_t edge, uint32_t to = ip(20)) {
    EventMsg e;
    e.mark = mark;
    e.boot = mark == Role::Start ? kStartBoot : kFinishBoot;
    e.seq = seq;
    e.age_ms = static_cast<uint32_t>(w.t - edge);
    e.t_ms = edge;
    char buf[200];
    const size_t n = encode(e, buf, sizeof(buf));
    gate(mark).say(to, std::string(buf, n));
  }
  // Call once per ms: what a gate sends for an event cut at `edge` — unicast
  // and broadcast at 0, 10, 20, 40, 80, 160 and 320 ms, then once a second
  // (nobody acknowledges it).
  void repeat_event(Role mark, uint32_t seq, int64_t edge) {
    const int64_t age = w.t - edge;
    bool due = age > 320 && age % 1000 == 0;
    for (int64_t b : {0, 10, 20, 40, 80, 160, 320}) due = due || age == b;
    if (!due) return;
    event(mark, seq, edge);
    event(mark, seq, edge, kBroadcast);
  }
  // The backend: one SHOW, sent now.
  void show(Show state, uint32_t rev = 0, uint32_t age_ms = 0, uint32_t ms = 0) {
    ShowMsg m;
    m.state = state;
    m.rev = rev;
    m.age_ms = age_ms;
    m.ms = ms;
    char buf[120];
    const size_t n = encode(m, buf, sizeof(buf));
    backend.say(ip(20), std::string(buf, n));
  }
  // The last HELLO the display sent (as heard by the START gate).
  HelloMsg last_hello() const {
    Msg m, out;
    for (const sim::Datagram& d : start_gate.heard) {
      if (decode(d.data.data(), d.data.size(), &m) && m.kind == Msg::Kind::Hello) out = m;
    }
    return out.hello;
  }
  // Runs until true time `t`; before every millisecond `traffic()` may send
  // something, and the gates announce themselves as healthy every 2 s.
  template <class F>
  void run_with_gates(int64_t t, F traffic) {
    while (w.t < t) {
      if (w.t % 2000 == 0) {
        hello(Role::Start);
        hello(Role::Finish);
      }
      traffic();
      run(1);
    }
  }
};

}                                                 // namespace

// -------------------------------------------------------------- T-1 power-up
TEST(t1_all_segments_for_the_first_second) {
  Rig r;
  r.run(1);
  CHECK_STR(r.text(), "8888");
  r.run_until(999);
  CHECK_STR(r.text(), "8888");
  r.run_until(1000);
  CHECK(r.text() != "8888");
  CHECK(r.timer.hal.logged("hhj-timer 1.2.0 boot 00071ae5 - showing what the backend says, UDP 3004"));
}

TEST(t1_selftest_counts_from_power_up_not_from_zero) {
  // The device clock does not start at 0 in the simulation.
  sim::World w;
  sim::FakeTimer hal(w, ip(20));
  hal.clock_offset_ms = 500000;
  TimerApp app(hal, sim::TimerUnit::config(1));
  w.run(999, [&] { app.tick(); });                // begin() runs on the first tick
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

// ------------------------------------- T-4 the display never times a run
TEST(t4_no_backend_gate_events_never_show_a_time) {
  // The gates and the display are on, the server is still booting. Kids cross
  // both gates; every crossing arrives as a burst of copies, repeated once a
  // second because nobody acknowledges it.
  Rig r;
  int wrong = 0;
  r.run_with_gates(60000, [&] {
    for (uint32_t i = 0; i < 2; i++) {
      const int64_t start = 5000 + 15000 * i, finish = start + 7000 + 5500 * i;
      r.repeat_event(Role::Start, i + 1, start);
      r.repeat_event(Role::Finish, i + 1, finish);
    }
    if (r.w.t > 2002 && r.text() != "----") wrong++;
  });
  CHECK_EQ(wrong, 0);                             // the idle picture all along
  CHECK(!r.timer.app.remote().active(r.w.t));
  CHECK(r.timer.app.shown(r.w.t).view == TimerApp::View::Idle);
  CHECK_EQ(r.timer.app.rx_invalid(), 0);          // well-formed: not counted as garbage either
  CHECK(!r.timer.hal.logged("#"));                // and not logged
  CHECK_EQ(r.last_hello().st, kStIdle);
  CHECK_EQ(r.last_hello().ms, 0);
}

TEST(t4_backend_idle_or_ready_a_start_event_starts_nothing_until_run) {
  Rig r;
  int wrong = 0;
  // no session: the backend says IDLE; a kid crosses START
  r.run_with_gates(10000, [&] {
    if (r.w.t % 1000 == 0) r.show(Show::Idle);
    r.repeat_event(Role::Start, 1, 4000);
    if (r.w.t > 2002 && r.text() != "----") wrong++;
  });
  CHECK_EQ(wrong, 0);
  // a session is open: READY. The rider crosses START, then FINISH, but the
  // backend has not said RUN (yet)
  r.run_with_gates(16000, [&] {
    if (r.w.t % 1000 == 0) r.show(Show::Ready);
    r.repeat_event(Role::Start, 2, 13000);
    r.repeat_event(Role::Finish, 1, 15000);
    if (r.w.t > 10001 && r.text() != " 000") wrong++;
  });
  CHECK_EQ(wrong, 0);
  CHECK(r.timer.app.shown(r.w.t).view == TimerApp::View::Ready);
  r.show(Show::Run, 1, 3000);                     // now it does: the START was cut 3 s ago
  r.run(10);                                      // arrived at 16001: start 13001
  CHECK_STR(r.text(), " 300");
  CHECK(r.timer.app.shown(r.w.t).view == TimerApp::View::Running);
}

TEST(t4_backend_silent_mid_run_idle_after_the_timeout_even_while_gate_events_arrive) {
  Rig r;
  auto traffic = [&] {
    if (r.w.t >= 8000 && r.w.t <= 15000 && r.w.t % 1000 == 0) r.show(Show::Run, 1, static_cast<uint32_t>(r.w.t - 8000));
    r.repeat_event(Role::Start, 1, 8000);
    r.repeat_event(Role::Finish, 1, 18000);
  };
  r.run_with_gates(15001 + 5000, traffic);        // the last SHOW arrived at 15001: start 8001
  CHECK_STR(r.text(), "1200");                    // 5000 ms of silence: still the backend's clock
  r.run_with_gates(15001 + 5001, traffic);
  CHECK_STR(r.text(), "----");                    // 5001 ms: the idle picture, not a clock of its own
  CHECK_EQ(r.timer.hal.count_logged("backend silent -> idle picture"), 1);
  int wrong = 0;
  r.run_with_gates(60000, [&] {
    traffic();
    if (r.text() != "----") wrong++;
  });
  CHECK_EQ(wrong, 0);                             // the FINISH (and the retries) changed nothing
  CHECK_EQ(r.last_hello().st, kStIdle);
}

TEST(t4_wifi_lost_mid_run_clock_until_the_timeout_then_the_no_wifi_blink) {
  Rig r;
  r.run_until(2000);
  r.show(Show::Run, 1, 0);                        // arrives at 2001: start 2001
  r.run_until(3000);
  r.w.ap[0] = false;
  r.run_until(2001 + 5000);
  CHECK_STR(r.text(), " 500");
  r.run_until(2001 + 5001);
  CHECK_STR(r.text(), "----");                    // 7002 / 500 = 14: the "no WiFi" blink, on phase
  r.run_until(7600);
  CHECK_STR(r.text(), "    ");
}

TEST(t4_long_runs_switch_to_minutes) {
  Rig r;
  r.run_with_gates(2001 + 125000, [&] {
    if (r.w.t % 1000 == 0) r.show(Show::Run, 1, static_cast<uint32_t>(r.w.t));   // START cut at 0: start 1
    if (r.w.t == 1 + 99999) CHECK_STR(r.text(), "9999");
    if (r.w.t == 1 + 100000) CHECK_STR(r.text(), " 140");
    if (r.w.t == 1 + 124999) CHECK_STR(r.text(), " 204");
    if (r.w.t == 1 + 125000) CHECK_STR(r.text(), " 205");
  });
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
  r.show(Show::Run, 1, 0);
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
  CHECK_STR(m.hello.fw, "1.2.0");
  CHECK_EQ(m.hello.st, kStIdle);
  CHECK_EQ(m.hello.ms, 0);
  CHECK_EQ(m.hello.rssi, -50);
  CHECK_EQ(m.hello.net, 0);
}

// ------------------------------------------------------------- T-9 console
TEST(t9_lamp_test_shows_8888_for_two_seconds_without_blocking) {
  Rig r;
  r.run_until(2000);
  r.show(Show::Run, 1, 0);                        // start 2001
  r.run_until(3000);
  r.timer.hal.type("t");
  r.run(1);                                       // key read at t = 3001
  CHECK_STR(r.text(), "8888");
  CHECK(r.timer.hal.logged("lamp test"));
  r.run_until(4990);
  r.show(Show::Result, 2, 0, 2990);               // SHOWs are still handled during the lamp test
  r.run_until(5000);
  CHECK_STR(r.text(), "8888");
  CHECK(r.timer.app.shown(r.w.t).view == TimerApp::View::Result);
  r.run_until(5100);
  CHECK_STR(r.text(), " 299");
}

TEST(t9_only_the_lamp_test_key_does_anything) {
  // s / f / r used to start, finish and reset a clock of the display's own.
  Rig r;
  r.run_until(2000);
  const size_t lines = r.timer.hal.logs.size();
  r.timer.hal.type("sfrSFR xyz\r\n?");
  r.run(5);
  CHECK(r.timer.hal.keys.empty());
  CHECK_EQ(r.timer.hal.logs.size(), lines);
  CHECK_STR(r.text(), "____");
  r.timer.hal.type("st");                         // several keys in one tick: none is lost
  r.run(1);
  CHECK_STR(r.text(), "8888");
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
  CHECK_STR(r.text(), "____");
  const long sent = r.timer.hal.sent;
  r.run(4000);                                    // two HELLO rounds, no gate known: one broadcast each
  CHECK_EQ(r.timer.hal.sent - sent, 2);
}

// ------------------------------------------------------------ T-7 gate events
TEST(t7_well_formed_events_are_ignored_whatever_they_say) {
  Rig r;
  r.run_until(2000);
  const size_t lines = r.timer.hal.logs.size();
  const char* events[] = {"{\"time_mark\":\"START\",\"age_ms\":0,\"boot\":1,\"seq\":1}",
                          "{\"time_mark\":\"FINISH\",\"age_ms\":12340,\"boot\":2,\"seq\":1}",
                          "{\"time_mark\":\"START\",\"age_ms\":600001,\"boot\":1,\"seq\":9}",
                          "{\"time_mark\":\"FINISH\",\"age_ms\":4294967295,\"boot\":4294967295,\"seq\":4294967295,\"n\":65535}"};
  for (const char* e : events) {
    r.start_gate.say(ip(20), e);
    r.start_gate.say(kBroadcast, e);
  }
  r.run(10);
  CHECK_STR(r.text(), "____");
  CHECK_EQ(r.timer.app.rx_invalid(), 0);
  CHECK_EQ(r.timer.hal.logs.size(), lines);
  CHECK(!r.timer.app.remote().active(r.w.t));
  // nor do they disturb what the backend says
  r.show(Show::Ready);
  for (const char* e : events) r.start_gate.say(ip(20), e);
  r.run(10);
  CHECK_STR(r.text(), " 000");
}

TEST(event_flood_is_drained) {
  // 7 copies x unicast + broadcast from each gate at once must all be consumed.
  Rig r;
  r.run_until(2000);
  for (int i = 0; i < 40; i++) r.event(Role::Start, 1, 2000);
  r.run(20);
  CHECK(r.timer.hal.inbox.empty());
  CHECK_STR(r.text(), "____");
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
  Rig r(c);
  r.w.ap[0] = false;
  r.run_until(1000);
  for (int i = 0; i < 3000; i++) {
    r.run(1);
    CHECK_STR(r.text(), "----");                  // no WiFi, steady
  }
  r.w.ap[0] = true;
  r.backend.join();
  r.run(25000);
  CHECK(r.timer.app.uplink().up());
  r.show(Show::Result, 1, 0, 2500);
  for (int i = 0; i < 4000; i++) {
    r.run(1);
    if (i % 1000 == 999) r.show(Show::Result, 1, 0, 2500);
    CHECK_STR(r.text(), " 250");                  // result, steady from the first moment
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

// ------------------------------------------------- found by mutation testing
TEST(default_settings) {
  const TimerAppConfig c;
  CHECK_EQ(c.boot, 0);
  CHECK_STR(c.fw, "1.2.0");
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
  Rig r(c);
  r.run_until(3500);
  r.show(Show::Result, 1, 0, 2000);
  r.run(1);                                       // arrived at 3501: result 2.00 s
  const int64_t shown_at = 3501;
  const char* want[] = {" 200", "    ", " 200", " 200", " 200"};   // 0-299, 300-599, 600-899, 900-1199, ...
  for (int64_t since = 0; since < 1500; since++) {
    CHECK_STR(r.text(), want[since / 300]);
    r.run(1);
  }
  CHECK_EQ(r.timer.app.remote().result_since_ms(), shown_at);
}

TEST(t9_a_nul_byte_on_the_console_is_just_an_unknown_key) {
  Rig r;
  r.run_until(1500);
  r.timer.hal.keys.push_back(0);
  r.timer.hal.keys.push_back('t');
  r.run(1);                                       // both are read in the same tick
  CHECK(r.timer.hal.keys.empty());
  CHECK_STR(r.text(), "8888");
}

// ------------------------------------------------ T-12 the backend's display
TEST(t12_backend_takes_the_display_over_and_hands_it_back) {
  Rig r;
  r.run_until(2000);
  CHECK(!r.timer.app.remote().active(r.w.t));
  r.show(Show::Ready);
  r.run(2);                                       // arrives at 2001
  CHECK(r.timer.app.remote().active(r.w.t));
  CHECK_STR(r.text(), " 000");                    // 0.00: a rider is about to start
  CHECK_EQ(r.timer.hal.count_logged("backend took over the display"), 1);
  r.run_until(2001 + 5000);
  CHECK_STR(r.text(), " 000");                    // 5000 ms after the last SHOW: still the backend's
  r.run(1);
  CHECK_STR(r.text(), "____");                    // 5001 ms: back to the idle picture
  CHECK_EQ(r.timer.hal.count_logged("backend silent -> idle picture"), 1);
  r.show(Show::Ready);
  r.run(2);
  CHECK_EQ(r.timer.hal.count_logged("backend took over the display"), 2);
}

TEST(t12_gate_crossings_are_not_shown_while_the_backend_decides) {
  // Kids run through the gates between sessions: the display must not start counting.
  Rig r;
  r.run_until(2000);
  r.hello(Role::Start);
  r.hello(Role::Finish);
  for (int64_t t = 2000; t < 12000; t += 1000) {
    r.run_until(t);
    r.show(Show::Idle);
    if (t == 4000) r.event(Role::Start, 1, 4000);
    if (t == 7000) r.event(Role::Finish, 1, 7000);
    if (t % 2000 == 0) {
      r.hello(Role::Start);
      r.hello(Role::Finish);
    }
    r.run(500);
    CHECK_STR(r.text(), "----");                  // the idle picture throughout
  }
  r.run_until(11000 + 5010);
  CHECK_STR(r.text(), "----");                    // backend silent: still nothing of the display's own
}

TEST(t12_running_clock_from_the_backend) {
  Rig r;
  r.run_until(10000);
  r.show(Show::Run, 77, 4000);                    // the counting START was cut 4 s ago; arrives at 10001
  r.run(2);
  CHECK_STR(r.text(), " 400");
  for (int64_t t = 11000; t <= 18000; t += 1000) {          // the backend repeats it every second
    r.run_until(t);
    r.show(Show::Run, 77, static_cast<uint32_t>(t - 6000));
  }
  r.run_until(6001 + 12340);
  CHECK_STR(r.text(), "1234");                    // 12.34 s after the START as the timer placed it (6001)
}

TEST(t12_copies_refine_the_backend_clock) {
  Rig r;
  r.run_until(10000);
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 41;     // the first copy is 41 ms late
  r.show(Show::Run, 5, 1000);                     // true start: 9000
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 1;
  r.run_until(10100);
  CHECK_EQ(r.timer.app.remote().shown_ms(10100), 1059);     // start estimated 9041
  r.show(Show::Run, 5, 1100);                     // 1 ms on the wire: start 9001
  r.run_until(10200);
  CHECK_EQ(r.timer.app.remote().shown_ms(10200), 1199);
}

TEST(t12_operator_picks_another_start_and_the_clock_jumps) {
  Rig r;
  r.run_until(10000);
  r.show(Show::Run, 1, 6000);                     // first START counts: 6 s on the clock
  r.run(2);
  CHECK_STR(r.text(), " 600");
  r.show(Show::Run, 2, 2000);                     // the operator picked a later START: 2 s
  r.run(2);
  CHECK_STR(r.text(), " 200");
}

TEST(t12_result_from_the_backend_blinks_then_stays) {
  Rig r;
  r.run_until(10000);
  r.show(Show::Result, 9, 0, 23450);              // arrives at 10001
  for (int64_t since = 1; since < 3000; since++) {
    r.run_until(10001 + since);
    const bool off = (since / 300) % 2 == 1;
    CHECK_STR(r.text(), off ? "    " : "2345");
    if (since % 1000 == 0) r.show(Show::Result, 9, 0, 23450);   // the 1 Hz repeats do not restart the blinking
  }
  r.run_until(10001 + 3000);
  CHECK_STR(r.text(), "2345");
  // the operator picks another FINISH: new result, blinking again
  r.show(Show::Result, 10, 0, 21000);
  r.run_until(13002 + 299);
  CHECK_STR(r.text(), "2100");
  r.run_until(13002 + 300);
  CHECK_STR(r.text(), "    ");
}

TEST(t12_idle_from_the_backend_shows_gate_status_and_no_wifi_still_blinks) {
  Rig r;
  r.run_until(2000);
  r.show(Show::Idle);
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

TEST(t8_hello_reports_what_the_backend_says_and_nothing_else) {
  Rig r;
  r.run_with_gates(3900, [&] { r.repeat_event(Role::Start, 1, 1500); });
  CHECK_EQ(r.last_hello().st, kStIdle);           // HELLO at 2051, after a START: still idle
  CHECK_EQ(r.last_hello().ms, 0);
  r.run_until(3900);
  r.show(Show::Run, 1, 1000);                     // arrives 3901: start 2901
  r.run_until(4100);                              // HELLO at 4051
  CHECK_EQ(r.last_hello().st, kStRunning);
  CHECK_EQ(r.last_hello().ms, 1150);
  r.show(Show::Result, 2, 0, 9870);
  r.run_until(6100);
  CHECK_EQ(r.last_hello().st, kStFinished);
  CHECK_EQ(r.last_hello().ms, 9870);
  r.show(Show::Ready);
  r.run_until(8100);
  CHECK_EQ(r.last_hello().st, kStIdle);
  CHECK_EQ(r.last_hello().ms, 0);
  r.show(Show::Idle, 3, 500, 600);                // idle: whatever else the datagram carries
  r.run_until(10100);
  CHECK_EQ(r.last_hello().st, kStIdle);
  CHECK_EQ(r.last_hello().ms, 0);
  r.show(Show::Result, 4, 0, 5550);
  r.run_until(12100);
  CHECK_EQ(r.last_hello().st, kStFinished);
  // the backend falls silent; the gates keep reporting crossings
  r.run_with_gates(20100, [&] {
    r.repeat_event(Role::Start, 2, 13000);
    r.repeat_event(Role::Finish, 2, 15000);
  });
  CHECK_EQ(r.last_hello().st, kStIdle);           // HELLO at 20051, 10 s after the last SHOW
  CHECK_EQ(r.last_hello().ms, 0);
}

TEST(t12_default_remote_timeout) {
  CHECK_EQ(TimerAppConfig{}.remote.timeout_ms, 5000);
}
