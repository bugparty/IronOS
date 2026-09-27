#pragma once
#include <stdint.h>
// Fusion Pixel 10px proportional, the HS-02 Wash UI font.
// Source: TakWolf/fusion-pixel-font, SIL Open Font License 1.1.
//
// The glyph subset and the translated labels are generated per language into
// Translation.<LANG>.cpp by Translations/wash_font.py (LCD_160x80 builds only).
namespace Hs02WashFont {
// 8 columns x two 8-row strips, one byte per column per strip, LSB = top row.
// Row 0 is the font's ascent line; caps start kTopPad rows lower, and the rows
// above them hold marks such as the ring of Å.
constexpr uint8_t kGlyphWidth = 8, kGlyphHeight = 16, kGlyphBytes = 16, kTopPad = 2;

extern const uint8_t  kGlyphCount;
extern const uint16_t kCodepoints[];
extern const uint8_t  kAdvance[];
extern const uint8_t  kGlyphs[][kGlyphBytes];

struct Labels {
  const char *ready, *set, *boost, *sleep; // UTF-8
};
extern const Labels kLabels;

inline int16_t glyphIndex(uint16_t codepoint) {
  for (uint8_t i = 0; i < kGlyphCount; i++) {
    if (kCodepoints[i] == codepoint) {
      return i;
    }
  }
  return -1;
}

// Decodes one UTF-8 character (up to 3 bytes, enough for the BMP) and advances text.
inline uint16_t nextCodepoint(const char *&text) {
  const uint8_t lead = static_cast<uint8_t>(*text++);
  if (lead < 0x80) {
    return lead;
  }
  const uint8_t extra = lead >= 0xE0 ? 2 : 1;
  uint16_t      cp    = lead & (extra == 2 ? 0x0F : 0x1F);
  for (uint8_t i = 0; i < extra && (static_cast<uint8_t>(*text) & 0xC0) == 0x80; i++) {
    cp = (cp << 6) | (static_cast<uint8_t>(*text++) & 0x3F);
  }
  return cp;
}
} // namespace Hs02WashFont
