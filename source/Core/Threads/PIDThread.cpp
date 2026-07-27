/*
 * PIDThread.cpp
 *
 *  Created on: 29 May 2020
 *      Author: Ralim
 */

#include "BSP.h"
#include "FreeRTOS.h"
#include "Settings.h"
#include "TipThermoModel.h"
#include "cmsis_os.h"
#include "configuration.h"
#include "history.hpp"
#include "main.hpp"
#include "power.hpp"
#include "task.h"

#ifdef POW_PD
#if POW_PD == 1
#include "USBPD.h"
#endif
#endif

static TickType_t          powerPulseWaitUnit          = 25 * TICKS_100MS;      // 2.5 s
static TickType_t          powerPulseDurationUnit      = (5 * TICKS_100MS) / 2; // 250 ms
volatile TaskHandle_t      pidTaskNotification         = NULL;
volatile TemperatureType_t currentTempTargetDegC       = 0; // Current temperature target in C
int32_t                    powerSupplyWattageLimit     = 0;
uint8_t                    heaterThermalRunawayCounter = 0;

static int32_t getPIDResultX10Watts(TemperatureType_t set_point, TemperatureType_t current_value);
static void    detectThermalRunaway(const TemperatureType_t currentTipTempInC, const uint32_t x10WattsOut);
static void    setOutputx10WattsViaFilters(int32_t x10Watts);
static int32_t getX10WattageLimits();

/* StartPIDTask function */
void startPIDTask(void const *argument __unused) {
  /*
   * We take the current tip temperature & evaluate the next step for the tip
   * control PWM.
   */
  setTipX10Watts(0); // disable the output at startup

  currentTempTargetDegC = 0; // Force start with no output (off). If in sleep / soldering this will
                             // be over-ridden rapidly

  pidTaskNotification = xTaskGetCurrentTaskHandle();

  TemperatureType_t PIDTempTarget = 0;
  // Pre-seed the adc filters
  for (int i = 0; i < 32; i++) {
    ulTaskNotifyTake(pdTRUE, 5);
    TipThermoModel::getTipInC(true);
    getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 1);
  }

  while (preStartChecks() == 0) {
    resetWatchdog();
    ulTaskNotifyTake(pdTRUE, 2000);
  }
// Wait for PD if its in the middle of negotiation
#ifdef POW_PD
#if POW_PD == 1
  // This is an FUSB based PD capable device
  // Wait up to 3 seconds for USB-PD to settle
  while (USBPowerDelivery::negotiationInProgress() && xTaskGetTickCount() < (TICKS_SECOND * 3)) {
    resetWatchdog();
    ulTaskNotifyTake(pdTRUE, TICKS_100MS);
  }
#endif
#endif

  int32_t    x10WattsOut             = 0;
  TickType_t lastThermalRunawayDecay = xTaskGetTickCount();

  for (;;) {
    x10WattsOut = 0;
    // This is a call to block this thread until the ADC does its samples
    if (ulTaskNotifyTake(pdTRUE, TICKS_SECOND * 2)) {
      // Do the reading here to keep the temp calculations churning along
      TemperatureType_t currentTipTempInCx10 = TipThermoModel::getTipInCx10(true);

      PIDTempTarget = currentTempTargetDegC;
      // Cap the max set point to 450C
      if (PIDTempTarget > 450) {
        // Maximum allowed output
        PIDTempTarget = 450;
      }
      // Safety check that not aiming higher than current tip can measure
      if (PIDTempTarget > TipThermoModel::getTipMaxInC()) {
        PIDTempTarget = TipThermoModel::getTipMaxInC();
      }

      x10WattsOut = getPIDResultX10Watts(PIDTempTarget, currentTipTempInCx10);
      detectThermalRunaway(currentTipTempInCx10 / 10, PIDTempTarget > 0 ? x10WattsOut : 0);
      setOutputx10WattsViaFilters(x10WattsOut);
    } else {
      // ADC interrupt timeout
      setTipPWM(0, false);
    }
#ifdef DEBUG_UART_OUTPUT
    log_system_state(x10WattsOut);
#endif
    if (xTaskGetTickCount() - lastThermalRunawayDecay > TICKS_SECOND) {
      lastThermalRunawayDecay = xTaskGetTickCount();
      if (heaterThermalRunawayCounter > 0) {
        heaterThermalRunawayCounter--;
      }
    }
  }
}

