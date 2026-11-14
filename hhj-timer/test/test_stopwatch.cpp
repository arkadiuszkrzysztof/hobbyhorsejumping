// Tests for stopwatch.h — requirement ids (S-n) refer to that file.
#include <algorithm>
#include <map>
#include <vector>

#include "hhj_test.h"
#include "stopwatch.h"

using hhj::Role;
using hhj::Stopwatch;
using hhj::StopwatchConfig;
using Outcome = hhj::Stopwatch::Outcome;
using State = hhj::Stopwatch::State;

namespace {

constexpr uint32_t kS = 0x51A27u;      // boot nonce of the START gate
constexpr uint32_t kF = 0xF1215u;      // boot nonce of the FINISH gate

// A copy of START #seq whose beam was cut at `edge`, arriving `delay` ms later.
Outcome start(Stopwatch& sw, uint32_t seq, int64_t edge, int64_t delay = 2, uint32_t boot = kS) {
  return sw.on_event(Role::Start, boot, seq, static_cast<uint32_t>(delay), edge + delay);
}
Outcome finish(Stopwatch& sw, uint32_t seq, int64_t edge, int64_t delay = 2, uint32_t boot = kF) {
  return sw.on_event(Role::Finish, boot, seq, static_cast<uint32_t>(delay), edge + delay);
}

}  // namespace

// -------------------------------------------------------------------- basics
TEST(starts_idle) {
  Stopwatch sw;
  CHECK(sw.state() == State::Idle);
  CHECK_EQ(sw.shown_ms(0), 0);
  CHECK_EQ(sw.shown_ms(123456), 0);
  CHECK(!sw.tick(0));
  CHECK(!sw.tick(100000000));
}

TEST(default_configuration) {
  const StopwatchConfig c;
  CHECK_EQ(c.min_run_ms, 1000);
  CHECK_EQ(c.run_timeout_ms, 600000);
  CHECK_EQ(c.max_age_ms, 600000);
}

TEST(start_then_finish_gives_the_time_between_the_cuts) {
  Stopwatch sw;
  CHECK(start(sw, 1, 10000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
  CHECK_EQ(sw.shown_ms(10002), 2);
  CHECK_EQ(sw.shown_ms(25000), 15000);
  CHECK(finish(sw, 1, 42340) == Outcome::Finished);
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(42342), 32340);
  CHECK_EQ(sw.shown_ms(99999), 32340);          // S-9: the result does not move
  CHECK_EQ(sw.finished_at_ms(), 42342);         // local arrival time of the FINISH
}

TEST(network_delay_is_taken_out_by_the_age) {
  // START reaches us 700 ms late, FINISH 5 ms late: the result is still edge to edge.
  Stopwatch sw;
  CHECK(start(sw, 1, 10000, 700) == Outcome::Started);
  CHECK_EQ(sw.shown_ms(10700), 700);            // the clock starts "in the past"
  CHECK(finish(sw, 1, 30000, 5) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(30005), 20000);
}

TEST(only_start_and_finish_are_events) {
  Stopwatch sw;
  CHECK(sw.on_event(Role::Timer, 1, 1, 0, 100) == Outcome::Invalid);
  CHECK(sw.on_event(Role::Backend, 1, 1, 0, 100) == Outcome::Invalid);
  CHECK(sw.on_event(Role::None, 1, 1, 0, 100) == Outcome::Invalid);
  CHECK(sw.state() == State::Idle);
}

// --------------------------------------------------------- S-1 best estimate
TEST(s1_later_copies_can_only_improve_the_start) {
  Stopwatch sw;
  CHECK(start(sw, 1, 10000, 90) == Outcome::Started);        // estimate: 10000 (edge) — arrival 10090, age 90
  // a copy that was held up: arrival - age is 40 ms late
  CHECK(sw.on_event(Role::Start, kS, 1, 100, 10140) == Outcome::Duplicate);
  CHECK_EQ(sw.shown_ms(11000), 1000);
  // a copy that implies an earlier cut wins
  CHECK(sw.on_event(Role::Start, kS, 1, 120, 10115) == Outcome::Refined);
  CHECK_EQ(sw.shown_ms(11000), 1005);
  // the same estimate again is nothing new
  CHECK(sw.on_event(Role::Start, kS, 1, 125, 10120) == Outcome::Duplicate);
  CHECK_EQ(sw.shown_ms(11000), 1005);
}

TEST(s1_later_copies_can_only_improve_the_finish) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  CHECK(sw.on_event(Role::Finish, kF, 1, 10, 30030) == Outcome::Finished);   // estimate 30020
  CHECK_EQ(sw.shown_ms(31000), 20020);
  CHECK(sw.on_event(Role::Finish, kF, 1, 30, 30055) == Outcome::Duplicate);  // 30025: worse
  CHECK(sw.on_event(Role::Finish, kF, 1, 50, 30052) == Outcome::Refined);    // 30002: better
  CHECK_EQ(sw.shown_ms(31000), 20002);
  CHECK(sw.on_event(Role::Finish, kF, 1, 60, 30062) == Outcome::Duplicate);  // 30002 again
  CHECK_EQ(sw.finished_at_ms(), 30030);         // refining does not restart the blinking
  CHECK(sw.state() == State::Finished);
}

