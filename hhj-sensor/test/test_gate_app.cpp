// Tests for gate_app.h — the whole gate on fake hardware. Requirement ids (A-n)
// refer to that file.
#include <string>
#include <vector>

#include "hhj_test.h"
#include "sim_devices.h"

using namespace hhj;
using sim::ip;

namespace {

constexpr uint32_t kBoot = 0xB0071234u;

// A gate (192.168.10.10) with a pretend timer (.20), backend (.30) and a
// bystander (.40) that never announces itself.
struct Rig {
  sim::World w;
  sim::GateUnit gate;
  sim::Probe timer, backend, bystander;

  explicit Rig(Role mark = Role::Start, uint64_t seed = 1)
      : w(seed), gate(w, ip(10), mark, kBoot), timer(w, ip(20)), backend(w, ip(30)), bystander(w, ip(40)) {
    timer.join();
    backend.join();
    bystander.join();
  }

  void run(int64_t ms) {
    w.run(ms, [&] {
      gate.tick();
      timer.listen();
      backend.listen();
      bystander.listen();
    });
  }
  void run_until(int64_t t) { run(t - w.t); }

  void announce(sim::Probe& p, Role role) {
    HelloMsg h;
    h.role = role;
    h.boot = 99;
    snprintf(h.fw, sizeof(h.fw), "probe");
    char buf[200];
    const size_t n = encode(h, buf, sizeof(buf));
    p.say(kBroadcast, std::string(buf, n));
  }
  void ack(sim::Probe& p, Role mark, uint32_t boot, uint32_t seq) {
    AckMsg a;
    a.mark = mark;
    a.boot = boot;
    a.seq = seq;
    char buf[100];
    const size_t n = encode(a, buf, sizeof(buf));
    p.say(ip(10), std::string(buf, n));
  }
};

struct Heard {
  int64_t at;
  bool broadcast;
  Msg m;
};

std::vector<Heard> heard(const sim::Probe& p, Msg::Kind kind) {
  std::vector<Heard> out;
  for (const sim::Datagram& d : p.heard) {
    Msg m;
    if (decode(d.data.data(), d.data.size(), &m) && m.kind == kind) out.push_back(Heard{d.at, d.broadcast, m});
  }
  return out;
}

int count(const std::vector<Heard>& v, bool broadcast) {
  int n = 0;
  for (const Heard& h : v) n += h.broadcast == broadcast ? 1 : 0;
  return n;
}

}  // namespace

// ------------------------------------------------------------ start-up, LEDs
TEST(banner_names_role_version_and_settings) {
  Rig r(Role::Finish);
  CHECK(r.gate.hal.logged("hhj-sensor 2.1.0 [FINISH] boot b0071234"));
  CHECK(r.gate.hal.logged("gate: min_pulse 20 ms, window 150 ms, refractory 1500 ms, stuck 5000 ms"));
}

TEST(a8_wifi_led_blinks_until_connected_then_stays_on) {
  Rig r;
  r.gate.hal.connect_delay_ms = 3000;
  int on = 0, off = 0;
  for (int i = 0; i < 2900; i++) {
    r.run(1);
    (r.gate.hal.led[1] ? on : off)++;
  }
  CHECK(on > 1200 && off > 1200);                 // 1 Hz, half on / half off
  r.run(200);
  CHECK(r.gate.app.uplink().up());
  for (int i = 0; i < 2000; i++) {
    r.run(1);
    CHECK(r.gate.hal.led[1]);
  }
}

TEST(a8_wifi_led_blink_phase_is_500ms) {
  Rig r;
  r.w.ap[0] = false;                              // never connects
  r.run_until(499);
  CHECK(!r.gate.hal.led[1]);
  r.run_until(500);
  CHECK(r.gate.hal.led[1]);
  r.run_until(999);
  CHECK(r.gate.hal.led[1]);
  r.run_until(1000);
  CHECK(!r.gate.hal.led[1]);
}

TEST(a8_state_led_blinks_until_both_beams_have_been_free) {
  Rig r;
  r.gate.hal.beam[0] = true;                      // emitter not aligned at power-up
  int on = 0;
  for (int i = 0; i < 4000; i++) {
    r.run(1);
    on += r.gate.hal.led[0] ? 1 : 0;
  }
  CHECK(on > 1800 && on < 2200);                  // slow blink, even though "stuck" is true by now
  CHECK(!r.gate.app.ready());
  CHECK((r.gate.app.status(r.w.t) & kStReady) == 0);
  r.gate.hal.beam[0] = false;
  r.run(1);
  CHECK(r.gate.app.ready());
  CHECK(r.gate.hal.led[0]);                       // solid on: both beams free
  CHECK_EQ(r.gate.hal.count_logged("both beams free -> gate ready"), 1);
  for (int i = 0; i < 1500; i++) {
    r.run(1);
    CHECK(r.gate.hal.led[0]);
  }
}

