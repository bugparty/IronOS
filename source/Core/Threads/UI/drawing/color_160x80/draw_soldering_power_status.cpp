#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>
#ifdef LCD_160x80

void ui_draw_soldering_power_status(bool boost_mode_on) {
  // Print tip temp
  Display::setCursor(4, 24);
  ui_draw_tip_temperature(true, FontStyle::LARGE);

  Display::setCursor(24, 0);
  if (boost_mode_on) { // Boost mode is on
    Display::drawSymbol(2);
    // } else {
    //   Display::print(LargeSymbolSpace, FontStyle::SMALL);
  }
#ifndef NO_SLEEP_MODE
  if (getSettingValue(SettingsOptions::Sensitivity) && getSettingValue(SettingsOptions::SleepTime)) {
    Display::setCursor(120, 48);
    printCountdownUntilSleep(getSleepTimeout());
  }
#endif
  // Print power source
  Display::setCursor(132, 0);
  Display::print(PowerSourceNames[getPowerSourceNumber()], FontStyle::SMALL, 2);
  // Print set temp
  Display::setCursor(40, 0);
  Display::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::SMALL);
  Display::printSymbolDeg(FontStyle::EXTRAS);
  // Print voltage
  Display::setCursor(96, 16);
  printVoltage();
  Display::print(SmallSymbolVolts, FontStyle::SMALL);
  // Print wattage
  Display::setCursor(96, 32);
  uint32_t x10Watt = x10WattHistory.average();
  {
    if (x10Watt > 999) {
      // If we exceed 99.9W we drop the decimal place to keep it all fitting
      Display::print(SmallSymbolSpace, FontStyle::SMALL);
      Display::printNumber(x10WattHistory.average() / 10, 3, FontStyle::SMALL);
    } else {
      Display::printNumber(x10WattHistory.average() / 10, 2, FontStyle::SMALL);
      Display::print(SmallSymbolDot, FontStyle::SMALL);
      Display::printNumber(x10WattHistory.average() % 10, 1, FontStyle::SMALL);
    }
    Display::print(SmallSymbolWatts, FontStyle::SMALL);
  }
}
#endif
