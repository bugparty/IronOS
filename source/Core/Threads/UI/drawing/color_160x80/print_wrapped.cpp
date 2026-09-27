#include "ui_drawing.hpp"
#ifdef LCD_160x80

// Full-screen messages were written for 96x16/128x32 panels. On this one the large font fits
// 6 glyphs per line and printWholeScreen() neither wraps nor scrolls, so longer messages were
// cut off. This word-wraps a message in the font it was encoded for (the generator encodes
// single-line messages for the large font with a leading 0x01, multi-line ones for the small
// font; the two fonts use different glyph indices, so a message cannot switch fonts).

namespace {
// Same glyph encoding as Display::print(): one byte, or two when the first is > 0xF0.
const uint8_t *nextGlyph(const uint8_t *p) { return (p[0] > 0xF0 && p[1]) ? p + 2 : p + 1; }
} // namespace

bool ui_print_wrapped(const char *text, uint8_t y, bool draw) {
  const uint8_t *p     = reinterpret_cast<const uint8_t *>(text);
  const bool     large = p[0] == '\x01';
  if (large) {
    p++;
  }
  const FontStyle font    = large ? FontStyle::LARGE : FontStyle::SMALL;
  const uint8_t   width   = large ? FONT_LARGE_WIDTH : FONT_SMALL_WIDTH;
  const uint8_t   height  = large ? FONT_LARGE_HEIGHT : FONT_SMALL_HEIGHT;
  const uint8_t   space   = static_cast<uint8_t>((large ? LargeSymbolSpace : SmallSymbolSpace)[0]);
  const uint8_t   columns = DISPLAY_WIDTH / width;

  bool    fits = true;
  uint8_t row = 0, column = 0;
  bool    pendingSpace = false;
  while (*p) {
    if (*p == '\x01') { // explicit line break; a trailing one is ignored
      if (p[1]) {
        row++;
        column = 0;
      }
      pendingSpace = false;
      p++;
      continue;
    }
    if (*p == space) {
      pendingSpace = column > 0;
      p++;
      continue;
    }
    uint8_t length = 0;
    for (const uint8_t *q = p; *q && *q != space && *q != '\x01'; q = nextGlyph(q)) {
      length++;
    }
    const uint8_t gap = pendingSpace ? 1 : 0;
    if (column > 0 && column + gap + length > columns) { // wrap before this word
      row++;
      column = 0;
    } else {
      column += gap;
    }
    pendingSpace = false;
    // A word longer than a line is hard-broken across lines.
    for (uint8_t left = length; left > 0;) {
      if (y + (row + 1) * height > DISPLAY_HEIGHT) {
        return false; // ran off the bottom
      }
      const uint8_t chunk = (left < columns - column) ? left : columns - column;
      if (draw) {
        Display::setCursor(column * width, y + row * height);
        Display::print(reinterpret_cast<const char *>(p), font, chunk);
      }
      for (uint8_t i = 0; i < chunk; i++) {
        p = nextGlyph(p);
      }
      column += chunk;
      left -= chunk;
      if (left > 0) {
        fits = false;
        row++;
        column = 0;
      }
    }
  }
  return fits;
}
#endif
