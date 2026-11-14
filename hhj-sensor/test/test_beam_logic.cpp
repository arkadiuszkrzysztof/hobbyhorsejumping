// Tests for beam_logic.h — requirement ids (G-n) refer to that file.
#include <algorithm>
#include <string>
#include <vector>

#include "beam_logic.h"
#include "hhj_test.h"

using hhj::GateConfig;
using hhj::GateEvent;
using hhj::GateLogic;

namespace {

struct Rec {
  int64_t at;      // when update() reported it
  GateEvent ev;
};

// A gate fed one sample per millisecond.
struct Bench {
  GateLogic g;
  bool a = false, b = false;
  int64_t t = 0;                 // next sample time
  std::vector<Rec> events;

  explicit Bench(const GateConfig& c = GateConfig{}) : g(c) {}

  // Hold the given beam states for every millisecond from now through `until`.
  void hold(bool na, bool nb, int64_t until) {
    a = na;
    b = nb;
    for (; t <= until; t++) {
      GateEvent ev;
      if (g.update(a, b, t, &ev)) events.push_back(Rec{t, ev});
    }
  }
  // A clean crossing: first beam at `at`, second `gap` ms later, both released at at+len.
  void cross(int64_t at, int64_t gap, int64_t len, bool a_first = true) {
    hold(false, false, at - 1);
    if (gap > 0) hold(a_first, !a_first, at + gap - 1);
    hold(true, true, at + len - 1);
    hold(false, false, at + len);
  }
};

}  // namespace

// ------------------------------------------------------------------ basics
TEST(idle_gate_reports_nothing) {
  Bench s;
  s.hold(false, false, 10000);
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.events, 0);
  CHECK_EQ(s.g.lone_a + s.g.lone_b + s.g.rejected_short, 0);
  CHECK(s.g.armed());
  CHECK(!s.g.stuck(10000));
}

TEST(default_configuration) {
  const GateConfig c;
  CHECK_EQ(c.min_pulse_ms, 20);
  CHECK_EQ(c.window_ms, 150);
  CHECK_EQ(c.refractory_ms, 1500);
  CHECK_EQ(c.stuck_ms, 5000);
  GateLogic g;
  CHECK_EQ(g.config().window_ms, 150);
}

TEST(g2_g3_g4_crossing_a_then_b) {
  Bench s;
  s.cross(1000, 40, 300);
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].ev.t_ms, 1000);          // G-3: the first raw edge, not the detection time
  CHECK_EQ(s.events[0].ev.dir, 1);              // G-4
  CHECK_EQ(s.events[0].ev.gap_ms, 40);
  CHECK_EQ(s.events[0].at, 1060);               // detected when the second beam has lasted min_pulse
  CHECK_EQ(s.g.events, 1);
  CHECK_EQ(s.g.lone_a + s.g.lone_b + s.g.rejected_short, 0);
}

TEST(g4_crossing_b_then_a) {
  Bench s;
  s.cross(500, 30, 300, false);
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].ev.t_ms, 500);
  CHECK_EQ(s.events[0].ev.dir, -1);
  CHECK_EQ(s.events[0].ev.gap_ms, 30);
  CHECK_EQ(s.events[0].at, 550);
}

TEST(g4_simultaneous_beams_count_as_a_first) {
  Bench s;
  s.cross(700, 0, 200);
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].ev.t_ms, 700);
  CHECK_EQ(s.events[0].ev.dir, 1);
  CHECK_EQ(s.events[0].ev.gap_ms, 0);
  CHECK_EQ(s.events[0].at, 720);
}

TEST(null_output_pointer_is_allowed) {
  GateLogic g;
  int got = 0;
  for (int64_t t = 0; t < 400; t++) got += g.update(t >= 100 && t < 300, t >= 120 && t < 300, t, nullptr) ? 1 : 0;
  CHECK_EQ(got, 1);
  CHECK_EQ(g.events, 1);
}

// ------------------------------------------------------- G-1 minimum pulse
TEST(g1_short_break_is_noise) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 118);                     // broken for samples 100..118
  s.hold(false, false, 600);                    // released at 119: lasted 19 ms
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.rejected_short, 1);
  CHECK_EQ(s.g.lone_a, 0);
}

TEST(g1_break_of_exactly_min_pulse_is_real) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 119);
  s.hold(false, false, 600);                    // released at 120: lasted 20 ms
  CHECK_EQ(s.g.rejected_short, 0);
  CHECK_EQ(s.g.lone_a, 1);                      // real, but alone
}

