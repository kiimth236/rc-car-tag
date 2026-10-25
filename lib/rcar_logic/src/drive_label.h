#pragma once
#include "rcar_types.h"

namespace rcar {

// 두 모터 듀티(−255~255, 부호 = 방향)로 화면에 보일 주행 상태 글자를 정함.
// 앞 막힘 > 멈춤(둘 다 |d|<5) > 제자리(부호 반대) > 직진·후진(비율 ≥ 0.75) > 좌·우 회전
DriveLabel classifyDrive(int dutyL, int dutyR, bool frontBlocked);

}  // namespace rcar