TEST(s1_start_refined_while_showing_a_result_changes_the_result) {
  Stopwatch sw;
  CHECK(start(sw, 1, 10000, 80) == Outcome::Started);        // first copy
  sw.on_event(Role::Start, kS, 1, 80, 10085);                // estimate 10005, worse
  finish(sw, 1, 30000, 0);
  CHECK_EQ(sw.shown_ms(40000), 20000);
  CHECK(sw.on_event(Role::Start, kS, 1, 5000, 14990) == Outcome::Refined);   // estimate 9990
  CHECK_EQ(sw.shown_ms(40000), 20010);
}

// ------------------------------------------------------------ S-2 new START
TEST(s2_newer_start_restarts_a_running_clock) {
  Stopwatch sw;
  CHECK(start(sw, 1, 10000) == Outcome::Started);
  CHECK(start(sw, 2, 15000) == Outcome::Restarted);
  CHECK(sw.state() == State::Running);
  CHECK_EQ(sw.shown_ms(16000), 1000);
  CHECK(finish(sw, 1, 20000) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(20002), 5000);
}

TEST(s2_s9_start_after_a_result_begins_a_new_run) {
  Stopwatch sw;
  start(sw, 1, 10000);
  finish(sw, 1, 20000);
  CHECK(sw.state() == State::Finished);
  CHECK(start(sw, 2, 50000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
  CHECK_EQ(sw.shown_ms(50500), 500);
  // late copies of the old FINISH must not end the new run
  CHECK(finish(sw, 1, 20000, 31000) == Outcome::Duplicate);
  CHECK(sw.state() == State::Running);
  CHECK(finish(sw, 2, 62500) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(70000), 12500);
}

// ------------------------------------------------------- S-3 choosing FINISH
TEST(s3_min_run_boundary_is_exact) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  CHECK(finish(sw, 1, 10999, 0) == Outcome::Buffered);       // 999 ms: not a finish
  CHECK(sw.state() == State::Running);
  CHECK(finish(sw, 2, 11000, 0) == Outcome::Finished);       // 1000 ms: counts
  CHECK_EQ(sw.shown_ms(20000), 1000);
}

TEST(s3_finish_before_the_start_edge_does_not_end_the_run) {
  Stopwatch sw;
  start(sw, 1, 10000);
  CHECK(finish(sw, 1, 9000) == Outcome::Buffered);           // someone walked through the finish earlier
  CHECK(finish(sw, 2, 10000) == Outcome::Buffered);
  CHECK(sw.state() == State::Running);
}

TEST(s3_first_finish_wins_later_ones_change_nothing) {
  Stopwatch sw;
  start(sw, 1, 10000);
  CHECK(finish(sw, 1, 30000) == Outcome::Finished);
  CHECK(finish(sw, 2, 35000) == Outcome::Buffered);
  CHECK(finish(sw, 3, 40000) == Outcome::Buffered);
  CHECK_EQ(sw.shown_ms(50000), 20000);
  CHECK_EQ(sw.finished_at_ms(), 30002);
}

TEST(s3_an_earlier_finish_arriving_late_replaces_the_result) {
  // FINISH #1 (at 30 s) was stuck in the air; #2 (at 35 s) got here first.
  Stopwatch sw;
  start(sw, 1, 10000);
  CHECK(finish(sw, 2, 35000) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(40000), 25000);
  CHECK(finish(sw, 1, 30000, 6000) == Outcome::Finished);    // arrives at 36000
  CHECK_EQ(sw.shown_ms(40000), 20000);
  CHECK_EQ(sw.finished_at_ms(), 36000);                      // the corrected result blinks again
}

TEST(s3_a_refined_copy_can_make_a_finish_the_earliest) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  CHECK(sw.on_event(Role::Finish, kF, 1, 0, 30400) == Outcome::Finished);    // #1 estimated 30400
  CHECK(sw.on_event(Role::Finish, kF, 2, 0, 30500) == Outcome::Buffered);    // #2 estimated 30500
  CHECK(sw.on_event(Role::Finish, kF, 2, 300, 30600) == Outcome::Finished);  // #2 really 30300: now the earliest
  CHECK_EQ(sw.shown_ms(40000), 20300);
}