TEST(g1_shortest_possible_break) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(false, true, 100);                     // one sample
  s.hold(false, false, 300);
  CHECK_EQ(s.g.rejected_short, 1);
  CHECK_EQ(s.g.lone_b, 0);
}

TEST(g1_noise_on_one_beam_does_not_pair_with_a_real_break_on_the_other) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 110);                     // A really broken
  s.hold(true, true, 115);                      // B only for 5 ms
  s.hold(true, false, 400);
  s.hold(false, false, 600);
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.rejected_short, 1);
  CHECK_EQ(s.g.lone_a, 1);
  CHECK_EQ(s.g.lone_b, 0);
}

TEST(g2_second_beam_must_last_min_pulse_too) {
  // B joins and both are released before B has lasted 20 ms: no crossing.
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 139);
  s.hold(true, true, 158);                      // B broken 140..158 = 19 ms
  s.hold(false, false, 500);
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.rejected_short, 1);              // B
  CHECK_EQ(s.g.lone_a, 1);                      // A was real and unpaired
}

TEST(g2_both_must_still_be_broken_when_the_second_is_confirmed) {
  // A is released 1 ms before B reaches min_pulse.
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 139);                     // A from 100
  s.hold(true, true, 158);                      // B from 140
  s.hold(false, true, 400);                     // A released at 159; B would be confirmed at 160
  s.hold(false, false, 700);
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.lone_a, 1);
  CHECK_EQ(s.g.lone_b, 1);
}

// -------------------------------------------------------------- G-2 window
TEST(g2_gap_of_exactly_the_window_is_a_crossing) {
  Bench s;
  s.cross(1000, 150, 400);
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].ev.gap_ms, 150);
  CHECK_EQ(s.events[0].ev.t_ms, 1000);
  CHECK_EQ(s.g.lone_a + s.g.lone_b, 0);
}

TEST(g2_g5_gap_one_past_the_window_is_two_lone_beams) {
  Bench s;
  s.cross(1000, 151, 400);
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.lone_a, 1);
  CHECK_EQ(s.g.lone_b, 1);
  CHECK(s.g.armed());
}

TEST(g2_window_boundary_in_the_other_direction) {
  Bench ok;
  ok.cross(1000, 150, 400, false);
  CHECK_EQ(ok.events.size(), 1);
  CHECK_EQ(ok.events[0].ev.dir, -1);
  Bench far;
  far.cross(1000, 151, 400, false);
  CHECK_EQ(far.events.size(), 0);
  CHECK_EQ(far.g.lone_a, 1);
  CHECK_EQ(far.g.lone_b, 1);
}

// ---------------------------------------------------------------- G-5 lone
TEST(g5_long_lone_beam_is_counted_once_when_the_window_passes) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 250);
  CHECK_EQ(s.g.lone_a, 0);                      // 150 ms after the edge: still inside the window
  s.hold(true, false, 251);
  CHECK_EQ(s.g.lone_a, 1);                      // 151 ms: written off
  s.hold(true, false, 3000);
  s.hold(false, false, 4000);
  CHECK_EQ(s.g.lone_a, 1);                      // not counted again while held or on release
  CHECK_EQ(s.g.lone_b, 0);
  CHECK_EQ(s.events.size(), 0);
}

TEST(g5_lone_beam_shorter_than_the_window_is_counted_on_release) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(false, true, 180);                     // B broken 100..180
  CHECK_EQ(s.g.lone_b, 0);
  s.hold(false, false, 181);
  CHECK_EQ(s.g.lone_b, 1);
  s.hold(false, false, 1000);
  CHECK_EQ(s.g.lone_b, 1);
}

TEST(g5_spent_beam_cannot_pair_until_released_and_broken_again) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 999);                     // A blocked for a long time: lone, spent
  s.hold(true, true, 1500);                     // B arrives much later
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.lone_a, 1);
  CHECK_EQ(s.g.lone_b, 1);
  s.hold(false, true, 1600);                    // A released...
  s.hold(true, true, 1900);                     // ...and broken again while B is still (spent) broken
  CHECK_EQ(s.events.size(), 0);                 // B is spent: still nothing
  s.hold(false, false, 2000);
  s.cross(2100, 20, 200);                       // a fresh crossing works (gate was never quiet)
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].ev.t_ms, 2100);
}

