/*
 * LCD.cpp
 *
 *  Created on: 27May.,2026
 *      Author: Ben V. Brown
 *      Modified: MrTick, OK2CM
 *      Target ST7735, Fnirsi HS02
 */
#include "LCD.hpp"
#ifdef LCD_160x80

#include "LCD_Port.hpp"
#include "Settings.h"
#include "Translation.h"
#include "cmsis_os.h"
#include "configuration.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// rendering to the buffer
uint8_t *LCD::stripPointers[LCD_HEIGHT / 8]; // Pointers to the strips to allow for buffer having extra content

alignas(uint32_t) uint8_t LCD::screenBuffer[LCD_SCREEN_BUF_SIZE_2BPP]; // The data buffer (shared mono/colour, see LCD.hpp)
alignas(uint32_t) uint8_t LCD::secondFrameBuffer[LCD_SCREEN_BUF_SIZE];
uint32_t LCD::displayChecksum;
bool     LCD::colorModeActive = false;
uint8_t  LCD::loopCounter;

// True (non-inverted) RGB565 colours; refreshColor() inverts each one on the way out because the
// panel runs with INVON. Index 0 stays background so clearScreenColor()'s memset(0) blanks to it.
const uint16_t LCD::palette2bpp[4] = {
    0x0862, // background (navy)
    0xF77C, // ink (off-white)
    0xFBC5, // ember (heating)
    0x45B8, // cool (idle/sleep)
};
const uint16_t *LCD::activePalette2bpp = LCD::palette2bpp;

// ST7735 Commands
#define ST7735_NOP     0x00
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_NORON   0x13
#define ST7735_INVOFF  0x20
#define ST7735_INVON   0x21
#define ST7735_DISPOFF 0x28
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_MADCTL  0x36
#define ST7735_COLMOD  0x3A
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR  0xB4
#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_PWCTR4  0xC3
#define ST7735_PWCTR5  0xC4
#define ST7735_VMCTR1  0xC5
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

// LCD rotation
const FRToSSPI::SPI_CMD lcdInitCmdRotR = {ST7735_MADCTL, FRToSSPI::SPI_CMD_PAYLOAD, 1, (uint8_t[]){0x88}};
const FRToSSPI::SPI_CMD lcdInitCmdRotL = {ST7735_MADCTL, FRToSSPI::SPI_CMD_PAYLOAD, 1, (uint8_t[]){0x48}};

// LCD initialization
const FRToSSPI::SPI_CMD lcdInitCmds[] = {
    {ST7735_SWRESET, FRToSSPI::SPI_CMD_DELAY_MS, 150,                                                                                                        NULL},
    { ST7735_SLPOUT, FRToSSPI::SPI_CMD_DELAY_MS, 200,                                                                                                        NULL},

    {ST7735_FRMCTR1,  FRToSSPI::SPI_CMD_PAYLOAD,   3,                                                                               (uint8_t[]){0x05, 0x3A, 0x3A}},
    {ST7735_FRMCTR2,  FRToSSPI::SPI_CMD_PAYLOAD,   3,                                                                               (uint8_t[]){0x05, 0x3A, 0x3A}},
    {ST7735_FRMCTR3,  FRToSSPI::SPI_CMD_PAYLOAD,   6,                                                             (uint8_t[]){0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A}},

    { ST7735_PWCTR1,  FRToSSPI::SPI_CMD_PAYLOAD,   3,                                                                               (uint8_t[]){0x62, 0x02, 0x04}},
    { ST7735_PWCTR2,  FRToSSPI::SPI_CMD_PAYLOAD,   1,                                                                                           (uint8_t[]){0xC0}},
    { ST7735_PWCTR3,  FRToSSPI::SPI_CMD_PAYLOAD,   2,                                                                                     (uint8_t[]){0x0D, 0x00}},
    { ST7735_PWCTR4,  FRToSSPI::SPI_CMD_PAYLOAD,   2,                                                                                     (uint8_t[]){0x8D, 0x6A}},
    { ST7735_PWCTR5,  FRToSSPI::SPI_CMD_PAYLOAD,   2,                                                                                     (uint8_t[]){0x8D, 0xEE}},

    {ST7735_GMCTRP1,  FRToSSPI::SPI_CMD_PAYLOAD,  16, (uint8_t[]){0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07, 0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10}},
    {ST7735_GMCTRN1,  FRToSSPI::SPI_CMD_PAYLOAD,  16, (uint8_t[]){0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08, 0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10}},

    { ST7735_INVCTR,  FRToSSPI::SPI_CMD_PAYLOAD,   1,                                                                                           (uint8_t[]){0x03}},
    {  ST7735_INVON,  FRToSSPI::SPI_CMD_PAYLOAD,   0,                                                                                                        NULL},
    { ST7735_VMCTR1,  FRToSSPI::SPI_CMD_PAYLOAD,   1,                                                                                           (uint8_t[]){0x0E}},
    lcdInitCmdRotR,
    { ST7735_COLMOD,  FRToSSPI::SPI_CMD_PAYLOAD,   1,                                                                                           (uint8_t[]){0x05}},

    {  ST7735_NORON, FRToSSPI::SPI_CMD_DELAY_MS,  10,                                                                                                        NULL},
    { ST7735_DISPON, FRToSSPI::SPI_CMD_DELAY_MS, 100,                                                                                                        NULL},
};

