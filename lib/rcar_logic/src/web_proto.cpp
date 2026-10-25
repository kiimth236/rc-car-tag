#include "web_proto.h"
#include <stdio.h>
#include <string.h>

namespace rcar {

namespace {

constexpr size_t kMaxLine = 32;

// 부호 있는 정수 한 개를 읽어 −100~100으로 자름. 숫자가 없으면 false.
bool readInt(const char*& p, const char* end, int8_t& v) {
  bool neg = false;
  if (p < end && (*p == '-' || *p == '+')) { neg = (*p == '-'); ++p; }
  if (p >= end || *p < '0' || *p > '9') return false;
  long n = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    if (n < 100000) n = n * 10 + (*p - '0');
    ++p;
  }
  if (neg) n = -n;
  if (n > 100) n = 100;
  if (n < -100) n = -100;
  v = (int8_t)n;
  return true;
}

const char* roleStr(Role r) { return r == Role::Chaser ? "chaser" : "runner"; }
const char* modeStr(UiMode m) {
  switch (m) {
    case UiMode::Menu: return "menu";
    case UiMode::Lobby: return "lobby";
    case UiMode::Solo: return "solo";
    case UiMode::Rename: return "rename";
  }
  return "menu";
}
const char* phaseStr(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";
    case Phase::Countdown: return "countdown";
    case Phase::Playing: return "playing";
    case Phase::Over: return "over";
  }
  return "idle";
}
const char* labelStr(DriveLabel l) {
  switch (l) {
    case DriveLabel::Stop: return "stop";
    case DriveLabel::Forward: return "fwd";
    case DriveLabel::Back: return "back";
    case DriveLabel::Left: return "left";
    case DriveLabel::Right: return "right";
    case DriveLabel::SpinLeft: return "spinL";
    case DriveLabel::SpinRight: return "spinR";
    case DriveLabel::Blocked: return "blocked";
  }
  return "stop";
}
const char* bsStr(BatteryState b) {
  switch (b) {
    case BatteryState::NoBattery: return "none";
    case BatteryState::Ok: return "ok";
    case BatteryState::Warn: return "warn";
    case BatteryState::Cutoff: return "cut";
  }
  return "none";
}
const char* safeName(const char* n) { return (n && validName(n)) ? n : ""; }

}  // namespace

bool parseWebCmd(const char* text, size_t len, WebCmd& out) {
  out = WebCmd{};
  out.kind = WebCmd::Kind::None;
  if (!text || len == 0 || len > kMaxLine) return false;
  const char* p = text;
  const char* end = text + len;
  char c = *p++;
  switch (c) {
    case 'S':
      if (p != end) return false;
      out.kind = WebCmd::Kind::Select;
      return true;
    case 'G':
      if (p != end) return false;
      out.kind = WebCmd::Kind::Start;
      return true;
    case 'J': {
      if (p >= end || *p != ' ') return false;
      ++p;
      int8_t x, y;
      if (!readInt(p, end, x)) return false;
      if (p >= end || *p != ' ') return false;
      ++p;
      if (!readInt(p, end, y)) return false;
      if (p != end) return false;
      out.kind = WebCmd::Kind::Joy;
      out.x = x;
      out.y = y;
      return true;
    }
    case 'T':
      if (end - p != 2 || p[0] != ' ') return false;
      if (p[1] == '+') out.dir = 1;
      else if (p[1] == '-') out.dir = -1;
      else return false;
      out.kind = WebCmd::Kind::Time;
      return true;
    case 'P':
      if (end - p != 2 || p[0] != ' ' || p[1] < '1' || p[1] > '4') return false;
      out.id = (uint8_t)(p[1] - '0');
      out.kind = WebCmd::Kind::Toggle;
      return true;
    case 'M': {
      if (p >= end || *p != ' ') return false;
      ++p;
      size_t n = (size_t)(end - p);
      struct { const char* s; UiMode m; } t[] = {
          {"menu", UiMode::Menu}, {"lobby", UiMode::Lobby},
          {"solo", UiMode::Solo}, {"rename", UiMode::Rename}};
      for (auto& e : t) {
        if (strlen(e.s) == n && memcmp(e.s, p, n) == 0) {
          out.mode = e.m;
          out.kind = WebCmd::Kind::Mode;
          return true;
        }
      }
      return false;
    }
    case 'N': {
      if (p == end) {  // 빈 이름
        out.name[0] = '\0';
        out.kind = WebCmd::Kind::Name;
        return true;
      }
      if (*p != ' ') return false;
      ++p;
      size_t n = (size_t)(end - p);
      if (n > kNameBytes) return false;
      char tmp[kNameBytes + 1];
      memcpy(tmp, p, n);
      tmp[n] = '\0';
      if (strlen(tmp) != n || !validName(tmp)) return false;  // 중간 NUL 거부
      memcpy(out.name, tmp, n + 1);
      out.kind = WebCmd::Kind::Name;
      return true;
    }
  }
  return false;
}

size_t buildStateJson(const UiSnapshot& s, char* buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  const char* nm[5];
  for (int i = 0; i < 5; ++i) nm[i] = safeName(s.names[i]);
  int n = snprintf(buf, cap,
      "{\"id\":%u,\"role\":\"%s\",\"name\":\"%s\",\"mode\":\"%s\",\"phase\":\"%s\","
      "\"spec\":%d,\"me\":%d,\"lock\":%d,\"part\":%u,\"cm\":%u,\"elig\":%u,"
      "\"ms\":%lu,\"sec\":%u,\"win\":%u,\"label\":\"%s\",\"bat\":%u,\"bs\":\"%s\",\"dist\":%d,"
      "\"names\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}",
      (unsigned)s.id, roleStr(s.role), safeName(s.name), modeStr(s.mode), phaseStr(s.phase),
      s.spectating ? 1 : 0, s.caughtMe ? 1 : 0, s.lock ? 1 : 0,
      (unsigned)s.participants, (unsigned)s.caughtMask, (unsigned)s.eligible,
      (unsigned long)s.msLeft, (unsigned)s.gameSec, (unsigned)s.winner, labelStr(s.label),
      (unsigned)s.battDeciV, bsStr(s.bs), (int)s.distCm,
      nm[0], nm[1], nm[2], nm[3], nm[4]);
  if (n < 0 || (size_t)n >= cap) return 0;
  return (size_t)n;
}

}  // namespace rcar
