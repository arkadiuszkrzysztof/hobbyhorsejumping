// System tests: the real START gate, FINISH gate and timer applications plus a
// model of the backend, all on one simulated WiFi network. Nothing is mocked
// between them — they only meet through datagrams.
//
// Each device has its own clock (different boot moment, different crystal
// error), so any result that comes out right was really computed from ages,
// not from a shared clock.
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "hhj_test.h"
#include "sim_devices.h"

using namespace hhj;
using sim::ip;

namespace {

constexpr uint32_t kStartBoot = 0x57A27001u, kFinishBoot = 0xF1215002u;

struct Arena {
  sim::World w;
  sim::GateUnit start, finish;
  sim::TimerUnit timer;
  sim::ModelBackend backend;

  explicit Arena(uint64_t seed = 1)
      : w(seed), start(w, ip(10), Role::Start, kStartBoot), finish(w, ip(11), Role::Finish, kFinishBoot),
        timer(w, ip(20)), backend(w, ip(30)) {
    // four unrelated clocks
    start.hal.clock_offset_ms = 123456;
    start.hal.drift_ppm = 40;
    finish.hal.clock_offset_ms = 777;
    finish.hal.drift_ppm = -35;
    timer.hal.clock_offset_ms = 5000;
    timer.hal.drift_ppm = 20;
    backend.clock_offset_ms = 1794000000000LL;      // the server runs on wall-clock time
    backend.join(WifiPlan::Net::Primary);
  }

  void run(int64_t ms) {
    w.run(ms, [&] {
      start.tick();
      finish.tick();
      timer.tick();
      backend.tick();
    });
  }
  void run_until(int64_t t) { run(t - w.t); }

  // A rider: cuts the start beams at `t0` and the finish beams `elapsed` ms later.
  void ride(int64_t t0, int64_t elapsed) {
    start.hal.crossing(t0, 30 + w.rng.range(0, 40), 200 + w.rng.range(0, 150));
    finish.hal.crossing(t0 + elapsed, 30 + w.rng.range(0, 40), 200 + w.rng.range(0, 150), w.rng.chance(0.5));
  }

  bool timer_finished() const { return timer.app.stopwatch().state() == Stopwatch::State::Finished; }
  int64_t timer_result() { return timer.app.stopwatch().shown_ms(timer.hal.now_ms()); }

  // elapsed time according to the backend's two readings, or -1
  int64_t backend_result(uint32_t start_seq, uint32_t finish_seq) const {
    const auto* s = backend.find(Role::Start, kStartBoot, start_seq);
    const auto* f = backend.find(Role::Finish, kFinishBoot, finish_seq);
    return (s != nullptr && f != nullptr) ? f->edge_ms - s->edge_ms : -1;
  }
};

int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

}  // namespace

// ---------------------------------------------------------------- happy path
TEST(out_of_the_box_everything_finds_everything) {
  Arena a;
  a.run(3000);
  // gates know where to send, without any address having been configured
  for (sim::GateUnit* g : {&a.start, &a.finish}) {
    CHECK(g->app.uplink().up());
    CHECK(g->app.peers().has_role(Role::Timer));
    CHECK(g->app.peers().has_role(Role::Backend));
    CHECK_EQ(g->app.peers().count(), 2);
    CHECK(g->hal.led[0]);                           // beams free
    CHECK(g->hal.led[1]);                           // WiFi
  }
  CHECK_STR(a.timer.text(), "----");                // both gates online and ready
  CHECK(a.backend.hellos >= 3);
  CHECK_EQ(a.backend.readings.size(), 0);
}

TEST(one_run_shows_the_right_time_everywhere) {
  Arena a;
  a.run(3000);
  a.ride(5000, 23450);
  a.run_until(5200);
  CHECK(a.timer.app.stopwatch().state() == Stopwatch::State::Running);
  a.run_until(15000);
  CHECK(abs64(a.timer.shown_ms() - 10000) <= 10);     // what the digits say, 10 s into the run
  a.run_until(29500);
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 23450) <= 3);
  CHECK(abs64(a.backend_result(1, 1) - 23450) <= 3);
  CHECK_EQ(a.backend.readings.size(), 2);
  // both gates were told their event is stored, so nothing is left to retry
  a.run_until(31000);
  CHECK_EQ(a.start.app.outbox().pending(), 0);
  CHECK_EQ(a.finish.app.outbox().pending(), 0);
  CHECK_EQ(a.start.app.outbox().size(), 0);
  CHECK(a.start.hal.logged("event #1 confirmed by backend"));
  CHECK(a.finish.hal.logged("event #1 confirmed by backend"));
  CHECK(a.timer.hal.logged("RESULT 23.4"));
  // the backend stored the gate's own view of the event too
  CHECK_EQ(a.backend.find(Role::Start, kStartBoot, 1)->first.dir, 1);
  CHECK(a.backend.find(Role::Start, kStartBoot, 1)->copies >= 7);
}

