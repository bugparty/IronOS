/*
 * OLED.cpp
 *
 *  Created on: 29Aug.,2017
 *      Author: Ben V. Brown
 */

#include "Display.hpp"
#include "Buttons.hpp"
#include "Settings.h"
#include "Translation.h"
#include "cmsis_os.h"
#include "configuration.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// rendering to the buffer
bool                  Display::inLeftHandedMode; // Whether the screen is in left or not (used for offsets in GRAM)
Display::DisplayState Display::displayState;
int16_t               Display::cursor_x, Display::cursor_y;
bool                  Display::initDone = false;

/*
 * Animation timing function that follows a bezier curve.
 * @param t A given percentage value [0..<100]
 * Returns a new percentage value with ease in and ease out.
 * Original floating point formula: t * t * (3.0f - 2.0f * t);
 */
static uint16_t easeInOutTiming(uint16_t t) { return t * t * (300 - 2 * t) / 10000; }

/*
 * Returns the value between a and b, using a percentage value t.
 * @param a The value associated with 0%
 * @param b The value associated with 100%
 * @param t The percentage [0..<100]
 */
static uint16_t lerp(uint16_t a, uint16_t b, uint16_t t) { return a + t * (b - a) / 100; }

void Display::initialize() {
  cursor_x = cursor_y = 0;
  inLeftHandedMode    = false;

  DISPLAY_CLASS::initialize();
  setDisplayState(DisplayState::ON);

  initDone = true;
}

/*
 * Prints a char to the screen.
 * UTF font handling is done using the two input chars.
 * Precursor is the command char that is used to select the table.
 */
void Display::drawChar(const uint16_t charCode, const FontStyle fontStyle, const uint8_t soft_x_limit) {
  const uint8_t *currentFont;
  static uint8_t fontWidth, fontHeight;
  uint16_t       index;
  switch (fontStyle) {
  case FontStyle::EXTRAS:
    currentFont = ExtraFontChars;
    index       = charCode;
    fontHeight  = 16;
    fontWidth   = 12;
    break;
  case FontStyle::SMALL:
  case FontStyle::LARGE:
  default:
    currentFont = nullptr;
    index       = 0;
    switch (fontStyle) {
    case FontStyle::SMALL:
      fontHeight = FONT_SMALL_HEIGHT;
      fontWidth  = FONT_SMALL_WIDTH;
      break;
    case FontStyle::LARGE:
    default:
      fontHeight = FONT_LARGE_HEIGHT;
      fontWidth  = FONT_LARGE_WIDTH;
      break;
    }
    if (charCode == '\x01' && cursor_y == 0) { // 0x01 is used as new line char
      setCursor(soft_x_limit, fontHeight);
      return;
    } else if (charCode <= 0x01) {
      return;
    }

    currentFont = fontStyle == FontStyle::SMALL ? FontSectionInfo.font06_start_ptr : FontSectionInfo.font12_start_ptr;
    index       = charCode - 2;
    break;
  }
  const uint8_t *charPointer = currentFont + ((fontWidth * (fontHeight / 8)) * index);
  drawArea(cursor_x, cursor_y, fontWidth, fontHeight, charPointer);
  cursor_x += fontWidth;
}

/*
 * Draws a one pixel wide scrolling indicator. y is the upper vertical position
 * of the indicator in pixels (0..<16).
 */
void Display::drawScrollIndicator(uint8_t y, uint8_t height) {

  const uint32_t whole = ((1 << height) - 1) << y; // preload a set of set bits of height
                                                   // Shift down by the y value
  const uint8_t strips[4] = {static_cast<uint8_t>(whole & 0xff), static_cast<uint8_t>((whole & 0xff00) >> 8 * 1), static_cast<uint8_t>((whole & 0xff0000) >> 8 * 2),
                             static_cast<uint8_t>((whole & 0xff000000) >> 8 * 3)};
  // Draw a one pixel wide bar to the left with a single pixel as
  // the scroll indicator.
  DISPLAY_CLASS::fillArea(DISPLAY_WIDTH - 1, 0, 1, 8, strips[0]);
  DISPLAY_CLASS::fillArea(DISPLAY_WIDTH - 1, 8, 1, 8, strips[1]);
#if DISPLAY_HEIGHT == 32
  DISPLAY_CLASS::fillArea(DISPLAY_WIDTH - 1, 16, 1, 8, strips[2]);
  DISPLAY_CLASS::fillArea(DISPLAY_WIDTH - 1, 24, 1, 8, strips[3]);
#endif
}

