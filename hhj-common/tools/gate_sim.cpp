// hhj-gate-sim — the real gate application (gate_app.h, the code that runs on
// the ESP32) on a desktop, with POSIX sockets instead of WiFi and a scripted
// rider instead of IR beams.
//
//   hhj-gate-sim gate   --mode START|FINISH --peer HOST:PORT [--boot N]
//                       [--cross MS]... [--run MS]
//       Runs a gate for --run ms (default 1500). Each --cross is a rider
//       cutting the beams that many ms after start. Everything the gate would
//       broadcast or unicast goes to --peer. Prints the gate's log, one
//       "EDGE epoch_ms=..." line per crossing and a final "SUMMARY ..." line.
//
//   hhj-gate-sim timer  --peer HOST:PORT [--boot N] [--run MS]
//       Runs the display unit (timer_app.h) the same way and prints a line
//       "DISPLAY <4 characters> t=<ms>" whenever the digits change
//       (digits, '-' '_' '^' for the status bars, ' ' for a dark digit).
//
//   hhj-gate-sim decode
//       Reads datagrams as hex, one per line, from stdin and prints for each
//       what link.h makes of it, in the notation of protocol/vectors.tsv
//       ("-" = rejected). Used to compare the C++ and Python decoders.
//
// Used by `make test-link` (hhj-backend/timings/tests/test_cross.py) and handy
// for trying the backend or a frontend without any hardware.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <fcntl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "config.h"      // hhj-timer wiring
#include "gate_app.h"
#include "hhj/describe.h"
#include "timer_app.h"

namespace {

int64_t steady_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
int64_t epoch_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

class PosixGateHal final : public hhj::GateHal {
 public:
  PosixGateHal(const sockaddr_in& peer, std::vector<int64_t> crossings)
      : peer_(peer), crossings_(std::move(crossings)), t0_(steady_ms()) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    if (fd_ < 0 || bind(fd_, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) != 0) {
      perror("socket/bind");
      exit(2);
    }
    fcntl(fd_, F_SETFL, O_NONBLOCK);
  }
  ~PosixGateHal() { close(fd_); }

  int64_t now_ms() override { return steady_ms() - t0_; }

  // A rider breaks beam A at the crossing time, beam B 30 ms later, each for 250 ms.
  bool beam_broken(uint8_t beam) override {
    const int64_t now = now_ms();
    for (int64_t at : crossings_) {
      const int64_t from = at + (beam == 0 ? 0 : 30);
      if (now >= from && now < from + 250) return true;
    }
    return false;
  }
  void set_led(hhj::Led, bool) override {}

  bool wifi_up() override { return true; }
  void wifi_connect(hhj::WifiPlan::Net) override {}
  void net_open() override {}
  bool net_send(uint32_t, const char* data, size_t len) override {
    // one peer stands in for both "the broadcast address" and every unicast target
    return sendto(fd_, data, len, 0, reinterpret_cast<const sockaddr*>(&peer_), sizeof(peer_)) == static_cast<ssize_t>(len);
  }
  size_t net_recv(char* buf, size_t cap, uint32_t* from_ip) override {
    char big[2048];
    const ssize_t n = recv(fd_, big, sizeof(big), 0);
    if (n <= 0 || static_cast<size_t>(n) > cap) return 0;
    memcpy(buf, big, static_cast<size_t>(n));
    *from_ip = 1;                          // any non-zero value: there is only the one peer
    return static_cast<size_t>(n);
  }
  int rssi() override { return -42; }
  void log(const char* line) override {
    puts(line);
    fflush(stdout);
  }

 private:
  sockaddr_in peer_;
  std::vector<int64_t> crossings_;
  int64_t t0_;
  int fd_ = -1;
};

int run_gate(int argc, char** argv) {
  hhj::GateAppConfig cfg;
  cfg.boot = 0x51310000u | static_cast<uint32_t>(getpid() & 0xFFFF);
  cfg.fw = "sim";
  std::string peer;
  std::vector<int64_t> crossings;
  int64_t run_ms = 1500;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--mode") {
      if (value != "START" && value != "FINISH") return fprintf(stderr, "--mode START|FINISH\n"), 2;
      cfg.mark = value == "START" ? hhj::Role::Start : hhj::Role::Finish;
    } else if (key == "--peer") {
      peer = value;
    } else if (key == "--boot") {
      cfg.boot = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 0));
    } else if (key == "--cross") {
      crossings.push_back(atoll(value.c_str()));
    } else if (key == "--run") {
      run_ms = atoll(value.c_str());
    } else {
      return fprintf(stderr, "unknown option %s\n", key.c_str()), 2;
    }
  }
  const size_t colon = peer.rfind(':');
  if (colon == std::string::npos) return fprintf(stderr, "--peer HOST:PORT is required\n"), 2;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(static_cast<uint16_t>(atoi(peer.c_str() + colon + 1)));
  if (inet_pton(AF_INET, peer.substr(0, colon).c_str(), &to.sin_addr) != 1) return fprintf(stderr, "bad host\n"), 2;

  PosixGateHal hal(to, crossings);
  hhj::GateApp app(hal, cfg);
  app.begin();
  const int64_t epoch_at_start = epoch_ms() - hal.now_ms();
  for (int64_t at : crossings) printf("EDGE epoch_ms=%lld\n", static_cast<long long>(epoch_at_start + at));
  while (hal.now_ms() < run_ms) {
    app.tick();
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  printf("SUMMARY mark=%s boot=%lu events=%lu pending=%u acked=%lu peers=%u bad_datagrams=%lu failed_sends=%lu\n",
         hhj::role_name(cfg.mark), static_cast<unsigned long>(cfg.boot),
         static_cast<unsigned long>(app.gate().events), static_cast<unsigned>(app.outbox().pending()),
         static_cast<unsigned long>(app.outbox().acked), static_cast<unsigned>(app.peers().count()),
         static_cast<unsigned long>(app.rx_invalid()), static_cast<unsigned long>(app.tx_failed()));
  return 0;
}