FRToSSPI::SPI_CMD lcdSetAreaCmds[] = {
    {ST7735_RASET, FRToSSPI::SPI_CMD_PAYLOAD, 4,  (uint8_t[]){0, ST7735_XOFFSET, 0, LCD_WIDTH + ST7735_XOFFSET - 1}},
    {ST7735_CASET, FRToSSPI::SPI_CMD_PAYLOAD, 4, (uint8_t[]){0, ST7735_YOFFSET, 0, LCD_HEIGHT + ST7735_YOFFSET - 1}},
    {ST7735_RAMWR, FRToSSPI::SPI_CMD_PAYLOAD, 0,                                                               NULL},
};

void LCD::drawNativeImage(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *rgb565) {
  setDrawingWindow(x, y, w, h);
  SPI_CLASS::fastSend(rgb565, (size_t)w * h * 2);
}

void LCD::setDrawingWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  lcdSetAreaCmds[0].data[1] = x + ST7735_XOFFSET;
  lcdSetAreaCmds[0].data[3] = x + w + ST7735_XOFFSET - 1;
  lcdSetAreaCmds[1].data[1] = y + ST7735_YOFFSET;
  lcdSetAreaCmds[1].data[3] = y + h + ST7735_YOFFSET - 1;

  FRToSSPI::sendCmdChain(lcdSetAreaCmds, sizeof(lcdSetAreaCmds) / sizeof(*lcdSetAreaCmds));
}

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

void LCD::initialize() {
  for (uint8_t i = 0; i < LCD_HEIGHT / 8; i++) {
    stripPointers[i] = &screenBuffer[i * LCD_WIDTH];
  }

  FRToSSPI::init();
  FRToSSPI::sendCmdChain(lcdInitCmds, sizeof(lcdInitCmds) / sizeof(*lcdInitCmds));

  // Keep the backlight off until the first meaningful frame is ready. The boot
  // logo is a full-screen image, so an intermediate GRAM clear is unnecessary.
}

void LCD::setFramebuffer(uint8_t *buffer) {
  for (uint8_t i = 0; i < LCD_HEIGHT / 8; i++) {
    stripPointers[i] = &buffer[i * LCD_WIDTH];
  }
}

/**
 * Plays a transition animation between two framebuffers.
 *
 * If forward is true, this displays a forward navigation to the second framebuffer contents.
 * Otherwise a rewinding navigation animation is shown to the second framebuffer contents.
 */
bool LCD::scrollHorizontal(const bool dirForward, uint16_t progress, uint8_t offset) {
  uint8_t *stripBackPointers[LCD_HEIGHT / 8];
  for (uint8_t i = 0; i < LCD_HEIGHT / 8; i++) {
    stripBackPointers[i] = &secondFrameBuffer[i * LCD_WIDTH];
  }

  // When forward, current contents move to the left out.
  // Otherwise the contents move to the right out.
  uint8_t oldStart    = dirForward ? 0 : progress;
  uint8_t oldPrevious = dirForward ? progress - offset : offset;

  // Content from the second framebuffer moves in from the right (forward)
  // or from the left (not forward).
  uint8_t newStart = dirForward ? LCD_WIDTH - progress : 0;
  uint8_t newEnd   = dirForward ? 0 : LCD_WIDTH - progress;

  offset = progress;

  for (uint8_t i = 0; i < LCD_HEIGHT / 8; i++) {
    memmove(&stripPointers[i][oldStart], &stripPointers[i][oldPrevious], LCD_WIDTH - progress);
  }

  for (uint8_t i = 0; i < LCD_HEIGHT / 8; i++) {
    memmove(&stripPointers[i][newStart], &stripBackPointers[i][newEnd], progress);
  }

  return true;
}

