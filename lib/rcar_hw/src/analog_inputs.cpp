#include "analog_inputs.h"
#include <Arduino.h>
#include "rcar_config.h"

void analogInputsBegin() {
  analogSetPinAttenuation(cfg::PIN_VBAT, ADC_11db);
  analogSetPinAttenuation(cfg::PIN_HALL, ADC_11db);
}

float batteryPinVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(cfg::PIN_VBAT);
  return sum / 16.0f / 1000.0f;
}

float hallReadMv() {
  uint32_t sum = 0;
  for (int i = 0; i < 32; i++) sum += analogReadMilliVolts(cfg::PIN_HALL);
  return sum / 32.0f;
}