TEST(s3_refining_the_start_can_validate_a_buffered_finish) {
  Stopwatch sw;
  CHECK(sw.on_event(Role::Start, kS, 1, 0, 10010) == Outcome::Started);      // estimate 10010
  CHECK(sw.on_event(Role::Finish, kF, 1, 0, 11005) == Outcome::Buffered);    // 995 ms after: too early
  CHECK(sw.state() == State::Running);
  CHECK(sw.on_event(Role::Start, kS, 1, 1020, 11020) == Outcome::Refined);   // START really at 10000
  CHECK(sw.state() == State::Finished);                                      // 1005 ms: a finish after all
  CHECK_EQ(sw.shown_ms(20000), 1005);
  CHECK_EQ(sw.finished_at_ms(), 11020);
}

// --------------------------------------------------- S-4 FINISH before START
TEST(s4_finish_known_before_its_start) {
  // The START gate's WiFi was down for 20 s; the rider finished meanwhile.
  Stopwatch sw;
  CHECK(finish(sw, 1, 30000) == Outcome::Buffered);
  CHECK(sw.state() == State::Idle);
  CHECK(start(sw, 1, 12000, 20000) == Outcome::Started);     // arrives at 32000 with age 20 s
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(32000), 18000);
  CHECK_EQ(sw.finished_at_ms(), 32000);
}

TEST(s4_only_a_finish_after_the_start_is_adopted) {
  Stopwatch sw;
  finish(sw, 1, 5000);                                       // before the start
  finish(sw, 2, 10500);                                      // 500 ms after the start: too early
  CHECK(start(sw, 1, 10000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
  finish(sw, 3, 40000);
  CHECK_EQ(sw.shown_ms(50000), 30000);
}

TEST(s4_the_earliest_remembered_finish_is_adopted) {
  Stopwatch sw;
  finish(sw, 2, 35000);
  finish(sw, 1, 30000, 6000);
  finish(sw, 3, 38000);
  start(sw, 1, 10000, 30000);
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(50000), 20000);
}

// ------------------------------------------------------------- S-5 staleness
TEST(s5_stale_boundary_is_exact) {
  Stopwatch sw;
  CHECK(sw.on_event(Role::Start, kS, 1, 600001, 700000) == Outcome::Stale);
  CHECK(sw.state() == State::Idle);
  CHECK(sw.on_event(Role::Finish, kF, 1, 600001, 700000) == Outcome::Stale);
  CHECK(sw.on_event(Role::Start, kS, 1, 4294967295u, 700000) == Outcome::Stale);
  CHECK(sw.on_event(Role::Start, kS, 1, 600000, 700000) == Outcome::Started);   // exactly the limit: accepted
  CHECK_EQ(sw.shown_ms(700000), 600000);
}

TEST(s5_custom_stale_limit) {
  StopwatchConfig c;
  c.max_age_ms = 5000;
  Stopwatch sw(c);
  CHECK(sw.on_event(Role::Start, kS, 1, 5001, 100000) == Outcome::Stale);
  CHECK(sw.on_event(Role::Start, kS, 1, 5000, 100000) == Outcome::Started);
}

// -------------------------------------------------------------- S-6 old START
TEST(s6_older_start_from_the_same_gate_is_ignored) {
  Stopwatch sw;
  CHECK(start(sw, 5, 50000) == Outcome::Started);
  CHECK(start(sw, 4, 40000, 10500) == Outcome::Old);         // a retry of the previous crossing
  CHECK(start(sw, 1, 10000, 40500) == Outcome::Old);
  CHECK_EQ(sw.shown_ms(51000), 1000);
  finish(sw, 1, 60000);
  CHECK(start(sw, 4, 40000, 20500) == Outcome::Old);         // also while a result is shown
  CHECK(sw.state() == State::Finished);
}

TEST(s6_copies_of_the_current_start_never_restart_anything) {
  Stopwatch sw;
  start(sw, 1, 10000);
  finish(sw, 1, 30000);
  for (int i = 0; i < 300; i++) {                            // five minutes of once-a-second retries
    CHECK(start(sw, 1, 10000, 20000 + i * 1000) == Outcome::Duplicate);
    CHECK(sw.state() == State::Finished);
  }
  CHECK_EQ(sw.shown_ms(400000), 20000);
}

TEST(s6_start_from_a_rebooted_gate) {
  Stopwatch sw;
  start(sw, 7, 10000);
  // the gate rebooted: new nonce, numbering starts again at 1
  CHECK(sw.on_event(Role::Start, 0xAAAA, 1, 2, 20002) == Outcome::Restarted);
  CHECK_EQ(sw.shown_ms(21000), 1000);
  // a leftover from before the reboot: other nonce, older cut
  CHECK(start(sw, 7, 10000, 10500) == Outcome::Old);
  CHECK(start(sw, 8, 15000, 5600) == Outcome::Old);
  CHECK_EQ(sw.shown_ms(21000), 1000);
  // and the rebooted gate's next crossing
  CHECK(sw.on_event(Role::Start, 0xAAAA, 2, 2, 30002) == Outcome::Restarted);
}

TEST(s6_two_starts_in_the_same_millisecond_from_different_gates) {
  Stopwatch sw;
  CHECK(sw.on_event(Role::Start, 1, 1, 0, 5000) == Outcome::Started);
  CHECK(sw.on_event(Role::Start, 2, 1, 0, 5000) == Outcome::Old);            // not newer
  CHECK(sw.on_event(Role::Start, 2, 1, 0, 5001) == Outcome::Restarted);      // one ms newer
}

// ---------------------------------------------------------------- S-7 timeout
TEST(s7_run_timeout_boundary_is_exact) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  CHECK(!sw.tick(610000));                                   // exactly 600 s: still running
  CHECK(sw.state() == State::Running);
  CHECK(sw.tick(610001));                                    // reported once
  CHECK(sw.state() == State::Idle);
  CHECK(!sw.tick(610002));
  CHECK_EQ(sw.shown_ms(610002), 0);
}

