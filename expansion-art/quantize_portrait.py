#!/usr/bin/env python3
"""Convert the raw RGB antagonist portraits into engine-ready indexed portraits
for OpenOMF PIC photos (the original Nightshade Concord cast). Run with the
expansion-art dir to batch-convert every portraits/<stem>.png into a .vph.

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

from PIL import Image, ImageEnhance

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


def _load_ref_palette(pal_path):
    """Load a raw 48*3 RGB reference palette (from dump_pic_palette.py).

    Returns a flat list of 144 ints, or None if the file is absent.
    """
    from pathlib import Path

    path = Path(pal_path)
    if not path.exists():
        return None
    raw = path.read_bytes()
    if len(raw) < 48 * 3:
        return None
    return list(raw[: 48 * 3])


def _quantize_to_fixed(image, ref_palette):
    """Remap an RGB image onto a fixed reference palette (indices 1..47).

    The engine draws every portrait on a screen against ONE shared palette
    (player-0's, loaded into VGA indices 1..47). Portraits must therefore use
    that exact palette, not a private per-image one, or they render as garbled
    colour noise. Index 0 stays reserved for transparency.

    Args:
        image: RGB PIL image at portrait size.
        ref_palette: flat [r,g,b,...] of 48 entries; entries 1..47 are usable.

    Returns:
        (palette_flat_144, indices) where indices are 0 (clear) or 1..47.
    """
    # PIL will map to ANY of the 256 palette slots, but only indices 1..47 are
    # drawable portrait colours. So we build a working palette whose 256 slots
    # are filled ONLY with the 47 usable reference colours (entry 0 excluded and
    # the 47 colours tiled across the rest), forcing every mapped index into a
    # value we can translate back to 1..47.
    usable = []  # list of (r,g,b) for reference indices 1..47
    for i in range(1, 48):
        usable.append((ref_palette[i * 3], ref_palette[i * 3 + 1], ref_palette[i * 3 + 2]))

    # Fill a 256-entry PIL palette by repeating the usable colours; remember which
    # reference index (1..47) each PIL slot corresponds to.
    slot_to_refidx = []
    flat = []
    for slot in range(256):
        ref_idx = 1 + (slot % 47)
        color = usable[ref_idx - 1]
        flat += [color[0], color[1], color[2]]
        slot_to_refidx.append(ref_idx)

    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(flat)

    # Floyd-Steinberg dithering diffuses quantization error across neighbouring
    # pixels, which turns hard colour bands into smooth-reading gradients on the
    # limited 47-colour portrait palette. This is the main cure for the blocky,
    # muddy look.
    quantized = image.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
    # Translate PIL slot indices back to reference indices 1..47.
    remapped = [slot_to_refidx[slot] for slot in quantized.getdata()]
    return list(ref_palette[: 48 * 3]), remapped


def make_portrait(src_path, dst_path, out_w=PORTRAIT_W, out_h=PORTRAIT_H, ref_palette=None):
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

    # Pre-quantization tone shaping. The shared OMF portrait palette is limited
    # and skews dark, so gently lift contrast, saturation and edge sharpness to
    # keep the face reading after it snaps to the palette. Only applied in
    # fixed-palette mode where the muddiness shows.
    if ref_palette is not None:
        image = ImageEnhance.Color(image).enhance(1.35)
        image = ImageEnhance.Contrast(image).enhance(1.18)
        image = ImageEnhance.Brightness(image).enhance(1.08)
        image = ImageEnhance.Sharpness(image).enhance(1.6)

    key_color = _bg_key_color(image)
    alpha = _build_alpha(image, key_color)
    alpha_px = list(alpha.getdata())

    if ref_palette is not None:
        # Fixed-palette mode: remap onto the engine's shared portrait palette so
        # the face renders correctly against the mechlab/VS palette. src_indices
        # are already final reference indices in 1..47.
        palette, src_indices = _quantize_to_fixed(image, ref_palette)
        indices = []
        for ref_index, alpha_value in zip(src_indices, alpha_px):
            indices.append(0 if alpha_value < 128 else ref_index)
    else:
        # Legacy independent-palette mode (kept for the single-portrait pipeline).
        quantized = image.quantize(colors=MAX_COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        flat_palette = quantized.getpalette()[: MAX_COLORS * 3]
        src_indices = list(quantized.getdata())
        palette = [key_color[0], key_color[1], key_color[2]]
        palette += flat_palette
        while len(palette) < 48 * 3:
            palette += [0, 0, 0]
        palette = palette[: 48 * 3]
        indices = []
        for pixel_index, alpha_value in zip(src_indices, alpha_px):
            indices.append(0 if alpha_value < 128 else pixel_index + 1)

    with open(dst_path, "wb") as handle:
        handle.write(struct.pack("<HH", out_w, out_h))
        handle.write(bytes(palette))
        handle.write(bytes(indices))

    print(f"wrote {dst_path} ({out_w}x{out_h}, index0=transparent)")


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

    All faces are remapped onto a single shared reference palette
    (expansion-art/players_ref.pal, extracted from PLAYERS.PIC) because the
    engine renders every portrait on a screen against one shared palette.

    Args:
        base_dir: The expansion-art directory (contains a portraits/ subfolder).

    Returns:
        The list of .vph paths written.
    """
    from pathlib import Path

    base = Path(base_dir)
    ref_palette = _load_ref_palette(base / "players_ref.pal")
    if ref_palette is None:
        print("WARNING: players_ref.pal not found; portraits will use private "
              "palettes and may render garbled in-game.")

    portraits_dir = base / "portraits"
    written = []
    for stem in CONCORD_STEMS:
        src = portraits_dir / f"{stem}.png"
        dst = portraits_dir / f"{stem}.vph"
        if not src.exists():
            print(f"  (skip {stem}: {src} not found)")
            continue
        make_portrait(str(src), str(dst), ref_palette=ref_palette)
        written.append(str(dst))
    return written


if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else "."
    make_all(base)
