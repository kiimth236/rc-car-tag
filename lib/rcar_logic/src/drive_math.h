#pragma once
#include "rcar_types.h"

namespace rcar {

// 배터리 전압으로 최고 듀티 계산: 255 × min(1, vTarget ÷ (vbat − vDrop))
int maxDutyForBattery(float vbat, float vTarget, float vDrop);

// 탱크식 섞기: L = throttle + steer, R = throttle − steer (각각 −1~1로 자름)
void mixTank(float throttle, float steer, float& left, float& right);

// 명령(−1~1) → 듀티. |cmd| < 0.02 는 0, 그 밖은 minDuty ~ maxDuty 사이로
int commandToDuty(float cmd, int minDuty, int maxDuty);

// 현재 듀티를 목표 쪽으로 maxStep 만큼만 움직임. 방향이 바뀔 때는 먼저 0까지
int rampStep(int current, int target, int maxStep);

// 전진 제한: distCm ≤ stopCm 이면 0, ≤ slowCm 이면 × slowFactor. 후진·미검출(distCm < 0)은 그대로
float limitForward(float throttle, float distCm, float slowCm, float stopCm, float slowFactor);

// 초음파 3번 값의 가운데. −1(미검출)은 "아주 멀다"로 보고, 결과가 미검출이면 −1
float median3Distance(float a, float b, float c);

}  // namespace rcar
