#include "hall_detector.h"
#include <math.h>

namespace rcar {

bool HallDetector::update(float mv, bool armed) {
  delta_ = fabsf(mv - baseline_);
  if (!armed) {
    count_ = 0;
    fired_ = false;
    if (delta_ < cfg_.thresholdMv * 0.25f) baseline_ += cfg_.driftAlpha * (mv - baseline_);  // 온도 변화만 따라감
    return false;
  }
  if (fired_) return false;
  if (delta_ >= cfg_.thresholdMv) {
    if (++count_ >= cfg_.confirmSamples) {
      fired_ = true;
      return true;
    }
  } else {
    count_ = 0;
  }
  return false;
}

}  // namespace rcar
