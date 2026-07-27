/*
 * Setup.cpp
 *
 *  Created on: 29Aug.,2017
 *      Author: Ben V. Brown, MrTick
 */
#include "Setup.h"
#include "BSP.h"
#include "Pins.h"
#include "SPI_Wrapper.hpp"
#include "cmsis_os.h"
#include "configuration.h"
#include "n32l40x.h"
#include "n32l40x_dma.h"
#include "n32l40x_tim.h"
#include <stdint.h>

static void clockInit(void) {

  RCC_DeInit();

  /*Enable HSI*/
  RCC->CTRL |= RCC_CTRL_HSIEN;
  while ((RCC->CTRL & RCC_CTRL_HSIRDF) != RCC_CTRL_HSIRDF)
    ;

  /*Set PLL MUL*/
  RCC_ConfigPll(RCC_PLL_HSI_PRE_DIV2, RCC_PLL_MUL_8, RCC_PLLDIVCLK_DISABLE);
  /*Enable PLL*/
  RCC->CTRL |= RCC_CTRL_PLLEN;
  while ((RCC->CTRL & RCC_CTRL_PLLRDF) != RCC_CTRL_PLLRDF)
    ;

  /*Set AHB/APB1/APB2*/
  RCC->CFG |= RCC_CFG_AHBPRES_DIV1;  // AHB runs up to 64MHz
  RCC->CFG |= RCC_CFG_APB1PRES_DIV4; // APB1 runs up to 16MHz
  RCC->CFG |= RCC_CFG_APB2PRES_DIV2; // APB2 runs up to 32MHz

  FLASH->AC &= (uint32_t)((uint32_t)~FLASH_AC_LATENCY);
  FLASH->AC |= (uint32_t)(FLASH_AC_LATENCY_1); // 1 cycle for 32MHz <= Sysclk <= 64MHz

  /* Select PLL as system clock source */
  RCC->CFG &= (uint32_t)((uint32_t)~(RCC_CFG_SCLKSW));
  RCC->CFG |= (uint32_t)RCC_CFG_SCLKSW_PLL;
  while ((RCC->CFG & RCC_CFG_SCLKSTS) != RCC_CFG_SCLKSTS_PLL) {
  }

  SystemCoreClockUpdate();
}

static void nvicInit(void) { NVIC_SetPriorityGrouping(NVIC_PriorityGroup_4); }

static void iwdgInit(void) {
  IWDG_WriteConfig(IWDG_WRITE_ENABLE);
  IWDG_SetPrescalerDiv(IWDG_PRESCALER_DIV256);
  IWDG_CntReload(100);
  IWDG_WriteConfig(IWDG_WRITE_DISABLE);

#ifndef SWD_ENABLE
  IWDG_Enable();
#endif
}

static void systickInit(void) {
  SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
  SysTick_Config(8000 - 1); // 1KHz
  NVIC_SetPriority(SysTick_IRQn, 15);
  NVIC_EnableIRQ(SysTick_IRQn);
}

static void spiInit(void) {

  SPI_I2S_DeInit(SPI1);

  // Init SPI GPIO
  GPIO_InitType GPIO_InitStructure;
  GPIO_InitStruct(&GPIO_InitStructure);

  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_SPI1, ENABLE);

  GPIO_InitStructure.Pin            = LCD_SCK_Pin | LCD_MOSI_Pin;
  GPIO_InitStructure.GPIO_Current   = GPIO_DC_12mA;
  GPIO_InitStructure.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
  GPIO_InitStructure.GPIO_Pull      = GPIO_Pull_Up;
  GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_Out_PP;
  GPIO_InitPeripheral(LCD_Port, &GPIO_InitStructure); // silicone bug workaround (see N32L40x errata 6.1.3)

  GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Alternate = GPIO_AF0_SPI1;
  GPIO_InitPeripheral(LCD_Port, &GPIO_InitStructure);

  GPIO_InitStructure.Pin            = LCD_RESET_Pin | LCD_CMD_Pin | LCD_CS_Pin;
  GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Pull      = GPIO_No_Pull;
  GPIO_InitStructure.GPIO_Alternate = GPIO_NO_AF;
  GPIO_InitPeripheral(LCD_Port, &GPIO_InitStructure);

  GPIO_SetBits(LCD_Port, LCD_CS_Pin);
  GPIO_SetBits(LCD_Port, LCD_CMD_Pin);

  // Init SPI itself
  SPI_InitType SPI_InitStructure;
  SPI_InitStruct(&SPI_InitStructure);

  SPI_InitStructure.DataDirection = SPI_DIR_SINGLELINE_TX;
  SPI_InitStructure.SpiMode       = SPI_MODE_MASTER;
  SPI_InitStructure.DataLen       = SPI_DATA_SIZE_8BITS;
  SPI_InitStructure.CLKPOL        = SPI_CLKPOL_HIGH;
  SPI_InitStructure.CLKPHA        = SPI_CLKPHA_SECOND_EDGE;
  SPI_InitStructure.NSS           = SPI_NSS_SOFT;

  SPI_InitStructure.BaudRatePres = SPI_BR_PRESCALER_2; // 32MHz / 2 -> 16MHz

  SPI_InitStructure.FirstBit = SPI_FB_MSB;
  SPI_Init(SPI1, &SPI_InitStructure);

  SPI_Enable(SPI1, ENABLE);
}

