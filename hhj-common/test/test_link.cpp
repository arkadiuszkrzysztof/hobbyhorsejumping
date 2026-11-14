// Tests for hhj/link.h — including the conformance file shared with the backend.
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "hhj/describe.h"
#include "hhj/link.h"
#include "hhj_test.h"

#ifndef HHJ_VECTORS
#error "build with -DHHJ_VECTORS=\"path/to/hhj-common/protocol/vectors.tsv\""
#endif

using namespace hhj;

namespace {

bool dec(const std::string& s, Msg* m) { return decode(s.data(), s.size(), m); }
bool valid(const std::string& s) {
  Msg m;
  return dec(s, &m);
}

template <class M>
std::string enc(const M& m) {
  char buf[256];
  const size_t n = encode(m, buf, sizeof(buf));
  return std::string(buf, n);
}

std::string reencode(const Msg& m) {
  switch (m.kind) {
    case Msg::Kind::Event: return enc(m.event);
    case Msg::Kind::Hello: return enc(m.hello);
    case Msg::Kind::Ack:   return enc(m.ack);
    case Msg::Kind::None:  break;
  }
  return "";
}

EventMsg event(Role mark = Role::Start) {
  EventMsg e;
  e.mark = mark;
  e.age_ms = 12;
  e.t_ms = 123456;
  e.boot = 3735928559u;
  e.seq = 7;
  e.dir = 1;
  e.gap_ms = 38;
  return e;
}

HelloMsg hello(Role role) {
  HelloMsg h;
  h.role = role;
  h.boot = 5;
  snprintf(h.fw, sizeof(h.fw), "1.0");
  return h;
}

}  // namespace

