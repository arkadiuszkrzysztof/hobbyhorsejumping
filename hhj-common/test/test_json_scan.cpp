// Tests for hhj/json_scan.h
#include <string>

#include "hhj/json_scan.h"
#include "hhj_test.h"

using hhj::FlatJson;

namespace {

bool parses(const std::string& s) {
  FlatJson j;
  return j.parse(s.data(), s.size());
}

// The value of `key` as an int, or -999999 when it is not an int.
long long int_of(const std::string& s, const char* key) {
  FlatJson j;
  int64_t v = 0;
  if (!j.parse(s.data(), s.size()) || !j.get_int(key, &v)) return -999999;
  return v;
}

std::string token_of(const std::string& s, const char* key) {
  FlatJson j;
  const char* p = nullptr;
  size_t n = 0;
  if (!j.parse(s.data(), s.size()) || !j.get_token(key, &p, &n)) return "<none>";
  return std::string(p, n);
}

}  // namespace

TEST(empty_object_is_valid_and_has_nothing) {
  FlatJson j;
  CHECK(j.parse("{}", 2));
  CHECK_EQ(j.size(), 0);
  CHECK(!j.has("a"));
  CHECK(parses(" { } "));
  CHECK(parses("\t\r\n{\t\r\n}\t\r\n"));
}

TEST(a_scanner_that_has_parsed_nothing_is_empty) {
  FlatJson j;
  CHECK_EQ(j.size(), 0);
  CHECK(!j.has("a"));
  CHECK(!j.has(""));
  int64_t v = 5;
  CHECK(!j.get_int("a", &v));
  CHECK_EQ(v, 5);
}

TEST(null_empty_and_oversize_input_rejected) {
  FlatJson j;
  CHECK(!j.parse(nullptr, 0));
  CHECK(!j.parse(nullptr, 5));
  CHECK(!j.parse("{}", 0));
  CHECK(!parses(""));
  CHECK(!parses(" "));
  std::string big = "{\"k\":\"" + std::string(FlatJson::kMaxLen, 'x') + "\"}";
  CHECK(!parses(big));
}

TEST(length_limit_is_exact) {
  // {"k":"xxxx"} padded to exactly kMaxLen bytes is accepted, one more is not.
  const size_t overhead = std::string("{\"k\":\"\"}").size();
  const std::string at_limit = "{\"k\":\"" + std::string(FlatJson::kMaxLen - overhead, 'x') + "\"}";
  CHECK_EQ(at_limit.size(), FlatJson::kMaxLen);
  CHECK(parses(at_limit));
  const std::string over = "{\"k\":\"" + std::string(FlatJson::kMaxLen - overhead + 1, 'x') + "\"}";
  CHECK_EQ(over.size(), FlatJson::kMaxLen + 1);
  CHECK(!parses(over));
}

TEST(only_the_given_length_is_read) {
  // Bytes after `n` must not matter, even if they would complete or break the object.
  const char* s = "{\"a\":1}garbage";
  FlatJson j;
  CHECK(j.parse(s, 7));
  CHECK(!j.parse(s, 8));
  CHECK(!j.parse(s, 6));
  const char* t = "{\"a\":12}";
  CHECK(!j.parse(t, 7));     // cut inside: {"a":12
}

TEST(integers) {
  CHECK_EQ(int_of("{\"a\":0}", "a"), 0);
  CHECK_EQ(int_of("{\"a\":-0}", "a"), 0);
  CHECK_EQ(int_of("{\"a\":7}", "a"), 7);
  CHECK_EQ(int_of("{\"a\":-7}", "a"), -7);
  CHECK_EQ(int_of("{\"a\":4294967295}", "a"), 4294967295LL);
  CHECK_EQ(int_of("{\"a\":9007199254740993}", "a"), 9007199254740993LL);
  CHECK_EQ(int_of("{\"a\":999999999999999999}", "a"), 999999999999999999LL);      // 18 digits
  CHECK_EQ(int_of("{\"a\":-999999999999999999}", "a"), -999999999999999999LL);
  CHECK_EQ(int_of("{ \"a\" : 12 }", "a"), 12);
}

