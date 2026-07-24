/*
 * OLED.hpp
 *
 *  Created on: 20Jan.,2017
 *      Author: Ben V. Brown <Ralim>
 *      Designed for the SSD1307
 *      Cleared for release for TS100 2017/08/20
 */

#pragma once
#include "Font.h"
#include "cmsis_os.h"
#include "configuration.h"
#include <BSP.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#ifdef __cplusplus
}
#endif

#if defined(OLED_96x16)
#include "OLED.hpp"
#define DISPLAY_CLASS OLED
#define DISPLAY_WIDTH (96)
#define DISPLAY_HEIGHT (16)
#elif defined(OLED_128x32)
#include "OLED.hpp"
#define DISPLAY_CLASS OLED
#define DISPLAY_WIDTH (128)
#define DISPLAY_HEIGHT (32)
#elif defined(LCD_160x80)
#include "LCD.hpp"
#define DISPLAY_CLASS LCD
#define DISPLAY_WIDTH (160)
#define DISPLAY_HEIGHT (80)
#else
#error NO DISPLAY DEFINED
#endif

#if defined(LCD_160x80)
#define FONT_SMALL_HEIGHT   (16)
#define FONT_SMALL_WIDTH    (12)
#define FONT_LARGE_HEIGHT   (32)
#define FONT_LARGE_WIDTH    (24)
#else
#define FONT_SMALL_HEIGHT   (8)
#define FONT_SMALL_WIDTH    (6)
#define FONT_LARGE_HEIGHT   (16)
#define FONT_LARGE_WIDTH    (12)
#endif

class Display {
public:
  enum DisplayState : bool { OFF = false, ON = true };

  static void initialize(); // Startup the I2C coms (brings screen out of reset etc)
  static bool isInitDone();
  // Draw the buffer out to the LCD if any content has changed.
  static void refresh() {
      DISPLAY_CLASS::refresh();
  }

  static void setDisplayState(DisplayState state) {
    if (state != displayState) {
      displayState    = state;
      DISPLAY_CLASS::setDisplayState(state);
    }
  }

  // Set the rotation for the screen
  static void setRotation(bool leftHanded);
  // Get the current rotation of the LCD
  static bool getRotation() {
#ifdef DISPLAY_FLIP
    return !inLeftHandedMode;
#else
    return inLeftHandedMode;
#endif /* DISPLAY_FLIP */
  }
  static void    setBrightness(uint8_t brightness);
  static void    setInverseDisplay(bool inverted);
  static int16_t getCursorX() { return cursor_x; }
  // Draw a string to the current location, with selected font; optionally - with MAX length only
  static void print(const char *string, FontStyle fontStyle, uint8_t length = 255, const uint8_t soft_x_limit = 0);
  static void printWholeScreen(const char *string);
  // Print *F or *C - in font style of Small, Large (by default) or Extra based on input arg
  static void printSymbolDeg(FontStyle fontStyle = FontStyle::LARGE);
  // Set the cursor location by pixels
  static void setCursor(int16_t x, int16_t y) {
    cursor_x = x;
    cursor_y = y;
  }
  // Draws a number at the current cursor location
  static void printNumber(uint16_t number, uint8_t places, FontStyle fontStyle, bool noLeaderZeros = true);
  // Clears the buffer
  static void clearScreen() { DISPLAY_CLASS::clearScreen(); }
  // Draws the battery level symbol
  static void drawBattery(uint8_t state) { drawSymbol(3 + (state > 10 ? 10 : state)); }
  // Draws a checkbox
  static void drawCheckbox(bool state) { drawSymbol((state) ? 16 : 17); }
  // Draw icons
  inline static void drawUnavailableIcon() { DISPLAY_CLASS::drawArea(0, 40, 32, 32, UnavailableIcon); }
  inline static void drawRepeatOnceIcon() { DISPLAY_CLASS::drawArea(0, 40, 32, 32, RepeatOnce); }
  inline static void drawRepeatInfIcon() { DISPLAY_CLASS::drawArea(0, 40, 32, 32, RepeatInf); }
  static void debugNumber(int32_t val, FontStyle fontStyle);
  static void drawHex(uint32_t x, FontStyle fontStyle, uint8_t digits);
  static void drawSymbol(uint8_t symbolID);                                                           // Used for drawing symbols of a predictable width
  static void drawArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t *ptr);        // Draw an area, but y must be aligned on 0/8 offset
  static void drawAreaSwapped(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t *ptr); // Draw an area, but y must be aligned on 0/8 offset
  static void fillArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t value);       // Fill an area, but y must be aligned on 0/8 offset
  static void drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear);
  static void drawHeatSymbol(uint8_t state);
  static void drawScrollIndicator(uint8_t p, uint8_t h); // Draws a scrolling position indicator
  static void maskScrollIndicatorOnOLED();
  static void transitionSecondaryFramebuffer(const bool forwardNavigation, const TickType_t viewEnterTime);
  static void useSecondaryFramebuffer(bool useSecondary);
  static void transitionScrollDown(const TickType_t viewEnterTime);
  static void transitionScrollUp(const TickType_t viewEnterTime);

#if defined(LCD_160x80)
  // 2bpp colour screen support (HS-02 home/soldering gauge). Only exists on the colour panel --
  // DISPLAY_CLASS is OLED on mono boards, which has no 2bpp path at all.
  static void setColorMode(bool active) { DISPLAY_CLASS::setColorMode(active); }
  static void setColorPalette(const uint16_t *palette) { DISPLAY_CLASS::setColorPalette(palette); }
  static void clearScreenColor() { DISPLAY_CLASS::clearScreenColor(); }
  static void fillRectColor(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t colorIndex) { DISPLAY_CLASS::fillRect2bpp(x, y, w, h, colorIndex); }
  static void drawRingColor(uint8_t cx, uint8_t cy, uint8_t r, uint8_t thickness, uint8_t colorIndex, float startAngle, float endAngle) {
    DISPLAY_CLASS::drawRing2bpp(cx, cy, r, thickness, colorIndex, startAngle, endAngle);
  }
  static void drawTickColor(uint8_t cx, uint8_t cy, float angle, uint8_t rInner, uint8_t rOuter, uint8_t colorIndex) {
    DISPLAY_CLASS::drawTick2bpp(cx, cy, angle, rInner, rOuter, colorIndex);
  }
  static void printColor(const char *str, uint8_t x, uint8_t y, FontStyle fontStyle, uint8_t colorIndex, uint8_t maxChars = 255) {
    DISPLAY_CLASS::drawTextColor(str, x, y, fontStyle, colorIndex, maxChars);
  }
  static void printNumberColor(uint16_t number, uint8_t places, uint8_t x, uint8_t y, FontStyle fontStyle, uint8_t colorIndex);
  static void drawBitmapColor(const uint8_t *bitmap, uint8_t width, uint8_t height, uint8_t x, uint8_t y, uint8_t colorIndex) {
    DISPLAY_CLASS::drawBitmap2bpp(bitmap, width, height, x, y, colorIndex);
  }
#endif

private:
  static void         drawChar(uint16_t charCode, FontStyle fontStyle, const uint8_t soft_x_limit); // Draw a character to the current cursor location
  static bool         inLeftHandedMode; // Whether the screen is in left or not (used for offsets in GRAM)
  static bool         initDone;
  static DisplayState displayState;
  static int16_t      cursor_x, cursor_y;
  static uint32_t     displayChecksum;
};
