#include "drive_label.h"

namespace rcar {

namespace {
const int kDead = 5;  // 이보다 작으면 0으로 봄
int absInt(int v) { return v < 0 ? -v : v; }
}  // namespace

DriveLabel classifyDrive(int dutyL, int dutyR, bool frontBlocked) {
  if (frontBlocked) return DriveLabel::Blocked;
  int aL = absInt(dutyL);
  int aR = absInt(dutyR);
  bool moveL = aL >= kDead;
  bool moveR = aR >= kDead;
  if (!moveL && !moveR) return DriveLabel::Stop;
  if (moveL && moveR && ((dutyL < 0) != (dutyR < 0)))
    return dutyL < 0 ? DriveLabel::SpinLeft : DriveLabel::SpinRight;

  int lo = 0;
  int hi = aL > aR ? aL : aR;
  if (moveL && moveR) lo = aL < aR ? aL : aR;
  // ratio >= 0.75  <=>  lo*4 >= hi*3 (정수로 계산)
  if (lo * 4 >= hi * 3) return (dutyL + dutyR) > 0 ? DriveLabel::Forward : DriveLabel::Back;
  // 조이스틱 방향과 맞춤: 왼쪽 바퀴가 더 세면 오른쪽으로 돈다 (후진 때도 mixTank 결과와 일치)
  return (dutyL - dutyR) > 0 ? DriveLabel::Right : DriveLabel::Left;
}

}  // namespace rcar