TEST(g5_older_beam_is_written_off_when_the_partner_comes_too_late) {
  // A at 100, B at 400 while A is still broken: A lone (window), then B lone.
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 399);
  s.hold(true, true, 700);
  s.hold(false, false, 900);
  CHECK_EQ(s.events.size(), 0);
  CHECK_EQ(s.g.lone_a, 1);
  CHECK_EQ(s.g.lone_b, 1);
}

TEST(g5_partner_arriving_just_inside_the_window_delays_the_write_off) {
  // B breaks at edge+150 but is only confirmed 20 ms later: A must wait for it.
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 249);
  s.hold(true, true, 269);
  CHECK_EQ(s.g.lone_a, 0);
  CHECK_EQ(s.events.size(), 0);
  s.hold(true, true, 270);                      // B confirmed: gap 150 -> crossing
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].ev.gap_ms, 150);
  CHECK_EQ(s.g.lone_a, 0);
}

// ---------------------------------------------------------- G-6 quiet time
TEST(g6_rider_standing_in_the_gate_is_one_event) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, true, 60000);                    // a full minute
  CHECK_EQ(s.events.size(), 1);
  CHECK(!s.g.armed());
  s.hold(false, false, 61499);
  CHECK(!s.g.armed());
  s.hold(false, false, 61501);
  CHECK(s.g.armed());
  CHECK_EQ(s.g.lone_a + s.g.lone_b, 0);         // nothing about that visit counts as lone
}

TEST(g6_rearm_boundary_is_exact) {
  Bench s;
  s.cross(1000, 40, 300);                       // both free from sample 1300
  CHECK(!s.g.armed());
  s.hold(false, false, 2799);
  CHECK(!s.g.armed());                          // 1499 ms of quiet
  s.hold(false, false, 2800);
  CHECK(s.g.armed());                           // 1500 ms
}

TEST(g6_crossing_too_soon_is_swallowed_and_restarts_the_quiet_time) {
  Bench s;
  s.cross(1000, 40, 300);                       // free at 1300
  s.cross(2000, 40, 300);                       // 700 ms later: same rider's tail / second child too close
  CHECK_EQ(s.events.size(), 1);
  CHECK(!s.g.armed());
  s.hold(false, false, 3799);                   // free again at 2300: quiet until 3800
  CHECK(!s.g.armed());
  s.hold(false, false, 3800);
  CHECK(s.g.armed());
  CHECK_EQ(s.g.lone_a + s.g.lone_b, 0);
  s.cross(3900, 40, 300);
  CHECK_EQ(s.events.size(), 2);
  CHECK_EQ(s.events[1].ev.t_ms, 3900);
}

TEST(g6_first_possible_next_crossing) {
  // Free at 1300 -> armed at sample 2800. An edge AT 2800 is still inside the quiet time.
  Bench late;
  late.cross(1000, 40, 300);
  late.cross(2801, 40, 300);
  CHECK_EQ(late.events.size(), 2);
  CHECK_EQ(late.events[1].ev.t_ms, 2801);
  Bench early;
  early.cross(1000, 40, 300);
  early.cross(2800, 40, 300);
  CHECK_EQ(early.events.size(), 1);
}

TEST(g6_one_beam_still_blocked_keeps_the_gate_quiet) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, true, 400);
  s.hold(true, false, 5000);                    // A stays blocked
  CHECK(!s.g.armed());
  s.hold(false, false, 6499);                   // A free at 5001
  CHECK(!s.g.armed());
  s.hold(false, false, 6501);
  CHECK(s.g.armed());
}

TEST(g6_noise_does_not_prolong_the_quiet_time) {
  // The fix over the first version: one-sample glitches used to restart the
  // quiet timer, so a flickering beam could keep the gate disarmed for ever.
  Bench s;
  s.cross(1000, 40, 300);                       // free at 1300, armed at 2800
  for (int64_t t = 1400; t < 2700; t += 100) {  // a glitch every 100 ms
    s.hold(false, false, t - 1);
    s.hold(true, false, t + 9);                 // 10 ms on A
    s.hold(false, false, t + 49);
    s.hold(false, true, t + 54);                // 5 ms on B
  }
  s.hold(false, false, 2799);
  CHECK(!s.g.armed());
  s.hold(false, false, 2800);
  CHECK(s.g.armed());
  CHECK_EQ(s.g.rejected_short, 26);
  s.cross(2900, 40, 300);
  CHECK_EQ(s.events.size(), 2);
}

