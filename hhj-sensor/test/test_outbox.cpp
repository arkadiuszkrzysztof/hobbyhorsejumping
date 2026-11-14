// Tests for outbox.h — requirement ids (O-n) refer to that file.
#include <map>
#include <string>
#include <vector>

#include "hhj_test.h"
#include "outbox.h"

using hhj::GateEvent;
using hhj::Outbox;
using hhj::OutboxConfig;

namespace {

GateEvent ev(int64_t t, int8_t dir = 1, uint16_t gap = 40) {
  GateEvent e;
  e.t_ms = t;
  e.dir = dir;
  e.gap_ms = gap;
  return e;
}

// Poll once per millisecond from `from` through `to`; record "seq@time".
struct Sent {
  int64_t at;
  Outbox::Tx tx;
};
std::vector<Sent> drain(Outbox& o, int64_t from, int64_t to) {
  std::vector<Sent> out;
  for (int64_t t = from; t <= to; t++) {
    Outbox::Tx tx;
    while (o.poll(t, &tx)) out.push_back(Sent{t, tx});
  }
  return out;
}

std::string times(const std::vector<Sent>& s, uint32_t seq) {
  std::string out;
  for (const Sent& x : s) {
    if (x.tx.seq == seq) out += std::to_string(x.at) + " ";
  }
  return out;
}

}  // namespace

TEST(empty_outbox_has_nothing_to_send) {
  Outbox o;
  Outbox::Tx tx;
  CHECK(!o.poll(0, &tx));
  CHECK(!o.poll(1000000, &tx));
  CHECK_EQ(o.size(), 0);
  CHECK_EQ(o.pending(), 0);
  CHECK_EQ(o.last_seq(), 0);
  CHECK(!o.ack(1));
  CHECK_EQ(o.sent + o.acked + o.expired + o.evicted, 0);
}

TEST(protocol_schedule_constants) {
  CHECK_EQ(hhj::kBurstCopies, 7);
  const int gaps[] = {10, 10, 20, 40, 80, 160};
  for (int i = 0; i < 6; i++) CHECK_EQ(hhj::kBurstGapMs[i], gaps[i]);
  const OutboxConfig c;
  CHECK_EQ(c.slow_ms, 1000);
  CHECK_EQ(c.ttl_ms, 300000);
  CHECK_EQ(Outbox::kCap, 8);
}

TEST(works_with_any_time_origin) {
  // Nothing in the outbox assumes that time starts at zero.
  Outbox o;
  o.push(ev(-5000), -4980);
  Outbox::Tx tx;
  CHECK(o.poll(-4980, &tx));                   // the first copy is due the moment the event is queued
  CHECK_EQ(tx.age_ms, 20);
  CHECK(!o.poll(-4971, &tx));
  CHECK(o.poll(-4970, &tx));
  CHECK_EQ(tx.n, 1);
}

TEST(o1_sequence_numbers_count_up_from_one) {
  Outbox o;
  CHECK_EQ(o.push(ev(10), 10), 1);
  CHECK_EQ(o.push(ev(20), 20), 2);
  CHECK_EQ(o.push(ev(30), 30), 3);
  CHECK_EQ(o.last_seq(), 3);
  CHECK_EQ(o.size(), 3);
  CHECK_EQ(o.pending(), 3);
}

TEST(o2_o3_burst_then_slow_retries) {
  Outbox o;
  o.push(ev(1000), 1020);                      // detected 20 ms after the cut
  const std::vector<Sent> s = drain(o, 1020, 4500);
  // burst: 0,10,20,40,80,160,320 after the first copy; then one per second
  CHECK_STR(times(s, 1), "1020 1030 1040 1060 1100 1180 1340 2340 3340 4340 ");
  for (size_t i = 0; i < s.size(); i++) {
    CHECK_EQ(s[i].tx.n, i);                                        // copy numbers 0,1,2,...
    CHECK_EQ(s[i].tx.age_ms, s[i].at - 1000);                      // O-8
    CHECK_EQ(s[i].tx.t_ms, 1000);
    CHECK_EQ(s[i].tx.dir, 1);
    CHECK_EQ(s[i].tx.gap_ms, 40);
  }
  CHECK_EQ(o.sent, 10);
  CHECK_EQ(o.pending(), 1);
}

