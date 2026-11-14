// sim.h — a simulated WiFi network and fake hardware for host tests.
//
//   World   true time in ms, the access points on air, the packet network
//           (delay, loss, duplication, DTIM hold for broadcasts).
//   Node    one device on that network: its own clock (offset + drift), its
//           WiFi association, its inbox, its log.
//   NodeHal<Hal>  a Node that implements one of the firmware HAL interfaces.
//   Probe   a bare node for tests to inject and capture datagrams.
//   ModelBackend  what hhj-backend does on the wire, per PROTOCOL.md.
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "hhj/link.h"
#include "hhj/net.h"
#include "hhj_test.h"

namespace sim {

using hhj::WifiPlan;

// First octet in the low byte, like Arduino's IPAddress -> uint32_t.
constexpr uint32_t ip(uint8_t last) {
  return 192u | (168u << 8) | (10u << 16) | (static_cast<uint32_t>(last) << 24);
}

struct NetCfg {
  int64_t unicast_min_ms = 1, unicast_max_ms = 3;
  int64_t broadcast_min_ms = 1, broadcast_max_ms = 3;
  double unicast_loss = 0.0;
  double broadcast_loss = 0.0;
  double duplicate = 0.0;      // probability a delivered datagram arrives twice
  int64_t dtim_ms = 0;         // > 0: the AP releases broadcasts only at multiples of this
};

struct Datagram {
  int64_t at;                  // true time of arrival
  uint32_t from;
  std::string data;
  bool broadcast = false;      // it was sent to the broadcast address
};

class Node;

class World {
 public:
  explicit World(uint64_t seed = 1) : rng(seed) {}

  int64_t t = 0;                       // true time, ms
  NetCfg net;
  bool ap[2] = {true, false};          // access points on air: [primary, fallback]
  hhj_test::Rng rng;
  std::vector<Node*> nodes;
  long delivered = 0, lost = 0;

  inline void step();                  // advance one millisecond
  template <class F>
  void run(int64_t ms, F each_ms) {
    for (int64_t i = 0; i < ms; i++) {
      step();
      each_ms();
    }
  }
  inline void send(Node& from, uint32_t to_ip, const std::string& data);

 private:
  inline void schedule(Node& to, uint32_t from_ip, const std::string& data, bool broadcast);
};

class Node {
 public:
  Node(World& w, uint32_t address) : world(w), ip(address) { world.nodes.push_back(this); }

  World& world;
  uint32_t ip;

  // ---- knobs
  int64_t clock_offset_ms = 0;         // this device booted `offset` ms before/after the world started
  int64_t drift_ppm = 0;               // crystal error
  int64_t connect_delay_ms = 50;       // association + DHCP
  int rssi_dbm = -50;
  bool radio_ok = true;                // false: sends fail, as if the stack refused them
  bool in_range = true;                // false: this device cannot reach any access point

  // ---- observations
  std::vector<std::string> logs;
  std::vector<WifiPlan::Net> connect_calls;
  int opens = 0;
  long sent = 0;
  long recv_calls = 0;
  std::deque<Datagram> inbox;

  bool logged(const char* needle) const { return count_logged(needle) > 0; }
  int count_logged(const char* needle) const {
    int n = 0;
    for (const std::string& l : logs) n += l.find(needle) != std::string::npos ? 1 : 0;
    return n;
  }

  // ---- behaviour behind the HAL
  int64_t node_now() const { return clock_offset_ms + world.t + world.t * drift_ppm / 1000000; }

  bool node_wifi_up() const {
    return in_range && associating_ && world.ap[static_cast<int>(target_)] && world.t >= assoc_at_;
  }
  // -1 when not connected, else 0 / 1
  int network() const { return node_wifi_up() ? static_cast<int>(target_) : -1; }

  void node_wifi_connect(WifiPlan::Net net) {
    connect_calls.push_back(net);
    target_ = net;
    associating_ = in_range && world.ap[static_cast<int>(net)];   // no AP: this attempt goes nowhere
    assoc_at_ = world.t + connect_delay_ms;
  }

