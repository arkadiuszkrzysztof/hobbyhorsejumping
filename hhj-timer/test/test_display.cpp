// Tests for display.h and for the wiring in config.h — requirement ids (D-n)
// refer to display.h.
#include <set>
#include <string>

#include "config.h"
#include "display.h"
#include "hhj_test.h"

using namespace hhj;

namespace {

// An abstract wiring where each segment is its own letter's bit: a=1, b=2, ... g=64.
const Segments kAbstract{1, 2, 4, 8, 16, 32, 64};
// The wiring of the real board.
const Segments kBoard{SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G};

// The textbook 7-segment font, written independently of display.h.
const char* const kFont[10] = {"abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg"};

uint8_t from_letters(const Segments& s, const char* letters) {
  uint8_t g = 0;
  for (; *letters != '\0'; letters++) {
    switch (*letters) {
      case 'a': g = static_cast<uint8_t>(g | s.a); break;
      case 'b': g = static_cast<uint8_t>(g | s.b); break;
      case 'c': g = static_cast<uint8_t>(g | s.c); break;
      case 'd': g = static_cast<uint8_t>(g | s.d); break;
      case 'e': g = static_cast<uint8_t>(g | s.e); break;
      case 'f': g = static_cast<uint8_t>(g | s.f); break;
      case 'g': g = static_cast<uint8_t>(g | s.g); break;
      default: break;
    }
  }
  return g;
}

char to_char(const Segments& s, uint8_t g) {
  for (unsigned d = 0; d < 10; d++) {
    if (g == from_letters(s, kFont[d])) return static_cast<char>('0' + d);
  }
  return g == kBlank ? ' ' : '?';
}

std::string text(const Segments& s, int64_t ms) {
  const Frame f = frame_time(s, ms);
  std::string out;
  for (uint8_t g : f.d) out += to_char(s, g);
  return out;
}

}  // namespace

TEST(d1_digits_light_exactly_the_standard_segments) {
  for (const Segments& s : {kAbstract, kBoard}) {
    for (unsigned d = 0; d < 10; d++) CHECK_EQ(glyph_digit(s, d), from_letters(s, kFont[d]));
  }
}

TEST(d1_not_a_digit_is_blank) {
  CHECK_EQ(glyph_digit(kBoard, 10), kBlank);
  CHECK_EQ(glyph_digit(kBoard, 255), kBlank);
  CHECK_EQ(glyph_digit(kBoard, 4294967295u), kBlank);
}

TEST(d1_all_digits_look_different) {
  std::set<uint8_t> seen;
  for (unsigned d = 0; d < 10; d++) seen.insert(glyph_digit(kBoard, d));
  seen.insert(kBlank);
  seen.insert(glyph_dash(kBoard));
  seen.insert(glyph_low(kBoard));
  seen.insert(glyph_high(kBoard));
  CHECK_EQ(seen.size(), 14);
}

TEST(status_glyphs_are_single_bars) {
  CHECK_EQ(glyph_dash(kAbstract), 64);          // g: middle
  CHECK_EQ(glyph_low(kAbstract), 8);            // d: bottom
  CHECK_EQ(glyph_high(kAbstract), 1);           // a: top
  CHECK_EQ(kBlank, 0);
}

TEST(board_wiring_matches_the_soldering_notes) {
  // config.h: TPIC pin 6=A, 7=B, 14=C, 15=D, 16=E, 5=F, 4=G; pins 4,5,6,7,14,15,16 = DRAIN0..6.
  CHECK_EQ(SEG_G, 1 << 0);
  CHECK_EQ(SEG_F, 1 << 1);
  CHECK_EQ(SEG_A, 1 << 2);
  CHECK_EQ(SEG_B, 1 << 3);
  CHECK_EQ(SEG_C, 1 << 4);
  CHECK_EQ(SEG_D, 1 << 5);
  CHECK_EQ(SEG_E, 1 << 6);
  // seven different outputs, DRAIN7 unused
  CHECK_EQ(SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, 0x7F);
  CHECK_EQ(SEG_A + SEG_B + SEG_C + SEG_D + SEG_E + SEG_F + SEG_G, 0x7F);
  for (unsigned d = 0; d < 10; d++) CHECK_EQ(glyph_digit(kBoard, d) & 0x80, 0);
  CHECK(CHAIN_FIRST_IS_LEFT == 0 || CHAIN_FIRST_IS_LEFT == 1);
}

