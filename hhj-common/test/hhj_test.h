// hhj_test.h — tiny host test harness (no dependencies). One .cpp = one binary.
//
//   TEST(name) { CHECK(x); CHECK_EQ(a, b); }
//
// Environment:
//   HHJ_TEST_QUICK=1    fewer random iterations (used by the mutation run)
//   HHJ_TEST_VERBOSE=1  print each test name
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hhj_test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& cases() {
  static std::vector<Case> v;
  return v;
}
inline int& failures() {
  static int n = 0;
  return n;
}
inline long& checks() {
  static long n = 0;
  return n;
}

struct Reg {
  Reg(const char* name, void (*fn)()) { cases().push_back(Case{name, fn}); }
};

inline bool quick() { return getenv("HHJ_TEST_QUICK") != nullptr; }
// Number of iterations for a randomized test: `full` normally, `fast` in quick mode.
inline int iterations(int full, int fast) { return quick() ? fast : full; }

inline void fail(const char* file, int line, const char* expr) {
  if (failures() < 25) printf("FAIL %s:%d: %s\n", file, line, expr);
  failures()++;
}
inline void fail_eq(const char* file, int line, const char* a, const char* b, long long va, long long vb) {
  if (failures() < 25) printf("FAIL %s:%d: %s == %s  (%lld vs %lld)\n", file, line, a, b, va, vb);
  failures()++;
}

// Deterministic generator (splitmix64): same seed, same test, on every machine.
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  // uniform in [0, n)
  uint32_t below(uint32_t n) { return n == 0 ? 0 : static_cast<uint32_t>(next() % n); }
  // uniform in [lo, hi]
  int64_t range(int64_t lo, int64_t hi) {
    return lo + static_cast<int64_t>(next() % static_cast<uint64_t>(hi - lo + 1));
  }
  // true with probability p (0..1)
  bool chance(double p) { return static_cast<double>(next() >> 11) / 9007199254740992.0 < p; }
};

}  // namespace hhj_test

#define TEST(name)                                        \
  static void name();                                     \
  static hhj_test::Reg hhj_reg_##name(#name, name);       \
  static void name()

#define CHECK(cond)                                                    \
  do {                                                                 \
    hhj_test::checks()++;                                              \
    if (!(cond)) hhj_test::fail(__FILE__, __LINE__, #cond);            \
  } while (0)

// Integer equality with both values printed on failure.
#define CHECK_EQ(a, b)                                                             \
  do {                                                                             \
    hhj_test::checks()++;                                                          \
    const long long hhj_va = static_cast<long long>(a);                            \
    const long long hhj_vb = static_cast<long long>(b);                            \
    if (hhj_va != hhj_vb) hhj_test::fail_eq(__FILE__, __LINE__, #a, #b, hhj_va, hhj_vb); \
  } while (0)

#define CHECK_STR(a, b)                                                            \
  do {                                                                             \
    hhj_test::checks()++;                                                          \
    const std::string hhj_sa = (a);                                                \
    const std::string hhj_sb = (b);                                                \
    if (hhj_sa != hhj_sb) {                                                        \
      if (hhj_test::failures() < 25)                                               \
        printf("FAIL %s:%d: %s == %s\n   got  \"%s\"\n   want \"%s\"\n", __FILE__, __LINE__, #a, #b, \
               hhj_sa.c_str(), hhj_sb.c_str());                                    \
      hhj_test::failures()++;                                                      \
    }                                                                              \
  } while (0)

int main() {
  const bool verbose = getenv("HHJ_TEST_VERBOSE") != nullptr;
  int failed_cases = 0;
  for (const hhj_test::Case& c : hhj_test::cases()) {
    const int before = hhj_test::failures();
    c.fn();
    const bool ok = hhj_test::failures() == before;
    if (!ok) failed_cases++;
    if (verbose || !ok) printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
  }
  const size_t total = hhj_test::cases().size();
  if (failed_cases == 0) {
    printf("PASS  %zu tests, %ld checks\n", total, hhj_test::checks());
    return 0;
  }
  printf("FAILED  %d of %zu tests (%d failed checks)\n", failed_cases, total, hhj_test::failures());
  return 1;
}
