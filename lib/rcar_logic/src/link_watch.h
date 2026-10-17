#pragma once
#include <stdint.h>

namespace rcar {

// 폰 조종 연결 감시. BLE 링크가 붙어 있고, 붙은 뒤 새 입력을 받았고, rxTimeoutMs 안에 입력이 있었으면 "연결".
// rxTimeoutMs = 0 이면 입력 시간 제한 없이 링크만 봄 (앱이 바뀔 때만 값을 보내는 경우).
class LinkWatch {
 public:
  explicit LinkWatch(uint32_t rxTimeoutMs) : timeout_(rxTimeoutMs) {}
  void onRx(uint32_t now) { lastRx_ = now; hasRx_ = true; }
  bool update(bool linkUp, uint32_t now);
  bool takeReadvertise();  // 링크가 막 끊겼으면 한 번 true (광고 다시 시작)

 private:
  uint32_t timeout_;
  uint32_t lastRx_ = 0;
  bool hasRx_ = false;
  bool prevLink_ = false;
  bool readvertise_ = false;
};

}  // namespace rcar