TEST(d2_seconds_and_hundredths) {
  CHECK_STR(text(kBoard, 0), " 000");
  CHECK_STR(text(kBoard, 10), " 001");
  CHECK_STR(text(kBoard, 120), " 012");
  CHECK_STR(text(kBoard, 990), " 099");
  CHECK_STR(text(kBoard, 1000), " 100");
  CHECK_STR(text(kBoard, 9990), " 999");
  CHECK_STR(text(kBoard, 10000), "1000");       // tens of seconds appear at 10 s
  CHECK_STR(text(kBoard, 12340), "1234");
  CHECK_STR(text(kBoard, 59870), "5987");
  CHECK_STR(text(kBoard, 99990), "9999");
}

TEST(d2_time_is_truncated_never_rounded_up) {
  CHECK_STR(text(kBoard, 9), " 000");
  CHECK_STR(text(kBoard, 19), " 001");
  CHECK_STR(text(kBoard, 999), " 099");
  CHECK_STR(text(kBoard, 12349), "1234");
  CHECK_STR(text(kBoard, 99999), "9999");
}

TEST(d3_minutes_and_seconds_from_100_seconds) {
  CHECK_STR(text(kBoard, 100000), " 140");      // 1:40
  CHECK_STR(text(kBoard, 100999), " 140");
  CHECK_STR(text(kBoard, 101000), " 141");
  CHECK_STR(text(kBoard, 119999), " 159");
  CHECK_STR(text(kBoard, 120000), " 200");
  CHECK_STR(text(kBoard, 599999), " 959");
  CHECK_STR(text(kBoard, 600000), "1000");      // 10:00
  CHECK_STR(text(kBoard, 3599999), "5959");
  CHECK_STR(text(kBoard, 5999000), "9959");     // 99:59
}

TEST(d3_every_second_around_the_top_of_the_range) {
  // From 96:40 to 101:40, second by second, against the rule itself.
  for (int64_t total = 5800; total <= 6100; total++) {
    const int64_t shown = total > 5999 ? 5999 : total;
    char want[8];
    snprintf(want, sizeof(want), "%02d%02d", static_cast<int>(shown / 60), static_cast<int>(shown % 60));
    CHECK_STR(text(kBoard, total * 1000), want);
    CHECK_STR(text(kBoard, total * 1000 + 999), want);
  }
  CHECK_STR(text(kBoard, 5999 * 1000), "9959");
  CHECK_STR(text(kBoard, 5998 * 1000), "9958");
  CHECK_STR(text(kBoard, 5940 * 1000), "9900");
  CHECK_STR(text(kBoard, 5939 * 1000), "9859");
}

TEST(d3_saturates_at_99_59) {
  CHECK_STR(text(kBoard, 5999999), "9959");
  CHECK_STR(text(kBoard, 6000000), "9959");     // 100:00 does not fit
  CHECK_STR(text(kBoard, 86400000), "9959");
  CHECK_STR(text(kBoard, 9223372036854775807LL), "9959");
}

TEST(d4_negative_time_shows_zero) {
  CHECK_STR(text(kBoard, -1), " 000");
  CHECK_STR(text(kBoard, -123456), " 000");
  CHECK_STR(text(kBoard, -9223372036854775807LL - 1), " 000");
}

TEST(d2_d3_every_value_reads_back_correctly) {
  // Every millisecond up to 11 minutes: read the digits back like a spectator would.
  const int failures_before = hhj_test::failures();
  for (int64_t ms = 0; ms < 660000; ms++) {
    const std::string t = text(kBoard, ms);
    int v = 0;
    for (char c : t) {
      CHECK(c == ' ' || (c >= '0' && c <= '9'));
      v = v * 10 + (c == ' ' ? 0 : c - '0');
    }
    if (ms < 100000) {
      CHECK_EQ(v, ms / 10);                                      // SShh
      CHECK_EQ(t[0] == ' ', ms < 10000);
    } else {
      CHECK_EQ(v, (ms / 60000) * 100 + (ms / 1000) % 60);        // MMSS
      CHECK_EQ(t[0] == ' ', ms < 600000);
    }
    CHECK(t[1] != ' ' && t[2] != ' ' && t[3] != ' ');
    if (hhj_test::failures() > failures_before) return;
  }
}

