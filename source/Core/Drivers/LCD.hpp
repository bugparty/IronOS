/*
 * LCD.hpp
 *
 *  Created on: 27May.,2026
 *      Author: Ben V. Brown <Ralim>
 *      Modified: MrTick, OK2CM
 *      Target ST7735, Fnirsi HS02
 */

#pragma once
#include "configuration.h"
#ifdef LCD_160x80
#include "Font.h"
#include "cmsis_os.h"
#include <BSP.h>
#include <stdbool.h>
#include <string.h>

#include <SPI_Wrapper.hpp>
#define SPI_CLASS FRToSSPI

#define LCD_WIDTH  160
#define LCD_HEIGHT 80

#define LCD_SCREEN_BUF_SIZE      ((LCD_WIDTH * LCD_HEIGHT) / 8)     // One 1bpp frame.
#define LCD_SCREEN_BUF_SIZE_2BPP ((LCD_WIDTH * LCD_HEIGHT * 2) / 8) // One 2bpp frame, or two 1bpp frames.

#define ST7735_XOFFSET 1
#define ST7735_YOFFSET 26

class LCD {
public:
  static void initialize(); // Startup the I2C coms (brings screen out of reset etc)
  // Draw the buffer out to the LCD if any content has changed.
  static void refresh(const bool force = false) {
    if (colorModeActive) {
      if (force || checkDisplayBufferChecksum()) {
        refreshColor();
      }
      return;
    }

    if (force || checkDisplayBufferChecksum()) {
      const int len = (LCD_WIDTH * (LCD_HEIGHT / 8));

      // TODO: don't use strip buffers
      for (uint8_t i = 0; i < LCD_HEIGHT / 8; i++) {
        setDrawingWindow(0, 8 * i, LCD_WIDTH, 8);
        SPI_CLASS::sendPixels(stripPointers[i], len / (LCD_HEIGHT / 8));
      }
    }
  }

  // The shared primary buffer is one complete 2bpp colour frame. In mono mode its two halves
  // become the primary and secondary 1bpp frames used by menu transitions.
  static void setColorMode(bool active);
  static bool isColorMode() { return colorModeActive; }
  // A colour page owns its four RGB565 entries. Index 0 must remain that page's
  // background because clearScreenColor() represents it with an all-zero buffer.
  static void setColorPalette(const uint16_t *palette) { activePalette2bpp = palette ? palette : palette2bpp; }

  // Clears the buffer to palette index 0 (background). Index 0 is chosen so this is a plain
  // memset, same trick as the existing mono clearScreen().
  static void clearScreenColor() { memset(activeColorBuffer, 0, LCD_SCREEN_BUF_SIZE_2BPP); }

  // 2bpp primitives for the colour home/soldering screen. Coordinates are real device pixels.
  static void fillRect2bpp(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, uint8_t colorIndex);
  // Draws an arc of the ring centred at (cx,cy), radius r, thickness px wide, from startAngle to
  // endAngle (radians, 0 = +x axis, increasing clockwise to match screen y-down coordinates).
  static void drawRing2bpp(uint8_t cx, uint8_t cy, uint8_t r, uint8_t thickness, uint8_t colorIndex, float startAngle, float endAngle);
  // Draws a short radial tick mark (e.g. the gauge's target marker) at the given angle.
  static void drawTick2bpp(uint8_t cx, uint8_t cy, float angle, uint8_t rInner, uint8_t rOuter, uint8_t colorIndex);
  // Blits one already-resolved glyph/symbol code (same convention as Display::drawChar) into the
  // 2bpp buffer with a caller-chosen ink colour; unset pixels are left untouched (transparent).
  static void drawGlyph2bpp(uint16_t charCode, FontStyle fontStyle, uint8_t x, uint8_t y, uint8_t colorIndex);
  // Blits a whole already-encoded string (same byte convention as Display::print) left-to-right.
  static void drawTextColor(const char *str, uint8_t x, uint8_t y, FontStyle fontStyle, uint8_t colorIndex, uint8_t maxChars = 255);
  // Blits a transparent 1bpp bitmap encoded as column-major vertical strips.
  // Page-specific colour UIs own their glyph data; this is only the generic blitter.
  static void drawBitmap2bpp(const uint8_t *bitmap, uint8_t width, uint8_t height, uint8_t x, uint8_t y, uint8_t colorIndex);

