// wifi_config.h — WiFi credentials for every firmware in this repo.
//
// PRIMARY  = the competition router. Tried first, and the one the devices keep
//            looking for when nothing else works (see wifi_plan.h).
// FALLBACK = a second network (home / workshop). Tried once, right after the
//            first PRIMARY attempt fails.
//
// The fallback is private, so it does not live in git: copy
// wifi_secrets.example.h to wifi_secrets.h (git-ignored) and fill it in.
// Without that file the firmware simply has no fallback.
//
// Everything here can also be overridden per build with -D in platformio.ini.
#pragma once

#if defined(__has_include)
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif
#endif

#ifndef HHJ_WIFI_PRIMARY_SSID
#define HHJ_WIFI_PRIMARY_SSID "NotForSquares"
#endif
#ifndef HHJ_WIFI_PRIMARY_PASS
#define HHJ_WIFI_PRIMARY_PASS "nfsWiFiPass"
#endif

#ifndef HHJ_WIFI_FALLBACK_SSID
#define HHJ_WIFI_FALLBACK_SSID ""          // empty = no fallback network
#endif
#ifndef HHJ_WIFI_FALLBACK_PASS
#define HHJ_WIFI_FALLBACK_PASS ""
#endif

#ifndef HHJ_WIFI_ATTEMPT_MS
#define HHJ_WIFI_ATTEMPT_MS 10000          // how long one connection attempt may take
#endif
