#include "Buttons.hpp"
#include "OperatingModeUtilities.h"
#include "OperatingModes.h"
#include "ScrollMessage.hpp"
#include "ui_drawing.hpp"
#ifdef LCD_160x80
bool warnUser(const char *warning, const ButtonState buttons) {
  // warnUser may be called inline from the colour soldering screen. It draws
  // through the legacy 1bpp primitives, so select that interpretation before
  // GUIThread performs its final refresh for this frame.
  Display::setColorMode(false);
  Display::clearScreen();
  // Wrap when every word fits a line. A large-font message that doesn't (e.g. an 8-glyph word
  // against the 6-glyph line) scrolls like userConfirmation() does, from when the warning
  // appeared; scrolling is large-font only, so small-font messages always wrap.
  if (warning[0] != '\x01' || ui_print_wrapped(warning, 0, false)) {
    ui_print_wrapped(warning, 0);
  } else {
    drawScrollingText(warning + 1, xTaskGetTickCount() - lastButtonTime);
  }
  // Also timeout after 5 seconds
  if ((xTaskGetTickCount() - lastButtonTime) > TICKS_SECOND * 5) {
    return true;
  }
  return buttons != BUTTON_NONE;
}
#endif
