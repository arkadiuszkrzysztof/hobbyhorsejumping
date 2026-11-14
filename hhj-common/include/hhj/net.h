// net.h — what gates and the timer need from the outside world to talk, plus
// the small pieces of link housekeeping both share (pure C++).
//
//   NetHal   the hardware / OS boundary: clock, WiFi, one UDP socket, a log line.
//            Arduino implements it in the firmware; tests and the host simulator
//            implement it with fakes. Nothing behind it is unit-tested, so it
//            has to stay trivially thin.
//   Uplink   drives the WifiPlan through a NetHal and reports link transitions.
//   Peers    the unicast targets a node has learned from HELLO datagrams.
//
// Host tests: hhj-common/test/test_net.cpp
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "link.h"
#include "wifi_plan.h"

namespace hhj {

// IPv4 addresses are opaque uint32 values exactly as the HAL hands them out;
// 0 means "the subnet broadcast address" when sending and "unknown" otherwise.
constexpr uint32_t kBroadcast = 0;

class NetHal {
 public:
  // Milliseconds since power-up. Must never go backwards and never wrap.
  virtual int64_t now_ms() = 0;

  // Associated to an access point and holding an IP address.
  virtual bool wifi_up() = 0;
  // Start (or restart) associating with the given network. Must not block.
  virtual void wifi_connect(WifiPlan::Net net) = 0;

  // (Re)open the UDP socket on kLinkPort. Called every time the link comes up.
  virtual void net_open() = 0;
  // Send one datagram to `ip` (kBroadcast = subnet broadcast), port kLinkPort.
  virtual bool net_send(uint32_t ip, const char* data, size_t len) = 0;
  // Fetch one received datagram: returns its length (0 = nothing waiting,
  // anything longer than `cap` is dropped) and the sender in `*from_ip`.
  virtual size_t net_recv(char* buf, size_t cap, uint32_t* from_ip) = 0;

  virtual int rssi() = 0;

  // One complete log line, without the newline.
  virtual void log(const char* line) = 0;

 protected:
  ~NetHal() = default;
};

// "[ ms since boot] text" — every log line of every node has this shape.
#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
inline void logf(NetHal& hal, int64_t t_ms, const char* fmt, ...) {
  char line[224];
  // The prefix is at most 23 characters ("[" + 20 for an int64 + "] "), so it always fits.
  const size_t n = static_cast<size_t>(snprintf(line, sizeof(line), "[%7lld] ", static_cast<long long>(t_ms)));
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);
  hal.log(line);
}

inline uint32_t clamp_u32(int64_t v) {
  if (v < 0) return 0;
  return v > 4294967295LL ? 4294967295u : static_cast<uint32_t>(v);
}

inline int8_t clamp_rssi(int v) {
  if (v < -128) return -128;
  if (v > 127) return 127;
  return static_cast<int8_t>(v);
}

// ------------------------------------------------------------------ Uplink
class Uplink {
 public:
  Uplink(NetHal& hal, const WifiPlanConfig& cfg) : hal_(hal), plan_(cfg) {}

  // Call once per network tick. Returns true while datagrams can flow.
  bool tick(int64_t now) {
    const bool up = hal_.wifi_up();
    const WifiPlan::Action act = plan_.update(up, now);
    came_up_ = false;
    if (up != was_up_) {
      if (up) {
        came_up_ = true;
        hal_.net_open();
        logf(hal_, now, "WiFi connected (%s network), rssi %d dBm", wifi_net_name(plan_.net()), hal_.rssi());
      } else {
        logf(hal_, now, "WiFi lost");
      }
      was_up_ = up;
    }
    if (act.connect) {
      logf(hal_, now, "WiFi: trying %s network%s", wifi_net_name(act.net),
           plan_.phase() == WifiPlan::Phase::SeekPrimary ? " (will keep trying it)" : "");
      hal_.wifi_connect(act.net);
    }
    return up;
  }

  bool up() const { return was_up_; }
  bool came_up() const { return came_up_; }            // true only in the tick the link came up
  WifiPlan::Net net() const { return plan_.net(); }
  const WifiPlan& plan() const { return plan_; }

 private:
  NetHal& hal_;
  WifiPlan plan_;
  bool was_up_ = false;
  bool came_up_ = false;
};

// ------------------------------------------------------------------- Peers
struct Peer {
  uint32_t ip = 0;
  Role role = Role::None;
  int64_t seen_ms = 0;
  bool used = false;
};

class Peers {
 public:
  static constexpr size_t kMax = 4;

  // Record that `ip` announced itself as `role`. Returns true if it is new.
  bool touch(uint32_t ip, Role role, int64_t now) {
    if (ip == kBroadcast || role == Role::None) return false;
    Peer* slot = nullptr;
    for (Peer& p : peers_) {
      if (p.used && p.ip == ip) {
        p.role = role;
        p.seen_ms = now;
        return false;
      }
      if (!p.used && slot == nullptr) slot = &p;
    }
    if (slot == nullptr) {                       // full: the longest-silent peer makes room
      for (Peer& p : peers_) {
        if (slot == nullptr || p.seen_ms < slot->seen_ms) slot = &p;
      }
    }
    slot->used = true;
    slot->ip = ip;
    slot->role = role;
    slot->seen_ms = now;
    return true;
  }

  // Forget peers silent for longer than `ttl_ms`. Returns how many were dropped.
  size_t expire(int64_t now, uint32_t ttl_ms) {
    size_t dropped = 0;
    for (Peer& p : peers_) {
      if (p.used && now - p.seen_ms > static_cast<int64_t>(ttl_ms)) {
        p.used = false;
        dropped++;
      }
    }
    return dropped;
  }

  void clear() {
    for (Peer& p : peers_) p = Peer{};
  }

  size_t count() const {
    size_t n = 0;
    for (const Peer& p : peers_) n += p.used ? 1u : 0u;
    return n;
  }

  bool has_role(Role role) const {
    for (const Peer& p : peers_) {
      if (p.used && p.role == role) return true;
    }
    return false;
  }

  // Slot access for iteration: check `.used`.
  const Peer& slot(size_t i) const { return peers_[i]; }

 private:
  Peer peers_[kMax];
};

}  // namespace hhj
