#!/usr/bin/env python3
"""Package the openomf-hdremaster-mod into a mod zip the current OpenOMF
(paletted) engine can load.

The upstream mod ships HD portraits as RGBA PNGs, but this engine's mod loader
only accepts 8-bit *paletted* PNGs. The engine colours each player portrait
using the sibling har_color.png palette (first 48 entries). So we quantize each
RGBA pilot.png down to that exact 48-colour palette and emit an indexed PNG.
RGBA scene frames the engine can't use are skipped; paletted scene frames are
included as-is.

Usage: hdmod_pack.py <hd_mod_source_dir> <output_zip>
"""
import io
import os
import sys
import zipfile
from PIL import Image

if len(sys.argv) < 3:
    sys.stderr.write("Usage: hdmod_pack.py <hd_mod_source_dir> <output_zip>\n")
    sys.exit(1)
ROOT = os.path.abspath(sys.argv[1])
OUT = sys.argv[2]

MANIFEST = (
    b'name = "OMF 2097 HD Remaster"\n'
    b'mod_api = "1.0"\n'
    b'version = "0.1.0"\n'
    b"load_order = 10\n"
)


def quantize_to_har_palette(pilot_path, har_path):
    """Quantize an RGBA portrait to the 48-colour har_color palette, mapping
    transparent pixels to index 0 (the engine's transparent sprite index)."""
    pal = Image.open(har_path).getpalette()[: 48 * 3]
    palimg = Image.new("P", (16, 16))
    palimg.putpalette(pal + [0] * (768 - len(pal)))

    src = Image.open(pilot_path).convert("RGBA")
    alpha = src.getchannel("A")
    q = src.convert("RGB").quantize(palette=palimg, dither=Image.Dither.NONE)

    qpx = q.load()
    apx = alpha.load()
    for y in range(q.height):
        for x in range(q.width):
            if apx[x, y] < 128:
                qpx[x, y] = 0

    q.putpalette(pal + [0] * (768 - len(pal)))
    buf = io.BytesIO()
    q.save(buf, format="PNG", transparency=0)
    return buf.getvalue()


def colortype(path):
    with open(path, "rb") as f:
        return f.read(26)[25]


def main():
    zf = zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED)
    zf.writestr("manifest.ini", MANIFEST)

    players = os.path.join(ROOT, "players")
    for i in sorted(os.listdir(players)):
        pdir = os.path.join(players, i)
        if not os.path.isdir(pdir):
            continue
        pilot = os.path.join(pdir, "pilot.png")
        har = os.path.join(pdir, "har_color.png")
        ini = os.path.join(pdir, "pilot.ini")
        if os.path.exists(pilot) and os.path.exists(har):
            zf.writestr("players/%s/pilot.png" % i, quantize_to_har_palette(pilot, har))
            print("converted players/%s/pilot.png -> indexed" % i)
        if os.path.exists(har):
            zf.write(har, "players/%s/har_color.png" % i)
        if os.path.exists(ini):
            zf.write(ini, "players/%s/pilot.ini" % i)

    scenes = os.path.join(ROOT, "scenes")
    for root, _, files in os.walk(scenes):
        for fn in files:
            if not fn.endswith(".png"):
                continue
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, ROOT)
            if colortype(full) == 3:  # PNG_COLOR_TYPE_PALETTE
                zf.write(full, rel)
                print("included %s" % rel)
            else:
                print("skipped (RGBA, engine-incompatible): %s" % rel)

    zf.close()
    print("wrote", OUT)


if __name__ == "__main__":
    main()
