// display.h — what the four 7-segment digits show (pure C++, no Arduino).
//
// A glyph is one byte for one TPIC6B595: bit n drives DRAINn. Which bit lights
// which segment is the wiring, described by a Segments value (config.h).
//
//   D-1  Digits 0-9 light exactly the standard 7-segment pattern.
//   D-2  Up to 99.99 s the frame is  S S h h  (seconds, hundredths); the tens
//        of seconds are blank below 10 s. Time is truncated, never rounded up.
//   D-3  From 100 s on the frame is  M M S S  (minutes, seconds), tens of
//        minutes blank below 10 min, saturating at 99:59.
//   D-4  Negative times show as 0.00.
//   D-5  A shift-register chain is filled last-chip-first: the byte shifted
//        out first ends up in the chip farthest from the microcontroller.
//
// Host tests: test/test_display.cpp
#pragma once
#include <stdint.h>

namespace hhj {

// Bit mask of each segment within a glyph byte.
struct Segments {
  uint8_t a, b, c, d, e, f, g;
};

// Four glyphs, index 0 = leftmost digit.
struct Frame {
  uint8_t d[4];
  bool operator==(const Frame& o) const { return d[0] == o.d[0] && d[1] == o.d[1] && d[2] == o.d[2] && d[3] == o.d[3]; }
  bool operator!=(const Frame& o) const { return !(*this == o); }
};

constexpr uint8_t kBlank = 0;

inline uint8_t glyph_digit(const Segments& s, unsigned digit) {      // D-1
  switch (digit) {
    case 0: return static_cast<uint8_t>(s.a | s.b | s.c | s.d | s.e | s.f);
    case 1: return static_cast<uint8_t>(s.b | s.c);
    case 2: return static_cast<uint8_t>(s.a | s.b | s.d | s.e | s.g);
    case 3: return static_cast<uint8_t>(s.a | s.b | s.c | s.d | s.g);
    case 4: return static_cast<uint8_t>(s.b | s.c | s.f | s.g);
    case 5: return static_cast<uint8_t>(s.a | s.c | s.d | s.f | s.g);
    case 6: return static_cast<uint8_t>(s.a | s.c | s.d | s.e | s.f | s.g);
    case 7: return static_cast<uint8_t>(s.a | s.b | s.c);
    case 8: return static_cast<uint8_t>(s.a | s.b | s.c | s.d | s.e | s.f | s.g);
    case 9: return static_cast<uint8_t>(s.a | s.b | s.c | s.d | s.f | s.g);
    default: return kBlank;
  }
}

inline uint8_t glyph_dash(const Segments& s) { return s.g; }   // middle bar
inline uint8_t glyph_low(const Segments& s) { return s.d; }    // bottom bar
inline uint8_t glyph_high(const Segments& s) { return s.a; }   // top bar

inline Frame frame_fill(uint8_t glyph) { return Frame{{glyph, glyph, glyph, glyph}}; }

inline Frame frame_time(const Segments& s, int64_t ms) {
  if (ms < 0) ms = 0;                                               // D-4
  const int64_t cs = ms / 10;                                       // D-2: truncate
  if (cs < 10000) {
    const unsigned sec = static_cast<unsigned>(cs / 100);
    const unsigned hun = static_cast<unsigned>(cs % 100);
    return Frame{{sec >= 10 ? glyph_digit(s, sec / 10) : kBlank, glyph_digit(s, sec % 10),
                  glyph_digit(s, hun / 10), glyph_digit(s, hun % 10)}};
  }
  int64_t total = ms / 1000;                                        // D-3
  if (total > 99 * 60 + 59) total = 99 * 60 + 59;
  const unsigned min = static_cast<unsigned>(total / 60);
  const unsigned sec = static_cast<unsigned>(total % 60);
  return Frame{{min >= 10 ? glyph_digit(s, min / 10) : kBlank, glyph_digit(s, min % 10),
                glyph_digit(s, sec / 10), glyph_digit(s, sec % 10)}};
}

// The four bytes in the order they must be shifted out (D-5). `out` holds 4 bytes.
// first_is_left: the chip wired to the microcontroller drives the LEFT digit.
inline void frame_to_chain(const Frame& f, bool first_is_left, uint8_t* out) {
  for (int i = 0; i < 4; i++) out[i] = first_is_left ? f.d[3 - i] : f.d[i];
}

}  // namespace hhj