#if defined(TIP_CONTROL_PID)
// Integral clamp as a multiple of max_output; boards can override to give the
// integral term more authority against sustained heavy loads.
#ifndef TIP_PID_INTEGRAL_LIMIT_SCALE
#define TIP_PID_INTEGRAL_LIMIT_SCALE 5
#endif
template <class T, T Kp, T Ki, T Kd, T integral_limit_scale> struct PID {
  T previous_error_term;
  T integration_running_sum;

  T update(const T set_point, const T new_reading, const TickType_t interval_ms, const T max_output) {
    const T target_delta = set_point - new_reading;

    // Proportional term
    const T kp_result = Kp * target_delta;

    // Calculate the integral term, we use a shift 100 to get precision in integral as we often need small amounts
    T ki_result = integration_running_sum / 100;

    // Derivative term
    T derivative = (target_delta - previous_error_term);
    T kd_result  = ((Kd * derivative) / (T)(interval_ms));

    // Summation of the outputs
    T output = kp_result + ki_result + kd_result;

    // Integral term as we use mixed sampling rates, we cant assume a constant sample interval
    // Thus we multiply this out by the interval time to ~= dv/dt
    // Then the shift by 1000 is ms -> Seconds
    // Conditional integration for anti-windup: skip accumulating while the output is
    // already saturated in the direction the error is pushing. Otherwise the sum rails
    // to +/- the clamp during heatup/overshoot and takes minutes to bleed back off,
    // which shows up as a slow limit cycle (oscillation) around the set point.
    const bool saturated_high = (output >= max_output) && (target_delta > 0);
    const bool saturated_low  = (output <= 0) && (target_delta < 0);
    if (!saturated_high && !saturated_low) {
      integration_running_sum += (target_delta * (T)interval_ms * Ki) / 1000;
    }

    // We constrain integration_running_sum to limit windup
    // This is not overly required for most use cases but can prevent large overshoot in constrained implementations
    if (integration_running_sum > integral_limit_scale * max_output) {
      integration_running_sum = integral_limit_scale * max_output;
    } else if (integration_running_sum < -integral_limit_scale * max_output) {
      integration_running_sum = -integral_limit_scale * max_output;
    }

    // Restrict to max / 0
    if (output > max_output) {
      output = max_output;
    } else if (output < 0) {
      output = 0;
    }

    // Save target_delta to previous target_delta
    previous_error_term = target_delta;

    // Make sure that output is disabled
    if (set_point < MIN_TEMP_C) {
      output = 0;
    }

    return output;
  }
};
#elif defined(TIP_CONTROL_ARDC1)
template <class T> struct ARDC1 {
  T xe[2]      = {0, 0};
  T output     = 0;
  T prev_delta = 0;

  int32_t update(const T set_point, const T new_reading, const T interval, const T max_output) {

    // Configuration
    const T A[] = TIP_ARDC1_A;
    const T B[] = TIP_ARDC1_B;
    const T Kp  = TIP_ARDC1_Kp;
    const T Kd  = TIP_ARDC1_Kd;
    const T b0  = TIP_ARDC1_b0;

    //// Observer
    // xe = A*xe + B*[output; new_reading];
    const T tmp_xe[] = {A[0] * xe[0] + A[1] * xe[1] + B[0] * output + B[1] * new_reading, A[2] * xe[0] + A[3] * xe[1] + B[2] * output + B[3] * new_reading};
    xe[0]            = tmp_xe[0];
    xe[1]            = tmp_xe[1];

    const T delta = set_point - xe[0];

    if (set_point < MIN_TEMP_C) {
      output = 0;
    } else {
      const T u0 = Kp * delta + Kd * (delta - prev_delta) / interval;
      // const T u0 = Kp*delta + Kd*dd;
      output = (u0 - xe[1]) / b0;

      // Restrict output to max / 0
      if (output > max_output)
        output = max_output;
      else if (output < 0)
        output = 0;
    }

    prev_delta = delta;

    return (int32_t)output;
  }
};
#else
template <class T = TemperatureType_t> struct Integrator {
  T sum;

  T update(const T val, const int32_t inertia, const int32_t gain, const int32_t rate, const int32_t limit) {
    // Decay the old value. This is a simplified formula that still works with decent results
    // Ideally we would have used an exponential decay but the computational effort required
    // by exp function is just not justified here in respect to the outcome
    sum = (sum * (100 - (inertia / rate))) / 100;
    // Add the new value x integration interval ( 1 / rate)
    sum += (gain * val) / rate;

    // constrain the output between +- our max power output, this limits windup when doing the inital heatup or when solding something large
    if (sum > limit) {
      sum = limit;
    } else if (sum < -limit) {
      sum = -limit;
    }

    return sum;
  }

  void set(T const val) { sum = val; }

  T get(bool positiveOnly = true) const { return (positiveOnly) ? ((sum > 0) ? sum : 0) : sum; }
};
#endif
int32_t getPIDResultX10Watts(TemperatureType_t set_point, TemperatureType_t current_reading) {
  static TickType_t lastCall = 0;

#if defined(TIP_CONTROL_PID)
  static PID<TemperatureType_t, TIP_PID_KP, TIP_PID_KI, TIP_PID_KD, TIP_PID_INTEGRAL_LIMIT_SCALE> ctrl     = {0, 0};
  const TickType_t                                                                                interval = (xTaskGetTickCount() - lastCall);

#elif defined(TIP_CONTROL_ARDC1)
  static ARDC1<float> ctrl     = {};
  const TickType_t    interval = (xTaskGetTickCount() - lastCall);

#else
  static Integrator<TemperatureType_t> ctrl = {0};
  const TickType_t                     rate = TICKS_SECOND / (xTaskGetTickCount() - lastCall);
#endif
  lastCall = xTaskGetTickCount();
#if defined(TIP_CONTROL_PID)
  // Sandman note:
  // PID Challenge - we have a small thermal mass that we to want heat up as fast as possible but we don't
  // want to overshot excessively (if at all) the set point temperature. In the same time we have 'imprecise'
  // instant temperature measurements. The nature of temperature reading imprecision is not necessarily
  // related to the sensor (thermocouple) or DAQ system, that otherwise are fairly decent. The real issue	is
  // the thermal inertia. We basically read the temperature in the window between two heating sessions when
  // the output is off. However, the heater temperature does not dissipate instantly into the tip mass so
  // at any moment right after heating, the thermocouple would sense a temperature significantly higher than
  // moments later. We could use longer delays but that would slow the PID loop and that would lead to other
  // negative side effects. As a result, we can only rely on the I term but with a twist. Instead of a simple
  // integrator we are going to use a self decaying integrator that acts more like a dual I term / P term
  // rather than a plain I term. Depending on the circumstances, like when the delta temperature is large,
  // it acts more like a P term whereas on closing to set point it acts increasingly closer to a plain I term.
  // So in a sense, we have a bit of both.
  //																		 So there we go...

  // P = (Thermal Mass) x (Delta Temperature ) / 1sec, where thermal mass is in X10 J / °C and
  // delta temperature is in °C. The result is the power in X10 W needed to raise (or decrease!) the
  // tip temperature with (Delta Temperature ) °C in 1 second.
  // Note on powerStore. On update, if the value is provided in X10 (W) units then inertia shall be provided
  // in X10 (J / °C) units as well.

  return ctrl.update(set_point, current_reading / 10, interval, getX10WattageLimits());
#elif defined(TIP_CONTROL_ARDC1)
  return ctrl.update(set_point, current_reading / 10.0f, interval / 1000.0f, getX10WattageLimits());
#else
  return ctrl.update(((TemperatureType_t)getTipThermalMass()) * (set_point - current_reading / 10), // the required power
                     getTipInertia(),                                                               // Inertia, smaller numbers increase dominance of the previous value
                     2,                                                                             // gain
                     rate,                                                                          // PID cycle frequency
                     getX10WattageLimits());
#endif
}