TEST(a8_state_led_goes_off_while_a_beam_is_broken) {
  Rig r;
  r.run(100);
  CHECK(r.gate.hal.led[0]);
  r.gate.hal.beam[1] = true;
  r.run(1);
  CHECK(!r.gate.hal.led[0]);
  r.run(300);
  CHECK(!r.gate.hal.led[0]);
  r.gate.hal.beam[1] = false;
  r.run(1);
  CHECK(r.gate.hal.led[0]);
}

TEST(a8_state_led_blinks_fast_when_stuck) {
  Rig r;
  r.run(100);
  r.gate.hal.beam[0] = true;                      // broken from t=101
  r.run_until(5101);
  CHECK(!r.gate.hal.logged("STUCK"));             // exactly 5000 ms: not yet
  r.run(1);
  CHECK(r.gate.hal.logged("STUCK: a beam has been broken > 5000 ms"));
  CHECK((r.gate.app.status(r.w.t) & kStStuck) != 0);
  int toggles = 0;
  bool prev = r.gate.hal.led[0];
  for (int i = 0; i < 1000; i++) {
    r.run(1);
    if (r.gate.hal.led[0] != prev) toggles++;
    prev = r.gate.hal.led[0];
  }
  CHECK_EQ(toggles, 10);                          // 5 Hz
  r.gate.hal.beam[0] = false;
  r.run(1);
  CHECK(r.gate.hal.logged("stuck cleared"));
  CHECK(r.gate.hal.led[0]);
  CHECK((r.gate.app.status(r.w.t) & kStStuck) == 0);
}

// ---------------------------------------------------------------- A-4 hello
TEST(a4_hello_is_broadcast_on_link_up_and_every_two_seconds) {
  Rig r;
  r.run(10000);
  const std::vector<Heard> h = heard(r.bystander, Msg::Kind::Hello);
  CHECK_EQ(h.size(), 5);                          // link up at ~51 ms, then every 2 s
  for (size_t i = 0; i < h.size(); i++) CHECK(h[i].broadcast);
  CHECK(h[0].at <= 51 + 3);
  for (size_t i = 1; i < h.size(); i++) {
    const int64_t gap = h[i].at - h[i - 1].at;
    CHECK(gap >= 1998 && gap <= 2002);            // 2000 ms, +- network jitter
  }
}

TEST(a4_hello_content) {
  Rig r(Role::Finish);
  r.gate.hal.rssi_dbm = -61;
  r.run(3000);
  const std::vector<Heard> h = heard(r.bystander, Msg::Kind::Hello);
  CHECK(h.size() >= 2);
  const HelloMsg& m = h.back().m.hello;
  CHECK(m.role == Role::Finish);
  CHECK_EQ(m.boot, kBoot);
  CHECK_STR(m.fw, "2.1.0");
  CHECK_EQ(m.st, kStReady | kStArmed);
  CHECK(gate_ok(m.st));
  CHECK_EQ(m.seq, 0);
  CHECK_EQ(m.pending, 0);
  CHECK_EQ(m.rssi, -61);
  CHECK_EQ(m.net, 0);
  CHECK(m.up_ms >= 2000 && m.up_ms <= 2100);
}

TEST(a4_hello_status_bits_follow_the_beams) {
  Rig r;
  r.run(100);
  r.gate.hal.beam[0] = true;
  r.run(2100);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).back().m.hello.st, kStBeamA | kStReady | kStArmed);
  CHECK(!gate_ok(heard(r.bystander, Msg::Kind::Hello).back().m.hello.st));
  r.gate.hal.beam[0] = false;
  r.run(100);
  r.gate.hal.beam[0] = r.gate.hal.beam[1] = true; // both together: a crossing -> not armed any more
  r.run(2000);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).back().m.hello.st, kStBeamA | kStBeamB | kStReady);
  r.run(4000);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).back().m.hello.st, kStBeamA | kStBeamB | kStStuck | kStReady);
  r.gate.hal.beam[0] = r.gate.hal.beam[1] = false;
  r.run(4000);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).back().m.hello.st, kStReady | kStArmed);
}