static void i2cInit(void) {
#ifdef I2C_SOFT_BUS_1
  // Init I2C GPIO
  GPIO_InitType GPIO_InitStructure;
  GPIO_InitStruct(&GPIO_InitStructure);

  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);

  GPIO_InitStructure.Pin            = I2C_SCL_Pin | I2C_SDA_Pin;
  GPIO_InitStructure.GPIO_Current   = GPIO_DC_12mA;
  GPIO_InitStructure.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
  GPIO_InitStructure.GPIO_Pull      = GPIO_Pull_Up;
  GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_Out_OD;
  GPIO_InitPeripheral(I2C_Port, &GPIO_InitStructure);
#endif
}

static void gpioInit(void) {

  GPIO_DeInit(GPIOA);
  GPIO_DeInit(GPIOB);
  GPIO_DeInit(GPIOD);

  GPIO_InitType GPIO_InitStructure;
  GPIO_InitStruct(&GPIO_InitStructure);

  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE); // Alternative Function Input/Output
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOD, ENABLE);

  // Button inputs
  GPIO_InitStructure.Pin       = BUTTON_OK_Pin | BUTTON_UP_Pin | BUTTON_DOWN_Pin;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Input;
  GPIO_InitStructure.GPIO_Pull = GPIO_Pull_Up;
  GPIO_InitPeripheral(BUTTON_Port, &GPIO_InitStructure);

  // ADC inputs
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Analog;
  GPIO_InitStructure.GPIO_Pull = GPIO_No_Pull;
  GPIO_InitStructure.Pin       = ADC_TEMP_Pin;
  GPIO_InitPeripheral(ADC_TEMP_Port, &GPIO_InitStructure);
  GPIO_InitStructure.Pin = ADC_3V3_Pin;
  GPIO_InitPeripheral(ADC_3V3_Port, &GPIO_InitStructure);
  GPIO_InitStructure.Pin = ADC_VBUS_Pin;
  GPIO_InitPeripheral(ADC_VBUS_Port, &GPIO_InitStructure);
  GPIO_InitStructure.Pin = ADC_CURR_Pin;
  GPIO_InitPeripheral(ADC_CURR_Port, &GPIO_InitStructure);

  // TODO: PWMs
  GPIO_InitStructure.GPIO_Mode    = GPIO_Mode_Out_PP; // temporary to not mess with HW
  GPIO_InitStructure.GPIO_Current = GPIO_DC_2mA;
  GPIO_InitStructure.GPIO_Pull    = GPIO_No_Pull;

  GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Alternate = GPIO_AF2_TIM1;
  GPIO_InitStructure.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
  GPIO_InitStructure.Pin            = PWR_OUT_Pin;
  GPIO_InitPeripheral(PWR_OUT_Port, &GPIO_InitStructure);

  GPIO_InitStructure.Pin = LCD_BL_Pin;
  GPIO_InitPeripheral(LCD_BL_Port, &GPIO_InitStructure);

  GPIO_InitStructure.Pin            = BUZZ_Pin;
  GPIO_InitStructure.GPIO_Alternate = GPIO_AF5_TIM2;
  GPIO_InitPeripheral(BUZZ_Port, &GPIO_InitStructure);

  // Misc outputs
  GPIO_InitStructure.GPIO_Mode    = GPIO_Mode_Out_PP; // temporary to not mess with HW
  GPIO_InitStructure.GPIO_Current = GPIO_DC_12mA;
  GPIO_InitStructure.GPIO_Pull    = GPIO_No_Pull;

  GPIO_InitStructure.Pin = USB_CTL_Pin;
  GPIO_InitPeripheral(USB_CTL_Port, &GPIO_InitStructure);
  GPIO_InitStructure.Pin = CH224_CFG2_Pin | CH224_CFG3_Pin;
  GPIO_InitPeripheral(CH224_CFG_Port, &GPIO_InitStructure);

  GPIO_InitStructure.Pin = LED1_Pin | LED2_Pin;