  void node_open() { opens++; }

  bool node_send(uint32_t to_ip, const char* data, size_t len) {
    if (!radio_ok || !node_wifi_up()) return false;
    sent++;
    world.send(*this, to_ip, std::string(data, len));
    return true;
  }

  size_t node_recv(char* buf, size_t cap, uint32_t* from_ip) {
    recv_calls++;
    while (!inbox.empty() && inbox.front().at <= world.t) {
      const Datagram d = inbox.front();
      inbox.pop_front();
      if (d.data.size() > cap) continue;                  // too long for the caller: dropped
      memcpy(buf, d.data.data(), d.data.size());
      *from_ip = d.from;
      return d.data.size();
    }
    return 0;
  }

  // The AP vanished under us: the association is gone until the next connect.
  void link_check() {
    if (associating_ && !(in_range && world.ap[static_cast<int>(target_)])) associating_ = false;
    if (!node_wifi_up()) {                                // a down radio hears nothing
      while (!inbox.empty() && inbox.front().at <= world.t) inbox.pop_front();
    }
  }

 private:
  bool associating_ = false;
  WifiPlan::Net target_ = WifiPlan::Net::Primary;
  int64_t assoc_at_ = 0;
};

inline void World::step() {
  t++;
  for (Node* n : nodes) n->link_check();
}

inline void World::schedule(Node& to, uint32_t from_ip, const std::string& data, bool broadcast) {
  if (rng.chance(broadcast ? net.broadcast_loss : net.unicast_loss)) {
    lost++;
    return;
  }
  const int copies = rng.chance(net.duplicate) ? 2 : 1;
  for (int c = 0; c < copies; c++) {
    int64_t at = t + (broadcast ? rng.range(net.broadcast_min_ms, net.broadcast_max_ms)
                                : rng.range(net.unicast_min_ms, net.unicast_max_ms));
    if (broadcast && net.dtim_ms > 0) at = ((at + net.dtim_ms - 1) / net.dtim_ms) * net.dtim_ms;
    Datagram d{at, from_ip, data, broadcast};
    auto pos = std::upper_bound(to.inbox.begin(), to.inbox.end(), d,
                                [](const Datagram& x, const Datagram& y) { return x.at < y.at; });
    to.inbox.insert(pos, d);
    delivered++;
  }
}

inline void World::send(Node& from, uint32_t to_ip, const std::string& data) {
  for (Node* n : nodes) {
    if (n == &from) continue;
    if (n->network() < 0 || n->network() != from.network()) continue;   // other SSID or offline
    if (to_ip == hhj::kBroadcast) {
      schedule(*n, from.ip, data, true);
    } else if (n->ip == to_ip) {
      schedule(*n, from.ip, data, false);
    }
  }
}

// A Node that is also one of the firmware HAL interfaces.
template <class Hal>
class NodeHal : public Hal, public Node {
 public:
  NodeHal(World& w, uint32_t address) : Node(w, address) {}
  int64_t now_ms() override { return node_now(); }
  bool wifi_up() override { return node_wifi_up(); }
  void wifi_connect(WifiPlan::Net net) override { node_wifi_connect(net); }
  void net_open() override { node_open(); }
  bool net_send(uint32_t to_ip, const char* data, size_t len) override { return node_send(to_ip, data, len); }
  size_t net_recv(char* buf, size_t cap, uint32_t* from_ip) override { return node_recv(buf, cap, from_ip); }
  int rssi() override { return rssi_dbm; }
  void log(const char* line) override {
    logs.emplace_back(line);
    if (getenv("HHJ_TEST_LOG") != nullptr) printf("   %08x %s\n", static_cast<unsigned>(ip), line);
  }
};

// A bare device: joins a network when told to, sends what the test gives it,
// keeps what it receives.
class Probe final : public NodeHal<hhj::NetHal> {
 public:
  Probe(World& w, uint32_t address) : NodeHal<hhj::NetHal>(w, address) {}