TEST(a4_hello_reports_fallback_network) {
  Rig r;
  r.w.ap[0] = false;
  r.w.ap[1] = true;
  sim::Probe home(r.w, ip(50));
  home.join(WifiPlan::Net::Fallback);
  r.w.run(13000, [&] { r.gate.tick(); home.listen(); });
  CHECK(r.gate.app.uplink().up());
  const std::vector<Heard> h = heard(home, Msg::Kind::Hello);
  CHECK(!h.empty());
  CHECK_EQ(h.back().m.hello.net, 1);
  CHECK(r.gate.hal.led[1]);
}

TEST(a4_rssi_out_of_range_is_clamped) {
  Rig r;
  r.gate.hal.rssi_dbm = -500;
  r.run(500);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).back().m.hello.rssi, -128);
}

// ---------------------------------------------------------------- A-5 peers
TEST(a5_timer_and_backend_become_peers_other_roles_do_not) {
  Rig r;
  r.run(100);
  CHECK_EQ(r.gate.app.peers().count(), 0);
  r.announce(r.timer, Role::Timer);
  r.announce(r.backend, Role::Backend);
  r.announce(r.bystander, Role::Finish);          // another gate: not an event target
  r.run(10);
  CHECK_EQ(r.gate.app.peers().count(), 2);
  CHECK(r.gate.app.peers().has_role(Role::Timer));
  CHECK(r.gate.app.peers().has_role(Role::Backend));
  CHECK(r.gate.hal.logged("TIMER found at 192.168.10.20"));
  CHECK(r.gate.hal.logged("BACKEND found at 192.168.10.30"));
  CHECK(!r.gate.hal.logged("192.168.10.40"));
  r.announce(r.timer, Role::Timer);               // again: no second "found"
  r.run(10);
  CHECK_EQ(r.gate.hal.count_logged("TIMER found"), 1);
}

TEST(a5_silent_peer_is_forgotten_after_ttl) {
  Rig r;
  r.run(100);
  r.announce(r.timer, Role::Timer);
  r.run(10);
  const int64_t seen = r.w.t;
  CHECK_EQ(r.gate.app.peers().count(), 1);
  r.run_until(seen + 6990);
  CHECK_EQ(r.gate.app.peers().count(), 1);
  r.run_until(seen + 7010);
  CHECK_EQ(r.gate.app.peers().count(), 0);
  CHECK(r.gate.hal.logged("1 peer(s) silent -> forgotten"));
}

TEST(a5_peers_are_dropped_when_the_link_drops) {
  Rig r;
  r.run(100);
  r.announce(r.timer, Role::Timer);
  r.run(10);
  CHECK_EQ(r.gate.app.peers().count(), 1);
  r.w.ap[0] = false;
  r.run(5);
  CHECK_EQ(r.gate.app.peers().count(), 0);
}

// --------------------------------------------------------- A-2 / A-3 events
TEST(a2_a3_crossing_is_sent_to_every_peer_and_to_the_broadcast_address) {
  Rig r(Role::Start);
  r.run(100);
  r.announce(r.timer, Role::Timer);
  r.announce(r.backend, Role::Backend);
  r.run(100);
  r.gate.hal.crossing(1000, 40, 250);             // A at 1000, B at 1040
  r.run_until(1900);

  CHECK(r.gate.hal.logged("EVENT #1 t=1000 dir=A->B gap=40 ms"));
  CHECK(r.gate.hal.logged("event #1 sent, 60 ms after the cut, to 2 peer(s) + broadcast"));
  CHECK(r.gate.hal.logged("A broken"));
  CHECK(r.gate.hal.logged("A free after 250 ms"));

  for (sim::Probe* p : {&r.timer, &r.backend}) {
    const std::vector<Heard> ev = heard(*p, Msg::Kind::Event);
    CHECK_EQ(count(ev, false), 7);                // the burst, unicast
    CHECK_EQ(count(ev, true), 7);                 // and the same seven as broadcasts
  }
  const std::vector<Heard> by = heard(r.bystander, Msg::Kind::Event);
  CHECK_EQ(count(by, false), 0);                  // not a peer: broadcasts only
  CHECK_EQ(count(by, true), 7);

  // detected at 1060 (second beam + 20 ms); copies leave at 1060 + {0,10,20,40,80,160,320}
  const int64_t offsets[] = {0, 10, 20, 40, 80, 160, 320};
  for (size_t i = 0; i < by.size(); i++) {
    const EventMsg& e = by[i].m.event;
    CHECK(e.mark == Role::Start);
    CHECK_EQ(e.boot, kBoot);
    CHECK_EQ(e.seq, 1);
    CHECK_EQ(e.t_ms, 1000);
    CHECK_EQ(e.dir, 1);
    CHECK_EQ(e.gap_ms, 40);
    CHECK_EQ(e.n, i);
    CHECK_EQ(e.age_ms, 60 + offsets[i]);          // the true age at the moment of sending
    CHECK(by[i].at - (1060 + offsets[i]) >= 1 && by[i].at - (1060 + offsets[i]) <= 3);
    CHECK(by[i].at - e.age_ms >= 1001);           // arrival - age never precedes the cut
  }
}

