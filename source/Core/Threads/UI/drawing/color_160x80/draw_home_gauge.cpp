#include "Hs02InterSemiBoldFont.hpp"
#include "Hs02WashFont.hpp"
#include "OperatingModes.h"
#include "TipThermoModel.h"
#include "power.hpp"
#include "ui_drawing.hpp"

#ifdef LCD_160x80

namespace {
// The 2bpp framebuffer stores semantic colour indices.  Each page supplies its
// own four-colour mapping, so the Wash UI stays legible without gradients.
constexpr uint8_t  kBg = 0, kInk = 1, kAccent = 2, kMuted = 3;
constexpr uint8_t  kScreenWidth                           = 160;
constexpr uint16_t kIdlePalette[4]                        = {0x0862, 0xF77C, 0x45B8, 0x536A};
constexpr uint16_t kSolderPalette[4]                      = {0x28A0, 0xF77C, 0xFBC5, 0x9B48};
constexpr uint16_t kBoostPalette[4]                       = {0x30C0, 0xF77C, 0xFBC5, 0xB344};
constexpr uint16_t kSleepPalette[4]                       = {0x0924, 0xF77C, 0x45B8, 0x4B8F};
constexpr uint8_t  kWashGlyphI[Hs02WashFont::kGlyphBytes] = {4, 4, 0xFC, 4, 4, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0};
constexpr uint8_t  kWashGlyphU[Hs02WashFont::kGlyphBytes] = {0xFC, 0, 0, 0, 0xFC, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
constexpr uint8_t  kWashCustomGlyphAdvance                = 6;

void drawTemperatureNumber(TemperatureType_t temperature, uint8_t x, uint8_t y, uint8_t color) {
  uint8_t digits[3] = {};
  for (uint8_t i = 3; i > 0; --i) {
    digits[i - 1] = temperature % 10;
    temperature /= 10;
  }
  for (uint8_t i = 0; i < 3; ++i) {
    Display::drawBitmapColor(Hs02InterSemiBoldFont::kDigits[digits[i]], Hs02InterSemiBoldFont::kDigitWidth, Hs02InterSemiBoldFont::kDigitHeight, x + i * Hs02InterSemiBoldFont::kDigitWidth, y, color);
  }
}

uint8_t washGlyphAdvance(char character) {
  const int8_t index = Hs02WashFont::glyphIndex(character);
  return index < 0 ? 0 : Hs02WashFont::kAdvance[index];
}

uint8_t washGlyphLeftBearing(char character) {
  const int8_t index = Hs02WashFont::glyphIndex(character);
  if (index < 0) {
    return 0;
  }
  for (uint8_t x = 0; x < Hs02WashFont::kGlyphWidth; ++x) {
    for (uint8_t strip = 0; strip < Hs02WashFont::kGlyphBytes / Hs02WashFont::kGlyphWidth; ++strip) {
      if (Hs02WashFont::kGlyphs[index][strip * Hs02WashFont::kGlyphWidth + x]) {
        return x;
      }
    }
  }
  return 0;
}

uint8_t measureWashText(const char *text) {
  uint8_t width = 0;
  while (*text) {
    width += washGlyphAdvance(*text++);
  }
  return width;
}

uint8_t drawWashText(const char *text, uint8_t x, uint8_t y, uint8_t color) {
  while (*text) {
    const char   character = *text++;
    const int8_t index     = Hs02WashFont::glyphIndex(character);
    if (index >= 0) {
      Display::drawBitmapColor(Hs02WashFont::kGlyphs[index], Hs02WashFont::kGlyphWidth, Hs02WashFont::kGlyphHeight, x - washGlyphLeftBearing(character), y, color);
    }
    x += washGlyphAdvance(character);
  }
  return x;
}

// The important bottom-line telemetry uses a 6:5 integer scale of the native
// 10px Fusion Pixel glyphs: a true 12px visual size without another font blob.
uint8_t drawWashTextEmphasis(const char *text, uint8_t x, uint8_t y, uint8_t color) {
  while (*text) {
    const char   character = *text++;
    const int8_t index     = Hs02WashFont::glyphIndex(character);
    if (index >= 0) {
      const uint8_t bearing = washGlyphLeftBearing(character);
      for (uint8_t gy = 0; gy < Hs02WashFont::kGlyphHeight; ++gy) {
        for (uint8_t gx = bearing; gx < Hs02WashFont::kGlyphWidth; ++gx) {
          const uint8_t bits = Hs02WashFont::kGlyphs[index][(gy / 8) * Hs02WashFont::kGlyphWidth + gx];
          if (bits & (1u << (gy % 8))) {
            const uint8_t sx        = (gx - bearing) * 6 / 5;
            const uint8_t sy        = gy * 6 / 5;
            const uint8_t rawWidth  = ((gx - bearing + 1) * 6 / 5) - sx;
            const uint8_t rawHeight = ((gy + 1) * 6 / 5) - sy;
            const uint8_t sw        = rawWidth ? rawWidth : 1;
            const uint8_t sh        = rawHeight ? rawHeight : 1;
            Display::fillRectColor(x + sx, y + sy, sw, sh, color);
          }
        }
      }
    }
    x += (washGlyphAdvance(character) * 6 + 2) / 5;
  }
  return x;
}

uint8_t measureWashTextEmphasis(const char *text) {
  uint8_t width = 0;
  while (*text) {
    width += (washGlyphAdvance(*text++) * 6 + 2) / 5;
  }
  return width;
}

uint8_t digitCount(uint32_t value) {
  uint8_t count = 1;
  while (value >= 10) {
    value /= 10;
    ++count;
  }
  return count;
}

uint8_t measureWashUnsigned(uint32_t value) {
  char          digits[5];
  const uint8_t count = digitCount(value);
  for (uint8_t i = count; i > 0; --i) {
    digits[i - 1] = '0' + value % 10;
    value /= 10;
  }
  uint8_t width = 0;
  for (uint8_t i = 0; i < count; ++i) {
    width += washGlyphAdvance(digits[i]);
  }
  return width;
}

uint8_t drawWashUnsigned(uint32_t value, uint8_t x, uint8_t y, uint8_t color) {
  char    digits[5];
  uint8_t count = digitCount(value);
  for (uint8_t i = count; i > 0; --i) {
    digits[i - 1] = '0' + value % 10;
    value /= 10;
  }
  for (uint8_t i = 0; i < count; ++i) {
    char glyph[] = {digits[i], '\0'};
    x            = drawWashText(glyph, x, y, color);
  }
  return x;
}

// Value is expressed in tenths (for example 200 = 20.0).  The UI font is
// deliberately separate from the shared translation font, so it accepts ASCII.
uint8_t measureWashTenths(uint32_t value, char unit) {
  const char tail[] = {'.', static_cast<char>('0' + value % 10), unit, '\0'};
  return measureWashUnsigned(value / 10) + measureWashText(tail);
}

uint8_t drawWashTenths(uint32_t value, uint8_t x, uint8_t y, uint8_t color, char unit) {
  const uint32_t whole = value / 10;
  x                    = drawWashUnsigned(whole, x, y, color);
  char tail[]          = {static_cast<char>('0' + value % 10), unit, '\0'};
  x                    = drawWashText(".", x, y, color);
  return drawWashText(tail, x, y, color);
}

uint8_t drawWashTenthsEmphasis(uint32_t value, uint8_t x, uint8_t y, uint8_t color, char unit) {
  char     text[10] = {};
  uint8_t  pos      = 0;
  uint32_t whole    = value / 10;
  char     reversed[5];
  uint8_t  count = digitCount(whole);
  for (uint8_t i = 0; i < count; ++i) {
    reversed[i] = '0' + whole % 10;
    whole /= 10;
  }
  while (count) {
    text[pos++] = reversed[--count];
  }
  text[pos++] = '.';
  text[pos++] = '0' + value % 10;
  text[pos++] = unit;
  return drawWashTextEmphasis(text, x, y, color);
}

void drawSleepCountdown() {
#ifndef NO_SLEEP_MODE
  if (!getSettingValue(SettingsOptions::Sensitivity) || !getSettingValue(SettingsOptions::SleepTime)) {
    return;
  }
  const TickType_t lastEventTime = lastButtonTime < lastMovementTime ? lastMovementTime : lastButtonTime;
  const TickType_t downCount     = getSleepTimeout() - xTaskGetTickCount() + lastEventTime;
  const bool       minutes       = downCount >= TICKS_MIN;
  const uint32_t   amount        = minutes ? (downCount + TICKS_MIN - 1) / TICKS_MIN : downCount / 1000 + 1;
  const char       unit[]        = {minutes ? 'm' : 's', '\0'};
  char             number[5]     = {};
  uint8_t          digits        = digitCount(amount);
  uint32_t         remaining     = amount;
  for (uint8_t i = digits; i > 0; --i) {
    number[i - 1] = '0' + remaining % 10;
    remaining /= 10;
  }
  const uint8_t width  = measureWashTextEmphasis("SLEEP ") + measureWashTextEmphasis(number) + measureWashTextEmphasis(unit);
  const uint8_t x      = kScreenWidth - 6 - width;
  uint8_t       cursor = drawWashTextEmphasis("SLEEP ", x, 64, kInk);
  cursor               = drawWashTextEmphasis(number, cursor, 64, kInk);
  drawWashTextEmphasis(unit, cursor, 64, kInk);
#endif
}

void drawWashTopVoltage() {
  const uint32_t voltage = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
  const uint8_t  cursor  = drawWashText("DC ", 6, 5, kMuted);
  drawWashTenths(voltage, cursor, 5, kMuted, 'V');
}

void drawWashTipMicrovolts() {
  const uint32_t tipMicrovolts = TipThermoModel::convertTipRawADCTouV(getTipRawTemp(0));
  const uint8_t  width         = measureWashText("T") + kWashCustomGlyphAdvance + measureWashText("P ") + measureWashUnsigned(tipMicrovolts) + kWashCustomGlyphAdvance + measureWashText("V");
  const uint8_t  x             = kScreenWidth - 6 - width;
  uint8_t        cursor        = drawWashText("T", x, 5, kMuted);
  Display::drawBitmapColor(kWashGlyphI, Hs02WashFont::kGlyphWidth, Hs02WashFont::kGlyphHeight, cursor, 5, kMuted);
  cursor += kWashCustomGlyphAdvance;
  cursor = drawWashText("P ", cursor, 5, kMuted);
  cursor = drawWashUnsigned(tipMicrovolts, cursor, 5, kMuted);
  Display::drawBitmapColor(kWashGlyphU, Hs02WashFont::kGlyphWidth, Hs02WashFont::kGlyphHeight, cursor, 5, kMuted);
  drawWashText("V", cursor + kWashCustomGlyphAdvance, 5, kMuted);
}

void drawWashTopTelemetry() {
  drawWashTopVoltage();
  drawWashTipMicrovolts();
}

void drawWashHeating(TemperatureType_t current, TemperatureType_t target, uint32_t x10Watt, bool boostModeOn) {
  drawWashTopTelemetry();
  drawTemperatureNumber(current, 80 - (3 * Hs02InterSemiBoldFont::kDigitWidth) / 2, 18, kInk);

  if (boostModeOn) {
    drawWashText("BOOST", (kScreenWidth - measureWashText("BOOST")) / 2, 47, kAccent);
  } else {
    const uint8_t digits = digitCount(target);
    const uint8_t width  = measureWashText("SET ") + measureWashUnsigned(target);
    const uint8_t x      = (kScreenWidth - width) / 2;
    drawWashUnsigned(target, drawWashText("SET ", x, 47, kMuted), 47, kMuted);
  }

  drawWashTenthsEmphasis(x10Watt, 6, 64, kAccent, 'W');
  drawSleepCountdown();
}
} // namespace

void ui_draw_home_gauge_idle(TemperatureType_t tipTemp) {
  Display::setColorPalette(kIdlePalette);
  if (isTipDisconnected()) {
    return;
  }
  drawWashTopTelemetry();
  drawTemperatureNumber(tipTemp, 80 - (3 * Hs02InterSemiBoldFont::kDigitWidth) / 2, 18, kInk);

  const TemperatureType_t target = getSettingValue(SettingsOptions::SolderingTemp);
  const uint8_t           width  = measureWashText("SET ") + measureWashUnsigned(target);
  const uint8_t           x      = (kScreenWidth - width) / 2;
  drawWashUnsigned(target, drawWashText("SET ", x, 47, kMuted), 47, kMuted);
  drawWashTextEmphasis("READY", 112, 64, kAccent);
}

void ui_draw_home_gauge_soldering(bool boostModeOn) {
  Display::setColorPalette(boostModeOn ? kBoostPalette : kSolderPalette);
  const TemperatureType_t current = TipThermoModel::getTipInC();
  const TemperatureType_t target  = getSettingValue(boostModeOn ? SettingsOptions::BoostTemp : SettingsOptions::SolderingTemp);
  drawWashHeating(current, target, x10WattHistory.average(), boostModeOn);
}

void ui_draw_home_gauge_sleep(TemperatureType_t tipTemp) {
  Display::setColorPalette(kSleepPalette);
  drawTemperatureNumber(tipTemp, 80 - (3 * Hs02InterSemiBoldFont::kDigitWidth) / 2, 18, kInk);
  const uint32_t voltage    = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
  const uint8_t  valueWidth = measureWashTenths(voltage, 'V');
  const uint8_t  labelWidth = measureWashText("SLEEP ");
  const uint8_t  x          = (kScreenWidth - labelWidth - valueWidth) / 2;
  drawWashTenthsEmphasis(voltage, drawWashTextEmphasis("SLEEP ", x, 64, kMuted), 64, kMuted, 'V');
}

#endif
