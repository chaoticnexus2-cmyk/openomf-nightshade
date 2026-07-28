"""Dump the 48-colour portrait palette(s) from a WORLD.PIC-style OMF PIC file.

PIC layout (see src/formats/pic.c):
  dword photo_count
  ... padding to offset 200 ...
  photo_count * dword offset_list
  per photo @ offset: u8 is_player, u16 sex, 48*3 palette RGB (6-bit VGA),
                      u8 unk_flag, then RLE sprite data.

We extract each photo's palette so the expansion portraits can be quantized to
the SAME palette the classic faces use (which the mechlab renders them against).
"""
import struct
import sys
from pathlib import Path


def read_dword(data, off):
    return struct.unpack_from("<I", data, off)[0]


def main(pic_path, out_path):
    data = Path(pic_path).read_bytes()
    photo_count = read_dword(data, 0)
    print(f"{pic_path}: {photo_count} photos")

    offsets = [read_dword(data, 200 + i * 4) for i in range(photo_count)]

    # Extract the first valid photo's 48-colour palette.
    palettes = []
    for idx, off in enumerate(offsets):
        pos = off
        pos += 1  # is_player u8
        pos += 2  # sex u16
        pal_raw = data[pos : pos + 48 * 3]
        if len(pal_raw) < 48 * 3:
            continue
        # OMF stores 6-bit VGA palette values (0..63); scale to 0..255.
        pal = [min(255, (b * 255) // 63) for b in pal_raw]
        palettes.append((idx, pal))

    # Report how many distinct palettes exist among the faces.
    distinct = {bytes(p): [] for _, p in palettes}
    for idx, p in palettes:
        distinct[bytes(p)].append(idx)
    print(f"distinct portrait palettes: {len(distinct)}")
    for pal_bytes, idxs in distinct.items():
        print(f"  palette used by photos {idxs[:8]}{'...' if len(idxs) > 8 else ''} ({len(idxs)} faces)")

    # Write the most common palette (the shared portrait palette) as raw RGB.
    most_common = max(distinct.items(), key=lambda kv: len(kv[1]))
    Path(out_path).write_bytes(most_common[0])
    print(f"wrote shared palette ({len(most_common[1])} faces) -> {out_path}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
