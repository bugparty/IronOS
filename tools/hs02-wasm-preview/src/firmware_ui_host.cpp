// Browser host for the HS-02 colour UI. It intentionally includes the production
// gauge source below: changes to that source are what the preview executes.
#ifndef MODEL_HS02
#define MODEL_HS02
#endif
#define HARDWARE_MAX_WATTAGE_X10 1000

#include "Settings.h"
#include "Translation.h"
#include "ui_drawing.hpp"
#include "TipThermoModel.h"
#include "power.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr uint16_t kWidth = 160, kHeight = 80, kBytes = kWidth * kHeight / 4;
uint8_t frame[kBytes];
const uint16_t kDefaultPalette[4] = {0x0862, 0xF77C, 0xFBC5, 0x45B8};
const uint16_t *activePalette = kDefaultPalette;
using TickType_t = uint32_t;
constexpr TickType_t TICKS_SECOND = 1000;
constexpr TickType_t TICKS_MIN    = 60 * TICKS_SECOND;
TickType_t lastButtonTime = 0, lastMovementTime = 0;
struct State { uint16_t tip = 428, target = 430, boost = 480, wattsX10 = 425, voltsX10 = 200; uint8_t source = 0; bool disconnected = false; } state;
void pixel(uint8_t x, uint8_t y, uint8_t color) {
  if (x >= kWidth || y >= kHeight) return;
  uint16_t i = y * (kWidth / 4) + x / 4; uint8_t shift = (x % 4) * 2;
  frame[i] = (frame[i] & ~(3u << shift)) | ((color & 3u) << shift);
}
void glyph(uint16_t code, FontStyle style, uint8_t x, uint8_t y, uint8_t color) {
  if (code <= 1) return;
  bool small = style == FontStyle::SMALL;
  uint8_t width = small ? 12 : 24, height = small ? 16 : 32;
  const uint8_t *font = small ? FontSectionInfo.font06_start_ptr : FontSectionInfo.font12_start_ptr;
  const uint8_t *p = font + (width * (height / 8)) * (code - 2);
  for (uint8_t strip = 0; strip < height / 8; ++strip) for (uint8_t col = 0; col < width; ++col) {
    uint8_t bits = p[strip * width + col];
    for (uint8_t bit = 0; bit < 8; ++bit) if (bits & (1u << bit)) pixel(x + col, y + strip * 8 + bit, color);
  }
}
}

// Production-generated glyph table: this gives the Wasm screen the same font bytes as HS-02 EN.
#include "../../../../source/Core/Gen/Translation.EN.cpp"

uint16_t getSettingValue(const enum SettingsOptions option) {
  switch (option) {
  case SolderingTemp: return state.target;
  case BoostTemp: return state.boost;
  case Sensitivity: return 1;
  case SleepTime: return 1;
  case VoltageDiv: return 0;
  default: return 0;
  }
}
bool isTipDisconnected() { return state.disconnected; }
namespace TipThermoModel { TemperatureType_t getTipInC() { return state.tip; } }
WasmWattHistory x10WattHistory;
uint32_t WasmWattHistory::average() const { return state.wattsX10; }
uint32_t getInputVoltageX10(uint16_t, uint8_t) { return state.voltsX10; }
uint8_t getPowerSourceNumber() { return state.source; }
TickType_t xTaskGetTickCount() { return 0; }
uint32_t getSleepTimeout() { return TICKS_MIN; } // Preview deliberately renders “1m”.

void Display::clearColor() { memset(frame, 0, sizeof(frame)); }
void Display::setColorPalette(const uint16_t *palette) { activePalette = palette ? palette : kDefaultPalette; }
void Display::fillRectColor(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t c) { for (uint16_t yy=y; yy<y+h; ++yy) for (uint16_t xx=x; xx<x+w; ++xx) pixel(xx, yy, c); }
void Display::drawTickColor(uint8_t cx, uint8_t cy, float a, uint8_t in, uint8_t out, uint8_t c) { for (uint8_t r=in; r<=out; ++r) pixel((int16_t)(cx + cosf(a)*r), (int16_t)(cy + sinf(a)*r), c); }
void Display::drawRingColor(uint8_t cx, uint8_t cy, uint8_t r, uint8_t thick, uint8_t c, float start, float end) {
  uint8_t in = thick/2 >= r ? 0 : r-thick/2, out = r+thick/2; constexpr float twoPi = 6.28318531f;
  for (int16_t y=cy-out; y<=cy+out; ++y) for (int16_t x=cx-out; x<=cx+out; ++x) {
    int16_t dx=x-cx, dy=y-cy; uint16_t d=dx*dx+dy*dy; if (d < in*in || d > out*out) continue;
    float a=atan2f((float)dy,(float)dx); if(a<0)a+=twoPi; if(a<start)a+=twoPi; if(a>=start && a<=end) pixel(x,y,c);
  }
}
void Display::printColor(const char *s, uint8_t x, uint8_t y, FontStyle style, uint8_t c, uint8_t max) {
  uint8_t w = style == FontStyle::SMALL ? 12 : 24; while (*s && max--) { glyph((uint8_t)*s++,style,x,y,c); x += w; }
}
void Display::printNumberColor(uint16_t n, uint8_t places, uint8_t x, uint8_t y, FontStyle style, uint8_t c) {
  char b[7] = {}; for (uint8_t i=places; i; --i) { b[i-1] = 2+n%10; n/=10; } for (uint8_t i=0; i+1<places && b[i]==2; ++i) b[i]=0x12; printColor(b,x,y,style,c);
}
void Display::drawBitmapColor(const uint8_t *bitmap, uint8_t width, uint8_t height, uint8_t x, uint8_t y, uint8_t c) {
  for(uint8_t strip=0; strip<(height+7)/8; strip++) for(uint8_t gx=0; gx<width; gx++) {
    const uint8_t bits = bitmap[strip*width+gx];
    for(uint8_t gy=0; gy<8; gy++) { const uint8_t py=strip*8+gy; if(py<height && (bits&(1u<<gy))) pixel(x+gx,y+py,c); }
  }
}

void ui_draw_home_gauge_soldering(bool boostModeOn);

// Include the actual committed UI implementation, not a copied browser version.
#include "../../../../source/Core/Threads/UI/drawing/color_160x80/draw_home_gauge.cpp"

extern "C" {
void hs02_ui_set_state(uint16_t tip, uint16_t target, uint16_t boost, uint16_t wattsX10, uint16_t voltsX10, uint8_t source, uint8_t disconnected) {
  state = {tip, target, boost, wattsX10, voltsX10, source, disconnected != 0};
}
void hs02_ui_render(uint8_t mode) {
  Display::clearColor(); if (mode == 0) ui_draw_home_gauge_idle(state.tip); else if (mode == 1) ui_draw_home_gauge_soldering(false); else if (mode == 2) ui_draw_home_gauge_soldering(true); else ui_draw_home_gauge_sleep(state.tip);
}
const uint8_t *hs02_ui_framebuffer() { return frame; }
uint32_t hs02_ui_framebuffer_size() { return sizeof(frame); }
const uint16_t *hs02_ui_palette() { return activePalette; }
}