TEST(twenty_riders_in_a_row) {
  Arena a(7);
  a.run(3000);
  int64_t t = 5000;
  std::vector<int64_t> truth;
  for (uint32_t i = 1; i <= 20; i++) {
    const int64_t elapsed = a.w.rng.range(8000, 95000);
    a.ride(t, elapsed);
    a.run_until(t + elapsed + 2500);
    CHECK(a.timer_finished());
    CHECK(abs64(a.timer_result() - elapsed) <= 3);
    CHECK(abs64(a.backend_result(i, i) - elapsed) <= 3);
    truth.push_back(elapsed);
    t += elapsed + a.w.rng.range(4000, 15000);
  }
  CHECK_EQ(a.backend.readings.size(), 40);
  CHECK_EQ(a.start.app.gate().events, 20);
  CHECK_EQ(a.finish.app.gate().events, 20);
  CHECK_EQ(a.start.app.outbox().evicted + a.finish.app.outbox().evicted, 0);
  CHECK_EQ(a.start.app.outbox().expired + a.finish.app.outbox().expired, 0);
  CHECK_EQ(a.start.app.outbox().pending() + a.finish.app.outbox().pending(), 0);
  CHECK_EQ(a.start.app.rx_invalid() + a.finish.app.rx_invalid() + a.timer.app.rx_invalid(), 0);
  CHECK_EQ(a.backend.invalid, 0);
  CHECK(a.start.app.max_gap_ms() <= 2);             // the measuring loop never waited (2 = a drift step of its clock)
}

// --------------------------------------------------------- timing accuracy
// A hostile but realistic WiFi: 1-30 ms of delay per datagram, one in five
// unicasts and one in three broadcasts lost, duplicates, broadcasts held by
// the access point until its next DTIM beacon.
TEST(accuracy_on_a_bad_network) {
  const int runs = hhj_test::iterations(150, 8);
  int worst = 0, within5 = 0, within10 = 0, total = 0;
  int64_t sum_abs = 0;
  for (int run = 0; run < runs; run++) {
    Arena a(1000 + static_cast<uint64_t>(run));
    a.w.net.unicast_min_ms = 1;
    a.w.net.unicast_max_ms = 30;
    a.w.net.unicast_loss = 0.2;
    a.w.net.broadcast_loss = 0.33;
    a.w.net.duplicate = 0.1;
    a.w.net.dtim_ms = 102;
    a.run(6000);
    const int64_t elapsed = a.w.rng.range(5000, 60000);
    a.ride(8000, elapsed);
    a.run_until(8000 + elapsed + 3000);
    CHECK(a.timer_finished());
    CHECK(a.backend_result(1, 1) >= 0);
    for (int64_t got : {a.timer_result(), a.backend_result(1, 1)}) {
      const int err = static_cast<int>(abs64(got - elapsed));
      worst = std::max(worst, err);
      within5 += err <= 5 ? 1 : 0;
      within10 += err <= 10 ? 1 : 0;
      sum_abs += err;
      total++;
    }
  }
  // Each event travels as seven copies and the receiver keeps the quickest, so
  // the error is far below the 29 ms spread of a single datagram.
  CHECK(worst <= 29);                               // hard bound: the spread of one unicast delay
  CHECK(sum_abs * 10 <= total * 45);                // mean error <= 4.5 ms
  CHECK(within5 * 100 >= total * 70);               // >= 70 % within half a hundredth of a second
  CHECK(within10 * 100 >= total * 93);              // >= 93 % within one hundredth
  if (getenv("HHJ_TEST_VERBOSE") != nullptr) {
    printf("   bad network: worst %d ms, mean %.2f ms, within 5 ms: %d/%d, within 10 ms: %d/%d\n", worst,
           static_cast<double>(sum_abs) / total, within5, total, within10, total);
  }
}