TEST(event_fields_are_carried_unchanged) {
  Outbox o;
  o.push(ev(5, -1, 149), 30);
  Outbox::Tx tx;
  CHECK(o.poll(30, &tx));
  CHECK_EQ(tx.seq, 1);
  CHECK_EQ(tx.t_ms, 5);
  CHECK_EQ(tx.dir, -1);
  CHECK_EQ(tx.gap_ms, 149);
  CHECK_EQ(tx.n, 0);
  CHECK_EQ(tx.age_ms, 25);
}

TEST(one_copy_per_poll_and_nothing_until_the_next_is_due) {
  Outbox o;
  o.push(ev(0), 0);
  Outbox::Tx tx;
  CHECK(o.poll(0, &tx));
  CHECK(!o.poll(0, &tx));                      // the next copy is due at 10
  CHECK(!o.poll(9, &tx));
  CHECK(o.poll(10, &tx));
  CHECK_EQ(tx.n, 1);
  CHECK(!o.poll(19, &tx));
  CHECK(o.poll(20, &tx));
}

TEST(a_late_poll_does_not_compress_the_burst) {
  // The loop was busy for 100 ms: copies are spaced from when they were really sent.
  Outbox o;
  o.push(ev(0), 0);
  Outbox::Tx tx;
  CHECK(o.poll(0, &tx));
  CHECK(o.poll(100, &tx));                     // due at 10, sent at 100
  CHECK(!o.poll(109, &tx));                    // next gap (10 ms) counts from 100
  CHECK(o.poll(110, &tx));
  CHECK_EQ(tx.n, 2);
  CHECK_EQ(tx.age_ms, 110);
}

TEST(o3_ack_after_the_burst_ends_everything) {
  Outbox o;
  o.push(ev(0), 0);
  drain(o, 0, 1000);                           // burst done (7 copies), first slow copy due at 1320
  CHECK_EQ(o.sent, 7);
  CHECK(o.ack(1));
  CHECK_EQ(o.size(), 0);
  CHECK_EQ(o.pending(), 0);
  CHECK_EQ(drain(o, 1001, 20000).size(), 0);
  CHECK_EQ(o.acked, 1);
  CHECK_EQ(o.expired, 0);
}

TEST(o4_ack_during_the_burst_does_not_shorten_it) {
  Outbox o;
  o.push(ev(0), 0);
  drain(o, 0, 5);                              // one copy out
  CHECK(o.ack(1));                             // backend was quick
  CHECK_EQ(o.pending(), 0);                    // nothing is waiting for a backend any more...
  CHECK_EQ(o.size(), 1);                       // ...but the event is still being repeated for the timer
  const std::vector<Sent> s = drain(o, 6, 20000);
  CHECK_STR(times(s, 1), "10 20 40 80 160 320 ");                  // the remaining six burst copies, no slow phase
  CHECK_EQ(o.sent, 7);
  CHECK_EQ(o.size(), 0);
  CHECK_EQ(o.acked, 1);
}

TEST(o4_ack_before_the_first_copy_still_sends_the_whole_burst) {
  Outbox o;
  o.push(ev(0), 0);
  CHECK(o.ack(1));
  const std::vector<Sent> s = drain(o, 0, 5000);
  CHECK_EQ(s.size(), 7);
  CHECK_EQ(o.size(), 0);
}