TEST(g6_noise_in_progress_at_the_deadline_only_delays_arming_until_it_ends) {
  Bench s;
  s.cross(1000, 40, 300);                       // armed at 2800 if quiet
  s.hold(false, false, 2794);
  s.hold(true, false, 2804);                    // glitch 2795..2804 spans the deadline
  CHECK(!s.g.armed());
  s.hold(false, false, 2805);
  CHECK(s.g.armed());                           // first free sample after the deadline
}

TEST(g6_real_break_just_under_the_deadline_restarts_the_quiet_time) {
  Bench s;
  s.cross(1000, 40, 300);                       // deadline 2800
  s.hold(false, false, 2789);
  s.hold(true, false, 2850);                    // real break from 2790
  s.hold(false, false, 4349);                   // free at 2851 -> quiet until 4351
  CHECK(!s.g.armed());
  s.hold(false, false, 4351);
  CHECK(s.g.armed());
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.g.lone_a, 0);                      // breaks during the quiet time are never "lone"
}

TEST(g6_beams_released_at_different_times) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, true, 300);
  s.hold(false, true, 500);                     // A free at 301, B free at 501
  s.hold(false, false, 2000);
  CHECK(!s.g.armed());                          // quiet counted from 501, not 301
  s.hold(false, false, 2001);
  CHECK(s.g.armed());
}

// --------------------------------------------------------------- G-7 stuck
TEST(g7_stuck_boundary_is_exact) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, false, 5200);                    // A broken from 100
  CHECK(!s.g.stuck(5100));                      // 5000 ms: not yet
  CHECK(s.g.stuck(5101));
  CHECK_EQ(s.events.size(), 0);
  s.hold(false, false, 5300);
  CHECK(!s.g.stuck(5300));
  CHECK(!s.g.stuck(99999));
}

TEST(g7_stuck_is_reported_for_either_beam_and_during_quiet_time) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(false, true, 6000);
  CHECK(s.g.stuck(6000));
  Bench q;
  q.hold(false, false, 99);
  q.hold(true, true, 5200);                     // event, then both stay blocked
  CHECK_EQ(q.events.size(), 1);
  CHECK(q.g.stuck(5200));
  CHECK(!q.g.armed());
}

// ----------------------------------------------------------------- G-8 clock
TEST(g8_clock_stepping_back_creates_nothing) {
  GateLogic g;
  GateEvent ev;
  int events = 0;
  for (int64_t t = 0; t < 1000; t++) events += g.update(false, false, t, &ev) ? 1 : 0;
  // the clock jumps back 900 ms while both beams break
  for (int64_t t = 100; t < 119; t++) events += g.update(true, true, t, &ev) ? 1 : 0;
  CHECK_EQ(events, 0);                          // frozen at 999: nothing can be confirmed
  for (int64_t t = 119; t < 1019; t++) events += g.update(true, true, t, &ev) ? 1 : 0;
  CHECK_EQ(events, 0);                          // still only 19 ms of real progress past 999
  events += g.update(true, true, 1019, &ev) ? 1 : 0;
  CHECK_EQ(events, 1);                          // 20 ms after the edge (stamped 999)
  CHECK_EQ(ev.t_ms, 999);
}

TEST(g8_repeated_timestamps_are_harmless) {
  GateLogic g;
  GateEvent ev;
  int events = 0;
  for (int rep = 0; rep < 1000; rep++) events += g.update(true, true, 500, &ev) ? 1 : 0;
  CHECK_EQ(events, 0);                          // no time passes: never confirmed
  events += g.update(true, true, 520, &ev) ? 1 : 0;
  CHECK_EQ(events, 1);
  CHECK_EQ(ev.t_ms, 500);
}

// ----------------------------------------------------------- configuration
TEST(custom_configuration_is_honoured) {
  GateConfig c;
  c.min_pulse_ms = 5;
  c.window_ms = 30;
  c.refractory_ms = 200;
  c.stuck_ms = 1000;
  Bench s(c);
  s.cross(100, 30, 100);                        // gap == window
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].at, 135);                // second edge 130 + min_pulse 5
  s.hold(false, false, 399);                    // free at 200 -> armed at 400
  CHECK(!s.g.armed());
  s.hold(false, false, 400);
  CHECK(s.g.armed());
  s.cross(500, 31, 100);                        // one past the window
  CHECK_EQ(s.events.size(), 1);
  s.hold(true, false, 1700);                    // A broken from 601
  CHECK(!s.g.stuck(1601));                      // 1000 ms
  CHECK(s.g.stuck(1602));
}