TEST(s7_a_result_never_times_out) {
  Stopwatch sw;
  start(sw, 1, 10000);
  finish(sw, 1, 30000);
  CHECK(!sw.tick(100000000));
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(100000000), 20000);
}

TEST(s7_after_a_timeout_old_copies_do_nothing_and_a_new_start_works) {
  Stopwatch sw;
  start(sw, 1, 10000);
  CHECK(sw.tick(700000));
  CHECK(start(sw, 1, 10000, 500000) == Outcome::Duplicate);
  CHECK(sw.state() == State::Idle);
  CHECK(finish(sw, 1, 650000) == Outcome::Buffered);         // a very late finish: no run to end
  CHECK(sw.state() == State::Idle);
  CHECK(start(sw, 2, 800000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
}

TEST(s7_custom_timeout) {
  StopwatchConfig c;
  c.run_timeout_ms = 2000;
  Stopwatch sw(c);
  start(sw, 1, 1000, 0);
  CHECK(!sw.tick(3000));
  CHECK(sw.tick(3001));
}

// ------------------------------------------------------------------ S-8 reset
TEST(s8_reset_returns_to_idle) {
  Stopwatch sw;
  start(sw, 1, 10000);
  sw.reset(15000);
  CHECK(sw.state() == State::Idle);
  CHECK_EQ(sw.shown_ms(16000), 0);
  finish(sw, 1, 20000);
  CHECK(sw.state() == State::Idle);
  Stopwatch s2;
  start(s2, 1, 10000);
  finish(s2, 1, 20000);
  s2.reset(25000);
  CHECK(s2.state() == State::Idle);
}

TEST(s8_nothing_from_before_the_reset_can_start_or_end_a_run) {
  Stopwatch sw;
  start(sw, 1, 10000);
  finish(sw, 1, 20000);
  sw.reset(25000);
  CHECK(start(sw, 1, 10000, 16000) == Outcome::Duplicate);   // copy of the START that was showing
  CHECK(start(sw, 2, 24999, 5000) == Outcome::Old);          // a crossing just before the reset, arriving late
  CHECK(sw.on_event(Role::Start, 0xBBBB, 1, 5000, 29000) == Outcome::Old);   // other gate, cut at 24000
  CHECK(sw.state() == State::Idle);
  CHECK(start(sw, 3, 25000, 100) == Outcome::Started);       // cut exactly at the reset instant: counts
  CHECK(finish(sw, 1, 20000, 9000) == Outcome::Buffered);    // the old FINISH cannot end the new run
  CHECK(sw.state() == State::Running);
  CHECK(finish(sw, 2, 40000) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(50000), 15000);
}

TEST(s8_reset_forgets_remembered_finishes) {
  Stopwatch sw;
  finish(sw, 1, 30000);
  sw.reset(30500);
  CHECK(start(sw, 1, 31000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
  // had the FINISH at 30000 been kept it could not apply anyway; one after the start does
  CHECK(finish(sw, 2, 33000) == Outcome::Finished);
}

TEST(s8_reset_in_the_past_does_not_lower_the_floor) {
  Stopwatch sw;
  start(sw, 5, 50000);
  sw.reset(20000);                                           // clock oddity: an earlier "now"
  CHECK(sw.on_event(Role::Start, 0xCCCC, 1, 0, 30000) == Outcome::Old);      // still older than START #5
  CHECK(sw.on_event(Role::Start, 0xCCCC, 1, 0, 50001) == Outcome::Started);
}

// -------------------------------------------------------- operator commands
TEST(manual_start_and_finish) {
  Stopwatch sw;
  CHECK(sw.manual_start(1000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
  CHECK_EQ(sw.shown_ms(1500), 500);
  CHECK(sw.manual_finish(1999) == Outcome::Buffered);        // the 1 s rule applies to the console too
  CHECK(sw.manual_finish(3500) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(9000), 2500);
  CHECK(sw.manual_start(9000) == Outcome::Started);
  CHECK(sw.manual_start(9500) == Outcome::Restarted);
}

TEST(manual_start_is_not_touched_by_copies_of_an_earlier_gate_start) {
  Stopwatch sw;
  start(sw, 1, 10000);
  sw.reset(11000);
  CHECK(sw.manual_start(12000) == Outcome::Started);
  // a retry of gate START #1 implying an earlier cut must not be taken as a refinement
  CHECK(start(sw, 1, 9990, 3000) == Outcome::Duplicate);
  CHECK_EQ(sw.shown_ms(13000), 1000);
  CHECK(start(sw, 2, 20000) == Outcome::Restarted);          // a real new crossing still restarts
}

TEST(manual_start_right_after_reset_in_the_same_millisecond) {
  Stopwatch sw;
  start(sw, 1, 10000);
  sw.reset(15000);
  CHECK(sw.manual_start(15000) == Outcome::Started);
  CHECK(sw.state() == State::Running);
}

TEST(manual_finish_ends_a_gate_started_run) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  CHECK(sw.manual_finish(14000) == Outcome::Finished);
  CHECK_EQ(sw.shown_ms(20000), 4000);
  CHECK(sw.manual_finish(15000) == Outcome::Buffered);       // a second press is a new, later finish
  CHECK_EQ(sw.shown_ms(20000), 4000);
}

TEST(shown_time_is_never_negative) {
  Stopwatch sw;
  sw.manual_start(5000);
  CHECK_EQ(sw.shown_ms(4000), 0);                            // asked about a moment before the start
  CHECK_EQ(sw.shown_ms(5000), 0);
  CHECK_EQ(sw.shown_ms(5001), 1);
}

// ------------------------------------------------------- remembering FINISHes
TEST(many_finishes_while_running_keep_the_earliest_as_result) {
  Stopwatch sw;
  start(sw, 1, 10000);
  for (uint32_t i = 1; i <= 20; i++) finish(sw, i, 20000 + static_cast<int64_t>(i) * 2000);
  CHECK_EQ(sw.shown_ms(100000), 12000);                      // FINISH #1 at 22000
  // and its late copies are still recognised (it was never dropped)
  CHECK(finish(sw, 1, 22000, 50000) == Outcome::Duplicate);
  CHECK(sw.on_event(Role::Finish, kF, 1, 60000, 81990) == Outcome::Refined);   // 21990
  CHECK_EQ(sw.shown_ms(100000), 11990);
}

TEST(only_the_newest_finishes_are_remembered_while_idle) {
  // Six people walk through the finish; then a START that only the last two could end.
  Stopwatch sw;
  for (uint32_t i = 1; i <= 6; i++) finish(sw, i, static_cast<int64_t>(i) * 10000);
  CHECK(start(sw, 1, 45000, 20000) == Outcome::Started);
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(70000), 5000);                        // FINISH #5 at 50000
}

TEST(a_finish_older_than_everything_remembered_is_dropped) {
  Stopwatch sw;
  for (uint32_t i = 2; i <= 5; i++) finish(sw, i, static_cast<int64_t>(i) * 10000);   // 20 s .. 50 s
  CHECK(finish(sw, 1, 10000, 45000) == Outcome::Old);
  CHECK(finish(sw, 6, 20000, 45000) == Outcome::Old);        // same age as the oldest kept: not newer
  CHECK(finish(sw, 7, 20001, 45000) == Outcome::Buffered);   // newer than the oldest: replaces it
  CHECK(start(sw, 1, 15000, 60000) == Outcome::Started);
  CHECK_EQ(sw.shown_ms(80000), 5001);                        // 20001 survived, 20000 did not
}

TEST(the_finish_that_ends_the_run_is_kept_even_when_it_is_the_oldest) {
  // Memory is full of later FINISHes when the true first one finally arrives.
  Stopwatch sw;
  start(sw, 1, 10000);
  for (uint32_t i = 2; i <= 5; i++) finish(sw, i, static_cast<int64_t>(i) * 10000);   // result so far: 20 s - 10 s
  CHECK_EQ(sw.shown_ms(60000), 10000);
  CHECK(finish(sw, 1, 15000, 50000) == Outcome::Finished);   // the earliest of all
  CHECK_EQ(sw.shown_ms(70000), 5000);
  CHECK(finish(sw, 9, 12000, 60000) == Outcome::Finished);   // and an even earlier one
  CHECK_EQ(sw.shown_ms(80000), 2000);
  CHECK(finish(sw, 8, 10500, 60000) == Outcome::Old);        // too early to count and older than all: dropped
  CHECK_EQ(sw.shown_ms(80000), 2000);
}

// --------------------------------------------------------------------- fuzz
namespace {

struct Copy {
  int64_t arrival;
  Role mark;
  uint32_t seq;
  uint32_t age;
};

}  // namespace

// A whole afternoon of runs with every datagram copied, delayed and shuffled:
// the display must end up showing exactly what the true cut times say.
TEST(fuzz_result_depends_on_events_not_on_delivery) {
  hhj_test::Rng rng(31337);
  const int runs = hhj_test::iterations(4000, 80);
  const int64_t kJitter = 250;                   // per-copy network delay, 0..250 ms
  const int failures_before = hhj_test::failures();
  int finished = 0, running = 0;
  for (int run = 0; run < runs; run++) {
    std::vector<Copy> copies;
    std::map<uint32_t, int64_t> best_start, best_finish;       // seq -> best (smallest) estimate
    int64_t t = 5000;
    uint32_t fseq = 0;
    const int n_starts = 1 + static_cast<int>(rng.below(4));
    int64_t last_start = 0;
    std::vector<uint32_t> finishes_after_last;
    for (int s = 1; s <= n_starts; s++) {
      const int64_t start_edge = t;
      last_start = start_edge;
      finishes_after_last.clear();
      auto emit = [&](Role mark, uint32_t seq, int64_t edge, std::map<uint32_t, int64_t>& best) {
        const int n = 1 + static_cast<int>(rng.below(7));
        for (int c = 0; c < n; c++) {
          const int64_t sent_after = rng.chance(0.2) ? rng.range(0, 8000) : rng.range(0, 400);   // some copies very late
          const int64_t delay = rng.range(0, kJitter);
          copies.push_back(Copy{edge + sent_after + delay, mark, seq, static_cast<uint32_t>(sent_after)});
          const int64_t estimate = edge + delay;
          if (!best.count(seq) || estimate < best[seq]) best[seq] = estimate;
        }
      };
      emit(Role::Start, static_cast<uint32_t>(s), start_edge, best_start);
      int64_t cursor = start_edge;
      if (rng.chance(0.3)) {                     // someone crosses the finish too early: clearly not a finish
        const int64_t early = start_edge + rng.range(-3000, 1000 - kJitter - 10);
        emit(Role::Finish, ++fseq, early, best_finish);
      }
      const int n_fin = static_cast<int>(rng.below(3));
      cursor = start_edge + 1000 + kJitter + 10;
      for (int f = 0; f < n_fin; f++) {          // real finishes, clearly late enough and 1.6 s apart
        cursor += rng.range(0, 20000);
        emit(Role::Finish, ++fseq, cursor, best_finish);
        finishes_after_last.push_back(fseq);
        cursor += 1600;
      }
      t = cursor + rng.range(4000, 20000);       // next rider (the gate needs 1.5 s of quiet anyway)
    }
    // deliver in arrival order (ties in generation order)
    std::stable_sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) { return a.arrival < b.arrival; });
    Stopwatch sw;
    int64_t now = 0;
    for (const Copy& c : copies) {
      now = c.arrival;
      sw.tick(now);
      sw.on_event(c.mark, c.mark == Role::Start ? kS : kF, c.seq, c.age, c.arrival);
    }
    const int64_t s_best = best_start[static_cast<uint32_t>(n_starts)];
    CHECK(s_best >= last_start && s_best <= last_start + kJitter);
    if (finishes_after_last.empty()) {
      running++;
      CHECK(sw.state() == State::Running);
      CHECK_EQ(sw.shown_ms(now + 1000), now + 1000 - s_best);
    } else {
      finished++;
      CHECK(sw.state() == State::Finished);
      CHECK_EQ(sw.shown_ms(now), best_finish[finishes_after_last[0]] - s_best);
    }
    if (hhj_test::failures() > failures_before) {
      printf("   (run %d, %d starts, %zu copies)\n", run, n_starts, copies.size());
      return;
    }
  }
  CHECK(finished > runs / 4);
  CHECK(running > runs / 10);
}

// Random events, resets and operator keys: the stopwatch must stay coherent.
TEST(fuzz_state_stays_coherent) {
  hhj_test::Rng rng(8);
  const int runs = hhj_test::iterations(500, 20);
  for (int run = 0; run < runs; run++) {
    Stopwatch sw;
    int64_t now = 1000;
    for (int step = 0; step < 2000; step++) {
      now += rng.range(0, 3000);
      const State before = sw.state();
      const int64_t shown_before = sw.shown_ms(now);
      Outcome o = Outcome::Invalid;
      switch (rng.below(8)) {
        case 0: sw.reset(now); break;
        case 1: o = sw.manual_start(now); break;
        case 2: o = sw.manual_finish(now); break;
        case 3: sw.tick(now); break;
        default: {
          const Role mark = rng.chance(0.5) ? Role::Start : Role::Finish;
          const uint32_t age = rng.chance(0.05) ? 700000u : static_cast<uint32_t>(rng.range(0, 5000));
          o = sw.on_event(mark, rng.chance(0.9) ? 1u : 2u, 1 + rng.below(40), age, now);
          break;
        }
      }
      const State after = sw.state();
      const int64_t shown = sw.shown_ms(now);
      CHECK(shown >= 0);
      if (after == State::Idle) CHECK_EQ(shown, 0);
      if (after == State::Finished) {
        CHECK(shown >= 1000);                                    // S-3: never a result under min_run
        CHECK_EQ(sw.shown_ms(now + 50000), shown);               // S-9
        CHECK(sw.finished_at_ms() <= now);
      }
      if (after == State::Running) CHECK_EQ(sw.shown_ms(now + 777), shown + 777);
      // outcomes that promise "nothing happened" really change nothing
      if (o == Outcome::Duplicate || o == Outcome::Old || o == Outcome::Stale || o == Outcome::Buffered) {
        CHECK(after == before);
        CHECK_EQ(shown, shown_before);
      }
      if (o == Outcome::Started || o == Outcome::Restarted) CHECK(after != State::Idle);
      if (o == Outcome::Restarted) CHECK(before == State::Running);
      if (o == Outcome::Finished) CHECK(after == State::Finished);
    }
  }
}

// ------------------------------------------------- found by mutation testing
TEST(a_late_start_that_adopts_the_same_finish_shows_a_fresh_result) {
  // Result shown for START #1. Then START #2 turns up late (cut before the FINISH):
  // the same FINISH now ends a different run, and that is a new result to announce.
  Stopwatch sw;
  start(sw, 1, 10000);
  finish(sw, 1, 30000);
  CHECK_EQ(sw.finished_at_ms(), 30002);
  CHECK(start(sw, 2, 20000, 15000) == Outcome::Started);     // arrives at 35000
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(40000), 10000);
  CHECK_EQ(sw.finished_at_ms(), 35000);
}

TEST(events_from_before_the_timer_was_switched_on) {
  // The timer rebooted mid-run: its clock is near zero, the cuts lie "before" it.
  Stopwatch sw;
  CHECK(sw.on_event(Role::Finish, kF, 1, 2000, 500) == Outcome::Buffered);   // cut at -1500
  CHECK(sw.on_event(Role::Start, kS, 1, 13000, 510) == Outcome::Started);    // cut at -12490
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(600), 10990);
  // and a run that is still on
  Stopwatch running;
  CHECK(running.on_event(Role::Start, kS, 1, 8000, 300) == Outcome::Started);
  CHECK_EQ(running.shown_ms(300), 8000);
  CHECK(running.on_event(Role::Finish, kF, 1, 9000, 400) == Outcome::Buffered);   // before that start
  CHECK(running.state() == State::Running);
}

