#include "protocol.h"
#include <string.h>

namespace rcar {

Message makeMessage(MsgType t, uint8_t from, uint16_t round) {
  Message m;
  memset(&m, 0, sizeof m);
  m.magic = kTeamMagic;
  m.type = (uint8_t)t;
  m.from = from;
  m.round = round;
  return m;
}

size_t encode(const Message& m, uint8_t* buf, size_t cap) {
  if (cap < sizeof(Message)) return 0;
  memcpy(buf, &m, sizeof(Message));
  return sizeof(Message);
}

bool decode(const uint8_t* data, size_t len, uint16_t expectedMagic, Message& out) {
  if (len != sizeof(Message)) return false;
  Message m;
  memcpy(&m, data, sizeof m);
  if (m.magic != expectedMagic) return false;
  if (m.type < (uint8_t)MsgType::Status || m.type > (uint8_t)MsgType::Ack) return false;
  if (m.from < 1 || m.from > 4) return false;
  if (m.role > (uint8_t)Role::Runner) return false;
  if (m.phase > (uint8_t)Phase::Over) return false;
  out = m;
  return true;
}

}  // namespace rcar