void LCD::useSecondaryFramebuffer(bool useSecondary) {
  if (useSecondary) {
    setFramebuffer(secondFrameBuffer);
  } else {
    setFramebuffer(screenBuffer);
  }
}

/**
 * This assumes that the current display output buffer has the current on screen contents
 * Then the secondary buffer has the "new" contents to be slid up onto the screen
 * Sadly we cant use the hardware scroll as some devices with the 128x32 screens dont have the GRAM for holding both screens at once
 *
 * **This function blocks until the transition has completed or user presses button**
 */
bool LCD::scrollDown(uint8_t pos) {
  static_assert(LCD_WIDTH % 4 == 0, "LCD_WIDTH must be multiple of 4");
  static_assert(LCD_HEIGHT == 80, "LCD_HEIGHT must be 80");
  uint32_t *const pA = (uint32_t *)screenBuffer;
  uint32_t *const pB = (uint32_t *)secondFrameBuffer;
  // For each line, we shuffle all bits up a row
  for (uint8_t xPos = 0; xPos < LCD_WIDTH / 4; xPos++) {
    const uint16_t Strip01Pos = xPos;
    const uint16_t Strip02Pos = Strip01Pos + LCD_WIDTH / 4;
    const uint16_t Strip03Pos = Strip02Pos + LCD_WIDTH / 4;
    const uint16_t Strip04Pos = Strip03Pos + LCD_WIDTH / 4;
    const uint16_t Strip05Pos = Strip04Pos + LCD_WIDTH / 4;
    const uint16_t Strip06Pos = Strip05Pos + LCD_WIDTH / 4;
    const uint16_t Strip07Pos = Strip06Pos + LCD_WIDTH / 4;
    const uint16_t Strip08Pos = Strip07Pos + LCD_WIDTH / 4;
    const uint16_t Strip09Pos = Strip08Pos + LCD_WIDTH / 4;
    const uint16_t Strip10Pos = Strip09Pos + LCD_WIDTH / 4;

    pA[Strip01Pos] = ((pA[Strip01Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip02Pos] & 0x01010101) << 7);
    pA[Strip02Pos] = ((pA[Strip02Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip03Pos] & 0x01010101) << 7);
    pA[Strip03Pos] = ((pA[Strip03Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip04Pos] & 0x01010101) << 7);
    pA[Strip04Pos] = ((pA[Strip04Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip05Pos] & 0x01010101) << 7);
    pA[Strip05Pos] = ((pA[Strip05Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip06Pos] & 0x01010101) << 7);
    pA[Strip06Pos] = ((pA[Strip06Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip07Pos] & 0x01010101) << 7);
    pA[Strip07Pos] = ((pA[Strip07Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip08Pos] & 0x01010101) << 7);
    pA[Strip08Pos] = ((pA[Strip08Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip09Pos] & 0x01010101) << 7);
    pA[Strip09Pos] = ((pA[Strip09Pos] >> 1) & 0x7F7F7F7F) | ((pA[Strip10Pos] & 0x01010101) << 7);
    pA[Strip10Pos] = ((pA[Strip10Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip01Pos] & 0x01010101) << 7);

    pB[Strip01Pos] = ((pB[Strip01Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip02Pos] & 0x01010101) << 7);
    pB[Strip02Pos] = ((pB[Strip02Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip03Pos] & 0x01010101) << 7);
    pB[Strip03Pos] = ((pB[Strip03Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip04Pos] & 0x01010101) << 7);
    pB[Strip04Pos] = ((pB[Strip04Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip05Pos] & 0x01010101) << 7);
    pB[Strip05Pos] = ((pB[Strip05Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip06Pos] & 0x01010101) << 7);
    pB[Strip06Pos] = ((pB[Strip06Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip07Pos] & 0x01010101) << 7);
    pB[Strip07Pos] = ((pB[Strip07Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip08Pos] & 0x01010101) << 7);
    pB[Strip08Pos] = ((pB[Strip08Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip09Pos] & 0x01010101) << 7);
    pB[Strip09Pos] = ((pB[Strip09Pos] >> 1) & 0x7F7F7F7F) | ((pB[Strip10Pos] & 0x01010101) << 7);
    pB[Strip10Pos] = ((pB[Strip10Pos] >> 1) & 0x7F7F7F7F);
  }
  return (loopCounter++ % 3 == 0);
}
/**
 * This assumes that the current display output buffer has the current on screen contents
 * Then the secondary buffer has the "new" contents to be slid down onto the screen
 * Sadly we cant use the hardware scroll as some devices with the 128x32 screens dont have the GRAM for holding both screens at once
 *
 * **This function blocks until the transition has completed or user presses button**
 */