TEST(the_four_newest_finishes_are_the_ones_remembered) {
  // Seven people walk through the finish while idle; a late START needs the 4th.
  Stopwatch sw;
  for (uint32_t i = 1; i <= 7; i++) finish(sw, i, static_cast<int64_t>(i) * 10000);
  CHECK(start(sw, 1, 35000, 40000) == Outcome::Started);
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(80000), 5000);                        // FINISH #4 at 40000 was still remembered
  Stopwatch older;
  for (uint32_t i = 1; i <= 7; i++) finish(older, i, static_cast<int64_t>(i) * 10000);
  CHECK(start(older, 1, 25000, 50000) == Outcome::Started);
  CHECK_EQ(older.shown_ms(80000), 15000);                    // FINISH #3 at 30000 was not: #4 ends the run
}

TEST(a_late_finish_exactly_min_run_after_the_start_still_ends_the_run_when_memory_is_full) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  for (uint32_t i = 2; i <= 5; i++) finish(sw, i, static_cast<int64_t>(i) * 10000, 0);   // chosen: 20000
  CHECK(finish(sw, 1, 11000, 60000) == Outcome::Finished);   // exactly 1000 ms after the start
  CHECK_EQ(sw.shown_ms(80000), 1000);
  Stopwatch too_early;
  start(too_early, 1, 10000, 0);
  for (uint32_t i = 2; i <= 5; i++) finish(too_early, i, static_cast<int64_t>(i) * 10000, 0);
  CHECK(finish(too_early, 1, 10999, 60000) == Outcome::Old);  // 999 ms: not a finish, and older than all kept
  CHECK_EQ(too_early.shown_ms(80000), 10000);
}

