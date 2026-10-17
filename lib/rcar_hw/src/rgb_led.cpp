#include "rgb_led.h"
#include <Arduino.h>
#include "rcar_config.h"

void ledBegin() {
  ledcSetup(cfg::CH_LED_R, cfg::LED_PWM_HZ, cfg::PWM_BITS);
  ledcAttachPin(cfg::PIN_LED_R, cfg::CH_LED_R);
  ledcSetup(cfg::CH_LED_G, cfg::LED_PWM_HZ, cfg::PWM_BITS);
  ledcAttachPin(cfg::PIN_LED_G, cfg::CH_LED_G);
  ledcSetup(cfg::CH_LED_B, cfg::LED_PWM_HZ, cfg::PWM_BITS);
  ledcAttachPin(cfg::PIN_LED_B, cfg::CH_LED_B);
  ledWrite({0, 0, 0});
}

void ledWrite(rcar::Rgb c) {
  if (cfg::LED_COMMON_ANODE) { c.r = 255 - c.r; c.g = 255 - c.g; c.b = 255 - c.b; }
  ledcWrite(cfg::CH_LED_R, c.r);
  ledcWrite(cfg::CH_LED_G, c.g);
  ledcWrite(cfg::CH_LED_B, c.b);
}
