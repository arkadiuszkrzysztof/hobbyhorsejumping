// config.h — hhj-timer: how the 4-digit display [D][D][d][d] is wired and how
// the stopwatch behaves. 4x TPIC6B595 driven by a XIAO ESP32-C3.
//
// WiFi credentials are shared with the gates: hhj-common/include/hhj/wifi_config.h
// The UDP port (3004) is part of the protocol: hhj-common/PROTOCOL.md
#pragma once

// ------------------------------------------------------------------ PINS
#define PIN_DATA    D8              // -> (AHCT) -> pin 3 of the first TPIC (SER IN)
#define PIN_CLK     D9              // -> (AHCT) -> pin 13 of every TPIC (SRCK)
#define PIN_LATCH   D10             // -> (AHCT) -> pin 12 of every TPIC (RCK)

// 1 = the first TPIC in the chain (the one wired to the XIAO) drives the LEFT digit.
// Digits appear in reverse order? Change to 0.
#define CHAIN_FIRST_IS_LEFT 1

// ------------------------------------------------- SEGMENT -> TPIC OUTPUT
// DRAINn = bit n. TPIC pins: 4=DRAIN0, 5=DRAIN1, 6=DRAIN2, 7=DRAIN3, 14=DRAIN4, 15=DRAIN5, 16=DRAIN6.
// As soldered (large and small digits alike): pin 6=A, 7=B, 14=C, 15=D, 16=E, 5=F, 4=G.
#define SEG_A (1 << 2)
#define SEG_B (1 << 3)
#define SEG_C (1 << 4)
#define SEG_D (1 << 5)
#define SEG_E (1 << 6)
#define SEG_F (1 << 1)
#define SEG_G (1 << 0)

// ------------------------------------------------------------------ LOGIC
#define MIN_RUN_MS      1000        // a FINISH sooner than 1 s after START is ignored
#define RUN_TIMEOUT_MS  600000      // 10 min without FINISH = back to ready
#define RESULT_BLINK_MS 3000        // how long the result blinks after FINISH