TEST(a_valid_finish_later_than_the_result_and_older_than_everything_kept_is_dropped) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  finish(sw, 1, 20000, 0);                                   // the result
  for (uint32_t i = 3; i <= 5; i++) finish(sw, i, static_cast<int64_t>(i) * 10000, 0);   // 30 s, 40 s, 50 s
  CHECK(finish(sw, 9, 25000, 40000) == Outcome::Old);        // would end nothing; older than 30 s
  CHECK(finish(sw, 8, 35000, 40000) == Outcome::Buffered);   // newer than the oldest kept: replaces it
  CHECK_EQ(sw.shown_ms(90000), 10000);
  // what is remembered now: 20 s (result), 35 s, 40 s, 50 s - a late START shows it
  CHECK(start(sw, 2, 26000, 70000) == Outcome::Started);
  CHECK_EQ(sw.shown_ms(99000), 9000);                        // 35 s - 26 s; the 30 s one was replaced
}

TEST(reset_while_showing_a_result_forgets_which_finish_was_chosen) {
  Stopwatch sw;
  start(sw, 1, 10000);
  finish(sw, 1, 20000);
  sw.reset(25000);
  // memory is empty again: four new finishes all fit, and the oldest of them can still end a run
  for (uint32_t i = 2; i <= 5; i++) CHECK(finish(sw, i, static_cast<int64_t>(i) * 10000 + 10000) == Outcome::Buffered);
  CHECK(start(sw, 2, 25500, 40000) == Outcome::Started);
  CHECK_EQ(sw.shown_ms(90000), 4500);                        // FINISH #2 at 30000
}