TEST(a2_direction_and_mark_are_reported) {
  Rig r(Role::Finish);
  r.run(100);
  r.gate.hal.crossing(1000, 25, 250, false);      // B first
  r.run_until(1500);
  const std::vector<Heard> by = heard(r.bystander, Msg::Kind::Event);
  CHECK(!by.empty());
  CHECK(by[0].m.event.mark == Role::Finish);
  CHECK_EQ(by[0].m.event.dir, -1);
  CHECK_EQ(by[0].m.event.gap_ms, 25);
  CHECK(r.gate.hal.logged("dir=B->A gap=25 ms"));
}

TEST(a3_without_peers_the_event_still_goes_out_as_broadcast) {
  Rig r;
  r.run(100);
  r.gate.hal.crossing(1000);
  r.run_until(1500);
  CHECK(r.gate.hal.logged("to 0 peer(s) + broadcast"));
  CHECK_EQ(count(heard(r.timer, Msg::Kind::Event), true), 7);
  CHECK_EQ(count(heard(r.timer, Msg::Kind::Event), false), 0);
}

TEST(o3_unconfirmed_event_is_repeated_every_second_and_shown_as_pending) {
  Rig r;
  r.run(100);
  r.gate.hal.crossing(1000);
  r.run_until(6500);
  const std::vector<Heard> by = heard(r.bystander, Msg::Kind::Event);
  CHECK_EQ(by.size(), 7 + 5);                     // burst ends at 1380; then 2380, 3380, 4380, 5380, 6380
  CHECK_EQ(by.back().m.event.n, 11);
  CHECK_EQ(by.back().m.event.age_ms, 5380);
  CHECK_EQ(r.gate.app.outbox().pending(), 1);
  const HelloMsg h = heard(r.bystander, Msg::Kind::Hello).back().m.hello;
  CHECK_EQ(h.pending, 1);
  CHECK_EQ(h.seq, 1);
}

TEST(sequence_numbers_increase_per_crossing) {
  Rig r;
  r.run(100);
  r.gate.hal.crossing(1000);
  r.gate.hal.crossing(4000);
  r.gate.hal.crossing(7000);
  r.run_until(8000);
  CHECK_EQ(r.gate.app.gate().events, 3);
  CHECK_EQ(r.gate.app.outbox().last_seq(), 3);
  uint32_t max_seq = 0;
  for (const Heard& h : heard(r.bystander, Msg::Kind::Event)) max_seq = std::max(max_seq, h.m.event.seq);
  CHECK_EQ(max_seq, 3);
  CHECK(r.gate.hal.logged("EVENT #3 t=7000"));
}

// ------------------------------------------------------------------ A-6 ack
TEST(a6_ack_ends_the_retries_but_not_the_burst) {
  Rig r(Role::Start);
  r.run(100);
  r.announce(r.backend, Role::Backend);
  r.run(100);
  r.gate.hal.crossing(1000);
  r.run_until(1066);                              // first copy has reached the backend
  CHECK(!heard(r.backend, Msg::Kind::Event).empty());
  r.ack(r.backend, Role::Start, kBoot, 1);
  r.run_until(8000);
  CHECK(r.gate.hal.logged("event #1 confirmed by backend"));
  CHECK_EQ(count(heard(r.bystander, Msg::Kind::Event), true), 7);   // the whole burst, and nothing after it
  CHECK_EQ(r.gate.app.outbox().pending(), 0);
  CHECK_EQ(r.gate.app.outbox().size(), 0);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).back().m.hello.pending, 0);
}

TEST(a6_acks_for_someone_else_are_ignored) {
  Rig r(Role::Start);
  r.run(100);
  r.gate.hal.crossing(1000);
  r.run_until(1500);
  r.ack(r.backend, Role::Finish, kBoot, 1);       // the other gate's mark
  r.ack(r.backend, Role::Start, kBoot + 1, 1);    // this gate before its reboot
  r.ack(r.backend, Role::Start, kBoot, 2);        // an event that does not exist
  r.run(100);
  CHECK_EQ(r.gate.app.outbox().pending(), 1);
  CHECK(!r.gate.hal.logged("confirmed by backend"));
  r.ack(r.backend, Role::Start, kBoot, 1);
  r.ack(r.backend, Role::Start, kBoot, 1);        // every copy is acknowledged: duplicates are normal
  r.run(100);
  CHECK_EQ(r.gate.app.outbox().pending(), 0);
  CHECK_EQ(r.gate.hal.count_logged("confirmed by backend"), 1);
}

