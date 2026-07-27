/*
 * preRTOS.c
 *
 *  Created on: 29 May 2020
 *      Author: Ralim, MrTick
 */

#include "BSP.h"
#include "I2CBB1.hpp"
// #include "I2CBB2.hpp"
// #include "Pins.h"
#include "Setup.h"
// #include "configuration.h"
// #include <I2C_Wrapper.hpp>
#include "Display.hpp"
#include "main.hpp"
#include "n32l40x_rcc.h"
#include "n32l40x_wwdg.h"

void preRToSInit() {
  __enable_irq(); // Fnirsi bootloader disables interrupts before jumping to the user's app

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  hwInit();
  //   HAL_Init();
  //   Setup_HAL(); // Setup all the HAL objects
  BSPInit();

  FRToSSPI::FRToSInit();
  FRToSSPI::sendLcdReset();

#ifdef I2C_SOFT_BUS_1
  I2CBB1::init();
#endif
}