TEST(zero_min_pulse_and_zero_window) {
  GateConfig c;
  c.min_pulse_ms = 0;                           // every break is real at once
  c.window_ms = 0;                              // beams must break in the same millisecond
  c.refractory_ms = 0;                          // armed again as soon as both are free
  Bench s(c);
  s.hold(false, false, 9);
  s.hold(true, true, 10);
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.events[0].at, 10);
  s.hold(false, false, 11);
  CHECK(s.g.armed());
  s.hold(true, false, 12);
  s.hold(true, true, 20);                       // 1 ms apart: not a crossing with window 0
  CHECK_EQ(s.events.size(), 1);
  CHECK_EQ(s.g.rejected_short, 0);
}

// ------------------------------------------------------------ irregular loop
TEST(irregular_sampling_still_gives_one_event_with_a_bounded_timestamp_error) {
  hhj_test::Rng rng(11);
  for (int run = 0; run < hhj_test::iterations(300, 20); run++) {
    GateLogic g;
    GateEvent ev{};
    int events = 0;
    int64_t t = 0;
    const int64_t edge = 1000 + rng.range(0, 50), gap = rng.range(0, 100), len = 300;
    while (t < 4000) {
      t += rng.range(1, 5);                     // the loop is late by up to 4 ms
      const bool a = t >= edge && t < edge + len;
      const bool b = t >= edge + gap && t < edge + gap + len;
      GateEvent e;
      if (g.update(a, b, t, &e)) {
        events++;
        ev = e;
      }
    }
    CHECK_EQ(events, 1);
    CHECK(ev.t_ms >= edge && ev.t_ms <= edge + 4);          // never early, at most one loop late
    CHECK(ev.gap_ms + 4 >= gap && ev.gap_ms <= gap + 4);
    CHECK_EQ(ev.dir, 1);
  }
}

// ------------------------------------------------------------------- fuzzing
namespace {

struct Edge {
  int64_t at;
  int beam;       // 0 = A, 1 = B
  bool broken;
};

struct Expected {
  std::vector<GateEvent> events;
  std::vector<int64_t> detected;
  uint32_t lone_a = 0, lone_b = 0, shorts = 0;
};

// Builds a timeline whose correct outcome is known by construction — crossings,
// noise, lone breaks and too-late partners, placed either clearly while the
// gate is armed or clearly inside its quiet time.
void generate(hhj_test::Rng& rng, const GateConfig& c, std::vector<Edge>* edges, Expected* want) {
  int64_t t = 50;               // the timeline is free of breaks before this point
  int64_t armed_at = 0;         // the gate is armed from this sample on
  const int segments = 30;
  for (int k = 0; k < segments; k++) {
    // Either well after re-arming, or (when there is room) well inside the quiet time.
    const bool room_in_quiet = armed_at - 50 > t + 2;
    const bool in_quiet = room_in_quiet && rng.chance(0.3);
    int64_t s;
    if (in_quiet) {
      s = rng.range(t + 2, armed_at - 50);
    } else {
      s = std::max(t + 2, armed_at + 50) + rng.range(0, 300);
    }
    const uint32_t kind = rng.below(10);
    if (kind < 4) {                                                   // crossing
      const bool a_first = rng.chance(0.5);
      const int64_t gap = rng.range(0, c.window_ms);
      const int64_t confirm = s + gap + c.min_pulse_ms;               // second beam has lasted min_pulse
      const int64_t r1 = confirm + rng.range(1, 250), r2 = confirm + rng.range(1, 250);
      edges->push_back(Edge{s, a_first ? 0 : 1, true});
      edges->push_back(Edge{s + gap, a_first ? 1 : 0, true});
      edges->push_back(Edge{r1, 0, false});
      edges->push_back(Edge{r2, 1, false});
      const int64_t free_at = std::max(r1, r2);
      if (!in_quiet) {
        GateEvent ev;
        ev.t_ms = s;
        ev.dir = (a_first || gap == 0) ? int8_t{1} : int8_t{-1};
        ev.gap_ms = static_cast<uint16_t>(gap);
        want->events.push_back(ev);
        want->detected.push_back(confirm);
      }
      armed_at = free_at + c.refractory_ms;
      t = free_at;
    } else if (kind < 7) {                                            // noise
      const int beam = static_cast<int>(rng.below(2));
      const int64_t len = rng.range(1, c.min_pulse_ms - 1);
      edges->push_back(Edge{s, beam, true});
      edges->push_back(Edge{s + len, beam, false});
      want->shorts++;
      t = s + len;                                                    // armed_at unchanged (G-6)
    } else if (kind < 9) {                                            // one beam alone
      const int beam = static_cast<int>(rng.below(2));
      const int64_t len = rng.range(c.min_pulse_ms, 700);
      edges->push_back(Edge{s, beam, true});
      edges->push_back(Edge{s + len, beam, false});
      if (in_quiet) {
        armed_at = s + len + c.refractory_ms;                         // a real break restarts the quiet time
      } else {
        (beam == 0 ? want->lone_a : want->lone_b)++;
      }
      t = s + len;
    } else {                                                          // partner too late
      const bool a_first = rng.chance(0.5);
      const int64_t gap = c.window_ms + rng.range(1, 300);
      const int64_t end = s + gap + c.min_pulse_ms + rng.range(1, 200);
      edges->push_back(Edge{s, a_first ? 0 : 1, true});
      edges->push_back(Edge{s + gap, a_first ? 1 : 0, true});
      edges->push_back(Edge{end, 0, false});
      edges->push_back(Edge{end + rng.range(0, 30), 1, false});
      const int64_t free_at = edges->back().at;
      if (in_quiet) {
        armed_at = free_at + c.refractory_ms;
      } else {
        want->lone_a++;
        want->lone_b++;
      }
      t = free_at;
    }
  }
}

}  // namespace