// Before a gate has heard the timer's HELLO it can only broadcast, and
// broadcasts wait for the access point's DTIM beacon. The spread of the burst
// still bounds the error.
TEST(accuracy_with_broadcast_only_and_dtim) {
  const int runs = hhj_test::iterations(100, 6);
  int worst = 0;
  for (int run = 0; run < runs; run++) {
    sim::World w(500 + static_cast<uint64_t>(run));
    w.net.dtim_ms = 102;
    sim::GateUnit start(w, ip(10), Role::Start, kStartBoot), finish(w, ip(11), Role::Finish, kFinishBoot);
    sim::TimerUnit timer(w, ip(20));
    timer.hal.radio_ok = false;                     // the timer can listen but its HELLOs never get out
    auto tick = [&] { start.tick(); finish.tick(); timer.tick(); };
    w.run(3000, tick);
    CHECK_EQ(start.app.peers().count(), 0);
    const int64_t t0 = 4000 + w.rng.range(0, 101);    // any phase relative to the beacons
    const int64_t elapsed = w.rng.range(5000, 30000);
    start.hal.crossing(t0);
    finish.hal.crossing(t0 + elapsed);
    w.run(t0 + elapsed + 3000 - w.t, tick);
    CHECK(timer.app.stopwatch().state() == Stopwatch::State::Finished);
    worst = std::max(worst, static_cast<int>(abs64(timer.app.stopwatch().shown_ms(timer.hal.now_ms()) - elapsed)));
  }
  CHECK(worst <= 26);                               // copies at 0,10,20,40,80,160,320 ms: one of them leaves <= 22 ms before a beacon
  CHECK(worst >= 3);                                // ...and it really is worse than unicast: the hold is being simulated
}

TEST(clock_drift_does_not_accumulate_over_a_long_day) {
  // Six hours after power-up the three crystals disagree by more than a second.
  Arena a;
  a.start.hal.drift_ppm = 60;
  a.finish.hal.drift_ppm = -60;
  a.timer.hal.drift_ppm = 0;
  a.w.t = 6 * 3600 * 1000;                          // jump: nothing happened for six hours
  a.run(40000);                                     // WiFi plans re-run from this "boot"
  CHECK(abs64(a.start.hal.now_ms() - a.start.hal.clock_offset_ms - (a.finish.hal.now_ms() - a.finish.hal.clock_offset_ms)) > 2000);
  const int64_t t0 = a.w.t + 2000;
  a.ride(t0, 30000);
  a.run_until(t0 + 33000);
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 30000) <= 3);
  CHECK(abs64(a.backend_result(1, 1) - 30000) <= 3);
}

// ------------------------------------------------------------------- faults
TEST(backend_down_during_the_run_gets_both_events_when_it_returns) {
  Arena a;
  a.run(3000);
  a.backend.power(false);                           // server process stopped
  a.ride(5000, 20000);
  a.run_until(27000);
  CHECK(a.timer_finished());                        // the display does not need the server
  CHECK(abs64(a.timer_result() - 20000) <= 3);
  CHECK_EQ(a.backend.readings.size(), 0);
  CHECK_EQ(a.start.app.outbox().pending(), 1);      // still being retried
  CHECK_EQ(a.finish.app.outbox().pending(), 1);
  a.run_until(60000);
  a.backend.power(true);                            // back after ~55 s
  a.run_until(63000);
  CHECK_EQ(a.backend.readings.size(), 2);
  CHECK(abs64(a.backend_result(1, 1) - 20000) <= 6);          // 3 ms network + clock drift over the wait
  CHECK_EQ(a.start.app.outbox().pending(), 0);
  CHECK_EQ(a.finish.app.outbox().pending(), 0);
  CHECK(a.backend.find(Role::Start, kStartBoot, 1)->first.age_ms > 50000);
}

TEST(no_backend_at_all_timer_still_works_and_gates_give_up_after_five_minutes) {
  sim::World w;
  sim::GateUnit start(w, ip(10), Role::Start, kStartBoot), finish(w, ip(11), Role::Finish, kFinishBoot);
  sim::TimerUnit timer(w, ip(20));
  auto tick = [&] { start.tick(); finish.tick(); timer.tick(); };
  w.run(3000, tick);
  CHECK_STR(timer.text(), "----");
  start.hal.crossing(5000);
  finish.hal.crossing(17340);
  w.run(20000 - w.t, tick);
  CHECK(abs64(timer.app.stopwatch().shown_ms(timer.hal.now_ms()) - 12340) <= 3);
  CHECK_STR(timer.text(), "1234");
  w.run(320000 - w.t, tick);
  CHECK_EQ(start.app.outbox().expired, 1);
  CHECK_EQ(finish.app.outbox().expired, 1);
  CHECK_EQ(start.app.outbox().size(), 0);
  CHECK_STR(timer.text(), "1234");                  // the retries never disturbed the result
}