#ifdef SWD_ENABLE
  GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Alternate = GPIO_AF0_SW_JTAG;
  GPIO_InitPeripheral(LED_Port, &GPIO_InitStructure);
#else
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitPeripheral(LED_Port, &GPIO_InitStructure);
#if BUTTON_BACKLIGHT != 0
  GPIO_SetBits(LED_Port, LED1_Pin); // Enable <-> button LEDs
#endif
#endif

  // Temporary manual configs
  GPIO_ResetBits(USB_CTL_Port, USB_CTL_Pin); // Route USB to CH224 and request 20V
  GPIO_ResetBits(CH224_CFG_Port, CH224_CFG3_Pin);
  GPIO_SetBits(CH224_CFG_Port, CH224_CFG2_Pin);
}

static void adcInit(void) {
  RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC, ENABLE);           // Enable HCLK
  ADC_ConfigClk(ADC_CTRL3_CKMOD_PLL, RCC_ADCPLLCLK_DIV8);       // ADC_CLK set to 8MHz
  RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSI, RCC_ADC1MCLK_DIV16); // ADC_1MCLK must run at 1MHz

  ADC_InitType ADC_InitStructure;
  ADC_InitStruct(&ADC_InitStructure);

  ADC_InitStructure.ContinueConvEn = ENABLE;
  ADC_InitStructure.ExtTrigSelect  = ADC_EXT_TRIGCONV_NONE;
  ADC_InitStructure.DatAlign       = ADC_DAT_ALIGN_R;
  ADC_InitStructure.MultiChEn      = ENABLE;
  ADC_InitStructure.ChsNumber      = ADC_CHANNELS;

  ADC_Init(ADC, &ADC_InitStructure);
  ADC_EnableTempSensorVrefint(ENABLE);

  // Configure regular channels
  ADC_ConfigRegularChannel(ADC, ADC_3V3_Channel, 1, ADC_SAMP_TIME_239CYCLES5);
  ADC_ConfigRegularChannel(ADC, ADC_CURR_Channel, 2, ADC_SAMP_TIME_239CYCLES5);
  ADC_ConfigRegularChannel(ADC, ADC_CH_TEMP_SENSOR, 3, ADC_SAMP_TIME_239CYCLES5);
  ADC_ConfigRegularChannel(ADC, ADC_VBUS_Channel, 4, ADC_SAMP_TIME_239CYCLES5);

  // Configure injected channels
  ADC_ConfigInjectedSequencerLength(ADC, 4);
  ADC_ConfigInjectedChannel(ADC, ADC_TEMP_Channel, 1, ADC_SAMP_TIME_239CYCLES5);
  ADC_ConfigInjectedChannel(ADC, ADC_TEMP_Channel, 2, ADC_SAMP_TIME_239CYCLES5);
  ADC_ConfigInjectedChannel(ADC, ADC_TEMP_Channel, 3, ADC_SAMP_TIME_239CYCLES5);
  ADC_ConfigInjectedChannel(ADC, ADC_TEMP_Channel, 4, ADC_SAMP_TIME_239CYCLES5);

  // Injected conversions are triggered by TIM4 channel1 OC event
  ADC_ConfigExternalTrigInjectedConv(ADC, ADC_EXT_TRIG_INJ_CONV_T4_TRGO);
  ADC_EnableExternalTrigInjectedConv(ADC, ENABLE);

  // Enable DMA
  ADC_EnableDMA(ADC, ENABLE);

  // Enable injected conversion completed interrupt
  ADC_ConfigInt(ADC, ADC_INT_JENDC, ENABLE);
  NVIC_SetPriority(ADC_IRQn, 15);
  NVIC_EnableIRQ(ADC_IRQn);

  // Enable ADC and perform calibration
  ADC_Enable(ADC, ENABLE);
  while (ADC_GetFlagStatusNew(ADC, ADC_FLAG_RDY) == RESET)
    ;
  ADC_StartCalibration(ADC);
  while (ADC_GetCalibrationStatus(ADC) == SET)
    ;

  // Start normal conversions
  ADC_EnableSoftwareStartConv(ADC, ENABLE);
}

