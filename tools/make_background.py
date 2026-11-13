#!/usr/bin/env python3
"""Turn a wallpaper into Vesper's 640x480 background (a .vimg file).

    python3 tools/make_background.py SOURCE.jpg OUT.vimg

Crops the largest 4:3 rectangle around the centre, scales it to 640x480 and
writes the .vimg format src/image.c reads: the 4 bytes "VIMG", width and
height as little-endian uint16, then RGB888 rows, top first.

The output is not committed: the user's wallpaper has no licence for
redistribution yet (see docs/0001).
"""
import struct
import sys

from PIL import Image

W, H = 640, 480


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    img = Image.open(sys.argv[1]).convert("RGB")
    sw, sh = img.size
    if sw * H > sh * W:  # wider than 4:3
        cw, ch = sh * W // H, sh
    else:
        cw, ch = sw, sw * H // W
    left, top = (sw - cw) // 2, (sh - ch) // 2
    img = img.crop((left, top, left + cw, top + ch)).resize((W, H), Image.LANCZOS)
    with open(sys.argv[2], "wb") as f:
        f.write(b"VIMG" + struct.pack("<HH", W, H) + img.tobytes())
    print(f"{sys.argv[2]}: {W}x{H} from a {cw}x{ch} crop of {sw}x{sh}")


if __name__ == "__main__":
    main()