TEST(ack_is_counted_once_and_unknown_acks_are_ignored) {
  Outbox o;
  o.push(ev(0), 0);
  o.push(ev(5), 5);
  CHECK(!o.ack(0));
  CHECK(!o.ack(3));
  CHECK(!o.ack(4294967295u));
  CHECK(o.ack(2));
  CHECK(!o.ack(2));                            // duplicate ACK (every copy is acknowledged)
  CHECK_EQ(o.acked, 1);
  CHECK_EQ(o.pending(), 1);
  drain(o, 0, 2000);
  CHECK(!o.ack(2));                            // long gone
  CHECK(o.ack(1));
  CHECK_EQ(o.acked, 2);
  CHECK_EQ(o.size(), 0);
}

TEST(o5_unacknowledged_event_expires_after_ttl) {
  Outbox o;
  o.push(ev(1000), 1020);
  const std::vector<Sent> s = drain(o, 1020, 400000);
  CHECK_EQ(o.size(), 0);
  CHECK_EQ(o.expired, 1);
  CHECK(s.back().at <= 1000 + 300000);                             // nothing is sent past the ttl
  CHECK(s.back().at > 1000 + 300000 - 1000);                       // but retried right up to it
  CHECK_EQ(s.size(), 7 + 299);                                     // burst + one per second
  CHECK(s.back().tx.age_ms <= 300000);
}

TEST(o5_ttl_boundary_is_exact) {
  OutboxConfig c;
  c.ttl_ms = 5000;
  Outbox o(c);
  o.push(ev(0), 0);
  drain(o, 0, 4999);                           // burst, then slow copies at 1320 ... 4320; next due 5320
  Outbox::Tx tx;
  CHECK(!o.poll(5000, &tx));                   // age == ttl: kept (nothing due)
  CHECK_EQ(o.size(), 1);
  CHECK_EQ(o.expired, 0);
  CHECK(!o.poll(5001, &tx));                   // one ms older: dropped
  CHECK_EQ(o.size(), 0);
  CHECK_EQ(o.expired, 1);
}

TEST(o5_acknowledged_event_cut_off_by_ttl_is_not_counted_as_expired) {
  OutboxConfig c;
  c.ttl_ms = 50;                               // shorter than the burst
  Outbox o(c);
  o.push(ev(0), 0);
  drain(o, 0, 5);
  CHECK(o.ack(1));
  drain(o, 6, 500);
  CHECK_EQ(o.size(), 0);
  CHECK_EQ(o.expired, 0);                      // it was delivered
  CHECK_EQ(o.acked, 1);
}

TEST(event_queued_while_offline_is_sent_later_with_its_real_age) {
  // WiFi was down for 3 s: nobody polled. The first copy carries age 3 s and a full burst follows.
  Outbox o;
  o.push(ev(1000), 1020);
  const std::vector<Sent> s = drain(o, 4000, 4500);
  CHECK_STR(times(s, 1), "4000 4010 4020 4040 4080 4160 4320 ");
  CHECK_EQ(s[0].tx.age_ms, 3000);
  CHECK_EQ(s[0].tx.n, 0);
}

TEST(event_older_than_ttl_when_the_link_returns_is_dropped_unsent) {
  Outbox o;
  o.push(ev(0), 20);
  const std::vector<Sent> s = drain(o, 300001, 301000);
  CHECK_EQ(s.size(), 0);
  CHECK_EQ(o.expired, 1);
  CHECK_EQ(o.sent, 0);
}

TEST(o6_ninth_event_replaces_the_oldest) {
  Outbox o;
  for (int i = 1; i <= 8; i++) CHECK_EQ(o.push(ev(i * 2000), i * 2000 + 20), static_cast<uint32_t>(i));
  CHECK_EQ(o.size(), 8);
  CHECK_EQ(o.evicted, 0);
  CHECK_EQ(o.push(ev(18000), 18020), 9);
  CHECK_EQ(o.size(), 8);
  CHECK_EQ(o.evicted, 1);
  CHECK(!o.ack(1));                            // event 1 is gone
  CHECK(o.ack(2));                             // event 2 is still there (delivered, burst not finished)
  CHECK_EQ(o.push(ev(20000), 20020), 10);      // full again: the delivered one makes room...
  CHECK_EQ(o.evicted, 1);                      // ...and that is not a loss
  CHECK_EQ(o.size(), 8);
  CHECK_EQ(o.pending(), 8);
  CHECK(!o.ack(2));
  CHECK(o.ack(3));
  const std::vector<Sent> s = drain(o, 20020, 20030);
  CHECK(!s.empty());
  CHECK_EQ(s[0].tx.seq, 10);
}

