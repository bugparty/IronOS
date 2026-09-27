#!/usr/bin/env python3
"""The HS-02 colour screens' "Wash" UI font: per-language glyph subset + labels.

The HS-02 (LCD_160x80) home/soldering/sleep screens draw their small labels
(READY, SET, BOOST, SLEEP) in Fusion Pixel 10px proportional instead of the
shared IronOS fonts. For each language build, make_translation.py calls
build_wash_data() to pick the translated labels (falling back to English per
label) and emit only the glyphs those labels and the screens' fixed text need.

The glyphs come from fonts/fusion-pixel-10px-proportional-hs02.bdf, a subset
(U+0020..U+052F: Latin, Greek, Cyrillic) of TakWolf/fusion-pixel-font (SIL
OFL 1.1, see fonts/OFL.txt). Regenerate it from a release BDF with:

    ./wash_font.py subset fusion-pixel-10px-proportional-latin.bdf
"""

import argparse
import functools
import logging
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

from bdflib import reader as bdfreader
from bdflib.model import Font

HERE = Path(__file__).resolve().parent
FONT_PATH = HERE / "fonts" / "fusion-pixel-10px-proportional-hs02.bdf"
SUBSET_RANGE = (0x20, 0x52F)

# Glyph cell: 8 columns x two 8-row strips, one byte per column per strip, LSB =
# top row of the strip. Row 0 is the font's ascent line (room for Å/Й marks).
CELL_W, CELL_H = 8, 16

# Fixed ASCII the drawing code uses besides the labels (digits, units, "DC", "TP").
BASE_CHARS = " 0123456789.DCVTPWms"

LABEL_IDS = ("Ready", "Set", "Boost", "Sleep")


@dataclass
class WashData:
    labels: Dict[str, str]  # label id -> text (UTF-8 when written out)
    codepoints: List[int]
    advances: List[int]
    glyphs: List[bytes]  # CELL_W * CELL_H / 8 bytes each


@functools.lru_cache(maxsize=None)
def wash_font() -> Font:
    with open(FONT_PATH, "rb") as f:
        return bdfreader.read_bdf(f)


def render_cell(codepoint: int) -> Optional[bytes]:
    """The glyph in the 8x16 cell format, or None if missing or too big."""
    font = wash_font()
    if codepoint not in font.codepoints():
        return None
    glyph = font[codepoint]
    ascent = int(font[b"FONT_ASCENT"])
    left, bottom, width, height = glyph.get_bounding_box()
    cell = bytearray(CELL_W * CELL_H // 8)
    # glyph.data is bottom row first; within a row the LSB is the right-most pixel.
    for row_from_bottom, bits in enumerate(glyph.data):
        y = ascent - (bottom + row_from_bottom) - 1
        for col in range(width):
            if not bits & (1 << (width - 1 - col)):
                continue
            x = left + col
            if not (0 <= x < CELL_W and 0 <= y < CELL_H):
                return None
            cell[(y // 8) * CELL_W + x] |= 1 << (y % 8)
    return bytes(cell)


def build_wash_data(lang: dict, english: dict) -> WashData:
    """Labels for lang (English per missing/unrenderable label) and their glyphs."""
    wanted = lang.get("gaugeLabels", {})
    labels: Dict[str, str] = {}
    for label_id in LABEL_IDS:
        text = wanted.get(label_id) or english["gaugeLabels"][label_id]
        missing = sorted({c for c in text if render_cell(ord(c)) is None})
        if missing:
            logging.warning(
                f"gaugeLabels.{label_id} {text!r}: no HS-02 wash glyph for "
                f"{''.join(missing)!r}, using English"
            )
            text = english["gaugeLabels"][label_id]
        labels[label_id] = text

    chars = sorted(set(BASE_CHARS).union(*labels.values()), key=ord)
    glyphs = [render_cell(ord(c)) for c in chars]
    if any(g is None for g in glyphs):
        raise ValueError("HS-02 wash font is missing a base glyph")
    advances = [wash_font()[ord(c)].advance for c in chars]
    return WashData(labels, [ord(c) for c in chars], advances, glyphs)


def write_wash_data(data: WashData, f) -> None:
    def c_string(text: str) -> str:
        return "".join(f"\\x{b:02X}" for b in text.encode("utf-8"))

    f.write(
        "\n// HS-02 colour-screen labels and their Fusion Pixel glyphs (wash_font.py)\n"
    )
    f.write('#include "Hs02WashFont.hpp"\n')
    f.write("namespace Hs02WashFont {\n")
    f.write(f"extern const uint8_t kGlyphCount = {len(data.codepoints)};\n")
    f.write(
        "extern const uint16_t kCodepoints[] = {"
        + ", ".join(f"0x{cp:04X}" for cp in data.codepoints)
        + "};\n"
    )
    f.write(
        "extern const uint8_t kAdvance[] = {"
        + ", ".join(str(a) for a in data.advances)
        + "};\n"
    )
    f.write("extern const uint8_t kGlyphs[][kGlyphBytes] = {\n")
    for cp, glyph in zip(data.codepoints, data.glyphs):
        f.write(
            "    {" + ", ".join(f"0x{b:02X}" for b in glyph) + f"}}, // U+{cp:04X}\n"
        )
    f.write("};\n")
    f.write("extern const Labels kLabels = {\n")
    for label_id in LABEL_IDS:
        f.write(
            f'    "{c_string(data.labels[label_id])}", // {data.labels[label_id]}\n'
        )
    f.write("};\n")
    f.write("} // namespace Hs02WashFont\n")


def subset(source: Path) -> None:
    """Write FONT_PATH with only the glyphs in SUBSET_RANGE from a release BDF."""
    text = source.read_text(encoding="latin-1")
    header, rest = text.split("STARTCHAR", 1)
    blocks = re.findall(r"STARTCHAR.*?ENDCHAR\n", "STARTCHAR" + rest, re.S)
    kept = [
        b
        for b in blocks
        if SUBSET_RANGE[0]
        <= int(re.search(r"^ENCODING (-?\d+)", b, re.M).group(1))
        <= SUBSET_RANGE[1]
    ]
    header = re.sub(r"^CHARS \d+", f"CHARS {len(kept)}", header, flags=re.M)
    FONT_PATH.write_text(header + "".join(kept) + "ENDFONT\n", encoding="latin-1")
    print(f"{len(kept)} glyphs -> {FONT_PATH}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=["subset"])
    parser.add_argument(
        "source", type=Path, help="release fusion-pixel-10px-proportional-*.bdf"
    )
    args = parser.parse_args()
    subset(args.source)
    sys.exit(0)
