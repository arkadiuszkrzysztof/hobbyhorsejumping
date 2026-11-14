// Tests for remote.h — requirement ids (R-n) refer to that file.
#include <algorithm>

#include "hhj_test.h"
#include "remote.h"

using hhj::RemoteConfig;
using hhj::RemoteDisplay;
using hhj::Show;
using hhj::ShowMsg;

namespace {

ShowMsg show(Show state, uint32_t rev = 0, uint32_t age_ms = 0, uint32_t ms = 0) {
  ShowMsg m;
  m.state = state;
  m.rev = rev;
  m.age_ms = age_ms;
  m.ms = ms;
  return m;
}

}  // namespace

TEST(nothing_heard_means_not_remote) {
  RemoteDisplay r;
  CHECK(!r.active(0));
  CHECK(!r.active(1000000));
  CHECK(r.state() == Show::Idle);
  CHECK_EQ(r.shown_ms(12345), 0);
  CHECK_EQ(RemoteConfig{}.timeout_ms, 5000);
}

TEST(r1_r2_timeout_boundary_is_exact) {
  RemoteDisplay r;
  r.on_show(show(Show::Idle), 1000);
  CHECK(r.active(1000));
  CHECK(r.active(6000));                        // 5000 ms after the last SHOW: still remote
  CHECK(!r.active(6001));
  r.on_show(show(Show::Idle), 9000);            // it speaks again
  CHECK(r.active(9000));
  CHECK(r.active(14000));
  CHECK(!r.active(14001));
}

TEST(r1_every_show_extends_the_takeover) {
  RemoteDisplay r;
  for (int64_t t = 0; t <= 60000; t += 1000) {
    r.on_show(show(Show::Idle), t);
    CHECK(r.active(t + 999));
  }
  CHECK(r.active(65000));
  CHECK(!r.active(65001));
}

TEST(custom_timeout) {
  RemoteConfig c;
  c.timeout_ms = 1500;
  RemoteDisplay r(c);
  r.on_show(show(Show::Ready), 100);
  CHECK(r.active(1600));
  CHECK(!r.active(1601));
}

TEST(a_show_of_nothing_is_ignored) {
  RemoteDisplay r;
  r.on_show(show(Show::None), 1000);
  CHECK(!r.active(1000));
  r.on_show(show(Show::Run, 1, 0), 2000);
  r.on_show(show(Show::None, 9, 500, 500), 2500);
  CHECK(r.state() == Show::Run);
  CHECK_EQ(r.shown_ms(3000), 1000);             // untouched
  CHECK(!r.active(7001));                       // and it did not count as "heard"
}

TEST(r5_ready_and_idle_carry_no_time) {
  RemoteDisplay r;
  r.on_show(show(Show::Ready, 5, 777, 888), 1000);
  CHECK(r.state() == Show::Ready);
  CHECK_EQ(r.shown_ms(5000), 0);
  r.on_show(show(Show::Idle, 5, 777, 888), 2000);
  CHECK(r.state() == Show::Idle);
  CHECK_EQ(r.shown_ms(5000), 0);
}

TEST(r3_run_clock_counts_from_arrival_minus_age) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 4000), 10000);   // the START was cut 4 s before this arrived
  CHECK(r.state() == Show::Run);
  CHECK_EQ(r.shown_ms(10000), 4000);
  CHECK_EQ(r.shown_ms(10001), 4001);
  CHECK_EQ(r.shown_ms(25000), 19000);
}

TEST(r3_copies_of_the_same_run_keep_the_earliest_start) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 100), 10130);    // 30 ms late on the network: start looks like 10030
  CHECK_EQ(r.shown_ms(11000), 970);
  r.on_show(show(Show::Run, 1, 1100), 11102);   // 2 ms late: start 10002
  CHECK_EQ(r.shown_ms(12000), 1998);
  r.on_show(show(Show::Run, 1, 2100), 12150);   // 50 ms late: worse, ignored
  CHECK_EQ(r.shown_ms(13000), 2998);
  r.on_show(show(Show::Run, 1, 3100), 13102);   // the same estimate again
  CHECK_EQ(r.shown_ms(14000), 3998);
}

TEST(r3_another_rev_starts_afresh_even_if_it_is_later) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 5000), 10000);   // start 5000
  CHECK_EQ(r.shown_ms(10000), 5000);
  r.on_show(show(Show::Run, 2, 1000), 10500);   // the operator picked a later START: 9500
  CHECK_EQ(r.shown_ms(10500), 1000);
  r.on_show(show(Show::Run, 1, 5600), 10600);   // back to the first one: 5000 again, not min(…)
  CHECK_EQ(r.shown_ms(10600), 5600);
}

TEST(r3_run_after_another_state_starts_afresh) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 7, 9000), 10000);   // start 1000
  r.on_show(show(Show::Ready, 7), 10100);
  r.on_show(show(Show::Run, 7, 500), 10200);    // same rev, but the clock had been taken off: start 9700
  CHECK_EQ(r.shown_ms(10200), 500);
}