TEST(start_gate_offline_at_the_start_reports_late_and_the_result_is_still_right) {
  Arena a;
  a.run(3000);
  a.start.hal.in_range = false;                     // only the START gate drops off the WiFi (it keeps measuring)
  a.ride(5000, 15000);
  a.run_until(21000);
  CHECK(!a.start.app.uplink().up());
  CHECK(a.timer.app.stopwatch().state() == Stopwatch::State::Idle);   // FINISH came, but no START known
  CHECK(a.timer.hal.logged("FINISH #1"));
  CHECK(a.timer.hal.logged("does not end a run, noted"));
  a.start.hal.in_range = true;
  a.run_until(40000);                               // START gate is back and delivers
  CHECK(a.start.app.uplink().up());
  CHECK(a.start.hal.logged("EVENT #1"));
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 15000) <= 4);
  CHECK(abs64(a.backend_result(1, 1) - 15000) <= 4);
}

TEST(timer_reboots_mid_run_and_recovers_from_the_retries) {
  // No backend here, so the gates keep repeating their events once a second.
  sim::World w;
  sim::GateUnit start(w, ip(10), Role::Start, kStartBoot), finish(w, ip(11), Role::Finish, kFinishBoot);
  auto timer = std::make_unique<sim::TimerUnit>(w, ip(20));
  auto tick = [&] { start.tick(); finish.tick(); timer->tick(); };
  w.run(3000, tick);
  start.hal.crossing(5000);
  finish.hal.crossing(35000);
  w.run(12000 - w.t, tick);
  CHECK(timer->app.stopwatch().state() == Stopwatch::State::Running);
  // power glitch: a brand-new timer, new boot nonce, empty memory
  w.nodes.erase(std::remove(w.nodes.begin(), w.nodes.end(), static_cast<sim::Node*>(&timer->hal)), w.nodes.end());
  timer = std::make_unique<sim::TimerUnit>(w, ip(20), 0xABCDu);
  w.run(15000 - w.t, tick);
  CHECK(timer->app.stopwatch().state() == Stopwatch::State::Running);
  CHECK(abs64(timer->app.stopwatch().shown_ms(timer->hal.now_ms()) - 10000) <= 4);   // 15 s - 5 s, as if nothing happened
  w.run(38000 - w.t, tick);
  CHECK(timer->app.stopwatch().state() == Stopwatch::State::Finished);
  CHECK(abs64(timer->app.stopwatch().shown_ms(timer->hal.now_ms()) - 30000) <= 4);
}

TEST(gate_reboot_starts_a_new_numbering_that_nobody_confuses) {
  sim::World w;
  auto start = std::make_unique<sim::GateUnit>(w, ip(10), Role::Start, kStartBoot);
  sim::GateUnit finish(w, ip(11), Role::Finish, kFinishBoot);
  sim::TimerUnit timer(w, ip(20));
  sim::ModelBackend backend(w, ip(30));
  backend.join(WifiPlan::Net::Primary);
  auto tick = [&] { start->tick(); finish.tick(); timer.tick(); backend.tick(); };
  w.run(3000, tick);
  start->hal.crossing(5000);
  finish.hal.crossing(15000);
  w.run(20000 - w.t, tick);
  CHECK(abs64(timer.app.stopwatch().shown_ms(timer.hal.now_ms()) - 10000) <= 3);
  // the START gate is power-cycled: new nonce, its first event is #1 again
  w.nodes.erase(std::remove(w.nodes.begin(), w.nodes.end(), static_cast<sim::Node*>(&start->hal)), w.nodes.end());
  start = std::make_unique<sim::GateUnit>(w, ip(10), Role::Start, 0x0BADCAFEu);
  w.run(25000 - w.t, tick);
  start->hal.crossing(30000);
  finish.hal.crossing(42000);
  w.run(46000 - w.t, tick);
  CHECK(timer.app.stopwatch().state() == Stopwatch::State::Finished);
  CHECK(abs64(timer.app.stopwatch().shown_ms(timer.hal.now_ms()) - 12000) <= 3);
  CHECK_EQ(backend.readings.size(), 4);             // START#1 of each boot, FINISH#1 and #2
  CHECK(backend.find(Role::Start, kStartBoot, 1) != nullptr);
  CHECK(backend.find(Role::Start, 0x0BADCAFEu, 1) != nullptr);
  CHECK(backend.find(Role::Finish, kFinishBoot, 2) != nullptr);
}

