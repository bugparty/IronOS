#include "BSP.h"
#include "BSP_Power.h"
#include "Pins.h"
#include "Settings.h"
#include "Setup.h"
#include "configuration.h"
#include "n32l40x.h"

static uint16_t lastRequestedMode = 0xFFFF;

void power_check() {
  // Power Selector - 0: 9V, 1: 12V, 2: 15V, 3: 20V (Default)
  uint16_t mode = getSettingValue(SettingsOptions::QCIdealVoltage);

  if (mode != lastRequestedMode) {
    lastRequestedMode = mode;

    switch (mode) {
      case 0: // 9V (CFG3=0, CFG2=0)
        GPIO_ResetBits(CH224_CFG_Port, CH224_CFG3_Pin);
        GPIO_ResetBits(CH224_CFG_Port, CH224_CFG2_Pin);
        break;

      case 1: // 12V (CFG3=1, CFG2=0)
        GPIO_SetBits(CH224_CFG_Port, CH224_CFG3_Pin);
        GPIO_ResetBits(CH224_CFG_Port, CH224_CFG2_Pin);
        break;

      case 2: // 15V (CFG3=1, CFG2=1)
        GPIO_SetBits(CH224_CFG_Port, CH224_CFG3_Pin);
        GPIO_SetBits(CH224_CFG_Port, CH224_CFG2_Pin);
        break;

      case 3: // 20V (CFG3=0, CFG2=1)
      default:
        GPIO_ResetBits(CH224_CFG_Port, CH224_CFG3_Pin);
        GPIO_SetBits(CH224_CFG_Port, CH224_CFG2_Pin);
        break;
    }
  }
}

bool getIsPoweredByDCIN() {
  // #ifdef POW_PD
  //   // Cant start QC until either PD works or fails
  //   if (!USBPowerDelivery::negotiationComplete()) {
  //     return;
  //   }
  //   if (USBPowerDelivery::negotiationHasWorked()) {
  //     return; // We are using PD
  //   }
  // #endif
  // #ifdef POW_QC
  //   QC_resync();
  // #endif
  return true;
}