  static void setDisplayState(bool state) {
    // TODO: implement
    osDelay(TICKS_10MS);
  }

  // Set the rotation for the screen
  static void setRotation(bool leftHanded, bool refresh = true);
  static void setBrightness(uint8_t brightness);
  static void setInverse(bool inverted);

  // Clears the buffer
  static void clearScreen() { memset(stripPointers[0], 0, LCD_WIDTH * (LCD_HEIGHT / 8)); }

  // Blit a raw big-endian RGB565 image straight to the panel, bypassing the framebuffer.
  // Used for the colour boot logo; the next refresh() overwrites it.
  static void drawNativeImage(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *rgb565);
  static void drawArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t *ptr);        // Draw an area, but y must be aligned on 0/8 offset
  static void drawAreaSwapped(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t *ptr); // Draw an area, but y must be aligned on 0/8 offset
  static void fillArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t value);       // Fill an area, but y must be aligned on 0/8 offset
  static void drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear);

  static void flushSecondBuffer(void);
  static void useSecondaryFramebuffer(bool useSecondary);

  static bool scrollDown(uint8_t pos);
  static bool scrollUp(uint8_t pos);
  static bool scrollHorizontal(const bool dirForward, uint16_t progress, uint8_t offset);
  static void setFramebuffer(uint8_t *buffer);

private:
  static bool checkDisplayBufferChecksum() {
    static_assert(LCD_SCREEN_BUF_SIZE % 4 == 0, "1bpp framebuffer size must be a multiple of 4");
    static_assert(LCD_SCREEN_BUF_SIZE_2BPP % 4 == 0, "2bpp framebuffer size must be a multiple of 4");
    uint32_t        hash      = 0;
    const uint32_t  len       = (colorModeActive ? LCD_SCREEN_BUF_SIZE_2BPP : LCD_SCREEN_BUF_SIZE) / 4;
    const uint32_t *pBuffer   = (const uint32_t *)(colorModeActive ? activeColorBuffer : stripPointers[0]);
    uint32_t        wordsLeft = len;
    while (wordsLeft > 0) {
      hash += (wordsLeft * (*pBuffer++));
      wordsLeft--;
    }

    bool result     = hash != displayChecksum;
    displayChecksum = hash;
    return result;
  }
  static void drawChar(uint16_t charCode, FontStyle fontStyle, const uint8_t soft_x_limit); // Draw a character to the current cursor location
  static void setDrawingWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

  static void setPixel2bpp(uint8_t x, uint8_t y, uint8_t colorIndex) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
      return;
    }
    uint16_t byteIdx           = (uint16_t)y * (LCD_WIDTH / 4) + (x / 4);
    uint8_t  shift             = (x % 4) * 2;
    activeColorBuffer[byteIdx] = (activeColorBuffer[byteIdx] & ~(0x3 << shift)) | ((colorIndex & 0x3) << shift);
  }
  static uint8_t *monoSecondFrameBuffer() { return &screenBuffer[LCD_SCREEN_BUF_SIZE]; }
  static void     refreshColor();
  // Plots one radial segment at `angle`, covering radii [rInner, rOuter]. Shared by drawRing2bpp
  // and drawTick2bpp.
  static void plotRadialSegment(uint8_t cx, uint8_t cy, float angle, uint8_t rInner, uint8_t rOuter, uint8_t colorIndex);

  static uint8_t *stripPointers[LCD_HEIGHT / 8]; // Pointers to the strips to allow for buffer having extra content
  static uint32_t displayChecksum;
  static bool     colorModeActive;
  static uint8_t  screenBuffer[LCD_SCREEN_BUF_SIZE_2BPP]; // Colour primary, or two mono frames.
  static uint8_t  colorSecondFrameBuffer[LCD_SCREEN_BUF_SIZE_2BPP];
  static uint8_t *colorPrimaryBuffer;
  static uint8_t *colorSecondaryBuffer;
  static uint8_t *activeColorBuffer;
  static uint8_t  loopCounter;

  // 4-colour palette for the 2bpp colour screens, in device RGB565 (big-endian on the wire).
  // Index 0 must be the background colour (clearScreenColor() relies on an all-zero buffer).
  static const uint16_t  palette2bpp[4];
  static const uint16_t *activePalette2bpp;
};

#endif // LCD_160x80
