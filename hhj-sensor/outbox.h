// outbox.h — events waiting to be (re)transmitted by a gate (pure C++, no heap).
//
// UDP gives no delivery guarantee, so every event is sent several times
// (hhj-common/PROTOCOL.md, "Sending"):
//
//   O-1  Each event gets the next sequence number: 1, 2, 3, ...
//   O-2  Fast burst: kBurstCopies copies, the first one immediately, the
//        following ones kBurstGapMs after the previous copy.
//   O-3  After the burst: one copy every slow_ms until a backend acknowledges
//        the event.
//   O-4  An ACK never shortens the fast burst (the timer does not ACK and
//        relies on it); it only ends the slow phase.
//   O-5  An event older than ttl_ms is dropped (counted in `expired` if it was
//        never acknowledged).
//   O-6  At most kCap events are kept. When full, a new event replaces an
//        already acknowledged one if there is one, otherwise the OLDEST
//        (counted in `evicted`): the newest crossing is the one the display
//        needs.
//   O-7  When several copies are due, the newest event goes first.
//   O-8  Each copy carries age_ms = now - event time, recomputed per copy.
//
// Host tests: test/test_outbox.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "beam_logic.h"

namespace hhj {

constexpr uint16_t kBurstGapMs[] = {10, 10, 20, 40, 80, 160};                    // copies at 0,10,20,40,80,160,320 ms
constexpr uint16_t kBurstCopies = 1 + sizeof(kBurstGapMs) / sizeof(uint16_t);

// age_ms of a copy sent at `now_ms` for a cut at `t_ms`: never negative, never wrapped.
inline uint32_t event_age_ms(int64_t now_ms, int64_t t_ms) {
  const int64_t age = now_ms - t_ms;
  if (age <= 0) return 0;
  return age > 4294967295LL ? 4294967295u : static_cast<uint32_t>(age);
}

struct OutboxConfig {
  uint32_t slow_ms = 1000;     // retry period after the fast burst
  uint32_t ttl_ms = 300000;    // give up on an event after this long
};

class Outbox {
 public:
  static constexpr size_t kCap = 8;

  struct Tx {
    uint32_t seq;
    int64_t t_ms;
    int8_t dir;
    uint16_t gap_ms;
    uint16_t n;          // copy number, 0 = first
    uint32_t age_ms;
  };

  explicit Outbox(const OutboxConfig& c = OutboxConfig{}) : cfg_(c) {}

  // Queue a new event; its first copy is due immediately. Returns its sequence number.
  uint32_t push(const GateEvent& ev, int64_t now_ms) {
    Item* it = nullptr;
    for (Item& i : items_) {
      if (!i.used) { it = &i; break; }
    }
    if (it == nullptr) {                                  // O-6
      for (Item& i : items_) {                            // delivered, only finishing its burst
        if (i.acked && (it == nullptr || i.seq < it->seq)) it = &i;
      }
      if (it == nullptr) {
        for (Item& i : items_) {
          if (it == nullptr || i.seq < it->seq) it = &i;
        }
        evicted++;
      }
    }
    *it = Item{};
    it->used = true;
    it->seq = ++last_seq_;                                // O-1
    it->ev = ev;
    it->next_ms = now_ms;
    return it->seq;
  }

  // A backend confirmed event `seq`. Returns true if that was news.
  bool ack(uint32_t seq) {
    for (Item& i : items_) {
      if (i.used && i.seq == seq) {
        if (i.acked) return false;
        i.acked = true;
        acked++;
        if (i.sent >= kBurstCopies) i.used = false;       // O-4
        return true;
      }
    }
    return false;
  }

  // The next copy to transmit at `now_ms`, if one is due. Call until it returns false.
  bool poll(int64_t now_ms, Tx* out) {
    for (Item& i : items_) {                              // O-5
      if (i.used && now_ms - i.ev.t_ms > static_cast<int64_t>(cfg_.ttl_ms)) {
        if (!i.acked) expired++;
        i.used = false;
      }
    }
    Item* best = nullptr;
    for (Item& i : items_) {                              // O-7
      if (i.used && i.next_ms <= now_ms && (best == nullptr || i.seq > best->seq)) best = &i;
    }
    if (best == nullptr) return false;

    out->seq = best->seq;
    out->t_ms = best->ev.t_ms;
    out->dir = best->ev.dir;
    out->gap_ms = best->ev.gap_ms;
    out->n = best->sent;
    out->age_ms = event_age_ms(now_ms, best->ev.t_ms);    // O-8

    if (best->sent < 65535) best->sent++;
    if (best->sent < kBurstCopies) {
      best->next_ms = now_ms + kBurstGapMs[best->sent - 1];   // O-2
    } else if (best->acked) {
      best->used = false;                                 // burst complete and confirmed
    } else {
      best->next_ms = now_ms + static_cast<int64_t>(cfg_.slow_ms);   // O-3
    }
    sent++;
    return true;
  }

  uint32_t last_seq() const { return last_seq_; }

  // events not yet acknowledged by a backend
  uint16_t pending() const {
    uint16_t n = 0;
    for (const Item& i : items_) n = static_cast<uint16_t>(n + ((i.used && !i.acked) ? 1 : 0));
    return n;
  }

  // events still held (pending, or acknowledged but mid-burst)
  size_t size() const {
    size_t n = 0;
    for (const Item& i : items_) n += i.used ? 1u : 0u;
    return n;
  }

  // diagnostic counters
  uint32_t sent = 0, acked = 0, expired = 0, evicted = 0;

 private:
  struct Item {
    bool used = false;
    bool acked = false;
    uint32_t seq = 0;
    uint16_t sent = 0;        // copies transmitted so far
    int64_t next_ms = 0;      // when the next copy is due
    GateEvent ev;
  };

  OutboxConfig cfg_;
  Item items_[kCap];
  uint32_t last_seq_ = 0;
};

}  // namespace hhj