TEST(wifi_router_restart_in_the_middle_of_a_run) {
  Arena a;
  a.run(3000);
  a.ride(5000, 50000);
  a.run_until(10000);
  a.w.ap[0] = false;                                // router loses power for 8 s
  a.run_until(18000);
  a.w.ap[0] = true;
  a.backend.join(WifiPlan::Net::Primary);
  a.run_until(58000);
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 50000) <= 3);
  CHECK(abs64(a.backend_result(1, 1) - 50000) <= 3);
  CHECK(a.start.hal.logged("WiFi lost"));
}

// ------------------------------------------------------------ WiFi fallback
TEST(home_network_only_everything_works_on_the_fallback) {
  Arena a;
  a.w.ap[0] = false;                                // no competition router here
  a.w.ap[1] = true;                                 // only the home WiFi
  a.backend.join(WifiPlan::Net::Fallback);
  a.run(14000);
  for (sim::GateUnit* g : {&a.start, &a.finish}) {
    CHECK(g->app.uplink().up());
    CHECK(g->app.uplink().net() == WifiPlan::Net::Fallback);
    CHECK(g->hal.connect_calls[0] == WifiPlan::Net::Primary);     // primary was tried first
    CHECK(g->hal.connect_calls[1] == WifiPlan::Net::Fallback);
    CHECK_EQ(g->hal.connect_calls.size(), 2);
  }
  CHECK(a.timer.app.uplink().net() == WifiPlan::Net::Fallback);
  CHECK_STR(a.timer.text(), "----");
  a.ride(16000, 9870);
  a.run_until(29000);
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 9870) <= 3);
  CHECK(abs64(a.backend_result(1, 1) - 9870) <= 3);
}

TEST(router_switched_on_two_minutes_late) {
  Arena a;
  a.w.ap[0] = false;                                // nothing on air when the devices power up
  a.run(120000);
  CHECK(!a.start.app.uplink().up());
  const std::string idle = a.timer.text();
  CHECK(idle == "----" || idle == "    ");          // blinking dashes: no WiFi
  // the fallback was tried exactly once by everyone, then only the primary
  for (const std::vector<WifiPlan::Net>* calls :
       {&a.start.hal.connect_calls, &a.finish.hal.connect_calls, &a.timer.hal.connect_calls}) {
    CHECK(calls->size() >= 10);
    CHECK((*calls)[0] == WifiPlan::Net::Primary);
    CHECK((*calls)[1] == WifiPlan::Net::Fallback);
    for (size_t i = 2; i < calls->size(); i++) CHECK((*calls)[i] == WifiPlan::Net::Primary);
  }
  a.w.ap[0] = true;                                 // router on
  a.backend.join(WifiPlan::Net::Primary);
  a.run(13000);                                     // within one attempt period (+ HELLO)
  CHECK(a.start.app.uplink().up() && a.finish.app.uplink().up() && a.timer.app.uplink().up());
  CHECK(a.start.app.uplink().net() == WifiPlan::Net::Primary);
  CHECK_STR(a.timer.text(), "----");
  a.ride(a.w.t + 2000, 11110);
  a.run(16000);
  CHECK(abs64(a.timer_result() - 11110) <= 3);
  CHECK(abs64(a.backend_result(1, 1) - 11110) <= 3);
}

// ---------------------------------------------------------- things that are not runs
TEST(noise_and_single_beams_never_reach_the_display) {
  Arena a;
  a.run(3000);
  for (int i = 0; i < 40; i++) {
    const int64_t t = 4000 + i * 700;
    a.start.hal.at(t, i % 2, true);                 // one beam only, alternating
    a.start.hal.at(t + (i % 3 == 0 ? 8 : 400), i % 2, false);
  }
  a.run_until(40000);
  CHECK_EQ(a.start.app.gate().events, 0);
  CHECK(a.start.app.gate().lone_a + a.start.app.gate().lone_b > 20);
  CHECK(a.start.app.gate().rejected_short > 10);
  CHECK(a.timer.app.stopwatch().state() == Stopwatch::State::Idle);
  CHECK_EQ(a.backend.readings.size(), 0);
  CHECK_STR(a.timer.text(), "----");
}