TEST(o6_among_delivered_events_the_oldest_makes_room_first) {
  Outbox o;
  for (int i = 1; i <= 8; i++) o.push(ev(i * 10), i * 10);         // 1..8, none sent yet
  CHECK(o.ack(2));
  CHECK(o.ack(5));
  CHECK_EQ(o.push(ev(90), 90), 9);                                  // takes the place of 2 (older than 5)
  CHECK(!o.ack(2));
  CHECK_EQ(o.pending(), 7);                                         // 1,3,4,6,7,8,9
  CHECK(o.ack(9));                                                  // 9 now sits in an earlier slot than 5
  CHECK_EQ(o.push(ev(100), 100), 10);                               // takes the place of 5, not of 9
  CHECK_EQ(o.evicted, 0);
  CHECK_EQ(o.size(), 8);
  std::map<uint32_t, int> seen;
  for (const Sent& x : drain(o, 100, 100)) seen[x.tx.seq]++;        // every held event has a first copy due
  CHECK_EQ(seen.count(5), 0);
  CHECK_EQ(seen.count(2), 0);
  CHECK_EQ(seen.count(9), 1);
  CHECK_EQ(seen.count(10), 1);
  CHECK_EQ(seen.size(), 8);
}

TEST(o6_eviction_keeps_the_newest_eight) {
  Outbox o;
  for (int i = 1; i <= 30; i++) o.push(ev(i * 10), i * 10);
  CHECK_EQ(o.size(), 8);
  CHECK_EQ(o.evicted, 22);
  for (uint32_t seq = 1; seq <= 22; seq++) CHECK(!o.ack(seq));
  for (uint32_t seq = 23; seq <= 30; seq++) CHECK(o.ack(seq));
}

TEST(o7_newest_event_is_sent_first) {
  Outbox o;
  o.push(ev(100), 100);
  o.push(ev(101), 101);
  o.push(ev(102), 102);
  Outbox::Tx tx;
  CHECK(o.poll(200, &tx));
  CHECK_EQ(tx.seq, 3);
  CHECK(o.poll(200, &tx));
  CHECK_EQ(tx.seq, 2);
  CHECK(o.poll(200, &tx));
  CHECK_EQ(tx.seq, 1);
  CHECK(!o.poll(200, &tx));
}

TEST(o7_no_event_starves) {
  // Eight unacknowledged events in the slow phase: every one keeps getting its copy each second.
  Outbox o;
  for (int i = 0; i < 8; i++) o.push(ev(i), i);
  drain(o, 0, 5000);
  const std::vector<Sent> s = drain(o, 5001, 15000);
  std::map<uint32_t, int> per;
  for (const Sent& x : s) per[x.tx.seq]++;
  CHECK_EQ(per.size(), 8);
  for (const auto& kv : per) CHECK_EQ(kv.second, 10);
}

TEST(o8_age_is_clamped_not_wrapped) {
  CHECK_EQ(hhj::event_age_ms(1000, 1000), 0);
  CHECK_EQ(hhj::event_age_ms(1001, 1000), 1);
  CHECK_EQ(hhj::event_age_ms(999, 1000), 0);                   // event "in the future": never negative
  CHECK_EQ(hhj::event_age_ms(0, 5000000000LL), 0);
  CHECK_EQ(hhj::event_age_ms(4294967294LL, 0), 4294967294u);
  CHECK_EQ(hhj::event_age_ms(4294967295LL, 0), 4294967295u);   // top of the range
  CHECK_EQ(hhj::event_age_ms(4294967296LL, 0), 4294967295u);   // beyond: saturated, not wrapped to 0
  CHECK_EQ(hhj::event_age_ms(9000000000LL, -10), 4294967295u);
  Outbox o;
  o.push(ev(1000), 500);
  Outbox::Tx tx;
  CHECK(o.poll(500, &tx));
  CHECK_EQ(tx.age_ms, 0);
}