static void dmaInit(void) {
  RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA, ENABLE);

  DMA_InitType DMA_InitStructure;

  DMA_InitStructure.Mem2Mem        = DMA_M2M_DISABLE;
  DMA_InitStructure.Direction      = DMA_DIR_PERIPH_SRC;
  DMA_InitStructure.PeriphAddr     = (uint32_t)&ADC->DAT;
  DMA_InitStructure.MemAddr        = (uint32_t)&ADCReadings;
  DMA_InitStructure.BufSize        = ADC_SAMPLES;
  DMA_InitStructure.PeriphInc      = DMA_PERIPH_INC_DISABLE;
  DMA_InitStructure.DMA_MemoryInc  = DMA_MEM_INC_ENABLE;
  DMA_InitStructure.PeriphDataSize = DMA_PERIPH_DATA_SIZE_HALFWORD;
  DMA_InitStructure.MemDataSize    = DMA_MemoryDataSize_HalfWord;
  DMA_InitStructure.CircularMode   = DMA_MODE_CIRCULAR;
  DMA_InitStructure.Priority       = DMA_PRIORITY_LOW;

  DMA_Init(DMA_CH1, &DMA_InitStructure);
  DMA_RequestRemap(DMA_REMAP_ADC1, DMA, DMA_CH1, ENABLE);
  DMA_EnableChannel(DMA_CH1, ENABLE);
}

static void tim1Init(void) {
  // TIM1 is used for both output PWM and LCD backlight brightness.
  // Therefore we need to have it running quite fast (which is actually OK in both cases)
  // Original implementation seems to be using 40kHz here
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_TIM1, ENABLE); // APB2 runs at 32MHz, but timer clock is multiplied to 64MHz
  RCC_ConfigTim18Clk(RCC_TIM18CLK_SRC_TIM18CLK);
  TIM_ConfigInternalClk(TIM1);

  TIM_TimeBaseInitType timBaseInitStruct;
  TIM_InitTimBaseStruct(&timBaseInitStruct);

  timBaseInitStruct.CntMode   = TIM_CNT_MODE_UP;
  timBaseInitStruct.RepetCnt  = 0;
  timBaseInitStruct.Prescaler = 24;  // 64MHz / (24+1) -> 2.56MHz tick
  timBaseInitStruct.Period    = 127; // 2560kHz / (127+1) -> 20kHz

  TIM_InitTimeBase(TIM1, &timBaseInitStruct);

  OCInitType ocInitStruct;
  TIM_InitOcStruct(&ocInitStruct);
  ocInitStruct.OcMode = TIM_OCMODE_PWM1;
  ocInitStruct.Pulse  = 0;

  // Output PWM
  ocInitStruct.OcPolarity  = TIM_OC_POLARITY_HIGH;
  ocInitStruct.OutputState = TIM_OUTPUT_STATE_ENABLE;
  TIM_InitOc1(TIM1, &ocInitStruct);
  TIM_ConfigOc1Fast(TIM1, TIM_OC_FAST_ENABLE);

  // LCD backlight PWM
  ocInitStruct.OcNPolarity  = TIM_OCN_POLARITY_HIGH;
  ocInitStruct.OutputNState = TIM_OUTPUT_NSTATE_ENABLE;
  TIM_InitOc3(TIM1, &ocInitStruct);
  TIM_ConfigOc3Fast(TIM1, TIM_OC_FAST_ENABLE);

  TIM_Enable(TIM1, ENABLE);
  TIM_EnableCtrlPwmOutputs(TIM1, ENABLE);
}