// --------------------------------------------------------------- conformance
TEST(conformance_vectors) {
  std::ifstream in(HHJ_VECTORS);
  CHECK(in.good());
  std::string line;
  int ok = 0, bad = 0, canon = 0, lineno = 0;
  while (std::getline(in, line)) {
    lineno++;
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> col;
    size_t start = 0;
    for (int c = 0; c < 3; c++) {
      const size_t tab = line.find('\t', start);
      if (tab == std::string::npos) break;
      col.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
    col.push_back(line.substr(start));          // payload: verbatim to the end of the line
    if (col.size() != 4) {
      printf("vectors.tsv:%d: expected 4 columns\n", lineno);
      CHECK(false);
      continue;
    }
    const std::string &verdict = col[0], &is_canon = col[1], &expect = col[2], &payload = col[3];
    Msg m;
    const bool got = dec(payload, &m);
    if (verdict == "ok") {
      ok++;
      if (!got) printf("vectors.tsv:%d: must decode: %s\n", lineno, payload.c_str());
      CHECK(got);
      CHECK_STR(describe(m), expect);
      if (is_canon == "c") {
        canon++;
        CHECK_STR(reencode(m), payload);
      } else {
        CHECK_STR(is_canon, "-");
      }
    } else {
      bad++;
      CHECK_STR(verdict, "bad");
      if (got) printf("vectors.tsv:%d: must be rejected: %s\n", lineno, payload.c_str());
      CHECK(!got);
      CHECK(m.kind == Msg::Kind::None);
    }
  }
  // the file really was read, and nobody emptied it
  CHECK(ok >= 20);
  CHECK(bad >= 70);
  CHECK(canon >= 10);
}

// ------------------------------------------------------------------ defaults
TEST(fresh_messages_are_empty) {
  CHECK(Role{} == Role::None);                    // a zeroed role is "none"
  const EventMsg e;
  CHECK(e.mark == Role::None);
  CHECK_EQ(e.age_ms, 0);
  CHECK_EQ(e.t_ms, 0);
  CHECK_EQ(e.boot, 0);
  CHECK_EQ(e.seq, 0);
  CHECK_EQ(e.dir, 0);
  CHECK_EQ(e.gap_ms, 0);
  CHECK_EQ(e.n, 0);
  const HelloMsg h;
  CHECK(h.role == Role::None);
  CHECK_EQ(h.boot, 0);
  CHECK_EQ(h.up_ms, 0);
  for (char c : h.fw) CHECK_EQ(c, 0);
  CHECK_EQ(sizeof(h.fw), kFwMax + 1);
  CHECK_EQ(h.st, 0);
  CHECK_EQ(h.seq, 0);
  CHECK_EQ(h.pending, 0);
  CHECK_EQ(h.ms, 0);
  CHECK_EQ(h.rssi, 0);
  CHECK_EQ(h.net, 0);
  const AckMsg a;
  CHECK(a.mark == Role::None);
  CHECK_EQ(a.boot, 0);
  CHECK_EQ(a.seq, 0);
  const Msg m;
  CHECK(m.kind == Msg::Kind::None);
  // and none of them is a message the encoders would send
  CHECK(enc(e).empty());
  CHECK(enc(h).empty());
  CHECK(enc(a).empty());
}

// -------------------------------------------------------------------- roles
TEST(role_names_round_trip) {
  const Role all[] = {Role::Start, Role::Finish, Role::Timer, Role::Backend};
  for (Role r : all) {
    const char* n = role_name(r);
    CHECK(role_parse(n, strlen(n)) == r);
  }
  CHECK_STR(role_name(Role::Start), "START");
  CHECK_STR(role_name(Role::Finish), "FINISH");
  CHECK_STR(role_name(Role::Timer), "TIMER");
  CHECK_STR(role_name(Role::Backend), "BACKEND");
  CHECK_STR(role_name(Role::None), "");
}

TEST(role_parse_is_exact) {
  CHECK(role_parse("", 0) == Role::None);
  CHECK(role_parse("start", 5) == Role::None);
  CHECK(role_parse("STAR", 4) == Role::None);
  CHECK(role_parse("STARTS", 6) == Role::None);
  CHECK(role_parse("START", 4) == Role::None);      // only the given length counts
  CHECK(role_parse("STARTxx", 5) == Role::Start);
  CHECK(role_parse("FINISH", 6) == Role::Finish);
  CHECK(role_parse("TIMER", 5) == Role::Timer);
  CHECK(role_parse("BACKEND", 7) == Role::Backend);
}

TEST(only_start_and_finish_are_gates) {
  CHECK(is_gate(Role::Start));
  CHECK(is_gate(Role::Finish));
  CHECK(!is_gate(Role::Timer));
  CHECK(!is_gate(Role::Backend));
  CHECK(!is_gate(Role::None));
}

TEST(gate_ok_means_ready_and_nothing_wrong) {
  CHECK(gate_ok(kStReady));
  CHECK(gate_ok(kStReady | kStArmed));
  CHECK(!gate_ok(0));
  CHECK(!gate_ok(kStArmed));
  CHECK(!gate_ok(kStReady | kStBeamA));
  CHECK(!gate_ok(kStReady | kStBeamB));
  CHECK(!gate_ok(kStReady | kStStuck));
  CHECK(!gate_ok(kStReady | kStBeamA | kStBeamB | kStStuck));
  CHECK(gate_ok(kStReady | 0xFFFFFFE0u));           // unknown higher bits do not matter
  CHECK_EQ(kStBeamA, 1);
  CHECK_EQ(kStBeamB, 2);
  CHECK_EQ(kStStuck, 4);
  CHECK_EQ(kStReady, 8);
  CHECK_EQ(kStArmed, 16);
  CHECK_EQ(kStIdle, 0);
  CHECK_EQ(kStRunning, 1);
  CHECK_EQ(kStFinished, 2);
}

TEST(protocol_constants) {
  CHECK_EQ(kLinkPort, 3004);
  CHECK_EQ(kMaxDatagram, 512);
  CHECK_EQ(kFwMax, 15);
  CHECK_EQ(kHelloPeriodMs, 2000);
  CHECK_EQ(kPeerTtlMs, 7000);
  CHECK_EQ(kMaxAgeMs, 600000);
  CHECK_EQ(kMaxClockMs, 9007199254740992LL);
}

// ------------------------------------------------------------------- encode
TEST(encode_event_refuses_invalid_messages) {
  EventMsg e = event();
  CHECK(!enc(e).empty());
  e = event(); e.mark = Role::None;    CHECK(enc(e).empty());
  e = event(); e.mark = Role::Timer;   CHECK(enc(e).empty());
  e = event(); e.mark = Role::Backend; CHECK(enc(e).empty());
  e = event(); e.seq = 0;              CHECK(enc(e).empty());
  e = event(); e.t_ms = -1;            CHECK(enc(e).empty());
  e = event(); e.t_ms = kMaxClockMs + 1; CHECK(enc(e).empty());
  e = event(); e.t_ms = kMaxClockMs;   CHECK(!enc(e).empty());
  e = event(); e.t_ms = 0;             CHECK(!enc(e).empty());
  e = event(); e.dir = 2;              CHECK(enc(e).empty());
  e = event(); e.dir = -2;             CHECK(enc(e).empty());
  e = event(); e.dir = -1;             CHECK(!enc(e).empty());
  e = event(); e.dir = 0;              CHECK(!enc(e).empty());
  e = event(); e.seq = 1;              CHECK(!enc(e).empty());
}

TEST(encode_hello_refuses_invalid_messages) {
  HelloMsg h = hello(Role::Timer);
  CHECK(!enc(h).empty());
  h = hello(Role::None);                       CHECK(enc(h).empty());
  h = hello(Role::Timer); h.up_ms = -1;        CHECK(enc(h).empty());
  h = hello(Role::Timer); h.up_ms = kMaxClockMs + 1; CHECK(enc(h).empty());
  h = hello(Role::Timer); h.up_ms = kMaxClockMs; CHECK(!enc(h).empty());
  h = hello(Role::Timer); h.fw[0] = '\0';      CHECK(enc(h).empty());
  h = hello(Role::Timer); h.fw[1] = ' ';       CHECK(enc(h).empty());
  h = hello(Role::Timer); h.fw[1] = '"';       CHECK(enc(h).empty());
}

TEST(encode_ack_refuses_invalid_messages) {
  AckMsg a;
  a.mark = Role::Start; a.boot = 1; a.seq = 1;
  CHECK(!enc(a).empty());
  a.seq = 0;            CHECK(enc(a).empty());
  a.seq = 1; a.mark = Role::Timer;   CHECK(enc(a).empty());
  a.mark = Role::Backend;            CHECK(enc(a).empty());
  a.mark = Role::None;               CHECK(enc(a).empty());
  a.mark = Role::Finish;             CHECK(!enc(a).empty());
}

// The buffer must be at least one byte longer than the datagram, never overrun,
// and "does not fit" must be reported as 0 for every size.
TEST(encode_respects_the_buffer_size) {
  const EventMsg e = event();
  const std::string full = enc(e);
  CHECK(!full.empty());
  for (size_t cap = 0; cap <= full.size() + 2; cap++) {
    std::vector<char> buf(cap + 1, '#');                     // one guard byte after the buffer
    const size_t n = encode(e, cap ? buf.data() : nullptr, cap);
    if (cap > full.size()) {
      CHECK_EQ(n, full.size());
      CHECK(memcmp(buf.data(), full.data(), full.size()) == 0);
    } else {
      CHECK_EQ(n, 0);
    }
    CHECK_EQ(buf[cap], '#');
  }
  char one[1];
  CHECK_EQ(encode(e, nullptr, 100), 0);
  CHECK_EQ(encode(e, one, 0), 0);
  CHECK_EQ(encode(e, one, 1), 0);
  CHECK_EQ(encode(hello(Role::Timer), nullptr, 100), 0);
  CHECK_EQ(encode(hello(Role::Timer), one, 0), 0);
  AckMsg a;
  a.mark = Role::Start; a.seq = 1;
  CHECK_EQ(encode(a, nullptr, 100), 0);
  CHECK_EQ(encode(a, one, 0), 0);
  const std::string hfull = enc(hello(Role::Start));
  std::vector<char> hb(hfull.size());
  CHECK_EQ(encode(hello(Role::Start), hb.data(), hb.size()), 0);          // exactly one byte short
  const std::string afull = enc(a);
  std::vector<char> ab(afull.size());
  CHECK_EQ(encode(a, ab.data(), ab.size()), 0);
}

TEST(longest_possible_messages_fit_the_firmware_buffers) {
  EventMsg e;
  e.mark = Role::Finish; e.age_ms = 4294967295u; e.t_ms = kMaxClockMs; e.boot = 4294967295u;
  e.seq = 4294967295u; e.dir = -1; e.gap_ms = 65535; e.n = 65535;
  CHECK(!enc(e).empty());
  CHECK(enc(e).size() < 160);                   // gate_app.h event buffer
  HelloMsg h;
  h.role = Role::Finish; h.boot = 4294967295u; h.up_ms = kMaxClockMs; snprintf(h.fw, sizeof(h.fw), "123456789012345");
  h.st = 4294967295u; h.seq = 4294967295u; h.pending = 65535; h.ms = 4294967295u; h.rssi = -128; h.net = 255;
  CHECK(!enc(h).empty());
  CHECK(enc(h).size() < 200);                   // gate_app.h / timer_app.h hello buffer
  h.role = Role::Timer;
  CHECK(!enc(h).empty());
  CHECK(enc(h).size() < 200);
  Msg m;
  CHECK(dec(enc(e), &m));
  CHECK(dec(enc(h), &m));
}

TEST(fw_validation) {
  CHECK(fw_valid("1"));
  CHECK(fw_valid("2.1.0"));
  CHECK(fw_valid("Az09._-"));
  CHECK(fw_valid("123456789012345"));           // 15
  CHECK(!fw_valid("1234567890123456"));         // 16
  CHECK(!fw_valid(""));
  CHECK(!fw_valid(nullptr));
  const char* bad[] = {"a b", "a\"b", "a/b", "a\\b", "a,b", "a:b", "a{b", "\xC5\x82", "a\tb", "a+b", "a@b", "a[b", "a`b"};
  for (const char* f : bad) CHECK(!fw_valid(f));
  // every single character: only the documented set passes
  for (int c = 1; c < 256; c++) {
    const char s[2] = {static_cast<char>(c), '\0'};
    const bool expect = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
    CHECK_EQ(fw_valid(s), expect);
  }
}

// ------------------------------------------------------------------- decode
TEST(decode_null_output_is_refused) {
  const std::string s = enc(event());
  CHECK(!decode(s.data(), s.size(), nullptr));
}

TEST(decode_failure_clears_the_output) {
  Msg m;
  CHECK(dec(enc(event()), &m));
  CHECK(m.kind == Msg::Kind::Event);
  CHECK(!dec("{\"time_mark\":\"START\"}", &m));
  CHECK(m.kind == Msg::Kind::None);
  CHECK_EQ(m.event.seq, 0);
  CHECK(m.event.mark == Role::None);
}

TEST(decode_oversize_datagram_rejected) {
  // A valid event padded with an unknown key up to the limit decodes; one byte more does not.
  const std::string head = "{\"time_mark\":\"START\",\"age_ms\":5,\"boot\":9,\"seq\":2,\"pad\":\"";
  const std::string at_limit = head + std::string(kMaxDatagram - head.size() - 2, 'x') + "\"}";
  CHECK_EQ(at_limit.size(), kMaxDatagram);
  CHECK(valid(at_limit));
  CHECK(!valid(head + std::string(kMaxDatagram - head.size() - 1, 'x') + "\"}"));
  CHECK(!valid(""));
  CHECK(!valid(" "));
  CHECK(!valid("\r\n\t "));
}

TEST(decode_role_strings_must_be_plain) {
  // JSON escapes are legal, but protocol values never use them: \u0053TART is not START.
  CHECK(!valid("{\"time_mark\":\"\\u0053TART\",\"age_ms\":5,\"boot\":9,\"seq\":2}"));
  CHECK(!valid("{\"hello\":\"\\u0054IMER\",\"boot\":5}"));
  CHECK(!valid("{\"ack\":\"\\u0053TART\",\"boot\":5,\"seq\":1}"));
  CHECK(!valid("{\"hello\":\"TIMER\",\"boot\":5,\"fw\":\"1\\u002e0\"}"));
}

TEST(decode_event_field_ranges_are_exact) {
  auto ev = [](const char* field, const std::string& value) {
    std::string s = "{\"time_mark\":\"START\"";
    const char* base[][2] = {{"age_ms", "5"}, {"boot", "9"}, {"seq", "2"}};
    for (auto& kv : base) {
      if (strcmp(kv[0], field) != 0) s += std::string(",\"") + kv[0] + "\":" + kv[1];
    }
    return s + ",\"" + field + "\":" + value + "}";
  };
  struct Range { const char* field; const char* lo; const char* below; const char* hi; const char* above; };
  const Range ranges[] = {
      {"age_ms", "0", "-1", "4294967295", "4294967296"}, {"boot", "0", "-1", "4294967295", "4294967296"},
      {"seq", "1", "0", "4294967295", "4294967296"},     {"t", "0", "-1", "9007199254740992", "9007199254740993"},
      {"dir", "-1", "-2", "1", "2"},                     {"gap_ms", "0", "-1", "65535", "65536"},
      {"n", "0", "-1", "65535", "65536"},
  };
  for (const Range& r : ranges) {
    CHECK(valid(ev(r.field, r.lo)));
    CHECK(!valid(ev(r.field, r.below)));
    CHECK(valid(ev(r.field, r.hi)));
    CHECK(!valid(ev(r.field, r.above)));
    CHECK(!valid(ev(r.field, "\"1\"")));
    CHECK(!valid(ev(r.field, "1.0")));
    CHECK(!valid(ev(r.field, "true")));
    CHECK(!valid(ev(r.field, "null")));
  }
}

TEST(decode_hello_field_ranges_are_exact) {
  auto he = [](const char* field, const std::string& value) {
    std::string s = "{\"hello\":\"START\"";
    if (strcmp(field, "boot") != 0) s += ",\"boot\":5";
    return s + ",\"" + field + "\":" + value + "}";
  };
  struct Range { const char* field; const char* lo; const char* below; const char* hi; const char* above; };
  const Range ranges[] = {
      {"boot", "0", "-1", "4294967295", "4294967296"},    {"up_ms", "0", "-1", "9007199254740992", "9007199254740993"},
      {"st", "0", "-1", "4294967295", "4294967296"},      {"seq", "0", "-1", "4294967295", "4294967296"},
      {"pending", "0", "-1", "65535", "65536"},           {"ms", "0", "-1", "4294967295", "4294967296"},
      {"rssi", "-128", "-129", "127", "128"},             {"net", "0", "-1", "255", "256"},
  };
  for (const Range& r : ranges) {
    CHECK(valid(he(r.field, r.lo)));
    CHECK(!valid(he(r.field, r.below)));
    CHECK(valid(he(r.field, r.hi)));
    CHECK(!valid(he(r.field, r.above)));
    CHECK(!valid(he(r.field, "\"1\"")));
    CHECK(!valid(he(r.field, "null")));
  }
}

TEST(decode_ack_field_ranges_are_exact) {
  CHECK(valid("{\"ack\":\"START\",\"boot\":0,\"seq\":1}"));
  CHECK(valid("{\"ack\":\"START\",\"boot\":4294967295,\"seq\":4294967295}"));
  CHECK(!valid("{\"ack\":\"START\",\"boot\":-1,\"seq\":1}"));
  CHECK(!valid("{\"ack\":\"START\",\"boot\":4294967296,\"seq\":1}"));
  CHECK(!valid("{\"ack\":\"START\",\"boot\":0,\"seq\":0}"));
  CHECK(!valid("{\"ack\":\"START\",\"boot\":0,\"seq\":4294967296}"));
  CHECK(!valid("{\"ack\":\"START\",\"boot\":\"0\",\"seq\":1}"));
  CHECK(!valid("{\"ack\":\"START\",\"boot\":0,\"seq\":\"1\"}"));
}

TEST(decode_kind_needs_exactly_one_marker_key) {
  CHECK(!valid("{\"boot\":5,\"seq\":1,\"age_ms\":1}"));
  CHECK(!valid("{\"time_mark\":\"START\",\"hello\":\"START\",\"age_ms\":5,\"boot\":9,\"seq\":2}"));
  CHECK(!valid("{\"time_mark\":\"START\",\"ack\":\"START\",\"age_ms\":5,\"boot\":9,\"seq\":2}"));
  CHECK(!valid("{\"hello\":\"START\",\"ack\":\"START\",\"boot\":9,\"seq\":2}"));
  CHECK(!valid("{\"time_mark\":\"START\",\"hello\":\"START\",\"ack\":\"START\",\"age_ms\":5,\"boot\":9,\"seq\":2}"));
  // the marker key counts even when its value is of the wrong type
  CHECK(!valid("{\"hello\":\"TIMER\",\"boot\":5,\"time_mark\":null}"));
  CHECK(!valid("{\"hello\":\"TIMER\",\"boot\":5,\"ack\":0}"));
}

TEST(decode_absent_optional_fields_take_defaults) {
  Msg m;
  CHECK(dec("{\"time_mark\":\"FINISH\",\"age_ms\":5,\"boot\":9,\"seq\":2}", &m));
  CHECK(m.event.mark == Role::Finish);
  CHECK_EQ(m.event.t_ms, 0);
  CHECK_EQ(m.event.dir, 0);
  CHECK_EQ(m.event.gap_ms, 0);
  CHECK_EQ(m.event.n, 0);
  CHECK(dec("{\"hello\":\"TIMER\",\"boot\":5}", &m));
  CHECK_EQ(m.hello.up_ms, 0);
  CHECK_EQ(m.hello.fw[0], 0);
  CHECK_EQ(m.hello.st, 0);
  CHECK_EQ(m.hello.rssi, 0);
}

// ---------------------------------------------------------------- round trip
TEST(round_trip_random_events) {
  hhj_test::Rng rng(1);
  const int n = hhj_test::iterations(50000, 1000);
  for (int i = 0; i < n; i++) {
    EventMsg e;
    e.mark = rng.chance(0.5) ? Role::Start : Role::Finish;
    e.age_ms = rng.chance(0.2) ? 4294967295u : static_cast<uint32_t>(rng.next());
    e.t_ms = rng.chance(0.1) ? kMaxClockMs : rng.range(0, kMaxClockMs);
    e.boot = static_cast<uint32_t>(rng.next());
    e.seq = 1 + static_cast<uint32_t>(rng.next() % 4294967295ull);
    e.dir = static_cast<int8_t>(rng.range(-1, 1));
    e.gap_ms = static_cast<uint16_t>(rng.next());
    e.n = static_cast<uint16_t>(rng.next());
    const std::string s = enc(e);
    CHECK(!s.empty());
    Msg m;
    CHECK(dec(s, &m));
    CHECK(m.kind == Msg::Kind::Event);
    CHECK(m.event.mark == e.mark);
    CHECK_EQ(m.event.age_ms, e.age_ms);
    CHECK_EQ(m.event.t_ms, e.t_ms);
    CHECK_EQ(m.event.boot, e.boot);
    CHECK_EQ(m.event.seq, e.seq);
    CHECK_EQ(m.event.dir, e.dir);
    CHECK_EQ(m.event.gap_ms, e.gap_ms);
    CHECK_EQ(m.event.n, e.n);
    CHECK_STR(enc(m.event), s);
  }
}

TEST(round_trip_random_hellos) {
  hhj_test::Rng rng(2);
  const Role roles[] = {Role::Start, Role::Finish, Role::Timer, Role::Backend};
  const char fwset[] = "ABCxyz019._-";
  const int n = hhj_test::iterations(50000, 1000);
  for (int i = 0; i < n; i++) {
    HelloMsg h;
    h.role = roles[rng.below(4)];
    h.boot = static_cast<uint32_t>(rng.next());
    h.up_ms = rng.range(0, kMaxClockMs);
    const size_t fwlen = 1 + rng.below(kFwMax);
    for (size_t k = 0; k < fwlen; k++) h.fw[k] = fwset[rng.below(sizeof(fwset) - 1)];
    h.fw[fwlen] = '\0';
    // each role only carries its own optional fields
    if (is_gate(h.role)) {
      h.st = static_cast<uint32_t>(rng.next());
      h.seq = static_cast<uint32_t>(rng.next());
      h.pending = static_cast<uint16_t>(rng.next());
    }
    if (h.role == Role::Timer) {
      h.st = static_cast<uint32_t>(rng.next());
      h.ms = static_cast<uint32_t>(rng.next());
    }
    if (h.role != Role::Backend) {
      h.rssi = static_cast<int8_t>(rng.range(-128, 127));
      h.net = static_cast<uint8_t>(rng.next());
    }
    const std::string s = enc(h);
    CHECK(!s.empty());
    Msg m;
    CHECK(dec(s, &m));
    CHECK(m.kind == Msg::Kind::Hello);
    CHECK(m.hello.role == h.role);
    CHECK_EQ(m.hello.boot, h.boot);
    CHECK_EQ(m.hello.up_ms, h.up_ms);
    CHECK_STR(m.hello.fw, h.fw);
    CHECK_EQ(m.hello.st, h.st);
    CHECK_EQ(m.hello.seq, h.seq);
    CHECK_EQ(m.hello.pending, h.pending);
    CHECK_EQ(m.hello.ms, h.ms);
    CHECK_EQ(m.hello.rssi, h.rssi);
    CHECK_EQ(m.hello.net, h.net);
    CHECK_STR(enc(m.hello), s);
  }
}

TEST(round_trip_random_acks) {
  hhj_test::Rng rng(3);
  const int n = hhj_test::iterations(20000, 500);
  for (int i = 0; i < n; i++) {
    AckMsg a;
    a.mark = rng.chance(0.5) ? Role::Start : Role::Finish;
    a.boot = static_cast<uint32_t>(rng.next());
    a.seq = 1 + static_cast<uint32_t>(rng.next() % 4294967295ull);
    Msg m;
    CHECK(dec(enc(a), &m));
    CHECK(m.kind == Msg::Kind::Ack);
    CHECK(m.ack.mark == a.mark);
    CHECK_EQ(m.ack.boot, a.boot);
    CHECK_EQ(m.ack.seq, a.seq);
  }
}

// A gate's own optional fields do not leak into another role's encoding.
TEST(hello_encoding_is_role_specific) {
  HelloMsg h = hello(Role::Backend);
  h.st = 3; h.seq = 4; h.pending = 5; h.ms = 6; h.rssi = -7; h.net = 1;
  CHECK_STR(enc(h), "{\"hello\":\"BACKEND\",\"boot\":5,\"up_ms\":0,\"fw\":\"1.0\"}");
  h.role = Role::Timer;
  CHECK_STR(enc(h), "{\"hello\":\"TIMER\",\"boot\":5,\"up_ms\":0,\"fw\":\"1.0\",\"st\":3,\"ms\":6,\"rssi\":-7,\"net\":1}");
  h.role = Role::Start;
  CHECK_STR(enc(h), "{\"hello\":\"START\",\"boot\":5,\"up_ms\":0,\"fw\":\"1.0\",\"st\":3,\"seq\":4,\"pending\":5,\"rssi\":-7,\"net\":1}");
  h.role = Role::Finish;
  CHECK_STR(enc(h), "{\"hello\":\"FINISH\",\"boot\":5,\"up_ms\":0,\"fw\":\"1.0\",\"st\":3,\"seq\":4,\"pending\":5,\"rssi\":-7,\"net\":1}");
}

// Anything decode() accepts is a message encode() can reproduce, and anything
// it rejects leaves no message behind — for arbitrary damaged input.
TEST(fuzz_decode_is_safe_and_consistent) {
  hhj_test::Rng rng(99);
  const std::string seeds[] = {enc(event(Role::Start)), enc(event(Role::Finish)), enc(hello(Role::Start)),
                               enc(hello(Role::Timer)), enc(hello(Role::Backend)),
                               "{\"ack\":\"START\",\"boot\":3735928559,\"seq\":7}"};
  const int n = hhj_test::iterations(150000, 3000);
  int accepted = 0;
  for (int i = 0; i < n; i++) {
    std::string s = seeds[rng.below(6)];
    const int edits = 1 + static_cast<int>(rng.below(3));
    for (int k = 0; k < edits && !s.empty(); k++) {
      const uint32_t at = rng.below(static_cast<uint32_t>(s.size()));
      switch (rng.below(4)) {
        case 0: s[at] = static_cast<char>(rng.below(256)); break;
        case 1: s.erase(at, 1); break;
        case 2: s.insert(at, 1, "0123456789-\",:{}"[rng.below(16)]); break;
        default: s[at] = "0123456789"[rng.below(10)]; break;
      }
    }
    std::vector<char> buf(s.begin(), s.end());           // exact-size: ASan sees any overread
    Msg m;
    if (decode(buf.data(), buf.size(), &m)) {
      accepted++;
      // A HELLO may arrive without "fw" or with another role's optional fields;
      // the encoders always write the canonical set, so normalise before comparing.
      if (m.kind == Msg::Kind::Hello) {
        if (m.hello.fw[0] == '\0') snprintf(m.hello.fw, sizeof(m.hello.fw), "x");
        if (!is_gate(m.hello.role)) { m.hello.seq = 0; m.hello.pending = 0; }
        if (m.hello.role != Role::Timer) m.hello.ms = 0;
        if (m.hello.role == Role::Backend) { m.hello.st = 0; m.hello.rssi = 0; m.hello.net = 0; }
      }
      const std::string again = reencode(m);
      CHECK(!again.empty());
      Msg m2;
      CHECK(dec(again, &m2));
      CHECK_STR(describe(m2), describe(m));
    } else {
      CHECK(m.kind == Msg::Kind::None);
    }
  }
  CHECK(accepted > n / 100);       // the mutations do leave many datagrams valid
}

TEST(decode_overlong_fw_never_touches_memory_outside_the_message) {
  // ASan watches: a version string far longer than the field must be rejected before any copy.
  for (size_t len : {size_t{16}, size_t{17}, size_t{40}, size_t{200}, size_t{480}}) {
    const std::string s = "{\"hello\":\"TIMER\",\"boot\":5,\"fw\":\"" + std::string(len, 'a') + "\"}";
    Msg m;
    CHECK(!dec(s, &m));
  }
  Msg ok;
  CHECK(dec("{\"hello\":\"TIMER\",\"boot\":5,\"fw\":\"" + std::string(15, 'a') + "\"}", &ok));
  CHECK_EQ(strlen(ok.hello.fw), 15);
}