// ---------------------------------------------------------------- offline
TEST(a2_crossing_without_wifi_is_delivered_with_its_true_age_when_the_link_returns) {
  Rig r;
  r.w.ap[0] = false;                              // router is off
  r.run(100);
  r.gate.hal.crossing(1000);
  r.run_until(5000);
  CHECK(r.gate.hal.logged("EVENT #1 t=1000"));    // measured and logged regardless
  CHECK_EQ(heard(r.bystander, Msg::Kind::Event).size(), 0);
  CHECK_EQ(r.gate.app.outbox().pending(), 1);
  r.w.ap[0] = true;                               // router on at t=5000
  r.bystander.join();
  r.run_until(40000);
  const std::vector<Heard> by = heard(r.bystander, Msg::Kind::Event);
  CHECK(by.size() >= 7);
  const EventMsg& first = by[0].m.event;
  CHECK_EQ(first.n, 0);
  CHECK_EQ(first.t_ms, 1000);
  CHECK(first.age_ms > 4000);
  const int64_t implied = by[0].at - first.age_ms;
  CHECK(implied >= 1001 && implied <= 1003);      // the cut is still placed at t=1000 (+ network delay)
}

TEST(a1_measuring_continues_when_sending_fails) {
  Rig r;
  r.run(100);
  r.gate.hal.radio_ok = false;                    // the stack refuses every datagram
  r.gate.hal.crossing(1000);
  r.gate.hal.crossing(4000);
  r.run_until(5000);
  CHECK_EQ(r.gate.app.gate().events, 2);
  CHECK(r.gate.app.tx_failed() > 10);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Event).size(), 0);
  r.gate.hal.radio_ok = true;
  r.run(1500);
  CHECK(heard(r.bystander, Msg::Kind::Event).size() >= 2);          // retries get through afterwards
}

TEST(o6_outbox_overflow_is_logged) {
  Rig r;
  r.w.ap[0] = false;
  r.run(100);
  for (int i = 0; i < 9; i++) r.gate.hal.crossing(1000 + i * 2000);
  r.run_until(19000);
  CHECK_EQ(r.gate.app.gate().events, 9);
  CHECK_EQ(r.gate.app.outbox().evicted, 1);
  CHECK_EQ(r.gate.hal.count_logged("outbox full -> oldest unconfirmed event discarded"), 1);
}

TEST(o5_event_given_up_is_logged) {
  GateAppConfig c = sim::GateUnit::config(Role::Start, kBoot);
  c.outbox.ttl_ms = 3000;
  sim::World w;
  sim::GateUnit gate(w, ip(10), c);
  gate.hal.crossing(1000);
  w.run(6000, [&] { gate.tick(); });
  CHECK_EQ(gate.app.outbox().expired, 1);
  CHECK_EQ(gate.hal.count_logged("1 event(s) never confirmed by a backend -> given up"), 1);
}

// --------------------------------------------------------------- A-7 input
TEST(a7_garbage_and_foreign_datagrams_are_counted_and_ignored) {
  Rig r;
  r.run(100);
  r.bystander.say(ip(10), "not json at all");
  r.bystander.say(ip(10), "{\"hello\":\"TIMER\"}");                                      // boot missing
  r.bystander.say(ip(10), std::string(600, 'x'));                                        // longer than a datagram may be
  r.bystander.say(kBroadcast, "{\"time_mark\":\"FINISH\",\"age_ms\":1,\"boot\":7,\"seq\":1}");   // the other gate's event
  r.bystander.say(ip(10), std::string("{\"hello\":\"TIMER\",\"boot\":1}") + '\0');
  r.run(20);
  CHECK_EQ(r.gate.app.rx_invalid(), 3);           // the 600-byte one never reaches the decoder
  CHECK_EQ(r.gate.app.peers().count(), 0);
  CHECK_EQ(r.gate.app.gate().events, 0);
  r.gate.hal.crossing(1000);
  r.run_until(1500);
  CHECK_EQ(r.gate.app.gate().events, 1);          // still working
}

