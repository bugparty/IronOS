#!/usr/bin/env python3
import os
import sys
from PIL import Image, ImageSequence

try:
    import cv2
    HAS_CV2 = True
except ImportError:
    HAS_CV2 = False

MAX_FLASH_BYTES = 54000
SCREEN_W, SCREEN_H = 160, 80

def parse_color(s):
    s = s.strip().lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    if len(s) != 6:
        print(f"Error: invalid color '{s}' (use RRGGBB or #RGB)")
        sys.exit(1)
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))

def flatten(frame, bg):
    # Composite any transparency (GIF/WebP/APNG) onto the background color.
    rgba = frame.convert("RGBA")
    base = Image.new("RGBA", rgba.size, bg + (255,))
    return Image.alpha_composite(base, rgba).convert("RGB")

def load_frames(input_path, target_fps=12, max_frames=20, bg=(0, 0, 0)):
    ext = os.path.splitext(input_path)[1].lower()
    frames = []
    if ext in [".gif", ".webp", ".apng"]:
        im = Image.open(input_path)
        src_duration = im.info.get("duration", 80) or 80
        if src_duration <= 10:
            src_duration = 100  # browsers treat tiny GIF delays as 100ms
        src_fps = 1000.0 / src_duration
        step = max(1, round(src_fps / target_fps))
        delay_ms = int(step * src_duration)
        for idx, frame in enumerate(ImageSequence.Iterator(im)):
            if idx % step == 0:
                frames.append(flatten(frame, bg))
                if len(frames) >= max_frames:
                    break
    else:
        if not HAS_CV2:
            print("Error: OpenCV (cv2) is required for video files.")
            sys.exit(1)
        cap = cv2.VideoCapture(input_path)
        if not cap.isOpened():
            print(f"Error: Cannot open {input_path}")
            sys.exit(1)
        src_fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
        step = max(1, round(src_fps / target_fps))
        delay_ms = int(1000.0 * step / src_fps)
        idx = 0
        while len(frames) < max_frames:
            ret, bgr = cap.read()
            if not ret:
                break
            if idx % step == 0:
                frames.append(Image.fromarray(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)))
            idx += 1
        cap.release()
    return frames, delay_ms