/**
 * Plays a transition animation between two framebuffers.
 * @param forwardNavigation Direction of the navigation animation.
 *
 * If forward is true, this displays a forward navigation to the second framebuffer contents.
 * Otherwise a rewinding navigation animation is shown to the second framebuffer contents.
 */
void Display::transitionSecondaryFramebuffer(const bool forwardNavigation, const TickType_t viewEnterTime) {
  bool buttonsReleased = getButtonState() == BUTTON_NONE;

  TickType_t totalDuration = TICKS_100MS * 5; // 500ms
  TickType_t duration      = 0;
  TickType_t start         = xTaskGetTickCount();
  uint8_t    offset        = 0;
  TickType_t startDraw     = xTaskGetTickCount();
  while (duration <= totalDuration) {
    duration          = xTaskGetTickCount() - start;
    uint16_t progress = ((duration * 100) / totalDuration); // Percentage of the period we are through for animation
    progress          = easeInOutTiming(progress);
    progress          = lerp(0, DISPLAY_WIDTH, progress);
    // Constrain
    if (progress > DISPLAY_WIDTH) {
      progress = DISPLAY_WIDTH;
    }
    bool needsRefresh = DISPLAY_CLASS::scrollHorizontal(forwardNavigation, progress, offset);
    offset            = progress;

    if (needsRefresh) {
      DISPLAY_CLASS::refresh();
    }

    vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      DISPLAY_CLASS::flushSecondBuffer();
      DISPLAY_CLASS::refresh(); // Now refresh to write out the contents to the new page
      return;
    }
  }
  refresh(); // redraw at the end if required
}

void Display::useSecondaryFramebuffer(bool useSecondary) { DISPLAY_CLASS::useSecondaryFramebuffer(useSecondary); }

/**
 * This assumes that the current display output buffer has the current on screen contents
 * Then the secondary buffer has the "new" contents to be slid up onto the screen
 * Sadly we cant use the hardware scroll as some devices with the 128x32 screens dont have the GRAM for holding both screens at once
 *
 * **This function blocks until the transition has completed or user presses button**
 */
void Display::transitionScrollDown(const TickType_t viewEnterTime) {
  TickType_t startDraw       = xTaskGetTickCount();
  bool       buttonsReleased = getButtonState() == BUTTON_NONE;

  for (uint8_t heightPos = 0; heightPos < DISPLAY_HEIGHT; heightPos++) {
    bool needsRefresh = DISPLAY_CLASS::scrollDown(heightPos);
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      // Exit early, but have to transition whole buffer
      DISPLAY_CLASS::flushSecondBuffer();
      DISPLAY_CLASS::refresh(); // Now refresh to write out the contents to the new page
      return;
    }
    if (needsRefresh) {
      DISPLAY_CLASS::refresh();
      vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
    }
  }
}
/**
 * This assumes that the current display output buffer has the current on screen contents
 * Then the secondary buffer has the "new" contents to be slid down onto the screen
 * Sadly we cant use the hardware scroll as some devices with the 128x32 screens dont have the GRAM for holding both screens at once
 *
 * **This function blocks until the transition has completed or user presses button**
 */