TEST(frame_comparison_looks_at_each_digit_in_its_own_place) {
  const Frame f{{0x01, 0x02, 0x04, 0x08}};      // four different glyphs
  CHECK(f == Frame({{0x01, 0x02, 0x04, 0x08}}));
  CHECK(!(f != Frame({{0x01, 0x02, 0x04, 0x08}})));
  // the same glyphs in another order are a different frame
  CHECK(f != Frame({{0x02, 0x01, 0x04, 0x08}}));
  CHECK(f != Frame({{0x01, 0x04, 0x02, 0x08}}));
  CHECK(f != Frame({{0x01, 0x02, 0x08, 0x04}}));
  CHECK(f != Frame({{0x08, 0x02, 0x04, 0x01}}));
  CHECK(!(f == Frame({{0x01, 0x01, 0x04, 0x08}})));
  CHECK(!(f == Frame({{0x01, 0x02, 0x02, 0x08}})));
  CHECK(!(f == Frame({{0x01, 0x02, 0x04, 0x04}})));
  CHECK(!(f == Frame({{0x02, 0x02, 0x04, 0x08}})));
}

TEST(frame_fill_and_comparison) {
  const Frame eights = frame_fill(glyph_digit(kBoard, 8));
  for (uint8_t g : eights.d) CHECK_EQ(g, 0x7F);
  CHECK(eights == frame_fill(0x7F));
  CHECK(!(eights != frame_fill(0x7F)));
  for (int i = 0; i < 4; i++) {                 // a difference in any single digit is noticed
    Frame other = eights;
    other.d[i] = 0;
    CHECK(other != eights);
    CHECK(!(other == eights));
  }
}

TEST(d5_chain_order) {
  const Frame f{{0x11, 0x22, 0x33, 0x44}};      // left ... right
  uint8_t out[4];
  frame_to_chain(f, true, out);                 // first chip = left digit: its byte is shifted out LAST
  CHECK_EQ(out[0], 0x44);
  CHECK_EQ(out[1], 0x33);
  CHECK_EQ(out[2], 0x22);
  CHECK_EQ(out[3], 0x11);
  frame_to_chain(f, false, out);                // first chip = right digit
  CHECK_EQ(out[0], 0x11);
  CHECK_EQ(out[1], 0x22);
  CHECK_EQ(out[2], 0x33);
  CHECK_EQ(out[3], 0x44);
}

// Shift the bytes through a model of four chained 8-bit registers and check
// which register ends up with which digit.
TEST(d5_chain_order_against_a_shift_register_model) {
  const Frame f{{glyph_digit(kBoard, 1), glyph_digit(kBoard, 2), glyph_digit(kBoard, 3), glyph_digit(kBoard, 4)}};
  for (bool first_is_left : {true, false}) {
    uint8_t out[4];
    frame_to_chain(f, first_is_left, out);
    uint8_t reg[4] = {0, 0, 0, 0};              // reg[0] = chip wired to the microcontroller
    for (int byte = 0; byte < 4; byte++) {
      for (int bit = 7; bit >= 0; bit--) {      // MSB first, like shiftOut(MSBFIRST)
        const int in = (out[byte] >> bit) & 1;
        for (int chip = 3; chip > 0; chip--) reg[chip] = static_cast<uint8_t>((reg[chip] << 1) | (reg[chip - 1] >> 7));
        reg[0] = static_cast<uint8_t>((reg[0] << 1) | in);
      }
    }
    for (int digit = 0; digit < 4; digit++) {
      const int chip = first_is_left ? digit : 3 - digit;       // which chip drives this digit
      CHECK_EQ(reg[chip], f.d[digit]);
    }
  }
}
