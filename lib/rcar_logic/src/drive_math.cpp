#include "drive_math.h"
#include <math.h>

namespace rcar {

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int maxDutyForBattery(float vbat, float vTarget, float vDrop) {
  float avail = vbat - vDrop;
  if (avail <= vTarget) return 255;  // 배터리가 낮거나 측정이 안 되면 최대로
  int d = (int)lroundf(255.0f * vTarget / avail);
  return d > 255 ? 255 : d;
}

void mixTank(float throttle, float steer, float& left, float& right) {
  throttle = clampf(throttle, -1.0f, 1.0f);
  steer = clampf(steer, -1.0f, 1.0f);
  left = clampf(throttle + steer, -1.0f, 1.0f);
  right = clampf(throttle - steer, -1.0f, 1.0f);
}

int commandToDuty(float cmd, int minDuty, int maxDuty) {
  cmd = clampf(cmd, -1.0f, 1.0f);
  float a = fabsf(cmd);
  if (a < 0.02f) return 0;
  if (minDuty > maxDuty) minDuty = maxDuty;
  int d = minDuty + (int)lroundf(a * (float)(maxDuty - minDuty));
  return cmd > 0 ? d : -d;
}

int rampStep(int current, int target, int maxStep) {
  if (maxStep <= 0) return target;
  if (current != 0 && target != 0 && ((current > 0) != (target > 0))) target = 0;  // 반전은 0을 거쳐서
  int diff = target - current;
  if (diff > maxStep) diff = maxStep;
  if (diff < -maxStep) diff = -maxStep;
  return current + diff;
}

float limitForward(float throttle, float distCm, float slowCm, float stopCm, float slowFactor) {
  if (throttle <= 0.0f || distCm < 0.0f) return throttle;
  if (distCm <= stopCm) return 0.0f;
  if (distCm <= slowCm) return throttle * slowFactor;
  return throttle;
}

float median3Distance(float a, float b, float c) {
  const float kFar = 1e9f;
  float x = a < 0 ? kFar : a, y = b < 0 ? kFar : b, z = c < 0 ? kFar : c;
  float m = fmaxf(fminf(x, y), fminf(fmaxf(x, y), z));
  return m >= kFar ? -1.0f : m;
}

}  // namespace rcar