/*
 * Detection of thermal runaway
 * The goal of this is to handle cases where something has gone wrong
 * 1. The tip MOSFET is broken, so power is being constantly applied to the tip
 * a. This can show as temp being stuck at max
 * b. Or temp rising when the heater is off
 * 2. Broken temperature sense
 * a. Temp is stuck at a value
 * These boil down to either a constantly rising temperature or a temperature that is stuck at a value
 * These are both covered; but looking at the eye/delta between min and max temp seen
 */
void detectThermalRunaway(const TemperatureType_t currentTipTempInC, const uint32_t x10WattsOut) {

  static TemperatureType_t tiptempMin         = 0xFFFF; // Min tip temp seen
  static TemperatureType_t tipTempMax         = 0;      // Max tip temp seen while heater is on
  bool                     thisCycleIsHeating = x10WattsOut > 0;
  static TickType_t        heatCycleStart     = 0;

  static bool haveSeenDelta = false;

  // Check for readings being pegged at the top of the ADC while the heater is off
  if (!thisCycleIsHeating && (getTipRawTemp(0) > (ADC_MAX_READING - 8)) && heaterThermalRunawayCounter < 255) {
    heaterThermalRunawayCounter++;
  }

  if (haveSeenDelta) {
    return;
  }

  if (currentTipTempInC < tiptempMin) {
    tiptempMin = currentTipTempInC;
  }
  if (thisCycleIsHeating && currentTipTempInC > tipTempMax) {
    tipTempMax = currentTipTempInC;
  }
  if (thisCycleIsHeating) {
    if (heatCycleStart == 0) {
      heatCycleStart = xTaskGetTickCount();
    }
  } else {
    heatCycleStart = 0;
  }

  if ((xTaskGetTickCount() - heatCycleStart) > (THERMAL_RUNAWAY_TIME_SEC * TICKS_SECOND)) {
    if (tipTempMax > tiptempMin) {
      // Have been heating for min seconds, check if the delta is large enough
      TemperatureType_t delta = tipTempMax - tiptempMin;
      haveSeenDelta           = true;

      if (delta < THERMAL_RUNAWAY_TEMP_C && heaterThermalRunawayCounter < 255) {
        heaterThermalRunawayCounter++;
      }
    }
  }
}