bool LCD::scrollUp(uint8_t pos) {
  static_assert(LCD_WIDTH % 4 == 0, "LCD_WIDTH must be multiple of 4");
  static_assert(LCD_HEIGHT == 80, "LCD_HEIGHT must be 80");
  uint32_t *const pA = (uint32_t *)screenBuffer;
  uint32_t *const pB = (uint32_t *)secondFrameBuffer;
  // For each line, we shuffle all bits down a row
  for (uint8_t xPos = 0; xPos < LCD_WIDTH / 4; xPos++) {
    const uint16_t Strip01Pos = xPos;
    const uint16_t Strip02Pos = Strip01Pos + LCD_WIDTH / 4;
    const uint16_t Strip03Pos = Strip02Pos + LCD_WIDTH / 4;
    const uint16_t Strip04Pos = Strip03Pos + LCD_WIDTH / 4;
    const uint16_t Strip05Pos = Strip04Pos + LCD_WIDTH / 4;
    const uint16_t Strip06Pos = Strip05Pos + LCD_WIDTH / 4;
    const uint16_t Strip07Pos = Strip06Pos + LCD_WIDTH / 4;
    const uint16_t Strip08Pos = Strip07Pos + LCD_WIDTH / 4;
    const uint16_t Strip09Pos = Strip08Pos + LCD_WIDTH / 4;
    const uint16_t Strip10Pos = Strip09Pos + LCD_WIDTH / 4;

    pA[Strip10Pos] = ((pA[Strip10Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip09Pos] & 0x80808080) >> 7);
    pA[Strip09Pos] = ((pA[Strip09Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip08Pos] & 0x80808080) >> 7);
    pA[Strip08Pos] = ((pA[Strip08Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip07Pos] & 0x80808080) >> 7);
    pA[Strip07Pos] = ((pA[Strip07Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip06Pos] & 0x80808080) >> 7);
    pA[Strip06Pos] = ((pA[Strip06Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip05Pos] & 0x80808080) >> 7);
    pA[Strip05Pos] = ((pA[Strip05Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip04Pos] & 0x80808080) >> 7);
    pA[Strip04Pos] = ((pA[Strip04Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip03Pos] & 0x80808080) >> 7);
    pA[Strip03Pos] = ((pA[Strip03Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip02Pos] & 0x80808080) >> 7);
    pA[Strip02Pos] = ((pA[Strip02Pos] << 1) & 0xFEFEFEFE) | ((pA[Strip01Pos] & 0x80808080) >> 7);
    pA[Strip01Pos] = ((pA[Strip01Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip10Pos] & 0x80808080) >> 7);

    pB[Strip10Pos] = ((pB[Strip10Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip09Pos] & 0x80808080) >> 7);
    pB[Strip09Pos] = ((pB[Strip09Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip08Pos] & 0x80808080) >> 7);
    pB[Strip08Pos] = ((pB[Strip08Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip07Pos] & 0x80808080) >> 7);
    pB[Strip07Pos] = ((pB[Strip07Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip06Pos] & 0x80808080) >> 7);
    pB[Strip06Pos] = ((pB[Strip06Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip05Pos] & 0x80808080) >> 7);
    pB[Strip05Pos] = ((pB[Strip05Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip04Pos] & 0x80808080) >> 7);
    pB[Strip04Pos] = ((pB[Strip04Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip03Pos] & 0x80808080) >> 7);
    pB[Strip03Pos] = ((pB[Strip03Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip02Pos] & 0x80808080) >> 7);
    pB[Strip02Pos] = ((pB[Strip02Pos] << 1) & 0xFEFEFEFE) | ((pB[Strip01Pos] & 0x80808080) >> 7);
    pB[Strip01Pos] = ((pB[Strip01Pos] << 1) & 0xFEFEFEFE);
  }
  return (loopCounter++ % 3 == 0);
}