// ------------------------------------------------------------------- timer
const hhj::Segments kWiring = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G};

char glyph_char(uint8_t g) {
  for (unsigned d = 0; d < 10; d++) {
    if (g == hhj::glyph_digit(kWiring, d)) return static_cast<char>('0' + d);
  }
  if (g == hhj::glyph_dash(kWiring)) return '-';
  if (g == hhj::glyph_low(kWiring)) return '_';
  if (g == hhj::glyph_high(kWiring)) return '^';
  return g == hhj::kBlank ? ' ' : '?';
}

class PosixTimerHal final : public hhj::TimerHal {
 public:
  explicit PosixTimerHal(const sockaddr_in& peer) : peer_(peer), t0_(steady_ms()) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    if (fd_ < 0 || bind(fd_, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) != 0) {
      perror("socket/bind");
      exit(2);
    }
    fcntl(fd_, F_SETFL, O_NONBLOCK);
  }
  ~PosixTimerHal() { close(fd_); }

  int64_t now_ms() override { return steady_ms() - t0_; }
  bool wifi_up() override { return true; }
  void wifi_connect(hhj::WifiPlan::Net) override {}
  void net_open() override {}
  bool net_send(uint32_t, const char* data, size_t len) override {
    return sendto(fd_, data, len, 0, reinterpret_cast<const sockaddr*>(&peer_), sizeof(peer_)) == static_cast<ssize_t>(len);
  }
  size_t net_recv(char* buf, size_t cap, uint32_t* from_ip) override {
    char big[2048];
    const ssize_t n = recv(fd_, big, sizeof(big), 0);
    if (n <= 0 || static_cast<size_t>(n) > cap) return 0;
    memcpy(buf, big, static_cast<size_t>(n));
    *from_ip = 1;
    return static_cast<size_t>(n);
  }
  int rssi() override { return -42; }
  void log(const char* line) override {
    puts(line);
    fflush(stdout);
  }
  void show(const uint8_t chain[4]) override {
    char text[5];
    for (int i = 0; i < 4; i++) text[i] = glyph_char(CHAIN_FIRST_IS_LEFT != 0 ? chain[3 - i] : chain[i]);
    text[4] = '\0';
    if (strcmp(text, last_) != 0) {
      memcpy(last_, text, sizeof(last_));
      printf("DISPLAY %s t=%lld\n", text, static_cast<long long>(now_ms()));
      fflush(stdout);
    }
  }
  int read_key() override { return -1; }

 private:
  sockaddr_in peer_;
  int64_t t0_;
  int fd_ = -1;
  char last_[5] = "";
};

bool parse_peer(const std::string& peer, sockaddr_in* to) {
  const size_t colon = peer.rfind(':');
  if (colon == std::string::npos) return false;
  *to = sockaddr_in{};
  to->sin_family = AF_INET;
  to->sin_port = htons(static_cast<uint16_t>(atoi(peer.c_str() + colon + 1)));
  return inet_pton(AF_INET, peer.substr(0, colon).c_str(), &to->sin_addr) == 1;
}

int run_timer(int argc, char** argv) {
  hhj::TimerAppConfig cfg;
  cfg.boot = 0x71300000u | static_cast<uint32_t>(getpid() & 0xFFFF);
  cfg.fw = "sim";
  cfg.segments = kWiring;
  cfg.chain_first_is_left = CHAIN_FIRST_IS_LEFT != 0;
  std::string peer;
  int64_t run_ms = 5000;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--peer") {
      peer = value;
    } else if (key == "--boot") {
      cfg.boot = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 0));
    } else if (key == "--run") {
      run_ms = atoll(value.c_str());
    } else {
      return fprintf(stderr, "unknown option %s\n", key.c_str()), 2;
    }
  }
  sockaddr_in to{};
  if (!parse_peer(peer, &to)) return fprintf(stderr, "--peer HOST:PORT is required\n"), 2;
  PosixTimerHal hal(to);
  hhj::TimerApp app(hal, cfg);
  app.begin();
  while (hal.now_ms() < run_ms) {
    app.tick();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  return 0;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int run_decode() {
  std::string line;
  char buf[4096];
  while (fgets(buf, sizeof(buf), stdin) != nullptr) {
    line.assign(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    std::vector<char> data;
    bool ok = line.size() % 2 == 0;
    for (size_t i = 0; ok && i + 1 < line.size(); i += 2) {
      const int hi = hex_value(line[i]), lo = hex_value(line[i + 1]);
      ok = hi >= 0 && lo >= 0;
      data.push_back(static_cast<char>((hi << 4) | lo));
    }
    hhj::Msg m;
    if (ok && hhj::decode(data.data(), data.size(), &m)) {
      puts(hhj::describe(m).c_str());
    } else {
      puts("-");
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && strcmp(argv[1], "gate") == 0) return run_gate(argc, argv);
  if (argc >= 2 && strcmp(argv[1], "timer") == 0) return run_timer(argc, argv);
  if (argc == 2 && strcmp(argv[1], "decode") == 0) return run_decode();
  fprintf(stderr, "usage: %s gate --mode START|FINISH --peer HOST:PORT [--boot N] [--cross MS]... [--run MS]\n"
                  "       %s timer --peer HOST:PORT [--boot N] [--run MS]\n"
                  "       %s decode   < hex-lines\n", argv[0], argv[0], argv[0]);
  return 2;
}
