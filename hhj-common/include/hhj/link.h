// link.h — HHJ link protocol messages: encode / decode (pure C++, no heap).
//
// The contract is hhj-common/PROTOCOL.md; hhj-backend/timings/link.py is the
// Python twin. Both are checked against hhj-common/protocol/vectors.tsv.
//
// Rules this file guarantees:
//   - decode() accepts only datagrams that satisfy every rule of the spec;
//   - encode() refuses to produce a datagram that decode() would reject;
//   - neither ever reads or writes outside the buffers it is given.
//
// Host tests: hhj-common/test/test_link.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "json_scan.h"

namespace hhj {

constexpr uint16_t kLinkPort = 3004;
constexpr size_t kMaxDatagram = FlatJson::kMaxLen;
constexpr size_t kFwMax = 15;                 // longest version string
constexpr uint32_t kHelloPeriodMs = 2000;     // how often nodes announce themselves
constexpr uint32_t kPeerTtlMs = 7000;         // silence after which a node is gone
constexpr uint32_t kMaxAgeMs = 600000;        // older event copies are dropped as stale
constexpr int64_t kMaxClockMs = 9007199254740992LL;   // 2^53

enum class Role : uint8_t { None = 0, Start, Finish, Timer, Backend };

inline const char* role_name(Role r) {
  switch (r) {
    case Role::Start:   return "START";
    case Role::Finish:  return "FINISH";
    case Role::Timer:   return "TIMER";
    case Role::Backend: return "BACKEND";
    case Role::None:    break;
  }
  return "";
}

inline Role role_parse(const char* s, size_t n) {
  const Role all[] = {Role::Start, Role::Finish, Role::Timer, Role::Backend};
  for (Role r : all) {
    const char* name = role_name(r);
    if (strlen(name) == n && memcmp(name, s, n) == 0) return r;
  }
  return Role::None;
}

inline bool is_gate(Role r) { return r == Role::Start || r == Role::Finish; }

// HELLO "st" of a gate.
constexpr uint32_t kStBeamA = 1;    // beam A is broken right now
constexpr uint32_t kStBeamB = 2;    // beam B is broken right now
constexpr uint32_t kStStuck = 4;    // a beam has been broken longer than the stuck limit
constexpr uint32_t kStReady = 8;    // both beams have been seen free since power-up
constexpr uint32_t kStArmed = 16;   // not in the quiet period after an event
inline bool gate_ok(uint32_t st) { return (st & 15u) == kStReady; }

// HELLO "st" of the timer.
constexpr uint32_t kStIdle = 0;
constexpr uint32_t kStRunning = 1;
constexpr uint32_t kStFinished = 2;

struct EventMsg {
  Role mark = Role::None;   // Start or Finish
  uint32_t age_ms = 0;      // how long ago the beam was cut, at the moment of sending
  int64_t t_ms = 0;         // the cut on the gate's clock (ms since its boot)
  uint32_t boot = 0;        // gate boot nonce
  uint32_t seq = 0;         // event number since boot, starts at 1
  int8_t dir = 0;           // +1 beam A first, -1 beam B first, 0 unknown
  uint16_t gap_ms = 0;      // time between the two beams
  uint16_t n = 0;           // copy number of this event, 0 = first
};

struct HelloMsg {
  Role role = Role::None;
  uint32_t boot = 0;
  int64_t up_ms = 0;
  char fw[kFwMax + 1] = {0};
  uint32_t st = 0;
  uint32_t seq = 0;         // gates: last event number
  uint16_t pending = 0;     // gates: events waiting for a backend ACK
  uint32_t ms = 0;          // timer: running time or last result
  int8_t rssi = 0;
  uint8_t net = 0;          // 0 primary WiFi, 1 fallback
};

struct AckMsg {
  Role mark = Role::None;
  uint32_t boot = 0;
  uint32_t seq = 0;
};

struct Msg {
  enum class Kind : uint8_t { None, Event, Hello, Ack };
  Kind kind = Kind::None;
  EventMsg event;
  HelloMsg hello;
  AckMsg ack;
};

inline bool fw_valid(const char* fw) {
  if (fw == nullptr) return false;
  const size_t n = strnlen(fw, kFwMax + 1);
  if (n == 0 || n > kFwMax) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = fw[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

namespace detail {

constexpr int64_t kU32Max = 4294967295LL;

// Absent key: `*out` keeps its default. Present key: must be an integer in [lo, hi].
inline bool opt_int(const FlatJson& j, const char* key, int64_t lo, int64_t hi, int64_t* out) {
  if (!j.has(key)) return true;
  int64_t v = 0;
  if (!j.get_int(key, &v) || v < lo || v > hi) return false;
  *out = v;
  return true;
}

inline bool req_int(const FlatJson& j, const char* key, int64_t lo, int64_t hi, int64_t* out) {
  return j.has(key) && opt_int(j, key, lo, hi, out);
}

inline bool req_role(const FlatJson& j, const char* key, Role* out) {
  const char* p = nullptr;
  size_t n = 0;
  if (!j.get_token(key, &p, &n)) return false;
  const Role r = role_parse(p, n);
  if (r == Role::None) return false;
  *out = r;
  return true;
}

// snprintf result -> datagram length; 0 if it did not fit (a negative result
// becomes a huge size_t and fails the same test).
inline size_t finish(int written, size_t cap) {
  const size_t len = static_cast<size_t>(written);
  return len < cap ? len : 0;
}

}  // namespace detail

// ---------------------------------------------------------------- encode
// All encoders return the datagram length, or 0 if the message is not valid
// or does not fit (the buffer content is then unspecified).

inline size_t encode(const EventMsg& e, char* buf, size_t cap) {
  if (buf == nullptr) return 0;
  if (!is_gate(e.mark) || e.seq == 0 || e.t_ms < 0 || e.t_ms > kMaxClockMs || e.dir < -1 || e.dir > 1) return 0;
  const int w = snprintf(buf, cap,
      "{\"time_mark\":\"%s\",\"age_ms\":%lu,\"t\":%lld,\"boot\":%lu,\"seq\":%lu,"
      "\"dir\":%d,\"gap_ms\":%u,\"n\":%u}",
      role_name(e.mark), static_cast<unsigned long>(e.age_ms), static_cast<long long>(e.t_ms),
      static_cast<unsigned long>(e.boot), static_cast<unsigned long>(e.seq),
      static_cast<int>(e.dir), static_cast<unsigned>(e.gap_ms), static_cast<unsigned>(e.n));
  return detail::finish(w, cap);
}

inline size_t encode(const HelloMsg& h, char* buf, size_t cap) {
  if (buf == nullptr) return 0;
  if (h.role == Role::None || h.up_ms < 0 || h.up_ms > kMaxClockMs || !fw_valid(h.fw)) return 0;
  if (is_gate(h.role)) {
    return detail::finish(snprintf(buf, cap,
        "{\"hello\":\"%s\",\"boot\":%lu,\"up_ms\":%lld,\"fw\":\"%s\",\"st\":%lu,\"seq\":%lu,"
        "\"pending\":%u,\"rssi\":%d,\"net\":%u}",
        role_name(h.role), static_cast<unsigned long>(h.boot), static_cast<long long>(h.up_ms), h.fw,
        static_cast<unsigned long>(h.st), static_cast<unsigned long>(h.seq),
        static_cast<unsigned>(h.pending), static_cast<int>(h.rssi), static_cast<unsigned>(h.net)), cap);
  }
  if (h.role == Role::Timer) {
    return detail::finish(snprintf(buf, cap,
        "{\"hello\":\"TIMER\",\"boot\":%lu,\"up_ms\":%lld,\"fw\":\"%s\",\"st\":%lu,\"ms\":%lu,"
        "\"rssi\":%d,\"net\":%u}",
        static_cast<unsigned long>(h.boot), static_cast<long long>(h.up_ms), h.fw,
        static_cast<unsigned long>(h.st), static_cast<unsigned long>(h.ms),
        static_cast<int>(h.rssi), static_cast<unsigned>(h.net)), cap);
  }
  return detail::finish(snprintf(buf, cap, "{\"hello\":\"BACKEND\",\"boot\":%lu,\"up_ms\":%lld,\"fw\":\"%s\"}",
      static_cast<unsigned long>(h.boot), static_cast<long long>(h.up_ms), h.fw), cap);
}

inline size_t encode(const AckMsg& a, char* buf, size_t cap) {
  if (buf == nullptr) return 0;
  if (!is_gate(a.mark) || a.seq == 0) return 0;
  const int w = snprintf(buf, cap, "{\"ack\":\"%s\",\"boot\":%lu,\"seq\":%lu}",
      role_name(a.mark), static_cast<unsigned long>(a.boot), static_cast<unsigned long>(a.seq));
  return detail::finish(w, cap);
}

// ---------------------------------------------------------------- decode
// True if `data[0..len)` is a valid datagram; `*out` then holds it.
// On failure `out->kind` is Kind::None.

inline bool decode(const char* data, size_t len, Msg* out) {
  using detail::kU32Max;
  using detail::opt_int;
  using detail::req_int;
  if (out == nullptr) return false;
  *out = Msg{};
  FlatJson j;
  if (!j.parse(data, len)) return false;

  const bool is_event = j.has("time_mark");
  const bool is_hello = j.has("hello");
  const bool is_ack = j.has("ack");
  if ((is_event ? 1 : 0) + (is_hello ? 1 : 0) + (is_ack ? 1 : 0) != 1) return false;

  if (is_event) {
    EventMsg e;
    int64_t age = 0, boot = 0, seq = 0;          // required: always overwritten below
    int64_t t = 0, dir = 0, gap = 0, n = 0;      // optional: these are the defaults
    if (!detail::req_role(j, "time_mark", &e.mark) || !is_gate(e.mark)) return false;
    if (!req_int(j, "age_ms", 0, kU32Max, &age)) return false;
    if (!req_int(j, "boot", 0, kU32Max, &boot)) return false;
    if (!req_int(j, "seq", 1, kU32Max, &seq)) return false;
    if (!opt_int(j, "t", 0, kMaxClockMs, &t)) return false;
    if (!opt_int(j, "dir", -1, 1, &dir)) return false;
    if (!opt_int(j, "gap_ms", 0, 65535, &gap)) return false;
    if (!opt_int(j, "n", 0, 65535, &n)) return false;
    e.age_ms = static_cast<uint32_t>(age);
    e.t_ms = t;
    e.boot = static_cast<uint32_t>(boot);
    e.seq = static_cast<uint32_t>(seq);
    e.dir = static_cast<int8_t>(dir);
    e.gap_ms = static_cast<uint16_t>(gap);
    e.n = static_cast<uint16_t>(n);
    out->event = e;
    out->kind = Msg::Kind::Event;
    return true;
  }

  if (is_hello) {
    HelloMsg h;
    int64_t boot = 0;                            // required: always overwritten below
    int64_t up = 0, st = 0, seq = 0, pending = 0, ms = 0, rssi = 0, net = 0;   // optional: the defaults
    if (!detail::req_role(j, "hello", &h.role)) return false;
    if (!req_int(j, "boot", 0, kU32Max, &boot)) return false;
    if (!opt_int(j, "up_ms", 0, kMaxClockMs, &up)) return false;
    if (!opt_int(j, "st", 0, kU32Max, &st)) return false;
    if (!opt_int(j, "seq", 0, kU32Max, &seq)) return false;
    if (!opt_int(j, "pending", 0, 65535, &pending)) return false;
    if (!opt_int(j, "ms", 0, kU32Max, &ms)) return false;
    if (!opt_int(j, "rssi", -128, 127, &rssi)) return false;
    if (!opt_int(j, "net", 0, 255, &net)) return false;
    if (j.has("fw")) {
      const char* p = nullptr;
      size_t n = 0;
      if (!j.get_token("fw", &p, &n) || n > kFwMax) return false;
      memcpy(h.fw, p, n);                          // h.fw is all zeros: it stays terminated
      if (!fw_valid(h.fw)) return false;           // also rejects the empty string
    }
    h.boot = static_cast<uint32_t>(boot);
    h.up_ms = up;
    h.st = static_cast<uint32_t>(st);
    h.seq = static_cast<uint32_t>(seq);
    h.pending = static_cast<uint16_t>(pending);
    h.ms = static_cast<uint32_t>(ms);
    h.rssi = static_cast<int8_t>(rssi);
    h.net = static_cast<uint8_t>(net);
    out->hello = h;
    out->kind = Msg::Kind::Hello;
    return true;
  }

  AckMsg a;
  int64_t boot = 0, seq = 0;                     // required: always overwritten below
  if (!detail::req_role(j, "ack", &a.mark) || !is_gate(a.mark)) return false;
  if (!req_int(j, "boot", 0, kU32Max, &boot)) return false;
  if (!req_int(j, "seq", 1, kU32Max, &seq)) return false;
  a.boot = static_cast<uint32_t>(boot);
  a.seq = static_cast<uint32_t>(seq);
  out->ack = a;
  out->kind = Msg::Kind::Ack;
  return true;
}

}  // namespace hhj