TEST(a7_datagram_flood_is_drained_without_hurting_measurement) {
  Rig r;
  r.run(100);
  r.gate.hal.crossing(1000, 40, 250);
  for (int t = 900; t < 1400; t++) {
    r.run_until(t);
    for (int k = 0; k < 20; k++) r.bystander.say(ip(10), "{\"hello\":\"TIMER\",\"boot\":1}");
  }
  r.run_until(3000);
  CHECK_EQ(r.gate.app.gate().events, 1);
  CHECK(r.gate.hal.logged("EVENT #1 t=1000 dir=A->B gap=40 ms"));
  CHECK_EQ(r.gate.app.max_gap_ms(), 1);
}

// ------------------------------------------------------------ diagnostics
TEST(log_shows_every_edge_and_decision) {
  Rig r;
  r.run(100);
  r.gate.hal.at(1000, 0, true);                   // 10 ms of noise on A
  r.gate.hal.at(1010, 0, false);
  r.gate.hal.at(2000, 1, true);                   // B alone for 400 ms
  r.gate.hal.at(2400, 1, false);
  r.gate.hal.crossing(4000);
  r.run_until(8000);
  CHECK(r.gate.hal.logged("A free after 10 ms -> too short, ignored"));
  CHECK(r.gate.hal.logged("B alone: no A within 150 ms -> ignored (total 1)"));
  CHECK(r.gate.hal.logged("B free after 400 ms"));
  CHECK(!r.gate.hal.logged("B free after 400 ms -> too short"));
  CHECK_EQ(r.gate.hal.count_logged("EVENT #"), 1);
  CHECK_EQ(r.gate.hal.count_logged("gate armed"), 1);
  CHECK(r.gate.hal.logged("(lone A/B 0/1, short 1)"));
}

TEST(status_line_every_thirty_seconds) {
  Rig r;
  r.run(100);
  r.announce(r.timer, Role::Timer);
  r.gate.hal.crossing(1000);
  r.run_until(29999);
  CHECK_EQ(r.gate.hal.count_logged("status:"), 0);
  r.run_until(30001);
  CHECK_EQ(r.gate.hal.count_logged("status:"), 1);
  CHECK(r.gate.hal.logged("status: wifi up, peers 0, events 1, unconfirmed 1, longest loop pause 1 ms, "
                          "bad datagrams 0, failed sends 0"));
  r.run_until(90001);
  CHECK_EQ(r.gate.hal.count_logged("status:"), 3);
}

TEST(longest_loop_pause_is_measured) {
  Rig r;
  r.run(100);
  CHECK_EQ(r.gate.app.max_gap_ms(), 1);
  r.w.run(37, [] {});                             // the loop stalls for 37 ms
  r.gate.tick();
  CHECK_EQ(r.gate.app.max_gap_ms(), 37);
  r.run(100);
  CHECK_EQ(r.gate.app.max_gap_ms(), 37);
}

TEST(network_work_happens_at_most_once_per_millisecond) {
  Rig r;
  r.run(100);
  r.gate.hal.crossing(1000);
  const long before = r.gate.hal.recv_calls;
  r.w.run(2000, [&] {
    for (int k = 0; k < 5; k++) r.gate.tick();    // a fast loop: five passes per millisecond
    r.bystander.listen();
  });
  CHECK(r.gate.hal.recv_calls - before <= 2000);  // the socket is polled once per ms, not once per pass
  CHECK(r.gate.hal.recv_calls - before >= 1990);
  CHECK_EQ(count(heard(r.bystander, Msg::Kind::Event), true), 7);       // the same schedule as a slow loop
  CHECK_EQ(r.gate.app.gate().events, 1);
}

TEST(gate_clock_offset_is_carried_in_t_but_not_in_age) {
  // A gate that has been running for a day: "t" is on its own clock, age stays relative.
  Rig r;
  r.gate.hal.clock_offset_ms = 86400000;
  r.run(100);
  r.gate.hal.crossing(1000);
  r.run_until(1200);
  const std::vector<Heard> by = heard(r.bystander, Msg::Kind::Event);
  CHECK(!by.empty());
  CHECK_EQ(by[0].m.event.t_ms, 86400000 + 1000);
  CHECK_EQ(by[0].m.event.age_ms, 60);
}

// ------------------------------------------------------ remaining corners
TEST(a8_not_ready_while_beam_b_is_blocked_at_power_up) {
  Rig r;
  r.gate.hal.beam[1] = true;
  r.run(3000);
  CHECK(!r.gate.app.ready());
  CHECK(!r.gate.hal.logged("gate ready"));
  r.gate.hal.beam[1] = false;
  r.run(1);
  CHECK(r.gate.app.ready());
}

