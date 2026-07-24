#include "Buttons.hpp"
#include "OperatingModeUtilities.h"
#include "OperatingModes.h"
#ifdef LCD_160x80
bool warnUser(const char *warning, const ButtonState buttons) {
  // warnUser may be called inline from the colour soldering screen. It draws
  // through the legacy 1bpp primitives, so select that interpretation before
  // GUIThread performs its final refresh for this frame.
  Display::setColorMode(false);
  Display::clearScreen();
  Display::printWholeScreen(warning);
  // Also timeout after 5 seconds
  if ((xTaskGetTickCount() - lastButtonTime) > TICKS_SECOND * 5) {
    return true;
  }
  return buttons != BUTTON_NONE;
}
#endif