void LCD::setRotation(bool leftHanded, bool refresh) {
  if (leftHanded) {
    FRToSSPI::sendCmdChain(&lcdInitCmdRotL, 1);
  } else {
    FRToSSPI::sendCmdChain(&lcdInitCmdRotR, 1);
  }
  if (refresh) {
    LCD::refresh(true);
  }
}

void LCD::setBrightness(uint8_t brightness) { LCDSetBacklight(brightness); }

void LCD::setInverse(bool inverse) {
  const FRToSSPI::SPI_CMD cmdInvSet = {(uint8_t)(inverse ? ST7735_INVON : ST7735_INVOFF), FRToSSPI::SPI_CMD_PAYLOAD, 0, NULL};
  FRToSSPI::sendCmdChain(&cmdInvSet, 1);
}

// Mono-only: secondFrameBuffer (scroll-transition backing) is always 1bpp/LCD_SCREEN_BUF_SIZE,
// even though screenBuffer itself is now sized for the wider 2bpp colour mode -- must NOT use
// sizeof(screenBuffer) here, that would over-read secondFrameBuffer.
void LCD::flushSecondBuffer(void) { memcpy(screenBuffer, secondFrameBuffer, LCD_SCREEN_BUF_SIZE); }

// Draw an area, but y must be aligned on 0/8 offset
void LCD::drawArea(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  // Splat this from x->x+width in two strides
  if (x <= -width) {
    return; // cutoffleft
  }
  if (x > LCD_WIDTH) {
    return; // cutoff right
  }

  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = width;

  // trimming to draw partials
  if (x < 0) {
    visibleStart -= x; // subtract negative value == add absolute value
  }
  if (x + width > LCD_WIDTH) {
    visibleEnd = LCD_WIDTH - x;
  }
  uint8_t rowsDrawn = 0;
  while (height > 0) {
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx++) {
      stripPointers[(y / 8) + rowsDrawn][x + xx] = ptr[xx + (rowsDrawn * width)];
    }
    height -= 8;
    rowsDrawn++;
  }
}

// Draw an area, but y must be aligned on 0/8 offset
// For data which has octets swapped in a 16-bit word.
void LCD::drawAreaSwapped(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  // Splat this from x->x+width in two strides
  if (x <= -width) {
    return; // cutoffleft
  }
  if (x > LCD_WIDTH) {
    return; // cutoff right
  }

  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = width;

  // trimming to draw partials
  if (x < 0) {
    visibleStart -= x; // subtract negative value == add absolute value
  }
  if (x + width > LCD_WIDTH) {
    visibleEnd = LCD_WIDTH - x;
  }

  uint8_t rowsDrawn = 0;
  while (height > 0) {
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx += 2) {
      stripPointers[(y / 8) + rowsDrawn][x + xx]     = ptr[xx + 1 + (rowsDrawn * width)];
      stripPointers[(y / 8) + rowsDrawn][x + xx + 1] = ptr[xx + (rowsDrawn * width)];
    }
    height -= 8;
    rowsDrawn++;
  }
}

void LCD::fillArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t value) {
  // Splat this from x->x+wide in two strides
  if (x <= -wide) {
    return; // cutoffleft
  }
  if (x > LCD_WIDTH) {
    return; // cutoff right
  }

  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = wide;

  // trimming to draw partials
  if (x < 0) {
    visibleStart -= x; // subtract negative value == add absolute value
  }
  if (x + wide > LCD_WIDTH) {
    visibleEnd = LCD_WIDTH - x;
  }

  uint8_t rowsDrawn = 0;
  while (height > 0) {
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx++) {
      stripPointers[(y / 8) + rowsDrawn][x + xx] = value;
    }
    height -= 8;
    rowsDrawn++;
  }
}