void Display::transitionScrollUp(const TickType_t viewEnterTime) {
  TickType_t startDraw       = xTaskGetTickCount();
  bool       buttonsReleased = getButtonState() == BUTTON_NONE;

  for (uint8_t heightPos = 0; heightPos < DISPLAY_HEIGHT; heightPos++) {
    bool needsRefresh = DISPLAY_CLASS::scrollUp(heightPos);
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      // Exit early, but have to transition whole buffer
      DISPLAY_CLASS::flushSecondBuffer();
      DISPLAY_CLASS::refresh(); // Now refresh to write out the contents to the new page
      return;
    }
    if (needsRefresh) {
      DISPLAY_CLASS::refresh();
      vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
    }
  }
}

void Display::setRotation(bool leftHanded, bool refresh) {
#ifdef DISPLAY_FLIP
  leftHanded = !leftHanded;
#endif /* DISPLAY_FLIP */
  if (inLeftHandedMode == leftHanded) {
    return;
  }
#if defined(LCD_160x80)
  DISPLAY_CLASS::setRotation(leftHanded, refresh);
#else
  (void)refresh;
  DISPLAY_CLASS::setRotation(leftHanded);
#endif
  inLeftHandedMode = leftHanded;
}

void Display::setBrightness(uint8_t brightness) { DISPLAY_CLASS::setBrightness(brightness); }

void Display::setInverseDisplay(bool inverse) { DISPLAY_CLASS::setInverse(inverse); }

// print a string to the current cursor location, len chars MAX
void Display::print(const char *const str, FontStyle fontStyle, uint8_t len, const uint8_t soft_x_limit) {
  const uint8_t *next = reinterpret_cast<const uint8_t *>(str);
  if (next[0] == 0x01) {
    fontStyle = FontStyle::LARGE;
    next++;
  }
  while (next[0] && len--) {
    uint16_t index;
    if (next[0] <= 0xF0) {
      index = next[0];
      next++;
    } else {
      if (!next[1]) {
        return;
      }
      index = (next[0] - 0xF0) * 0xFF - 15 + next[1];
      next += 2;
    }
    drawChar(index, fontStyle, soft_x_limit);
  }
}

/**
 * Prints a static string message designed to use the whole screen, starting
 * from the top-left corner.
 *
 * If the message starts with a newline (`\\x01`), the string starting from
 * after the newline is printed in the large font. Otherwise, the message
 * is printed in the small font.
 *
 * @param string The string message to be printed
 */
void Display::printWholeScreen(const char *string) {
  setCursor(0, 0);
  if (string[0] == '\x01') {
    // Empty first line means that this uses large font (for CJK).
    Display::print(string + 1, FontStyle::LARGE);
  } else {
    Display::print(string, FontStyle::SMALL);
  }
}

// Print *F or *C - in font style of Small, Large (by default) or Extra based on input arg
void Display::printSymbolDeg(const FontStyle fontStyle) {
  switch (fontStyle) {
  case FontStyle::EXTRAS:
    // Picks *F or *C in ExtraFontChars[] from Font.h
    Display::drawSymbol(getSettingValue(SettingsOptions::TemperatureInF) ? 0 : 1);
    break;
  case FontStyle::LARGE:
    Display::print(getSettingValue(SettingsOptions::TemperatureInF) ? LargeSymbolDegF : LargeSymbolDegC, fontStyle);
    break;
  case FontStyle::SMALL:
  default:
    Display::print(getSettingValue(SettingsOptions::TemperatureInF) ? SmallSymbolDegF : SmallSymbolDegC, fontStyle);
    break;
  }
}

inline void stripLeaderZeros(char *buffer, uint8_t places) {
  // Removing the leading zero's by swapping them to SymbolSpace
  // Stop 1 short so that we dont blank entire number if its zero
  for (int i = 0; i < (places - 1); i++) {
    if (buffer[i] == 2) {
      buffer[i] = LargeSymbolSpace[0];
    } else {
      return;
    }
  }
}

void Display::drawHex(uint32_t x, FontStyle fontStyle, uint8_t digits) {
  // print number to hex
  for (uint_fast8_t i = 0; i < digits; i++) {
    uint16_t value = (x >> (4 * (7 - i))) & 0b1111;
    drawChar(value + 2, fontStyle, 0);
  }
}

