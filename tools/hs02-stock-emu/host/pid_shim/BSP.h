#pragma once
// Just enough of the board support interface for PIDThread.cpp and power.cpp.
#include "FreeRTOS.h"
#include <stdint.h>
#define __unused __attribute__((unused))
extern const uint16_t powerPWM;
extern uint16_t       totalPWM;
uint16_t              getTipRawTemp(uint8_t sample);
uint16_t              getInputVoltageX10(uint16_t divisor, uint8_t sample);
uint16_t              getTipResistanceX10();
void                  setTipPWM(const uint16_t pulse, const bool shouldUseFastModePWM);
void                  resetWatchdog();
uint8_t               preStartChecks();
