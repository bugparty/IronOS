#!/usr/bin/env python3
"""Turn the host renderer's frames into side-by-side contact sheets.

usage: render.py OUT_DIR LANG [LANG...]
Reads OUT_DIR/<LANG>/index.tsv + *.ppm, aligns frames across languages by
(group, label), and writes OUT_DIR/sheets/<nn>-<group>.png with one row per
screen and one column per language (2x scale, 1px frame so overflow is visible).
"""
import os, re, sys
from PIL import Image, ImageDraw

SCALE, ROWS_PER_SHEET, LABEL_W, PAD = 2, 12, 260, 8
out, langs = sys.argv[1], sys.argv[2:]


def load(lang):
    rows = []
    for line in open(os.path.join(out, lang, "index.tsv"), encoding="utf8"):
        f, group, label = line.rstrip("\n").split("\t")
        rows.append((group, label, os.path.join(out, lang, f)))
    return rows


frames = {l: load(l) for l in langs}
# Keep the first language's order; value labels differ only in the number, so key on
# (group, label, occurrence) to line up option N of each language.
keys, seen = [], {}
table = {}
for l in langs:
    counts = {}
    for g, lab, path in frames[l]:
        base = re.sub(r" value \d+", " value", lab)
        n = counts.get((g, base), 0)
        counts[(g, base)] = n + 1
        k = (g, base, n)
        if k not in seen:
            seen[k] = len(keys)
            keys.append(k)
        table[(k, l)] = path

os.makedirs(os.path.join(out, "sheets"), exist_ok=True)
cell_w, cell_h = 160 * SCALE + 2, 80 * SCALE + 2
groups = []
for k in keys:
    if not groups or groups[-1][0] != k[0]:
        groups.append((k[0], []))
    groups[-1][1].append(k)

sheet_no = 0
for group, ks in groups:
    for start in range(0, len(ks), ROWS_PER_SHEET):
        chunk = ks[start:start + ROWS_PER_SHEET]
        W = LABEL_W + len(langs) * (cell_w + PAD) + PAD
        H = 24 + len(chunk) * (cell_h + PAD) + PAD
        im = Image.new("RGB", (W, H), (60, 60, 60))
        d = ImageDraw.Draw(im)
        d.text((PAD, 6), f"{group}  ({start + 1}-{start + len(chunk)} of {len(ks)})", fill=(255, 255, 0))
        for i, l in enumerate(langs):
            d.text((LABEL_W + i * (cell_w + PAD), 6), l, fill=(255, 255, 0))
        for r, k in enumerate(chunk):
            y = 24 + r * (cell_h + PAD)
            d.text((PAD, y + 4), f"{k[1]}" + (f" #{k[2] + 1}" if k[2] else ""), fill=(230, 230, 230))
            for i, l in enumerate(langs):
                x = LABEL_W + i * (cell_w + PAD)
                d.rectangle([x, y, x + cell_w - 1, y + cell_h - 1], outline=(255, 0, 255))
                p = table.get((k, l))
                if p:
                    im.paste(Image.open(p).resize((160 * SCALE, 80 * SCALE), Image.NEAREST), (x + 1, y + 1))
                else:
                    d.text((x + 8, y + 8), "(no frame)", fill=(255, 120, 120))
        sheet_no += 1
        name = re.sub(r"[^A-Za-z0-9]+", "-", group).strip("-").lower()
        im.save(os.path.join(out, "sheets", f"{sheet_no:02d}-{name}.png"))
print(f"{sheet_no} sheets -> {os.path.join(out, 'sheets')}")