// maximum places is 5
void Display::printNumber(uint16_t number, uint8_t places, FontStyle fontStyle, bool noLeaderZeros) {
  char buffer[7] = {0};

  if (places >= 5) {
    buffer[5] = 2 + number % 10;
    number /= 10;
  }
  if (places > 4) {
    buffer[4] = 2 + number % 10;
    number /= 10;
  }

  if (places > 3) {
    buffer[3] = 2 + number % 10;
    number /= 10;
  }

  if (places > 2) {
    buffer[2] = 2 + number % 10;
    number /= 10;
  }

  if (places > 1) {
    buffer[1] = 2 + number % 10;
    number /= 10;
  }

  buffer[0] = 2 + number % 10;
  if (noLeaderZeros) {
    stripLeaderZeros(buffer, places);
  }
  print(buffer, fontStyle);
}

#if defined(LCD_160x80)
// Same digit-buffer convention as printNumber() (charCode = 2 + digit), but blits at an explicit
// (x,y) with a chosen ink colour instead of the shared cursor / 1bpp path. Unsigned only, same as
// printNumber -- the gauge screen only ever displays non-negative temperatures/wattage.
void Display::printNumberColor(uint16_t number, uint8_t places, uint8_t x, uint8_t y, FontStyle fontStyle, uint8_t colorIndex) {
  char buffer[7] = {0};
  for (uint8_t i = places; i > 0; i--) {
    buffer[i - 1] = 2 + number % 10;
    number /= 10;
  }
  stripLeaderZeros(buffer, places);
  printColor(buffer, x, y, fontStyle, colorIndex);
}
#endif

void Display::debugNumber(int32_t val, FontStyle fontStyle) {
  if (abs(val) > 99999) {
    Display::print(LargeSymbolSpace, fontStyle); // out of bounds
    return;
  }
  if (val >= 0) {
    Display::print(LargeSymbolSpace, fontStyle);
    Display::printNumber(val, 5, fontStyle);
  } else {
    Display::print(LargeSymbolMinus, fontStyle);
    Display::printNumber(-val, 5, fontStyle);
  }
}

void Display::drawSymbol(uint8_t symbolID) {
  // draw a symbol to the current cursor location
  drawChar(symbolID, FontStyle::EXTRAS, 0);
}

// Draw an area, but y must be aligned on 0/8 offset
void Display::drawArea(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  // TODO: it should be possible to abstract this
  DISPLAY_CLASS::drawArea(x, y, width, height, ptr);
}

// Draw an area, but y must be aligned on 0/8 offset
// For data which has octets swapped in a 16-bit word.
void Display::drawAreaSwapped(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  // TODO: it should be possible to abstract this
  DISPLAY_CLASS::drawAreaSwapped(x, y, width, height, ptr);
}

void Display::fillArea(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t value) {
  // TODO: it should be possible to abstract this
  DISPLAY_CLASS::fillArea(x, y, width, height, value);
}

void Display::drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear) {
  // TODO: it should be possible to abstract this
  DISPLAY_CLASS::drawFilledRect(x0, y0, x1, y1, clear);
}

void Display::drawHeatSymbol(uint8_t state) {
  // Draw symbol 14
  // Then draw over it, the bottom 5 pixels always stay. 8 pixels above that are
  // the levels masks the symbol nicely
  state /= 31; // 0-> 8 range
  // Then we want to draw down (16-(5+state)
  uint16_t cursor_x_temp = cursor_x;
  drawSymbol(14);
  /*
       / / / / /
      / / / / /
      +---------+
      |         |
      +---------+


      <- 14 px ->
      What we are doing is aiming to clear a section of the screen, down to the base depending on how much PWM we are using.
      Larger numbers mean more heat, so we clear less of the screen.
  */
  drawFilledRect(cursor_x_temp, 0, cursor_x_temp + 12, 2 + (8 - state), true);
}

bool Display::isInitDone() { return initDone; }
