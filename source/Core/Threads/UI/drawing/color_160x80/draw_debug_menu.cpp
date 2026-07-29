#include "OperatingModes.h"
#include "TipThermoModel.h"
#include "main.hpp"
#include "ui_drawing.hpp"

#ifdef LCD_160x80
extern osThreadId GUITaskHandle;
extern osThreadId MOVTaskHandle;
extern osThreadId PIDTaskHandle;

void ui_draw_debug_menu(const uint8_t item_number) {
  Display::setCursor(0, 0);                                   // Position the cursor at the 0,0 (top left)
  Display::print(SmallSymbolVersionNumber, FontStyle::SMALL); // Print version number
  Display::setCursor(0, 24);                                  // second line
  Display::print(DebugMenu[item_number], FontStyle::SMALL);
  Display::setCursor(LCD_WIDTH - 6 * FONT_SMALL_WIDTH, 48); // third line
  uint16_t tmp;
  switch (item_number) {
  case 0: // Build Date
    break;
  case 1: // Device ID
  {
    uint64_t id = getDeviceID();
#ifdef DEVICE_HAS_VALIDATION_CODE
    // If device has validation code; then we want to take over both lines of the screen
    Display::clearScreen();   // Ensure the buffer starts clean
    Display::setCursor(0, 0); // Position the cursor at the 0,0 (top left)
    Display::print(DebugMenu[item_number], FontStyle::SMALL);
    Display::drawHex(getDeviceValidation(), FontStyle::SMALL, 8);
    Display::setCursor(0, 16); // second line
#endif
    Display::setCursor(36, 24);
    Display::drawHex((uint32_t)(id >> 32), FontStyle::SMALL, 8);
    Display::setCursor(36, 48);
    Display::drawHex((uint32_t)(id & 0xFFFFFFFF), FontStyle::SMALL, 8);
  } break;
  case 2: // ACC Type
    Display::setCursor(0, 48);
    Display::print(AccelTypeNames[(int)DetectedAccelerometerVersion], FontStyle::SMALL);
    break;
  case 3: // Power Negotiation Status
    Display::setCursor(0, 48);
    Display::print(PowerSourceNames[getPowerSourceNumber()], FontStyle::SMALL);
    break;
  case 4: // Input Voltage
    Display::print(SmallSymbolSpace, FontStyle::SMALL);
    Display::print(SmallSymbolSpace, FontStyle::SMALL);
    printVoltage();
    break;
  case 5: // Temp in °C
    Display::printNumber(TipThermoModel::getTipInC(), 5, FontStyle::SMALL);
    break;
  case 6: // Handle Temp in °C
    tmp = getHandleTemperature(0);
    Display::printNumber(tmp / 10, 4, FontStyle::SMALL);
    Display::print(SmallSymbolDot, FontStyle::SMALL);
    Display::printNumber(tmp % 10, 1, FontStyle::SMALL);
    break;
  case 7: // Max Temp Limit in °C
    Display::printNumber(TipThermoModel::getTipMaxInC(), 5, FontStyle::SMALL);
    break;
  case 8: // System Uptime
    Display::printNumber(xTaskGetTickCount() / TICKS_100MS, 5, FontStyle::SMALL);
    break;
  case 9: // Movement Timestamp
    Display::printNumber(lastMovementTime / TICKS_100MS, 5, FontStyle::SMALL);
    break;
  case 10: // Tip Resistance in Ω
    Display::printNumber(getTipResistanceX10() / 10, 4, FontStyle::SMALL);
    Display::print(SmallSymbolDot, FontStyle::SMALL);
    Display::printNumber(getTipResistanceX10() % 10, 1, FontStyle::SMALL);
    break;
  case 11: // Raw Tip in µV
    Display::printNumber(TipThermoModel::convertTipRawADCTouV(getTipRawTemp(0), true), 5, FontStyle::SMALL);
    break;
  case 12: // Tip Cold Junction Compensation Offset in µV
    Display::printNumber(getSettingValue(SettingsOptions::CalibrationOffset), 5, FontStyle::SMALL);
    break;
  case 13: // High Water Mark for GUI
    Display::printNumber(uxTaskGetStackHighWaterMark(GUITaskHandle), 5, FontStyle::SMALL);
    break;
  case 14: // High Water Mark for Movement Task
    Display::printNumber(uxTaskGetStackHighWaterMark(MOVTaskHandle), 5, FontStyle::SMALL);
    break;
  case 15: // High Water Mark for PID Task
    Display::printNumber(uxTaskGetStackHighWaterMark(PIDTaskHandle), 5, FontStyle::SMALL);
    break;
    break;
#ifdef HALL_SENSOR
  case 16: // Raw Hall Effect Value
  {
    int16_t hallEffectStrength = getRawHallEffect();
    if (hallEffectStrength < 0) {
      hallEffectStrength = -hallEffectStrength;
    }
    Display::printNumber(hallEffectStrength, 6, FontStyle::SMALL);
  } break;
#else
  case 16: // HS-02 factory tip calibration readout
  {
    // Fnirsi BSP: three stock calibration ADC counts (140C/240C/340C reference
    // points) decoded from the stock settings page; see Core/BSP/Fnirsi/ThermoModel.cpp.
    // "In Use" means the piecewise curve built from them drives the tip readout;
    // "Custom Curve" means they were absent/neutral placeholders and the measured
    // absolute-temperature fallback is active instead.
    extern bool hs02GetFactoryTipCal(uint32_t &a140, uint32_t &a240, uint32_t &a340);
    uint32_t    a140, a240, a340;
    const bool  calValid = hs02GetFactoryTipCal(a140, a240, a340);
    Display::clearScreen();
    Display::setCursor(0, 0);
    Display::print(SmallSymbolTipCal, FontStyle::SMALL); // "Tip Cal"
    Display::setCursor(0, 24);
    Display::printNumber(a140, 3, FontStyle::SMALL, true);
    Display::print(SmallSymbolSpace, FontStyle::SMALL);
    Display::printNumber(a240, 3, FontStyle::SMALL, true);
    Display::print(SmallSymbolSpace, FontStyle::SMALL);
    Display::printNumber(a340, 4, FontStyle::SMALL, true);
    Display::setCursor(0, 48);
    Display::print(calValid ? SmallSymbolTipCalInUse : SmallSymbolTipCalUnused, FontStyle::SMALL);
  } break;
#endif

  default:
    break;
  }
}
#endif
