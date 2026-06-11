#!/usr/bin/env python3
"""Convert a raw RGB banner into an indexed PNG suitable for an OpenOMF
tournament logo sprite.

Output contract (consumed by tools/expansion/gen.c):
  - 8-bit PALETTE PNG
  - palette index 0 is reserved/transparent (no opaque pixel uses it)
  - opaque pixels use palette indices 1..39 (<= 39 distinct colours)
The generator remaps index k -> tournament palette slot 128+k, so the logo
renders with the tournament's 40-colour palette range (128..167).
"""
import sys
from PIL import Image

def make_logo(src, dst, out_w, out_h, crop_top=0.16, crop_bot=0.84):
    im = Image.open(src).convert("RGB")
    w, h = im.size
    # Crop to the central band holding the emblem + title (drop smoke/empty).
    im = im.crop((0, int(h * crop_top), w, int(h * crop_bot)))
    # Downscale to the in-game logo size.
    im = im.resize((out_w, out_h), Image.LANCZOS)
    # Quantize to 39 colours (leaving index 0 free for transparency).
    q = im.quantize(colors=39, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)

    pal = q.getpalette()  # flat [r,g,b, r,g,b, ...] for indices 0..38
    src_idx = list(q.getdata())

    # Build a new palette shifted by one: new index 0 = transparent marker,
    # colours move to 1..39.
    new_pal = [255, 0, 255]  # index 0 (transparent, never drawn)
    new_pal += pal[: 39 * 3]
    # Pad palette to 40 entries.
    while len(new_pal) < 40 * 3:
        new_pal += [0, 0, 0]

    out = Image.new("P", (out_w, out_h))
    out.putpalette(new_pal)
    out.putdata([i + 1 for i in src_idx])  # shift every pixel into 1..39
    out.info["transparency"] = 0
    out.save(dst, transparency=0, optimize=False)

    # Also emit a raw .lgo for the C generator (avoids libpng-in-memory in the
    # standalone tool). Format: <u16 w><u16 h><40*3 palette bytes><w*h indices>.
    import struct
    pal40 = (new_pal + [0] * (40 * 3))[: 40 * 3]
    idx = [i + 1 for i in src_idx]
    lgo = dst.rsplit(".", 1)[0] + ".lgo"
    with open(lgo, "wb") as fo:
        fo.write(struct.pack("<HH", out_w, out_h))
        fo.write(bytes(pal40))
        fo.write(bytes(idx))
    print("wrote %s (%dx%d, <=39 colours, index0=transparent) + %s" % (dst, out_w, out_h, lgo))

if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else "."
    # Logo region: ~300x66 banner across the top of the tournament-select screen.
    make_logo(base + "/nightshade_raw.png", base + "/nightshade_logo.png", 300, 66)
    make_logo(base + "/reckoning_raw.png", base + "/reckoning_logo.png", 300, 66)