TEST(copy_counter_saturates) {
  OutboxConfig c;
  c.slow_ms = 1;
  c.ttl_ms = 4294967295u;
  Outbox o(c);
  o.push(ev(0), 0);
  Outbox::Tx tx;
  uint32_t copies = 0;
  uint16_t last_n = 0;
  for (int64_t t = 0; t < 70000; t++) {
    while (o.poll(t, &tx)) {
      copies++;
      CHECK(tx.n >= last_n);
      last_n = tx.n;
    }
  }
  CHECK(copies > 65535);
  CHECK_EQ(last_n, 65535);
}

TEST(custom_slow_period) {
  OutboxConfig c;
  c.slow_ms = 250;
  Outbox o(c);
  o.push(ev(0), 0);
  const std::vector<Sent> s = drain(o, 0, 1100);
  CHECK_STR(times(s, 1), "0 10 20 40 80 160 320 570 820 1070 ");
}

// Random pushes, acks and polling gaps: the bookkeeping must always add up.
TEST(fuzz_outbox_accounting) {
  hhj_test::Rng rng(5);
  const int runs = hhj_test::iterations(300, 15);
  for (int run = 0; run < runs; run++) {
    OutboxConfig c;
    c.slow_ms = static_cast<uint32_t>(rng.range(50, 1500));
    c.ttl_ms = static_cast<uint32_t>(rng.range(500, 20000));
    Outbox o(c);
    std::map<uint32_t, int64_t> pushed_at;       // seq -> event time
    std::map<uint32_t, uint32_t> copies;         // seq -> copies seen
    std::map<uint32_t, int64_t> last_sent;       // seq -> time of the last copy
    uint32_t pushes = 0;
    int64_t t = 0;
    for (int step = 0; step < 4000; step++) {
      t += rng.range(1, 30);
      if (rng.chance(0.03)) {
        const uint32_t seq = o.push(ev(t - rng.range(0, 40)), t);
        CHECK_EQ(seq, ++pushes);
        pushed_at[seq] = t;
      }
      if (rng.chance(0.05) && pushes > 0) o.ack(1 + rng.below(pushes));
      Outbox::Tx tx;
      int guard = 0;
      while (o.poll(t, &tx)) {
        CHECK(++guard <= static_cast<int>(Outbox::kCap));             // at most one copy per event per instant
        CHECK(pushed_at.count(tx.seq) == 1);
        CHECK_EQ(tx.n, copies[tx.seq]);                               // copy numbers are consecutive
        copies[tx.seq]++;
        CHECK(static_cast<int64_t>(tx.age_ms) <= static_cast<int64_t>(c.ttl_ms));   // O-5
        CHECK_EQ(tx.age_ms, t - tx.t_ms);                             // O-8
        if (last_sent.count(tx.seq)) CHECK(t - last_sent[tx.seq] >= 10);            // never faster than the burst
        last_sent[tx.seq] = t;
      }
      CHECK(o.size() <= Outbox::kCap);
      CHECK(o.pending() <= o.size());
    }
    CHECK_EQ(o.last_seq(), pushes);
    // every event was acknowledged, expired, evicted or is still waiting — exactly one of them
    CHECK_EQ(o.acked + o.expired + o.evicted + o.pending(), pushes);
    uint32_t total = 0;
    for (const auto& kv : copies) total += kv.second;
    CHECK_EQ(total, o.sent);
  }
}