def fit_frame(img, tw, th, bg, mode):
    # stretch   : ignore aspect ratio, fill the whole target
    # letterbox : keep aspect ratio, pad the rest with the background color
    if mode == "stretch":
        return img.resize((tw, th), Image.Resampling.LANCZOS)
    w, h = img.size
    s = min(tw / w, th / h)
    nw = max(1, min(tw, round(w * s)))
    nh = max(1, min(th, round(h * s)))
    resized = img.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (tw, th), bg)
    canvas.paste(resized, ((tw - nw) // 2, (th - nh) // 2))
    return canvas

def encode_runs(vals):
    # byte = (idx<<4)|run  (run 1..15)
    # byte = (idx<<4)|0, next byte = run (16..255)
    out = bytearray()
    i, n = 0, len(vals)
    while i < n:
        v = vals[i]
        run = 1
        while i + run < n and run < 255 and vals[i + run] == v:
            run += 1
        if run <= 15:
            out.append((v << 4) | run)
        else:
            out.append(v << 4)
            out.append(run)
        i += run
    return bytes(out)

def build(frames, colors, scale, bg, fit):
    sw, sh = SCREEN_W // scale, SCREEN_H // scale
    small = [fit_frame(f, sw, sh, bg, fit) for f in frames]

    # Quantize ALL frames together -> one shared palette
    sheet = Image.new("RGB", (sw, sh * len(small)))
    for k, f in enumerate(small):
        sheet.paste(f, (0, k * sh))
    q = sheet.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)

    raw_pal = q.getpalette() or []
    raw_pal = (raw_pal + [0] * (16 * 3))[:16 * 3]
    palette = []
    for i in range(16):
        r, g, b = raw_pal[i * 3:i * 3 + 3]
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        palette.append(rgb565 ^ 0xFFFF)  # panel runs INVON

    blobs = []
    for k in range(len(small)):
        px = q.crop((0, k * sh, sw, (k + 1) * sh)).load()
        vals = [px[x, y] & 0x0F for x in range(sw) for y in range(sh)]  # column-major
        blobs.append(encode_runs(vals))
    return palette, blobs

def generate_header(input_path, output_path, fps=12, max_frames=16, colors=8, scale=2,
                    bg=(0, 0, 0), fit="letterbox"):
    colors = max(2, min(16, colors))
    if scale not in (1, 2):
        print("scale must be 1 or 2")
        sys.exit(1)
    if fit not in ("letterbox", "stretch"):
        print("fit must be 'letterbox' or 'stretch'")
        sys.exit(1)
    frames, delay_ms = load_frames(input_path, fps, max_frames, bg)
    if not frames:
        print("Error: No frames extracted.")
        return
    print(f"Source {frames[0].size[0]}x{frames[0].size[1]}, bg=#{bg[0]:02x}{bg[1]:02x}{bg[2]:02x}, fit={fit}, scale={scale}")
    palette, blobs = build(frames, colors, scale, bg, fit)

    all_data = bytearray()
    offsets, lengths = [], []
    for idx, b in enumerate(blobs):
        offsets.append(len(all_data))
        lengths.append(len(b))
        all_data.extend(b)
        print(f"Frame {idx:02d}: {len(b):5d} bytes (total: {len(all_data):6d} bytes)")

    if len(all_data) > MAX_FLASH_BYTES:
        print(f"\n[WARNING] Total size ({len(all_data)}) exceeds budget ({MAX_FLASH_BYTES})!")

    print(f"Frame delay: {delay_ms} ms ({1000.0 / delay_ms:.1f} fps)")
    n = len(frames)
    with open(output_path, "w") as f:
        f.write("// Auto-generated Boot Animation for Fnirsi HS-02 (IronOS) - palette RLE v2\n")
        f.write("#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n")
        f.write("#define FNIRSI_LOGO_ANIMATION 1\n")
        f.write(f"#define FNIRSI_LOGO_W {SCREEN_W}\n")
        f.write(f"#define FNIRSI_LOGO_H {SCREEN_H}\n")
        f.write(f"#define FNIRSI_LOGO_SCALE {scale}\n")
        f.write("#define FNIRSI_LOGO_X 0\n#define FNIRSI_LOGO_Y 0\n")
        f.write(f"#define FNIRSI_LOGO_FRAME_COUNT {n}\n")
        f.write(f"#define FNIRSI_LOGO_FRAME_DELAY_MS {delay_ms}\n\n")
        f.write("static const uint16_t fnirsiBootLogoPalette[16] = {\n  ")
        f.write(", ".join(f"0x{p:04x}" for p in palette))
        f.write("\n};\n\n")
        f.write(f"static const uint32_t fnirsiBootLogoOffsets[{n}] = {{\n  ")
        f.write(", ".join(str(o) for o in offsets))
        f.write("\n};\n\n")
        f.write(f"static const uint32_t fnirsiBootLogoLengths[{n}] = {{\n  ")
        f.write(", ".join(str(l) for l in lengths))
        f.write("\n};\n\n")
        f.write(f"static const uint8_t fnirsiBootLogoData[{len(all_data)}] = {{\n")
        for i in range(0, len(all_data), 16):
            chunk = all_data[i:i + 16]
            f.write("  " + ",".join(f"0x{b:02x}" for b in chunk) + ("," if i + 16 < len(all_data) else "") + "\n")
        f.write("};\n")
    print(f"\nDone! {output_path} ({n} frames, {len(all_data)} bytes + 32 palette)")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 convert_anim.py <input.gif|mp4> [fps=12] [max_frames=16] [colors=8 (2..16)] [scale=2 (1|2)] [bg=000000] [fit=letterbox|stretch]")
        print("  bg  : background color for transparent GIFs and letterbox bars (RRGGBB or #RGB)")
        print("  fit : letterbox (keep aspect ratio, pad with bg) or stretch (fill 160x80)")
        sys.exit(1)
    a = sys.argv
    generate_header(a[1], "./source/Core/BSP/Fnirsi/FnirsiBootLogo.h",
                    fps=int(a[2]) if len(a) > 2 else 12,
                    max_frames=int(a[3]) if len(a) > 3 else 16,
                    colors=int(a[4]) if len(a) > 4 else 8,
                    scale=int(a[5]) if len(a) > 5 else 2,
                    bg=parse_color(a[6]) if len(a) > 6 else (0, 0, 0),
                    fit=a[7].lower() if len(a) > 7 else "letterbox")
