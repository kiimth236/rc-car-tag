#include "names.h"
#include <stdint.h>

namespace rcar {

bool validName(const char* utf8) {
  if (!utf8) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8);
  size_t chars = 0, bytes = 0;
  while (*p) {
    if (++chars > 2) return false;
    uint8_t c = p[0];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
      p += 1;
      bytes += 1;
    } else if ((c & 0xF0) == 0xE0) {  // 3바이트만 허용 (한글 음절)
      if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;  // 잘림·잘못된 이어짐
      uint32_t cp = ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
      if (cp < 0xAC00 || cp > 0xD7A3) return false;
      p += 3;
      bytes += 3;
    } else {
      return false;
    }
    if (bytes > kNameBytes) return false;
  }
  return true;
}

}  // namespace rcar
