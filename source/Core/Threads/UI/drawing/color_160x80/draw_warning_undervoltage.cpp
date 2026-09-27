#include "ui_drawing.hpp"
#ifdef LCD_160x80

void ui_draw_warning_undervoltage(void) {
  // This warning is invoked synchronously from soldering, before GUIThread has
  // changed OperatingMode. Switch the shared buffer back to its 1bpp meaning
  // before clearing/refreshing it; otherwise refreshColor() expands the mono
  // glyph bits as 2bpp palette indices and corrupts the warning screen.
  Display::setColorMode(false);
  Display::clearScreen();
  if (getSettingValue(SettingsOptions::DetailedSoldering)) {
    // Both strings are translated and may need two lines; the reading ("12.3V", 5 glyphs)
    // goes after the label when it fits, else on the next line.
    ui_print_wrapped(translatedString(Tr->UndervoltageString), 8);
    ui_print_wrapped(translatedString(Tr->InputVoltageString), Display::getCursorY() + FONT_SMALL_HEIGHT + 8);
    if (Display::getCursorX() + 6 * FONT_SMALL_WIDTH > DISPLAY_WIDTH) {
      Display::setCursor(0, Display::getCursorY() + FONT_SMALL_HEIGHT);
    } else {
      Display::setCursor(Display::getCursorX() + FONT_SMALL_WIDTH, Display::getCursorY());
    }
    printVoltage();
    Display::print(SmallSymbolVolts, FontStyle::SMALL);
  } else {
    ui_print_wrapped(translatedString(Tr->UVLOWarningString), 8);
  }

  Display::refresh();
  GUIDelay();
  waitForButtonPress();
}
#endif