TEST(numbers_that_are_valid_json_but_not_usable_integers) {
  // 19+ digits, floats and exponents parse (so an unknown key may carry them) but are not ints.
  const char* others[] = {"1000000000000000000", "99999999999999999999", "-1000000000000000000",
                          "1.5",  "-1.5",  "0.0",  "1e3",  "1E3",  "1e+3", "1e-3", "1.25e10", "0e0"};
  for (const char* v : others) {
    const std::string s = std::string("{\"a\":") + v + "}";
    FlatJson j;
    CHECK(j.parse(s.data(), s.size()));
    CHECK(j.has("a"));
    CHECK_EQ(int_of(s, "a"), -999999);
  }
}

TEST(numbers_cut_off_by_the_end_of_the_buffer) {
  // The datagram ends in the middle of a number: no reading past the end, and no object.
  const char* cut[] = {"{\"a\":1", "{\"a\":-1", "{\"a\":1.5", "{\"a\":1.", "{\"a\":1e5", "{\"a\":1e", "{\"a\":1e+",
                       "{\"a\":1.5e10", "{\"a\":-", "{\"a\":0"};
  for (const char* v : cut) {
    const std::string s = v;
    std::vector<char> exact(s.begin(), s.end());       // exact-size heap buffer for ASan
    FlatJson j;
    CHECK(!j.parse(exact.data(), exact.size()));
  }
}

TEST(malformed_numbers_rejected) {
  const char* bad[] = {"-", "+1", "01", "-01", "00", "1.", ".5", "1e", "1e+", "1e-", "1.e3", "--1",
                       "1-", "0x10", "1 2", "1_000", "Infinity", "NaN", "-Infinity"};
  for (const char* v : bad) {
    const std::string s = std::string("{\"a\":") + v + "}";
    CHECK(!parses(s));
  }
}

TEST(strings) {
  CHECK_STR(token_of("{\"a\":\"START\"}", "a"), "START");
  CHECK_STR(token_of("{\"a\":\"\"}", "a"), "");
  CHECK_STR(token_of("{\"a\":\"with space, comma: colon } brace\"}", "a"), "with space, comma: colon } brace");
  // high bytes are ordinary string content
  CHECK_STR(token_of("{\"a\":\"\xC5\x82\"}", "a"), "\xC5\x82");
}

TEST(escaped_strings_parse_but_are_not_tokens) {
  // An escape may hide a quote or backslash; such a string is skipped correctly
  // but never handed out as a protocol value.
  const char* escaped[] = {"{\"a\":\"x\\\"y\",\"b\":1}", "{\"a\":\"x\\\\\",\"b\":1}", "{\"a\":\"\\u0053TART\",\"b\":1}",
                           "{\"a\":\"\\n\",\"b\":1}"};
  for (const char* e : escaped) {
    const std::string s = e;
    FlatJson j;
    CHECK(j.parse(s.data(), s.size()));
    CHECK(j.has("a"));
    CHECK_STR(token_of(s, "a"), "<none>");
    CHECK_EQ(int_of(s, "b"), 1);
  }
}

TEST(malformed_strings_rejected) {
  CHECK(!parses("{\"a\":\"x}"));                 // unterminated
  CHECK(!parses("{\"a\":\"x\\\"}"));             // the closing quote is escaped
  CHECK(!parses("{\"a\":\"x\\"));                // ends in a backslash
  CHECK(!parses("{\"a\":'x'}"));
  CHECK(!parses(std::string("{\"a\":\"x\ny\"}")));        // raw control characters
  CHECK(!parses(std::string("{\"a\":\"x\ty\"}")));
  CHECK(!parses(std::string("{\"a\":\"x") + '\0' + "y\"}"));
  CHECK(!parses(std::string("{\"a\":\"x\x1Fy\"}")));
  CHECK(parses(std::string("{\"a\":\"x\x20y\"}")));       // space is the first allowed character
}

TEST(keys) {
  CHECK(parses("{\"\":1}"));                     // empty key is legal JSON
  CHECK(parses("{\"a b\":1}"));                  // a space is an ordinary key character
  CHECK_EQ(int_of("{\" \":7}", " "), 7);
  CHECK(!parses("{a:1}"));
  CHECK(!parses("{'a':1}"));
  CHECK(!parses("{\"a\\\"b\":1}"));              // keys never carry escapes
  CHECK(!parses(std::string("{\"a\nb\":1}")));
  CHECK(!parses("{\"a:1}"));
  CHECK(!parses("{\"a\" 1}"));
  CHECK(!parses("{\"a\"}"));
  CHECK(!parses("{\"a\":}"));
  CHECK(!parses("{:1}"));
  CHECK(!parses("{1:1}"));
}

