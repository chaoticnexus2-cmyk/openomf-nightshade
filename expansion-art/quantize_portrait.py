#!/usr/bin/env python3
"""Convert a raw RGB antagonist portrait into an engine-ready indexed portrait
for an OpenOMF PIC photo (the new original "Vance" face).

OMF pilot portraits are paletted sprites: each PIC photo carries its own palette
of 48 colours (indices 0..47), and sprite index 0 is the transparent background
(see sd_sprite_vga_encode in src/formats/sprite.c, which skips index-0 pixels).

Output contract (consumed by tools/expansion/mkportrait.c):
  raw ".vph" file =
    <u16 width><u16 height>
    <48 * 3 palette RGB bytes>      # palette entry 0 is the transparent colour
    <width * height index bytes>    # opaque pixels use indices 1..47; 0 = clear

We crop to a portrait aspect, downscale to the in-game portrait size, quantize the
foreground to <=47 colours, reserve index 0 for transparency, and (optionally) key
out a near-uniform dark background so the face sits on the VS/mechlab screen cleanly.
"""
import struct
import sys

from PIL import Image

# Standard OMF pilot portrait footprint on the VS / mechlab screens.
PORTRAIT_W = 51
PORTRAIT_H = 61

# Max opaque colours (index 0 is reserved for transparency; PIC palette is 48).
MAX_COLORS = 47


def _bg_key_color(rgb_img):
    """Sample the four corners; return an average corner colour used as the
    background key so the dark studio backdrop becomes transparent."""
    width, height = rgb_img.size
    corners = [
        rgb_img.getpixel((0, 0)),
        rgb_img.getpixel((width - 1, 0)),
        rgb_img.getpixel((0, height - 1)),
        rgb_img.getpixel((width - 1, height - 1)),
    ]
    red = sum(color[0] for color in corners) // 4
    green = sum(color[1] for color in corners) // 4
    blue = sum(color[2] for color in corners) // 4
    return (red, green, blue)


def _build_alpha(rgb_img, key_color, tolerance=48):
    """Mark pixels close to the background key colour as transparent."""
    width, height = rgb_img.size
    alpha = Image.new("L", (width, height), 255)
    src = rgb_img.load()
    dst = alpha.load()
    key_r, key_g, key_b = key_color
    for y in range(height):
        for x in range(width):
            pixel_r, pixel_g, pixel_b = src[x, y][:3]
            distance = abs(pixel_r - key_r) + abs(pixel_g - key_g) + abs(pixel_b - key_b)
            if distance <= tolerance:
                dst[x, y] = 0
    return alpha


def make_portrait(src_path, dst_path, out_w=PORTRAIT_W, out_h=PORTRAIT_H):
    """Quantize an RGB portrait into a raw indexed .vph for the PIC injector."""
    image = Image.open(src_path).convert("RGB")
    width, height = image.size

    # Crop to the portrait aspect ratio (centred, biased slightly toward the top
    # so the face and shoulders are kept and empty headroom is dropped).
    target_ratio = out_w / out_h
    if width / height > target_ratio:
        new_width = int(height * target_ratio)
        left = (width - new_width) // 2
        image = image.crop((left, 0, left + new_width, height))
    else:
        new_height = int(width / target_ratio)
        top = int((height - new_height) * 0.12)
        image = image.crop((0, top, width, top + new_height))

    image = image.resize((out_w, out_h), Image.LANCZOS)

    key_color = _bg_key_color(image)
    alpha = _build_alpha(image, key_color)

    # Quantize foreground to <=47 colours, then shift indices up by one so index
    # 0 stays free for the transparent background.
    quantized = image.quantize(colors=MAX_COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat_palette = quantized.getpalette()[: MAX_COLORS * 3]
    src_indices = list(quantized.getdata())
    alpha_px = list(alpha.getdata())

    # Palette entry 0 = transparent key colour; foreground colours move to 1..47.
    palette = [key_color[0], key_color[1], key_color[2]]
    palette += flat_palette
    while len(palette) < 48 * 3:
        palette += [0, 0, 0]
    palette = palette[: 48 * 3]

    indices = []
    for pixel_index, alpha_value in zip(src_indices, alpha_px):
        if alpha_value < 128:
            indices.append(0)
        else:
            indices.append(pixel_index + 1)

    with open(dst_path, "wb") as handle:
        handle.write(struct.pack("<HH", out_w, out_h))
        handle.write(bytes(palette))
        handle.write(bytes(indices))

    print(f"wrote {dst_path} ({out_w}x{out_h}, <=47 colours, index0=transparent)")


# The original Nightshade Concord cast, matching enum concord_face in
# tools/expansion/vance.h. Each stem has a portraits/<stem>.png source.
CONCORD_STEMS = [
    "cardinal",
    "meridian",
    "requiem",
    "vesper",
    "cinder",
    "seraph",
    "bastion",
    "marrow",
    "wager",
    "sparrow",
]


def make_all(base_dir):
    """Quantize every Concord portrait PNG into a sibling .vph.

    Args:
        base_dir: The expansion-art directory (contains a portraits/ subfolder).

    Returns:
        The list of .vph paths written.
    """
    from pathlib import Path

    portraits_dir = Path(base_dir) / "portraits"
    written = []
    for stem in CONCORD_STEMS:
        src = portraits_dir / f"{stem}.png"
        dst = portraits_dir / f"{stem}.vph"
        if not src.exists():
            print(f"  (skip {stem}: {src} not found)")
            continue
        make_portrait(str(src), str(dst))
        written.append(str(dst))
    return written


if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else "."
    make_all(base)