// ---- 2bpp colour screen support --------------------------------------------------------------
// This variant is only ever built for the 160x80 colour panel, so the SMALL/LARGE glyph cell
// sizes are fixed constants here, matching Display.hpp's LCD_160x80 branch.
namespace {
constexpr uint8_t kSmallW = 12, kSmallH = 16;
constexpr uint8_t kLargeW = 24, kLargeH = 32;
} // namespace

void LCD::refreshColor() {
  // This panel scans COLUMN-MAJOR: within any drawing window the address auto-increments down a
  // column (y) first, then across (x) -- see the mono sendPixels() path, where each source byte
  // is one x-column's 8 vertical pixels. So we must emit one full column (y=0..H-1) at a time.
  // It also runs with INVON (hardware inversion, see the init sequence), so the boot logo stores
  // ~colour on the wire; we do the same, inverting each palette entry as it is sent.
  static uint8_t colRGB[LCD_HEIGHT * 2]; // one column of expanded RGB565, reused every column
  setDrawingWindow(0, 0, LCD_WIDTH, LCD_HEIGHT);
  for (uint16_t x = 0; x < LCD_WIDTH; x++) {
    for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
      uint8_t  packed   = screenBuffer[y * (LCD_WIDTH / 4) + (x / 4)];
      uint8_t  index    = (packed >> ((x % 4) * 2)) & 0x3;
      uint16_t rgb      = (uint16_t)~activePalette2bpp[index]; // INVON: send the inverse of the true colour
      colRGB[y * 2]     = (uint8_t)(rgb >> 8);
      colRGB[y * 2 + 1] = (uint8_t)(rgb & 0xFF);
    }
    SPI_CLASS::fastSend(colRGB, sizeof(colRGB));
  }
}

void LCD::fillRect2bpp(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h, uint8_t colorIndex) {
  for (uint8_t y = y0; y < y0 + h && y < LCD_HEIGHT; y++) {
    for (uint8_t x = x0; x < x0 + w && x < LCD_WIDTH; x++) {
      setPixel2bpp(x, y, colorIndex);
    }
  }
}

void LCD::plotRadialSegment(uint8_t cx, uint8_t cy, float angle, uint8_t rInner, uint8_t rOuter, uint8_t colorIndex) {
  const float c = cosf(angle), s = sinf(angle);
  for (uint8_t r = rInner; r <= rOuter; r++) {
    int16_t x = (int16_t)(cx + c * r);
    int16_t y = (int16_t)(cy + s * r);
    if (x >= 0 && y >= 0 && x < LCD_WIDTH && y < LCD_HEIGHT) {
      setPixel2bpp((uint8_t)x, (uint8_t)y, colorIndex);
    }
  }
}

void LCD::drawRing2bpp(uint8_t cx, uint8_t cy, uint8_t r, uint8_t thickness, uint8_t colorIndex, float startAngle, float endAngle) {
  const uint8_t   rInner       = (thickness / 2 >= r) ? 0 : r - thickness / 2;
  const uint8_t   rOuter       = r + thickness / 2;
  const uint16_t  innerSquared = rInner * rInner;
  const uint16_t  outerSquared = rOuter * rOuter;
  constexpr float kTwoPi       = 6.28318531f;

  // Rasterise the annular sector directly. Sampling radial spokes leaves holes
  // after float-to-integer truncation; testing every pixel guarantees a solid
  // ring on the actual 160x80 framebuffer.
  for (int16_t y = (int16_t)cy - rOuter; y <= (int16_t)cy + rOuter; y++) {
    for (int16_t x = (int16_t)cx - rOuter; x <= (int16_t)cx + rOuter; x++) {
      const int16_t  dx = x - cx, dy = y - cy;
      const uint16_t distanceSquared = dx * dx + dy * dy;
      if (distanceSquared < innerSquared || distanceSquared > outerSquared) {
        continue;
      }
      float angle = atan2f((float)dy, (float)dx);
      if (angle < 0.0f) {
        angle += kTwoPi;
      }
      if (angle < startAngle) {
        angle += kTwoPi;
      }
      if (angle >= startAngle && angle <= endAngle && x >= 0 && y >= 0 && x < LCD_WIDTH && y < LCD_HEIGHT) {
        setPixel2bpp((uint8_t)x, (uint8_t)y, colorIndex);
      }
    }
  }
}

