// Host build of the real Core/Threads/PIDThread.cpp and Core/Src/power.cpp with the
// real Core/BSP/Fnirsi/configuration.h, for control_sim.py. The PID task runs
// unmodified; its wait for the ADC is where this program talks to the simulation:
//   out: "PULSE"                          the heater level (0..powerPWM) for the next frame
//   in:  "NOW_MS SETPOINT_C TIP_CX10 VOLTS_X10 POWER_LIMIT_W"   the sample ending that frame
#include "../../../source/Core/Src/power.cpp"
#include "../../../source/Core/Threads/PIDThread.cpp"
#include <cstdio>
#include <cstdlib>

// Core/BSP/Fnirsi/BSP.cpp: the 20 Hz frame is 400 ticks, 395 of them can heat.
const uint16_t powerPWM     = 395;
uint16_t       totalPWM     = 400;
const int      adcFilterLen = 8; // ADC_FILTER_LEN: getTipRawTemp() averages 8 frames

static uint32_t nowMs;
static int32_t  tipHistory[adcFilterLen], tipSum;
static int      tipSlot;
static uint16_t voltsX10, powerLimitW, pulse;

TickType_t   xTaskGetTickCount() { return nowMs; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return (TaskHandle_t)1; }
uint8_t      preStartChecks() { return 1; }
void         resetWatchdog() {}
uint16_t     getTipRawTemp(uint8_t) { return 0; }
uint16_t     getTipResistanceX10() { return TIP_RESISTANCE; }
uint16_t     getInputVoltageX10(uint16_t, uint8_t) { return voltsX10; }
void         setTipPWM(const uint16_t newPulse, const bool) { pulse = newPulse; }
uint16_t     getSettingValue(SettingsOptions option) { return option == SettingsOptions::PowerLimit ? powerLimitW : 0; }

TemperatureType_t TipThermoModel::getTipInCx10(bool) { return tipSum / adcFilterLen; }
TemperatureType_t TipThermoModel::getTipInC(bool sampleNow) { return getTipInCx10(sampleNow) / 10; }
TemperatureType_t TipThermoModel::getTipMaxInC() { return 500; }

uint32_t ulTaskNotifyTake(int, TickType_t) {
  printf("%u\n", pulse);
  fflush(stdout);
  int setpoint, tipCx10, volts, limit;
  if (scanf("%u %d %d %d %d", &nowMs, &setpoint, &tipCx10, &volts, &limit) != 5) {
    exit(0);
  }
  currentTempTargetDegC = setpoint;
  voltsX10              = volts;
  powerLimitW           = limit;
  tipSum += tipCx10 - tipHistory[tipSlot];
  tipHistory[tipSlot] = tipCx10;
  tipSlot             = (tipSlot + 1) % adcFilterLen;
  return 1;
}

int main() {
  printf("%d %d %d %d %d %d\n", TIP_PID_KP, TIP_PID_KI, TIP_PID_KD, TIP_PID_INTEGRAL_LIMIT_SCALE, POWER_LIMIT, TIP_RESISTANCE);
  startPIDTask(nullptr);
}
