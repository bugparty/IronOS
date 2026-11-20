/*
 * IRQ.c
 *
 *  Created on: 30 May 2020
 *      Author: Ralim, MrTick
 */

#include "IRQ.h"

#include "cmsis_os.h"
#include "n32l40x_adc.h"
#include "n32l40x_tim.h"

#include "Pins.h"
#include "configuration.h"
#include <stdbool.h>

// Systick is used by FreeRTOS tick
void SysTick_Handler(void) { osSystickHandler(); }

void ADC_IRQHandler(void) {
  if (SET == ADC_GetIntStatus(ADC, ADC_INT_JENDC)) {
    ADC_ClearIntPendingBit(ADC, ADC_INT_JENDC);

    // Notify PID task that the injected ADC readout is completed
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (pidTaskNotification) {
      vTaskNotifyGiveFromISR(pidTaskNotification, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

extern uint16_t PWMSafetyTimer;
extern uint8_t  pendingPWM;

void TIM4_IRQHandler(void) {
  // Corresponds to ST PeriodElapsed
  if (SET == TIM_GetIntStatus(TIM4, TIM_INT_UPDATE)) {
    TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);

    // we want to turn on the output again
    if (PWMSafetyTimer) PWMSafetyTimer--;
    // We decrement this safety value so that lockups in the
    // scheduler will not cause the PWM to become locked in an
    // active driving state.
    // While we could assume this could never happen, its a small price for
    // increased safety
    if (PWMSafetyTimer == 0) {
      TIM1->CCDAT1 = 0;
    } else {
      TIM1->CCDAT1 = pendingPWM;
      return;
    }
  }

  // Corresponds to ST PulseFinished
  if (SET == TIM_GetIntStatus(TIM4, TIM_INT_CC2)) {
    TIM_ClrIntPendingBit(TIM4, TIM_INT_CC2);
    TIM1->CCDAT1 = 0;
    return;
  }

  // for (;;) {
  // }
  TIM4->STS |= TIM4->STS; // Should be unreachable, but for some reasons channels 2-4 fire interrupts
}