TEST(run_clock_is_never_negative) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 0), 5000);
  CHECK_EQ(r.shown_ms(4000), 0);                // asked about a moment before the start
  CHECK_EQ(r.shown_ms(5000), 0);
  CHECK_EQ(r.shown_ms(5001), 1);
}

TEST(run_from_before_the_timer_was_switched_on) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 60000), 300);    // timer just booted; the run is a minute old
  CHECK_EQ(r.shown_ms(300), 60000);
  CHECK_EQ(r.shown_ms(1300), 61000);
}

TEST(r4_result_shows_the_given_time_and_dates_its_appearance) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 100), 1000);
  r.on_show(show(Show::Result, 2, 0, 23450), 24400);
  CHECK(r.state() == Show::Result);
  CHECK_EQ(r.shown_ms(24400), 23450);
  CHECK_EQ(r.shown_ms(99999), 23450);           // it does not move
  CHECK_EQ(r.result_since_ms(), 24400);
}

TEST(r4_repeats_of_the_same_result_do_not_restart_the_blinking) {
  RemoteDisplay r;
  r.on_show(show(Show::Result, 2, 0, 23450), 1000);
  for (int64_t t = 2000; t <= 30000; t += 1000) r.on_show(show(Show::Result, 2, 0, 23450), t);
  CHECK_EQ(r.result_since_ms(), 1000);
  CHECK_EQ(r.shown_ms(30000), 23450);
}

TEST(r4_a_changed_result_counts_as_new) {
  RemoteDisplay r;
  r.on_show(show(Show::Result, 2, 0, 23450), 1000);
  r.on_show(show(Show::Result, 2, 0, 21000), 5000);         // same rev, other time
  CHECK_EQ(r.result_since_ms(), 5000);
  CHECK_EQ(r.shown_ms(6000), 21000);
  r.on_show(show(Show::Result, 3, 0, 21000), 8000);         // same time, other rev (another run)
  CHECK_EQ(r.result_since_ms(), 8000);
  r.on_show(show(Show::Result, 3, 0, 21000), 9000);         // repeat
  CHECK_EQ(r.result_since_ms(), 8000);
}

TEST(r4_result_after_another_state_counts_as_new) {
  RemoteDisplay r;
  r.on_show(show(Show::Result, 2, 0, 23450), 1000);
  r.on_show(show(Show::Idle, 2), 2000);
  r.on_show(show(Show::Result, 2, 0, 23450), 3000);         // shown again after something else
  CHECK_EQ(r.result_since_ms(), 3000);
}

TEST(r4_largest_result) {
  RemoteDisplay r;
  r.on_show(show(Show::Result, 1, 0, 4294967295u), 10);
  CHECK_EQ(r.shown_ms(10), 4294967295LL);
}

TEST(r6_after_a_silence_a_result_blinks_again) {
  RemoteDisplay r;
  r.on_show(show(Show::Result, 2, 0, 23450), 1000);
  r.on_show(show(Show::Result, 2, 0, 23450), 6000);         // exactly at the limit: still the same showing
  CHECK_EQ(r.result_since_ms(), 1000);
  r.on_show(show(Show::Result, 2, 0, 23450), 11001);        // 5001 ms of silence
  CHECK_EQ(r.result_since_ms(), 11001);
}

TEST(r6_after_a_silence_a_run_is_not_refined_against_the_old_one) {
  RemoteDisplay r;
  r.on_show(show(Show::Run, 1, 0), 1000);                   // start 1000
  r.on_show(show(Show::Run, 1, 0), 9000);                   // 8 s of silence, same rev: start 9000
  CHECK_EQ(r.shown_ms(9500), 500);
  RemoteDisplay within;
  within.on_show(show(Show::Run, 1, 0), 1000);
  within.on_show(show(Show::Run, 1, 0), 6000);              // no silence: the earlier start stays
  CHECK_EQ(within.shown_ms(6500), 5500);
}

// Copies of one run with random network delay: the clock equals the best copy.
TEST(fuzz_run_clock_follows_the_quickest_copy) {
  hhj_test::Rng rng(12);
  const int runs = hhj_test::iterations(2000, 60);
  for (int run = 0; run < runs; run++) {
    RemoteDisplay r;
    const int64_t start = rng.range(1000, 100000);          // the true START on the timer's clock
    int64_t t = start + rng.range(0, 500);
    int64_t best = INT64_MAX;
    for (int k = 0; k < 20; k++) {
      const int64_t delay = rng.range(0, 80);
      const int64_t sent = t;
      r.on_show(show(Show::Run, 5, static_cast<uint32_t>(sent - start)), sent + delay);
      best = std::min(best, start + delay);
      CHECK_EQ(r.shown_ms(sent + 1000), sent + 1000 - best);
      CHECK(r.shown_ms(sent + 1000) <= sent + 1000 - start);   // never ahead of the truth
      t += rng.range(200, 1500);
    }
  }
}
