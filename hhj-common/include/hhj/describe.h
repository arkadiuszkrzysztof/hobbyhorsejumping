// describe.h — a decoded message as text, in the notation of
// hhj-common/protocol/vectors.tsv. Host tools and tests only (uses std::string).
// hhj-backend/timings/link.py has the same function; the two outputs are
// compared byte for byte.
#pragma once
#include <string>

#include "link.h"

namespace hhj {

inline std::string describe(const Msg& m) {
  char buf[320];
  switch (m.kind) {
    case Msg::Kind::Event:
      snprintf(buf, sizeof(buf), "kind=event mark=%s age_ms=%lu t=%lld boot=%lu seq=%lu dir=%d gap_ms=%u n=%u",
               role_name(m.event.mark), static_cast<unsigned long>(m.event.age_ms), static_cast<long long>(m.event.t_ms),
               static_cast<unsigned long>(m.event.boot), static_cast<unsigned long>(m.event.seq),
               static_cast<int>(m.event.dir), static_cast<unsigned>(m.event.gap_ms), static_cast<unsigned>(m.event.n));
      return buf;
    case Msg::Kind::Hello:
      snprintf(buf, sizeof(buf),
               "kind=hello role=%s boot=%lu up_ms=%lld fw=%s st=%lu seq=%lu pending=%u ms=%lu rssi=%d net=%u",
               role_name(m.hello.role), static_cast<unsigned long>(m.hello.boot), static_cast<long long>(m.hello.up_ms),
               m.hello.fw[0] != '\0' ? m.hello.fw : "-", static_cast<unsigned long>(m.hello.st),
               static_cast<unsigned long>(m.hello.seq), static_cast<unsigned>(m.hello.pending),
               static_cast<unsigned long>(m.hello.ms), static_cast<int>(m.hello.rssi), static_cast<unsigned>(m.hello.net));
      return buf;
    case Msg::Kind::Ack:
      snprintf(buf, sizeof(buf), "kind=ack mark=%s boot=%lu seq=%lu", role_name(m.ack.mark),
               static_cast<unsigned long>(m.ack.boot), static_cast<unsigned long>(m.ack.seq));
      return buf;
    case Msg::Kind::Show:
      snprintf(buf, sizeof(buf), "kind=show state=%s rev=%lu age_ms=%lu ms=%lu", show_name(m.show.state),
               static_cast<unsigned long>(m.show.rev), static_cast<unsigned long>(m.show.age_ms),
               static_cast<unsigned long>(m.show.ms));
      return buf;
    case Msg::Kind::None:
      break;
  }
  return "-";
}

}  // namespace hhj