int32_t getX10WattageLimits() {
  int32_t limit = availableW10(0);

  if (getSettingValue(SettingsOptions::PowerLimit) && limit > (getSettingValue(SettingsOptions::PowerLimit) * 10)) {
    limit = getSettingValue(SettingsOptions::PowerLimit) * 10;
  }
  if (powerSupplyWattageLimit && limit > powerSupplyWattageLimit * 10) {
    limit = powerSupplyWattageLimit * 10;
  }
  if (HARDWARE_MAX_WATTAGE_X10 && limit > HARDWARE_MAX_WATTAGE_X10) {
    limit = HARDWARE_MAX_WATTAGE_X10;
  }
  return limit;
}

void setOutputx10WattsViaFilters(int32_t x10WattsOut) {
  static TickType_t lastPowerPulseStart = 0;
  static TickType_t lastPowerPulseEnd   = 0;
#ifdef SLEW_LIMIT
  static int32_t x10WattsOutLast = 0;
#endif

  // If the user turns on the option of using an occasional pulse to keep the power bank on
  if (getSettingValue(SettingsOptions::KeepAwakePulse)) {
    const TickType_t powerPulseWait = powerPulseWaitUnit * getSettingValue(SettingsOptions::KeepAwakePulseWait);
    if (xTaskGetTickCount() - lastPowerPulseStart > powerPulseWait) {
      const TickType_t powerPulseDuration = powerPulseDurationUnit * getSettingValue(SettingsOptions::KeepAwakePulseDuration);
      lastPowerPulseStart                 = xTaskGetTickCount();
      lastPowerPulseEnd                   = lastPowerPulseStart + powerPulseDuration;
    }

    // If current PID is less than the pulse level, check if we want to constrain to the pulse as the floor
    if (x10WattsOut < getSettingValue(SettingsOptions::KeepAwakePulse) && xTaskGetTickCount() < lastPowerPulseEnd) {
      x10WattsOut = getSettingValue(SettingsOptions::KeepAwakePulse);
    }
  }

  // Secondary safety check to forcefully disable header when within ADC noise of top of ADC
  if (getTipRawTemp(0) > (0x7FFF - 32)) {
    x10WattsOut = 0;
  }
  if (heaterThermalRunawayCounter > 8) {
    x10WattsOut = 0;
  }
#ifdef SLEW_LIMIT
  if (x10WattsOut - x10WattsOutLast > SLEW_LIMIT) {
    x10WattsOut = x10WattsOutLast + SLEW_LIMIT;
  }
  if (x10WattsOut < 0) {
    x10WattsOut = 0;
  }
  x10WattsOutLast = x10WattsOut;
#endif
  setTipX10Watts(x10WattsOut);
  resetWatchdog();
}
