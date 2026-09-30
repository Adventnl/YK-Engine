"""Collectible gems: a faceted stone with a sparkle that travels across it (4 frames)."""
import json
import math
import os

from PIL import Image

from common import Canvas, blur, mix, rgba, save, shade

SIZE = 64
FRAMES = 4


def gem_frame(frame, base, light, dark, outline, glint):
    c = Canvas(SIZE, SIZE)
    cx, cy = 32, 34
    pts = [(cx, 6), (cx + 21, 22), (cx + 13, 56), (cx - 13, 56), (cx - 21, 22)]
    # A soft glow behind the gem.
    glow = c.layer()
    glow.ellipse(cx, cy, 26, 26, (*rgba(light)[:3], 70))
    glow.img = blur(glow.img, 5 * c.ss)
    c.composite(glow)
    c.polygon(pts, outline)
    grown = pts
    c.polygon([(cx, 9), (cx + 18.5, 23), (cx + 11, 53), (cx - 11, 53), (cx - 18.5, 23)], base)
    # Facets.
    c.polygon([(cx, 9), (cx + 18.5, 23), (cx, 30)], light)
    c.polygon([(cx, 9), (cx - 18.5, 23), (cx, 30)], mix(light, base, 0.45))
    c.polygon([(cx - 18.5, 23), (cx, 30), (cx - 11, 53)], shade(base, 0.9))
    c.polygon([(cx + 18.5, 23), (cx, 30), (cx + 11, 53)], dark)
    c.polygon([(cx - 11, 53), (cx, 30), (cx + 11, 53)], mix(base, dark, 0.35))
    c.line([(cx - 18.5, 23), (cx + 18.5, 23)], (255, 255, 255, 90), 1.0)
    # A glint that sweeps across as the frames advance.
    t = frame / FRAMES
    gx = cx - 16 + 32 * t
    gy = 20 + 6 * math.sin(t * math.pi)
    c.line([(gx - 6, gy + 4), (gx + 6, gy - 4)], glint, 2.4)
    c.ellipse(gx, gy, 2.6, 2.6, (255, 255, 255, 255))
    if frame in (1, 2):
        c.line([(gx, gy - 8), (gx, gy + 8)], (255, 255, 255, 220), 1.4)
        c.line([(gx - 8, gy), (gx + 8, gy)], (255, 255, 255, 220), 1.4)
    return c.result()


def generate(out_dir):
    d = os.path.join(out_dir, "items")
    os.makedirs(d, exist_ok=True)
    gems = {
        "gem_red": ("#e8324f", "#ff8fa2", "#951533", "#5c0c1f", (255, 230, 235, 255)),
        "gem_blue": ("#2f9bf0", "#93dcff", "#175aa8", "#0c3468", (225, 248, 255, 255)),
    }
    for name, (base, light, dark, outline, glint) in gems.items():
        sheet = Image.new("RGBA", (SIZE * FRAMES, SIZE))
        for f in range(FRAMES):
            sheet.paste(gem_frame(f, base, light, dark, outline, glint), (f * SIZE, 0))
        save(sheet, os.path.join(d, name + ".png"))
        doc = {"format": "yk.animation", "version": 2, "texture": "assets/items/%s.png" % name,
               "columns": FRAMES, "rows": 1, "clips": [{"name": "shine", "first": 0, "count": FRAMES, "fps": 6}]}
        with open(os.path.join(d, name + ".ykanim"), "w") as f:
            json.dump(doc, f, indent=2)
            f.write("\n")


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