TEST(fuzz_constructed_timelines_match_their_known_outcome) {
  hhj_test::Rng rng(2026);
  const int runs = hhj_test::iterations(1500, 40);
  const int failures_before = hhj_test::failures();
  for (int run = 0; run < runs; run++) {
    GateConfig c;
    if (run % 3 == 1) {                           // also away from the defaults
      c.min_pulse_ms = static_cast<uint16_t>(rng.range(2, 40));
      c.window_ms = static_cast<uint16_t>(rng.range(20, 400));
      c.refractory_ms = static_cast<uint16_t>(rng.range(200, 3000));
    }
    std::vector<Edge> edges;
    Expected want;
    generate(rng, c, &edges, &want);
    std::stable_sort(edges.begin(), edges.end(), [](const Edge& x, const Edge& y) { return x.at < y.at; });

    GateLogic g(c);
    bool beam[2] = {false, false};
    std::vector<Rec> got;
    size_t next = 0;
    const int64_t end = edges.back().at + 5000;
    for (int64_t t = 0; t <= end; t++) {
      while (next < edges.size() && edges[next].at == t) {
        beam[edges[next].beam] = edges[next].broken;
        next++;
      }
      GateEvent ev;
      if (g.update(beam[0], beam[1], t, &ev)) got.push_back(Rec{t, ev});
    }

    CHECK_EQ(got.size(), want.events.size());
    for (size_t i = 0; i < got.size() && i < want.events.size(); i++) {
      CHECK_EQ(got[i].ev.t_ms, want.events[i].t_ms);
      CHECK_EQ(got[i].ev.dir, want.events[i].dir);
      CHECK_EQ(got[i].ev.gap_ms, want.events[i].gap_ms);
      CHECK_EQ(got[i].at, want.detected[i]);
    }
    CHECK_EQ(g.events, want.events.size());
    CHECK_EQ(g.lone_a, want.lone_a);
    CHECK_EQ(g.lone_b, want.lone_b);
    CHECK_EQ(g.rejected_short, want.shorts);
    CHECK(g.armed());                             // 5 s of silence at the end
    if (hhj_test::failures() > failures_before) {
      printf("   (run %d, min_pulse %u window %u refractory %u)\n", run, c.min_pulse_ms, c.window_ms, c.refractory_ms);
      return;
    }
  }
}