void LCD::drawTick2bpp(uint8_t cx, uint8_t cy, float angle, uint8_t rInner, uint8_t rOuter, uint8_t colorIndex) { plotRadialSegment(cx, cy, angle, rInner, rOuter, colorIndex); }

void LCD::drawGlyph2bpp(uint16_t charCode, FontStyle fontStyle, uint8_t x, uint8_t y, uint8_t colorIndex) {
  const uint8_t *currentFont;
  uint8_t        fontWidth, fontHeight;
  uint16_t       index;

  if (fontStyle == FontStyle::EXTRAS) {
    currentFont = ExtraFontChars;
    index       = charCode;
    fontWidth   = kSmallW;
    fontHeight  = kSmallH;
  } else {
    if (charCode <= 0x01) {
      return;
    }
    const bool small = (fontStyle == FontStyle::SMALL);
    fontWidth        = small ? kSmallW : kLargeW;
    fontHeight       = small ? kSmallH : kLargeH;
    currentFont      = small ? FontSectionInfo.font06_start_ptr : FontSectionInfo.font12_start_ptr;
    index            = charCode - 2;
  }

  const uint8_t *charPointer = currentFont + ((fontWidth * (fontHeight / 8)) * index);
  for (uint8_t strip = 0; strip < fontHeight / 8; strip++) {
    for (uint8_t col = 0; col < fontWidth; col++) {
      const uint8_t bits = charPointer[strip * fontWidth + col];
      if (bits == 0) {
        continue;
      }
      for (uint8_t bit = 0; bit < 8; bit++) {
        if (bits & (1 << bit)) {
          setPixel2bpp(x + col, y + strip * 8 + bit, colorIndex);
        }
      }
    }
  }
}

void LCD::drawTextColor(const char *str, uint8_t x, uint8_t y, FontStyle fontStyle, uint8_t colorIndex, uint8_t maxChars) {
  const uint8_t *next      = reinterpret_cast<const uint8_t *>(str);
  const uint8_t  fontWidth = (fontStyle == FontStyle::SMALL) ? kSmallW : (fontStyle == FontStyle::LARGE ? kLargeW : kSmallW);
  while (*next && maxChars--) {
    uint16_t index;
    if (*next <= 0xF0) {
      index = *next;
      next++;
    } else {
      if (!next[1]) {
        return;
      }
      index = (next[0] - 0xF0) * 0xFF - 15 + next[1];
      next += 2;
    }
    drawGlyph2bpp(index, fontStyle, x, y, colorIndex);
    x += fontWidth;
  }
}

void LCD::drawBitmap2bpp(const uint8_t *bitmap, uint8_t width, uint8_t height, uint8_t x, uint8_t y, uint8_t colorIndex) {
  const uint8_t strips = (height + 7) / 8;
  for (uint8_t strip = 0; strip < strips; strip++) {
    for (uint8_t gx = 0; gx < width; gx++) {
      const uint8_t bits = bitmap[strip * width + gx];
      for (uint8_t gy = 0; gy < 8; gy++) {
        const uint8_t py = strip * 8 + gy;
        if (py < height && (bits & (1u << gy))) {
          setPixel2bpp(x + gx, y + py, colorIndex);
        }
      }
    }
  }
}

void LCD::drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear) {
  // Ensure coordinates are within bounds
  if (x0 >= LCD_WIDTH || y0 >= LCD_HEIGHT || x1 >= LCD_WIDTH || y1 >= LCD_HEIGHT) {
    return;
  }

  // Calculate the height in rows
  uint8_t startRow  = y0 / 8;
  uint8_t endRow    = y1 / 8;
  uint8_t startMask = 0xFF << (y0 % 8);
  uint8_t endMask   = 0xFF >> (7 - (y1 % 8));

  for (uint8_t row = startRow; row <= endRow; row++) {
    uint8_t mask = 0xFF;
    if (row == startRow) {
      mask &= startMask;
    }
    if (row == endRow) {
      mask &= endMask;
    }

    for (uint8_t x = x0; x <= x1; x++) {
      if (clear) {
        stripPointers[row][x] &= ~mask;
      } else {
        stripPointers[row][x] |= mask;
      }
    }
  }
}

#endif // LCD_160x80