TEST(a8_blink_periods_of_zero_mean_no_blinking) {
  GateAppConfig c = sim::GateUnit::config(Role::Start, kBoot);
  c.blink_slow_ms = 0;
  c.blink_fast_ms = 0;
  sim::World w;
  w.ap[0] = false;                                // WiFi never comes up
  sim::GateUnit gate(w, ip(10), c);
  gate.hal.beam[0] = true;                        // never ready, and soon "stuck"
  for (int i = 0; i < 3000; i++) {
    w.run(1, [&] { gate.tick(); });
    CHECK(!gate.hal.led[0]);
    CHECK(!gate.hal.led[1]);
  }
  gate.hal.beam[0] = false;
  w.run(100, [&] { gate.tick(); });
  CHECK(gate.hal.led[0]);                         // ready: on
  gate.hal.beam[0] = true;
  for (int i = 0; i < 7000; i++) {
    w.run(1, [&] { gate.tick(); });
    CHECK(!gate.hal.led[0]);                      // broken, then stuck: off either way
  }
}

TEST(a4_known_peers_get_the_hello_unicast_as_well) {
  Rig r;
  r.run(100);
  r.announce(r.timer, Role::Timer);
  r.run(1900);                                    // t = 2000; next HELLO at 2051
  r.timer.heard.clear();
  r.bystander.heard.clear();
  r.run(4100);                                    // HELLOs at 2051, 4051, 6051
  r.announce(r.timer, Role::Timer);
  const std::vector<Heard> peer = heard(r.timer, Msg::Kind::Hello);
  CHECK_EQ(count(peer, true), 3);
  CHECK_EQ(count(peer, false), 3);                // one unicast copy of each
  const std::vector<Heard> other = heard(r.bystander, Msg::Kind::Hello);
  CHECK_EQ(count(other, true), 3);
  CHECK_EQ(count(other, false), 0);
}

TEST(a1_failed_unicast_hellos_are_counted) {
  Rig r;
  r.run(100);
  r.announce(r.timer, Role::Timer);
  r.run(100);
  const uint32_t before = r.gate.app.tx_failed();
  CHECK_EQ(before, 0);
  r.gate.hal.radio_ok = false;
  r.run(2000);                                    // one HELLO round: 1 unicast + 1 broadcast both refused
  CHECK_EQ(r.gate.app.tx_failed() - before, 2);
}

TEST(a7_a_clock_before_zero_sends_nothing_and_breaks_nothing) {
  // The HAL promises a clock that starts at 0. If it ever went negative the
  // protocol could not carry it: the gate must stay silent rather than send nonsense.
  Rig r;
  r.gate.hal.clock_offset_ms = -100000;
  r.gate.hal.crossing(1000);
  r.run(5000);
  CHECK_EQ(r.gate.app.gate().events, 1);          // still measuring
  CHECK(r.gate.hal.logged("EVENT #1 t=-99000"));
  CHECK_EQ(heard(r.bystander, Msg::Kind::Event).size(), 0);
  CHECK_EQ(heard(r.bystander, Msg::Kind::Hello).size(), 0);
  CHECK(r.gate.app.tx_failed() > 5);
  CHECK_EQ(r.bystander.heard.size(), 0);          // not a single byte left the gate
}

// ------------------------------------------------- found by mutation testing
TEST(default_settings) {
  const GateAppConfig c;
  CHECK(c.mark == Role::Finish);
  CHECK_EQ(c.boot, 0);
  CHECK_STR(c.fw, "2.1.0");
  CHECK_EQ(c.hello_ms, 2000);
  CHECK_EQ(c.peer_ttl_ms, 7000);
  CHECK_EQ(c.status_log_ms, 30000);
  CHECK_EQ(c.blink_slow_ms, 500);
  CHECK_EQ(c.blink_fast_ms, 100);
}

TEST(a_quiet_gate_logs_nothing_about_its_beams) {
  Rig r;
  CHECK_EQ(r.gate.app.max_gap_ms(), 0);           // nothing measured yet
  r.run(5000);
  for (const char* never : {"A broken", "B broken", "A free", "B free", "alone", "EVENT", "gate armed", "STUCK",
                            "stuck cleared", "outbox", "given up", "silent"}) {
    CHECK(!r.gate.hal.logged(never));
  }
  CHECK_EQ(r.gate.hal.count_logged("both beams free -> gate ready"), 1);
}

