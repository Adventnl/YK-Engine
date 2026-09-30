#!/usr/bin/env python3
"""Draws the demo game's app icon, assets/icon.png (1024 x 1024): two characters' colors, ember and
tide, as a pair of gems on a dark tile that already has the macOS icon shape. Needs Pillow.

    python3 YK-DemoGame/tools/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parents[1] / "assets" / "icon.png"
SCALE, SIZE = 3, 1024


def s(v):
    return int(round(v * SCALE))


def gem(center, radius, light, dark):
    """A faceted gem: a hexagon with lighter upper facets."""
    canvas = (s(SIZE), s(SIZE))
    layer = Image.new("RGBA", canvas, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    cx, cy = center
    top, bottom = cy - radius, cy + radius
    pts = [(cx, top), (cx + radius * 0.9, cy - radius * 0.45), (cx + radius * 0.9, cy + radius * 0.45),
           (cx, bottom), (cx - radius * 0.9, cy + radius * 0.45), (cx - radius * 0.9, cy - radius * 0.45)]
    draw.polygon([(s(x), s(y)) for x, y in pts], fill=dark)
    draw.polygon([(s(cx), s(top)), (s(cx + radius * 0.9), s(cy - radius * 0.45)), (s(cx), s(cy)),
                  (s(cx - radius * 0.9), s(cy - radius * 0.45))], fill=light)
    draw.polygon([(s(cx), s(cy)), (s(cx + radius * 0.9), s(cy - radius * 0.45)),
                  (s(cx + radius * 0.9), s(cy + radius * 0.45)), (s(cx), s(bottom))],
                 fill=tuple(int(c * 0.82) for c in dark[:3]) + (255,))
    return layer


def main():
    canvas = (s(SIZE), s(SIZE))
    tile = Image.new("L", canvas, 0)
    ImageDraw.Draw(tile).rounded_rectangle([s(100), s(100), s(924), s(924)], radius=s(185), fill=255)
    image = Image.new("RGBA", canvas, (0, 0, 0, 0))
    shadow = Image.new("L", canvas, 0)
    ImageDraw.Draw(shadow).rounded_rectangle([s(100), s(122), s(924), s(946)], radius=s(185), fill=140)
    image.paste(Image.new("RGBA", canvas, (0, 0, 0, 255)), (0, 0), shadow.filter(ImageFilter.GaussianBlur(s(16))))
    body = Image.new("RGBA", canvas)
    px = body.load()
    for y in range(canvas[1]):
        t = y / (canvas[1] - 1)
        for x in range(canvas[0]):
            px[x, y] = (int(22 + 8 * t), int(38 - 10 * t), int(46 - 14 * t), 255)
    image.paste(body, (0, 0), tile)
    # Warm glow on the left, cool glow on the right.
    for center, color in (((360, 540), (255, 120, 40, 110)), ((664, 540), (50, 150, 255, 110))):
        glow = Image.new("L", canvas, 0)
        cx, cy = center
        ImageDraw.Draw(glow).ellipse([s(cx - 190), s(cy - 190), s(cx + 190), s(cy + 190)], fill=255)
        glow = glow.filter(ImageFilter.GaussianBlur(s(70)))
        glow = ImageChops.multiply(glow, tile)
        image.paste(Image.new("RGBA", canvas, color), (0, 0), glow)
    ember = gem((360, 520), 205, (255, 176, 92, 255), (232, 84, 36, 255))
    tide = gem((664, 520), 205, (130, 205, 255, 255), (32, 116, 226, 255))
    image = Image.alpha_composite(image, ember)
    image = Image.alpha_composite(image, tide)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    image.resize((SIZE, SIZE), Image.LANCZOS).save(OUT, optimize=True)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