// Arbitrary beam activity: whatever happens, every reported event must be
// backed by what the beams really did.
TEST(fuzz_random_activity_never_breaks_the_rules) {
  hhj_test::Rng rng(77);
  const int runs = hhj_test::iterations(300, 15);
  uint32_t total_events = 0;
  for (int run = 0; run < runs; run++) {
    const GateConfig c;
    GateLogic g(c);
    bool beam[2] = {false, false};
    int64_t edge[2] = {0, 0};            // start of the current break
    int64_t last_event_at = -1;
    int64_t quiet_free_since = -1;       // first both-free sample after the last event
    // Bursts of chaotic activity separated by silences of 0.2 - 4 s.
    const double p_break = 0.01 + 0.02 * (run % 5), p_free = 0.004 + 0.01 * (run % 7);
    int64_t burst_end = 0, next_burst = 100;
    for (int64_t t = 1; t < 60000; t++) {
      if (t == next_burst) {
        burst_end = t + rng.range(30, 700);
        next_burst = burst_end + rng.range(200, 4000);
      }
      for (int k = 0; k < 2; k++) {
        if (t >= burst_end) beam[k] = false;
        else if (!beam[k] && rng.chance(p_break)) { beam[k] = true; edge[k] = t; }
        else if (beam[k] && rng.chance(p_free)) beam[k] = false;
      }
      if (last_event_at >= 0 && quiet_free_since < 0 && !beam[0] && !beam[1]) quiet_free_since = t;
      GateEvent ev;
      if (!g.update(beam[0], beam[1], t, &ev)) continue;

      CHECK(beam[0] && beam[1]);                                         // G-2: both broken now
      CHECK(t - edge[0] >= c.min_pulse_ms && t - edge[1] >= c.min_pulse_ms);
      const int64_t first = std::min(edge[0], edge[1]);
      CHECK_EQ(ev.t_ms, first);                                          // G-3
      CHECK_EQ(ev.gap_ms, std::max(edge[0], edge[1]) - first);
      CHECK(ev.gap_ms <= c.window_ms);
      CHECK_EQ(ev.dir, edge[0] <= edge[1] ? 1 : -1);                     // G-4
      if (last_event_at >= 0) {                                          // G-6
        CHECK(quiet_free_since >= 0);
        CHECK(first >= quiet_free_since + c.refractory_ms);
      }
      last_event_at = t;
      quiet_free_since = -1;
    }
    total_events += g.events;
  }
  CHECK(total_events > 5u * static_cast<uint32_t>(runs));   // the random activity does produce crossings
}

// ------------------------------------------------- found by mutation testing
TEST(a_fresh_event_record_is_all_zero) {
  const GateEvent ev;
  CHECK_EQ(ev.t_ms, 0);
  CHECK_EQ(ev.dir, 0);
  CHECK_EQ(ev.gap_ms, 0);
}

TEST(g5_lone_beam_b_is_also_counted_exactly_when_the_window_passes) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(false, true, 250);
  CHECK_EQ(s.g.lone_b, 0);                      // 150 ms: still inside the window
  s.hold(false, true, 251);
  CHECK_EQ(s.g.lone_b, 1);                      // 151 ms
  CHECK_EQ(s.g.lone_a, 0);
}

TEST(g7_stuck_boundary_is_exact_for_beam_b_too) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(false, true, 5200);                    // B broken from 100
  CHECK(!s.g.stuck(5100));
  CHECK(s.g.stuck(5101));
  CHECK(!s.g.stuck(100));
  s.hold(false, false, 5300);
  CHECK(!s.g.stuck(999999));
}

TEST(g6_quiet_time_counts_from_the_last_real_release_not_from_noise) {
  Bench s;
  s.cross(1000, 40, 300);                       // A released at 1300, B at 1300 (cross releases both)
  s.hold(false, false, 1349);
  s.hold(true, false, 1359);                    // 10 ms of noise right after: must not move the deadline
  s.hold(false, false, 2799);
  CHECK(!s.g.armed());
  s.hold(false, false, 2800);                   // 1300 + 1500
  CHECK(s.g.armed());
}

TEST(g6_the_later_of_the_two_releases_starts_the_quiet_time) {
  Bench s;
  s.hold(false, false, 99);
  s.hold(true, true, 300);                      // event
  s.hold(true, false, 900);                     // B released at 301, A at 901
  s.hold(false, false, 2400);
  CHECK(!s.g.armed());                          // 901 + 1500 = 2401
  s.hold(false, false, 2401);
  CHECK(s.g.armed());
}