TEST(lookup_matches_whole_keys_only) {
  const std::string s = "{\"ab\":1,\"a\":2,\"abc\":3}";
  CHECK_EQ(int_of(s, "a"), 2);
  CHECK_EQ(int_of(s, "ab"), 1);
  CHECK_EQ(int_of(s, "abc"), 3);
  CHECK_EQ(int_of(s, "abcd"), -999999);
  CHECK_EQ(int_of(s, ""), -999999);
  FlatJson j;
  CHECK(j.parse(s.data(), s.size()));
  CHECK(!j.has("b"));
  CHECK_EQ(j.size(), 3);
}

TEST(typed_getters_refuse_the_wrong_type) {
  const std::string s = "{\"s\":\"5\",\"i\":5,\"f\":5.0,\"t\":true,\"n\":null}";
  CHECK_EQ(int_of(s, "s"), -999999);
  CHECK_EQ(int_of(s, "i"), 5);
  CHECK_EQ(int_of(s, "f"), -999999);
  CHECK_EQ(int_of(s, "t"), -999999);
  CHECK_EQ(int_of(s, "n"), -999999);
  CHECK_EQ(int_of(s, "missing"), -999999);
  CHECK_STR(token_of(s, "s"), "5");
  CHECK_STR(token_of(s, "i"), "<none>");
  CHECK_STR(token_of(s, "t"), "<none>");
  CHECK_STR(token_of(s, "missing"), "<none>");
}

TEST(literals) {
  CHECK(parses("{\"a\":true,\"b\":false,\"c\":null}"));
  const char* bad[] = {"tru", "True", "TRUE", "fals", "nul", "truex", "nulll", "falsey", "t", "none", "undefined"};
  for (const char* v : bad) CHECK(!parses(std::string("{\"a\":") + v + "}"));
}

TEST(literals_cut_off_by_the_end_of_the_buffer) {
  // No reading past the end while comparing with "true" / "false" / "null".
  const char* cut[] = {"{\"a\":t", "{\"a\":tr", "{\"a\":tru", "{\"a\":true", "{\"a\":f", "{\"a\":fals", "{\"a\":false",
                       "{\"a\":n", "{\"a\":nul", "{\"a\":null", "{\"a\":", "{\"a\"", "{\"a", "{\"", "{"};
  for (const char* v : cut) {
    const std::string s = v;
    std::vector<char> exact(s.begin(), s.end());       // exact-size heap buffer for ASan
    FlatJson j;
    CHECK(!j.parse(exact.data(), exact.size()));
  }
}

TEST(strings_cut_off_by_the_end_of_the_buffer) {
  const char* cut[] = {"{\"a\":\"", "{\"a\":\"x", "{\"a\":\"x\\", "{\"a\":\"x\\\"", "{\"a\":\"x\"", "{\"a\":\"x\" ", "{\"a\":1 ", "{\"a\":1,",
                       "{\"a\":1, ", "{ "};
  for (const char* v : cut) {
    const std::string s = v;
    std::vector<char> exact(s.begin(), s.end());
    FlatJson j;
    CHECK(!j.parse(exact.data(), exact.size()));
  }
}

TEST(nested_values_rejected) {
  CHECK(!parses("{\"a\":{}}"));
  CHECK(!parses("{\"a\":{\"b\":1}}"));
  CHECK(!parses("{\"a\":[]}"));
  CHECK(!parses("{\"a\":[1,2]}"));
}

TEST(structure_errors_rejected) {
  const char* bad[] = {"{",           "}",            "{}}",          "{{}",           "[]",
                       "null",        "1",            "\"a\"",        "{\"a\":1",      "{\"a\":1,",
                       "{\"a\":1,}",  "{,\"a\":1}",   "{\"a\":1,,\"b\":2}",            "{\"a\":1 \"b\":2}",
                       "{\"a\":1;\"b\":2}",           "{\"a\":1} x",  "x{\"a\":1}",    "{\"a\":1}{\"b\":2}"};
  for (const char* v : bad) CHECK(!parses(v));
  CHECK(!parses(std::string("{\"a\":1}") + '\0'));         // a trailing NUL is garbage too
}

TEST(duplicate_keys_rejected) {
  CHECK(!parses("{\"a\":1,\"a\":1}"));
  CHECK(!parses("{\"a\":1,\"b\":2,\"a\":\"x\"}"));
  CHECK(parses("{\"a\":1,\"A\":1}"));            // keys are case-sensitive
}

