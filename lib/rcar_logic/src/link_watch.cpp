#include "link_watch.h"

namespace rcar {

bool LinkWatch::update(bool linkUp, uint32_t now) {
  if (prevLink_ && !linkUp) readvertise_ = true;
  prevLink_ = linkUp;
  if (!linkUp) {
    hasRx_ = false;  // 다시 붙으면 새 입력을 받을 때까지 옛 조이스틱 값을 믿지 않음
    return false;
  }
  if (!hasRx_) return false;
  if (timeout_ == 0) return true;
  return (int32_t)(now - lastRx_) < (int32_t)timeout_;
}

bool LinkWatch::takeReadvertise() {
  bool r = readvertise_;
  readvertise_ = false;
  return r;
}

}  // namespace rcar
