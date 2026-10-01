#!/usr/bin/env python3
"""Generates the YK Engine application icon from the YK logo (YK-source.png): YKEngine-1024.png, the
sizes an .icns needs, YKEngine.icns itself, the editor's window icon, and the clean repo logo
../../YK.png. The ring is cleaned up (the source has a blotchy halo and is slightly oval) and placed
on a black rounded tile in the macOS icon grid. Needs Pillow (pip install pillow); the generated
files are checked in, so building the engine does not need Python.

    python3 packaging/icons/make_icons.py
"""
import struct
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
SCALE = 4  # Supersampling: everything is drawn at 4x and reduced for smooth edges.
SIZE = 1024
# The ring of YK-source.png (left, top, right, bottom): found by thresholding the bright pixels.
SOURCE_RING = (205, 183, 1043, 1005)


def s(value):
    return int(round(value * SCALE))


def clean_logo(diameter):
    """The ring-and-monogram from YK-source.png on pure black, square, with the generated halo and
    blotches outside the ring removed. The source ring is a hair wider than tall, so it is stretched
    into a true circle. Returns the RGB ring and its circular mask; outside the mask the tile shows through."""
    source = Image.open(HERE / "YK-source.png").convert("RGBA")
    flat = Image.alpha_composite(Image.new("RGBA", source.size, (0, 0, 0, 255)), source).convert("RGB")
    ring = flat.crop(SOURCE_RING).resize((diameter, diameter), Image.LANCZOS)
    mask = Image.new("L", (diameter * 4, diameter * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, diameter * 4 - 1, diameter * 4 - 1], fill=255)
    mask = mask.resize((diameter, diameter), Image.LANCZOS)
    return ring, mask


def tile_with_logo(canvas, box, radius, ring_diameter):
    """A black rounded-square tile at box = (left, top, right, bottom) with the logo centred in it."""
    mask = Image.new("L", canvas, 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius=radius, fill=255)
    body = Image.new("RGBA", canvas, (0, 0, 0, 0))
    body.paste(Image.new("RGBA", canvas, (3, 4, 5, 255)), (0, 0), mask)
    logo, logo_mask = clean_logo(ring_diameter)
    cx, cy = (box[0] + box[2]) // 2, (box[1] + box[3]) // 2
    body.paste(logo.convert("RGBA"), (cx - ring_diameter // 2, cy - ring_diameter // 2), logo_mask)
    # Re-apply the tile shape so nothing pokes out of the rounded corners.
    out = Image.new("RGBA", canvas, (0, 0, 0, 0))
    out.paste(body, (0, 0), mask)
    return out, mask


def build():
    """The app icon: the logo tile on the macOS icon grid (824 px, inset 100) with a soft shadow."""
    canvas = (s(SIZE), s(SIZE))
    tile, mask = tile_with_logo(canvas, (s(100), s(100), s(924), s(924)), s(185), s(650))
    shadow = Image.new("L", canvas, 0)
    ImageDraw.Draw(shadow).rounded_rectangle(
        [s(100), s(124), s(924), s(948)], radius=s(185), fill=150)
    shadow = shadow.filter(ImageFilter.GaussianBlur(s(18)))
    image = Image.new("RGBA", canvas, (0, 0, 0, 0))
    image.paste(Image.new("RGBA", canvas, (0, 0, 0, 255)), (0, 0), shadow)
    image.alpha_composite(tile)
    rim = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(s(3) | 1)))
    image.paste(Image.new("RGBA", canvas, (255, 255, 255, 40)), (0, 0), rim)
    return image.resize((SIZE, SIZE), Image.LANCZOS)


def build_logo():
    """YK.png: the same logo as a full-bleed rounded tile (no shadow, no padding) for the README etc."""
    canvas = (s(SIZE), s(SIZE))
    tile, _ = tile_with_logo(canvas, (0, 0, s(SIZE), s(SIZE)), s(230), s(780))
    return tile.resize((SIZE, SIZE), Image.LANCZOS)


def icns(entries):
    """Apple icon file: 'icns', total length, then (type, length, PNG data) entries."""
    body = b""
    for kind, data in entries:
        body += kind.encode() + struct.pack(">I", 8 + len(data)) + data
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def main():
    master = build()
    master.save(HERE / "YKEngine-1024.png")
    sizes = {}
    for size in (16, 32, 64, 128, 256, 512, 1024):
        resized = master if size == 1024 else master.resize((size, size), Image.LANCZOS)
        path = HERE / f"_{size}.png"
        resized.save(path, optimize=True)
        sizes[size] = path.read_bytes()
        path.unlink()
    # PNG-carrying icon types: icp4..icp6 (16..64), ic07..ic10 (128..1024) and the @2x variants
    # ic11 (16@2x = 32 px), ic12 (32@2x = 64), ic13 (128@2x = 256), ic14 (256@2x = 512).
    entries = [("icp4", sizes[16]), ("icp5", sizes[32]), ("icp6", sizes[64]), ("ic07", sizes[128]),
               ("ic08", sizes[256]), ("ic09", sizes[512]), ("ic10", sizes[1024]),
               ("ic11", sizes[32]), ("ic12", sizes[64]), ("ic13", sizes[256]), ("ic14", sizes[512])]
    (HERE / "YKEngine.icns").write_bytes(icns(entries))
    build_logo().save(HERE.parents[1] / "YK.png", optimize=True)
    # The window icon of the editor on Windows and Linux (embedded in the program).
    master.resize((256, 256), Image.LANCZOS).save(HERE / "YKEngine-256.png", optimize=True)
    print("wrote", HERE / "YKEngine-1024.png", HERE / "YKEngine-256.png", HERE / "YKEngine.icns")


if __name__ == "__main__":
    main()
