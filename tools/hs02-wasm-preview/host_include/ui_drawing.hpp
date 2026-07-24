#pragma once
#include "Font.h"
#include <stdint.h>
#define FONT_SMALL_HEIGHT 16
#define FONT_SMALL_WIDTH 12
#define FONT_LARGE_HEIGHT 32
#define FONT_LARGE_WIDTH 24
class Display {
public:
  static void clearColor();
  static void setColorPalette(const uint16_t *palette);
  static void fillRectColor(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
  static void drawRingColor(uint8_t cx, uint8_t cy, uint8_t r, uint8_t thickness, uint8_t color, float start, float end);
  static void drawTickColor(uint8_t cx, uint8_t cy, float angle, uint8_t inner, uint8_t outer, uint8_t color);
  static void printColor(const char *text, uint8_t x, uint8_t y, FontStyle style, uint8_t color, uint8_t maxChars = 255);
  static void printNumberColor(uint16_t number, uint8_t places, uint8_t x, uint8_t y, FontStyle style, uint8_t color);
  static void drawBitmapColor(const uint8_t *bitmap, uint8_t width, uint8_t height, uint8_t x, uint8_t y, uint8_t color);
};