  void join(WifiPlan::Net net = WifiPlan::Net::Primary) {
    connect_delay_ms = 0;
    node_wifi_connect(net);
  }
  bool say(uint32_t to_ip, const std::string& data) { return node_send(to_ip, data.data(), data.size()); }

  // Move everything that has arrived into `heard` (`at` = when it arrived).
  void listen() {
    while (!inbox.empty() && inbox.front().at <= world.t) {
      heard.push_back(inbox.front());
      inbox.pop_front();
    }
  }
  // Decoded messages of one kind among what was heard.
  std::vector<hhj::Msg> heard_kind(hhj::Msg::Kind kind) const {
    std::vector<hhj::Msg> out;
    for (const Datagram& d : heard) {
      hhj::Msg m;
      if (hhj::decode(d.data.data(), d.data.size(), &m) && m.kind == kind) out.push_back(m);
    }
    return out;
  }
  std::vector<Datagram> heard;
};

// The wire behaviour of hhj-backend (PROTOCOL.md): answer HELLOs, store events
// with the min-latency rule, acknowledge every copy.
class ModelBackend final : public NodeHal<hhj::NetHal> {
 public:
  ModelBackend(World& w, uint32_t address) : NodeHal<hhj::NetHal>(w, address) { connect_delay_ms = 0; }

  struct Reading {
    int64_t edge_ms = 0;         // on the backend's clock: min over copies of (arrival - age)
    int64_t first_arrival_ms = 0;
    int copies = 0;
    hhj::EventMsg first;
  };
  using Key = std::tuple<int, uint32_t, uint32_t>;   // mark, boot, seq

  bool online = true;            // false = process not running: hears nothing, says nothing
  uint32_t boot = 0xBACCE2Du;
  std::map<Key, Reading> readings;
  int hellos = 0, acks = 0, stale = 0, invalid = 0;

  void power(bool on) {
    online = on;
    if (on) node_wifi_connect(WifiPlan::Net::Primary);
  }
  void join(WifiPlan::Net net) { node_wifi_connect(net); }

  const Reading* find(hhj::Role mark, uint32_t gate_boot, uint32_t seq) const {
    const auto it = readings.find(Key{static_cast<int>(mark), gate_boot, seq});
    return it == readings.end() ? nullptr : &it->second;
  }

  void tick() {
    char buf[1024];
    uint32_t from = 0;
    for (size_t n = node_recv(buf, sizeof(buf), &from); n != 0; n = node_recv(buf, sizeof(buf), &from)) {
      if (!online) continue;
      const int64_t now = node_now();
      hhj::Msg m;
      if (!hhj::decode(buf, n, &m)) {
        invalid++;
        continue;
      }
      char out[200];
      if (m.kind == hhj::Msg::Kind::Hello && m.hello.role != hhj::Role::Backend) {
        hhj::HelloMsg h;
        h.role = hhj::Role::Backend;
        h.boot = boot;
        h.up_ms = now < 0 ? 0 : now;
        snprintf(h.fw, sizeof(h.fw), "model");
        const size_t len = hhj::encode(h, out, sizeof(out));
        node_send(from, out, len);
        hellos++;
      } else if (m.kind == hhj::Msg::Kind::Event) {
        if (m.event.age_ms > hhj::kMaxAgeMs) {
          stale++;
          continue;
        }
        const int64_t edge = now - static_cast<int64_t>(m.event.age_ms);
        Reading& r = readings[Key{static_cast<int>(m.event.mark), m.event.boot, m.event.seq}];
        if (r.copies == 0) {
          r.edge_ms = edge;
          r.first_arrival_ms = now;
          r.first = m.event;
        } else if (edge < r.edge_ms) {
          r.edge_ms = edge;
        }
        r.copies++;
        hhj::AckMsg a;
        a.mark = m.event.mark;
        a.boot = m.event.boot;
        a.seq = m.event.seq;
        const size_t len = hhj::encode(a, out, sizeof(out));
        node_send(from, out, len);
        acks++;
      }
    }
  }
};

}  // namespace sim