TEST(all_four_places_for_finishes_are_used_also_before_time_zero) {
  // Four FINISHes with negative local times (timer just switched on); a START needs the last one stored.
  Stopwatch sw;
  CHECK(sw.on_event(Role::Finish, kF, 1, 9000, 100) == Outcome::Buffered);    // -8900
  CHECK(sw.on_event(Role::Finish, kF, 2, 7000, 100) == Outcome::Buffered);    // -6900
  CHECK(sw.on_event(Role::Finish, kF, 3, 5000, 100) == Outcome::Buffered);    // -4900
  CHECK(sw.on_event(Role::Finish, kF, 4, 3000, 100) == Outcome::Buffered);    // -2900
  CHECK(sw.on_event(Role::Start, kS, 1, 4500, 200) == Outcome::Started);      // -4300
  CHECK(sw.state() == State::Finished);
  CHECK_EQ(sw.shown_ms(300), 1400);                                           // -2900 - (-4300)
  // ...and the oldest of the four was kept as well
  Stopwatch all;
  all.on_event(Role::Finish, kF, 1, 9000, 100);
  all.on_event(Role::Finish, kF, 2, 7000, 100);
  all.on_event(Role::Finish, kF, 3, 5000, 100);
  all.on_event(Role::Finish, kF, 4, 3000, 100);
  CHECK(all.on_event(Role::Start, kS, 1, 10200, 200) == Outcome::Started);    // -10000
  CHECK_EQ(all.shown_ms(300), 1100);                                          // -8900 - (-10000)
}

TEST(a_second_finish_in_the_same_millisecond_as_the_result_is_dropped_when_memory_is_full) {
  Stopwatch sw;
  start(sw, 1, 10000, 0);
  finish(sw, 1, 20000, 0);                                   // the result
  for (uint32_t i = 3; i <= 5; i++) finish(sw, i, static_cast<int64_t>(i) * 10000, 0);   // 30 s, 40 s, 50 s
  CHECK(sw.on_event(Role::Finish, 0xABCu, 1, 0, 20000) == Outcome::Old);   // other gate, same instant
  CHECK_EQ(sw.shown_ms(90000), 10000);
  CHECK(start(sw, 2, 21000, 70000) == Outcome::Started);     // the 30 s FINISH is still remembered
  CHECK_EQ(sw.shown_ms(99000), 9000);
}