TEST(log_break_length_boundary_matches_the_gate_logic) {
  Rig r;
  r.run(100);
  r.gate.hal.at(1000, 0, true);
  r.gate.hal.at(1019, 0, false);                  // 19 ms: noise
  r.gate.hal.at(2000, 0, true);
  r.gate.hal.at(2020, 0, false);                  // 20 ms: a real break, alone
  r.run_until(3000);
  CHECK(r.gate.hal.logged("A free after 19 ms -> too short, ignored"));
  CHECK(r.gate.hal.logged("A free after 20 ms"));
  CHECK(!r.gate.hal.logged("A free after 20 ms -> too short"));
  CHECK(r.gate.hal.logged("A alone: no B within 150 ms -> ignored (total 1)"));
  CHECK_EQ(r.gate.hal.count_logged("A alone"), 1);
}

TEST(a8_fast_blink_phase) {
  Rig r;
  r.run(100);
  r.gate.hal.beam[1] = true;                      // stuck from 5102 on
  r.run_until(5999);
  CHECK(r.gate.hal.led[0]);                       // 5999 / 100 = 59: odd -> on
  r.run_until(6000);
  CHECK(!r.gate.hal.led[0]);                      // 60: even -> off
  r.run_until(6099);
  CHECK(!r.gate.hal.led[0]);
  r.run_until(6100);
  CHECK(r.gate.hal.led[0]);
}

TEST(a7_at_most_eight_datagrams_are_read_per_millisecond) {
  Rig r;
  r.w.net.unicast_min_ms = r.w.net.unicast_max_ms = 1;
  r.run(100);
  for (int i = 0; i < 20; i++) r.bystander.say(ip(10), "{\"hello\":\"FINISH\",\"boot\":1}");
  r.run(1);                                       // all 20 arrive in this millisecond
  CHECK_EQ(r.gate.hal.inbox.size(), 12);
  r.run(1);
  CHECK_EQ(r.gate.hal.inbox.size(), 4);
  r.run(1);
  CHECK_EQ(r.gate.hal.inbox.size(), 0);
}

TEST(o7_at_most_two_event_copies_are_sent_per_millisecond) {
  Rig r;
  r.w.net.broadcast_min_ms = r.w.net.broadcast_max_ms = 1;
  r.w.ap[0] = false;                              // three riders while WiFi is down
  r.run(100);
  r.gate.hal.crossing(1000);
  r.gate.hal.crossing(4000);
  r.gate.hal.crossing(7000);
  r.run_until(9000);
  r.w.ap[0] = true;
  r.bystander.join();
  r.run_until(20100);                             // link back at 20051: three first copies are due at once
  const std::vector<Heard> ev = heard(r.bystander, Msg::Kind::Event);
  CHECK(ev.size() >= 3);
  CHECK_EQ(ev[0].m.event.seq, 3);                 // newest first
  CHECK_EQ(ev[1].m.event.seq, 2);
  CHECK_EQ(ev[2].m.event.seq, 1);
  CHECK_EQ(ev[0].at, ev[1].at);                   // two in the first millisecond...
  CHECK_EQ(ev[2].at, ev[0].at + 1);               // ...the third in the next
}

TEST(o5_each_given_up_event_is_reported_when_it_happens) {
  GateAppConfig c = sim::GateUnit::config(Role::Start, kBoot);
  c.outbox.ttl_ms = 3000;
  sim::World w;
  sim::GateUnit gate(w, ip(10), c);
  gate.hal.crossing(1000);
  gate.hal.crossing(6000);
  w.run(12000, [&] { gate.tick(); });
  CHECK_EQ(gate.app.outbox().expired, 2);
  CHECK_EQ(gate.hal.count_logged("1 event(s) never confirmed by a backend -> given up"), 2);
  CHECK_EQ(gate.hal.count_logged("never confirmed"), 2);
}

TEST(a4_a_version_string_the_protocol_cannot_carry_stops_only_the_hello) {
  GateAppConfig c = sim::GateUnit::config(Role::Start, kBoot);
  c.fw = "not a version";
  sim::World w;
  sim::GateUnit gate(w, ip(10), c);
  sim::Probe listener(w, ip(40));
  listener.join();
  w.run(4100, [&] { gate.tick(); listener.listen(); });       // HELLOs would be due at 51, 2051, 4051
  CHECK_EQ(listener.heard.size(), 0);
  CHECK_EQ(gate.app.tx_failed(), 3);
  gate.hal.crossing(5000);
  w.run(1500, [&] { gate.tick(); listener.listen(); });
  CHECK_EQ(listener.heard_kind(Msg::Kind::Event).size(), 7);  // events do not depend on it
}

TEST(status_before_the_first_measurement) {
  sim::World w;
  sim::GateUnit gate(w, ip(10), Role::Start, kBoot);
  CHECK_EQ(gate.app.status(0), kStArmed);         // no beam seen broken, not ready yet, armed
  CHECK(!gate.app.ready());
}
