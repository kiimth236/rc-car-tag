#pragma once

void analogInputsBegin();
float batteryPinVolts();  // GPIO34 전압 (V), 16회 평균
float hallReadMv();       // GPIO36 전압 (mV), 32회 평균
