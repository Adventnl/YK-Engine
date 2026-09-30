#!/usr/bin/env python3
"""Generates the YK Engine application icon: YKEngine-1024.png, the sizes an .icns needs, and
YKEngine.icns itself. The design is original: a dark rounded tile in the macOS icon grid with a
geometric "YK" mark in a blue accent gradient. Needs Pillow (pip install pillow); the generated files
are checked in, so building the engine does not need Python.

    python3 packaging/icons/make_icons.py
"""
import struct
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
SCALE = 4  # Supersampling: everything is drawn at 4x and reduced for smooth edges.
SIZE = 1024


def s(value):
    return int(round(value * SCALE))


def rounded_tile(canvas):
    """The icon body: 824 px square with 185 px corners, inset 100 px (Apple's icon grid)."""
    mask = Image.new("L", canvas, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [s(100), s(100), s(924), s(924)], radius=s(185), fill=255)
    return mask


def vertical_gradient(canvas, top, bottom):
    gradient = Image.new("RGB", canvas)
    pixels = gradient.load()
    for y in range(canvas[1]):
        t = y / (canvas[1] - 1)
        color = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(canvas[0]):
            pixels[x, y] = color
    return gradient


def diagonal_gradient(canvas, start, end):
    gradient = Image.new("RGB", canvas)
    pixels = gradient.load()
    width, height = canvas
    for y in range(height):
        for x in range(width):
            t = (x / width * 0.55 + y / height * 0.45)
            pixels[x, y] = tuple(int(start[i] + (end[i] - start[i]) * t) for i in range(3))
    return gradient


def stroke(draw, points, width):
    """A thick polyline with round caps and joins."""
    for a, b in zip(points, points[1:]):
        draw.line([a, b], fill=255, width=width)
    radius = width // 2
    for x, y in points:
        draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=255)


def monogram(canvas):
    """The Y and the K as one mask."""
    mask = Image.new("L", canvas, 0)
    draw = ImageDraw.Draw(mask)
    w = s(70)
    # Y: two arms meeting on a stem.
    stroke(draw, [(s(262), s(300)), (s(385), s(500)), (s(508), s(300))], w)
    stroke(draw, [(s(385), s(500)), (s(385), s(724))], w)
    # K: a bar, an upper arm and a leg.
    stroke(draw, [(s(590), s(300)), (s(590), s(724))], w)
    stroke(draw, [(s(792), s(300)), (s(590), s(534))], w)
    stroke(draw, [(s(668), s(484)), (s(800), s(724))], w)
    return mask


def build():
    canvas = (s(SIZE), s(SIZE))
    tile = rounded_tile(canvas)

    # Soft shadow under the tile, as system icons have.
    shadow = Image.new("L", canvas, 0)
    ImageDraw.Draw(shadow).rounded_rectangle(
        [s(100), s(124), s(924), s(948)], radius=s(185), fill=150)
    shadow = shadow.filter(ImageFilter.GaussianBlur(s(18)))
    image = Image.new("RGBA", canvas, (0, 0, 0, 0))
    image.paste(Image.new("RGBA", canvas, (0, 0, 0, 255)), (0, 0), shadow)

    # The body: a dark neutral gradient with a faint scene-grid, and a thin lighter rim.
    body = vertical_gradient(canvas, (46, 50, 55), (20, 22, 25)).convert("RGBA")
    grid = Image.new("RGBA", canvas, (0, 0, 0, 0))
    grid_draw = ImageDraw.Draw(grid)
    for step in range(100, 925, 103):
        grid_draw.line([(s(step), s(100)), (s(step), s(924))], fill=(255, 255, 255, 10), width=s(2))
        grid_draw.line([(s(100), s(step)), (s(924), s(step))], fill=(255, 255, 255, 10), width=s(2))
    body = Image.alpha_composite(body, grid)
    image.paste(body, (0, 0), tile)
    rim = ImageChops.subtract(tile, tile.filter(ImageFilter.MinFilter(s(3) | 1)))
    image.paste(Image.new("RGBA", canvas, (255, 255, 255, 46)), (0, 0), rim)

    # The mark, with a soft glow behind it.
    mark = monogram(canvas)
    glow = mark.filter(ImageFilter.GaussianBlur(s(26)))
    image.paste(Image.new("RGBA", canvas, (40, 140, 255, 90)), (0, 0), glow)
    accent = diagonal_gradient(canvas, (120, 200, 255), (0, 120, 212)).convert("RGBA")
    image.paste(accent, (0, 0), mark)

    return image.resize((SIZE, SIZE), Image.LANCZOS)


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
    # The window icon of the editor on Windows and Linux (embedded in the program).
    master.resize((256, 256), Image.LANCZOS).save(HERE / "YKEngine-256.png", optimize=True)
    print("wrote", HERE / "YKEngine-1024.png", HERE / "YKEngine-256.png", HERE / "YKEngine.icns")


if __name__ == "__main__":
    main()