static void tim2Init(void) {
  // TIM2 is used for buzzer.
  RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM2, ENABLE); // APB1 runs at 16MHz, but timer clock is multiplied to 32MHz
  TIM_ConfigInternalClk(TIM2);

  TIM_TimeBaseInitType timBaseInitStruct;
  TIM_InitTimBaseStruct(&timBaseInitStruct);

  timBaseInitStruct.CntMode   = TIM_CNT_MODE_UP;
  timBaseInitStruct.RepetCnt  = 0;
  timBaseInitStruct.Prescaler = 249; // 64MHz / (249+1) -> 256kHz tick
  timBaseInitStruct.Period    = 127; // 256kHz / (127+1) -> 2kHz

  TIM_InitTimeBase(TIM2, &timBaseInitStruct);

  OCInitType ocInitStruct;
  TIM_InitOcStruct(&ocInitStruct);

  ocInitStruct.OcMode      = TIM_OCMODE_PWM1;
  ocInitStruct.Pulse       = 0;
  ocInitStruct.OcPolarity  = TIM_OC_POLARITY_HIGH;
  ocInitStruct.OutputState = TIM_OUTPUT_STATE_ENABLE;

  TIM_InitOc1(TIM2, &ocInitStruct);
  TIM_SetCmp1(TIM2, 0);

  TIM_Enable(TIM2, ENABLE);
}

static void tim4Init(void) {
  // TIM4 is used for ADC trigger
  RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM4, ENABLE); // APB1 runs at 16MHz, but timer clock is multiplied to 32MHz
  TIM_ConfigInternalClk(TIM4);

  TIM_TimeBaseInitType timBaseInitStruct;
  TIM_InitTimBaseStruct(&timBaseInitStruct);

  timBaseInitStruct.CntMode   = TIM_CNT_MODE_UP; // C*nt mode
  timBaseInitStruct.RepetCnt  = 0;
  timBaseInitStruct.Prescaler = 3999; // 32MHz / (3999+1) -> 8kHz tick
  timBaseInitStruct.Period    = 399;  // 8kHz / (399+1) -> 20Hz

  TIM_InitTimeBase(TIM4, &timBaseInitStruct);
  TIM_ConfigArPreload(TIM4, ENABLE); // Not really needed as we don't switch between fast/slow PWM

  // Output channel configuration:
  OCInitType ocInitStruct;
  TIM_InitOcStruct(&ocInitStruct);

  ocInitStruct.OcMode      = TIM_OCMODE_PWM1;
  ocInitStruct.OcPolarity  = TIM_OC_POLARITY_LOW;
  ocInitStruct.OutputState = TIM_OUTPUT_STATE_ENABLE;

  // Channel 1 to trigger ADC"
  ocInitStruct.Pulse = powerPWM + 14;
  TIM_InitOc1(TIM4, &ocInitStruct);
  TIM_ConfigOc1Fast(TIM4, TIM_OC_FAST_ENABLE);

  // Channel 2 to stop TIM1 output
  ocInitStruct.Pulse = powerPWM;
  TIM_InitOc2(TIM4, &ocInitStruct);
  TIM_ConfigOc2Fast(TIM4, TIM_OC_FAST_ENABLE);

  // Output events configuration:
  TIM_SelectOutputTrig(TIM4, TIM_TRGO_SRC_OC1); // Channel1 generates TRGO
  TIM_ConfigInt(TIM4, TIM_INT_CC2, ENABLE);     // Channel2 generates interrupt
  TIM_ConfigInt(TIM4, TIM_INT_UPDATE, ENABLE);  // Update generates interrupt

  // Enable interrupts and start the timer
  NVIC_SetPriority(TIM4_IRQn, 15);
  NVIC_EnableIRQ(TIM4_IRQn);

  TIM_Enable(TIM4, ENABLE);
}

void hwInit(void) {
  iwdgInit();
  nvicInit();
  clockInit();
  systickInit();

  gpioInit();
  spiInit();
  i2cInit();

  dmaInit();
  adcInit();

  tim1Init();
  tim2Init();
  tim4Init();
}