TEST(blocked_gate_is_visible_on_the_display) {
  Arena a;
  a.run(3000);
  CHECK_STR(a.timer.text(), "----");
  a.finish.hal.beam[1] = true;                      // someone parked a jump stand in the finish gate
  a.run(2500);
  CHECK_STR(a.timer.text(), "--^^");
  a.finish.hal.beam[1] = false;
  a.run(2500);
  CHECK_STR(a.timer.text(), "----");
  // and a gate that loses power shows as missing
  a.w.nodes.erase(std::remove(a.w.nodes.begin(), a.w.nodes.end(), static_cast<sim::Node*>(&a.start.hal)), a.w.nodes.end());
  a.w.run(8000, [&] { a.finish.tick(); a.timer.tick(); a.backend.tick(); });
  CHECK_STR(a.timer.text(), "__--");
}

TEST(finish_crossing_one_second_after_start_is_not_a_result) {
  Arena a;
  a.run(3000);
  a.start.hal.crossing(5000);
  a.finish.hal.crossing(5700);                      // the previous rider's parent walking out
  a.finish.hal.crossing(5000 + 19000);              // the real finish (gate re-armed long before)
  a.run_until(27000);
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 19000) <= 3);
  CHECK_EQ(a.backend.readings.size(), 3);           // the server keeps every reading; choosing is its business
}

// ----------------------------------------------- the operator's backend decides
// With a backend that drives the display (SHOW), the big digits follow the
// operator's session, not whoever runs through a gate.
TEST(display_follows_the_backend_not_stray_crossings) {
  Arena a;
  a.backend.show_idle();
  a.run(3000);
  CHECK(a.timer.app.remote().active(a.timer.hal.now_ms()));
  CHECK_STR(a.timer.text(), "----");
  // kids run through both gates while no session is open
  a.start.hal.crossing(4000);
  a.finish.hal.crossing(8000);
  a.run_until(12000);
  CHECK_STR(a.timer.text(), "----");
  CHECK_EQ(a.backend.readings.size(), 2);                    // the server still stored them
  // the operator opens a session: 0.00
  a.backend.show_ready();
  a.run(50);
  CHECK_STR(a.timer.text(), " 000");
  // the rider starts; the backend learns the cut from the gate and shows the clock from it
  a.start.hal.crossing(15000);
  a.run_until(15300);
  const auto* s = a.backend.find(Role::Start, kStartBoot, 2);
  CHECK(s != nullptr);
  a.backend.show_run(1, s->edge_ms);
  a.run_until(25000);
  CHECK(abs64(a.timer.shown_ms() - 10000) <= 10);            // 10 s after the cut, to the hundredth shown
  CHECK(abs64(a.timer.app.remote().shown_ms(a.timer.hal.now_ms()) - 10000) <= 6);
  // finish: the backend computes the result and the display shows exactly that number
  a.finish.hal.crossing(15000 + 23450);
  a.run_until(15000 + 23450 + 400);
  const auto* f = a.backend.find(Role::Finish, kFinishBoot, 2);
  CHECK(f != nullptr);
  const int64_t result = f->edge_ms - s->edge_ms;
  CHECK(abs64(result - 23450) <= 3);
  a.backend.show_result(2, static_cast<uint32_t>(result));
  a.run(100);
  CHECK_EQ(a.timer.app.remote().shown_ms(a.timer.hal.now_ms()), result);
  // later crossings change nothing on the display
  a.start.hal.crossing(45000);
  a.run_until(50000);
  CHECK_EQ(a.timer.app.remote().shown_ms(a.timer.hal.now_ms()), result);
  CHECK(a.backend.shows > 40);                               // once a second, plus the changes
}

TEST(display_carries_on_alone_when_the_backend_dies_mid_run) {
  Arena a;
  a.backend.show_ready();
  a.run(3000);
  a.ride(5000, 20000);
  a.run_until(5300);
  a.backend.show_run(1, a.backend.find(Role::Start, kStartBoot, 1)->edge_ms);
  a.run_until(10000);
  CHECK(a.timer.app.remote().active(a.timer.hal.now_ms()));
  a.backend.power(false);                                    // server crashes
  a.run_until(16000);
  CHECK(!a.timer.app.remote().active(a.timer.hal.now_ms()));
  CHECK(abs64(a.timer.shown_ms() - 11000) <= 10);            // the own stopwatch had the same START
  a.run_until(27000);
  CHECK(a.timer_finished());
  CHECK(abs64(a.timer_result() - 20000) <= 3);               // and finishes the run by itself
  CHECK(a.timer.hal.logged("backend silent -> showing the own stopwatch"));
}