TEST(key_count_limit_is_exact) {
  auto object = [](size_t keys) {
    std::string s = "{";
    for (size_t i = 0; i < keys; i++) s += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":" + std::to_string(i);
    return s + "}";
  };
  FlatJson j;
  const std::string full = object(FlatJson::kMaxFields);
  CHECK(j.parse(full.data(), full.size()));
  CHECK_EQ(j.size(), FlatJson::kMaxFields);
  CHECK_EQ(int_of(full, "k0"), 0);
  CHECK_EQ(int_of(full, "k15"), 15);
  CHECK(!parses(object(FlatJson::kMaxFields + 1)));
}

TEST(failed_parse_leaves_nothing_behind) {
  FlatJson j;
  CHECK(j.parse("{\"a\":1,\"b\":2}", 13));
  CHECK_EQ(j.size(), 2);
  CHECK(!j.parse("{\"c\":3,\"d\":", 11));
  CHECK_EQ(j.size(), 0);
  CHECK(!j.has("a"));
  CHECK(!j.has("c"));
  int64_t v = 77;
  CHECK(!j.get_int("c", &v));
  CHECK_EQ(v, 77);
}

TEST(reparse_replaces_previous_content) {
  FlatJson j;
  CHECK(j.parse("{\"a\":1}", 7));
  CHECK(j.parse("{\"b\":2}", 7));
  CHECK(!j.has("a"));
  CHECK(j.has("b"));
}

// Whatever bytes arrive, the scanner must stay inside the buffer (ASan/UBSan
// watch) and must agree with itself: a parse that succeeds exposes only keys
// that are really in the input.
TEST(fuzz_random_bytes_never_misbehave) {
  hhj_test::Rng rng(20261114);
  const char alphabet[] = "{}[]\":,-+.eE0123456789 \t\n\\truefalsn\"\"{{}}::,,ab";
  const int n = 200000;                       // cheap, and fewer would rarely hit a valid object
  int accepted = 0;
  for (int i = 0; i < n; i++) {
    const size_t len = rng.below(40);
    // exact-size heap buffer: any read past the end is an ASan error
    std::vector<char> buf(len);
    for (size_t k = 0; k < len; k++) {
      buf[k] = rng.chance(0.9) ? alphabet[rng.below(sizeof(alphabet) - 1)] : static_cast<char>(rng.below(256));
    }
    FlatJson j;
    if (j.parse(buf.data(), len)) {
      accepted++;
      CHECK(j.size() <= FlatJson::kMaxFields);
    } else {
      CHECK_EQ(j.size(), 0);
    }
  }
  CHECK(accepted > 0);     // the alphabet does produce valid objects sometimes
}

// Take valid objects and damage one byte: must never crash, and truncation must always be rejected.
TEST(fuzz_mutated_valid_objects) {
  hhj_test::Rng rng(7);
  const std::string seeds[] = {
      "{\"time_mark\":\"START\",\"age_ms\":12,\"t\":123456,\"boot\":3735928559,\"seq\":7,\"dir\":1,\"gap_ms\":38,\"n\":0}",
      "{\"hello\":\"TIMER\",\"boot\":77,\"up_ms\":5000,\"fw\":\"1.1.0\",\"st\":1,\"ms\":12340,\"rssi\":-55,\"net\":0}",
      "{\"ack\":\"FINISH\",\"boot\":0,\"seq\":4294967295}",
  };
  const int n = hhj_test::iterations(60000, 2000);
  for (int i = 0; i < n; i++) {
    std::string s = seeds[rng.below(3)];
    switch (rng.below(3)) {
      case 0: s[rng.below(static_cast<uint32_t>(s.size()))] = static_cast<char>(rng.below(256)); break;
      case 1: s.erase(rng.below(static_cast<uint32_t>(s.size())), 1); break;
      default: s.insert(rng.below(static_cast<uint32_t>(s.size())), 1, static_cast<char>(rng.below(128))); break;
    }
    std::vector<char> buf(s.begin(), s.end());
    FlatJson j;
    (void)j.parse(buf.data(), buf.size());
  }
  for (const std::string& seed : seeds) {
    for (size_t cut = 0; cut < seed.size(); cut++) CHECK(!parses(seed.substr(0, cut)));
    CHECK(parses(seed));
  }
}
