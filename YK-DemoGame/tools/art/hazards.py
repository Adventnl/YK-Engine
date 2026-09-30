"""Animated hazard surfaces: lava, water and goo. Each is a strip of `FRAMES` frames (128 x 64 px,
2 x 1 world units) whose wavy top edge and body repeat seamlessly across a pool of any width."""
import json
import math
import os
import random

import numpy as np
from PIL import Image

from common import Canvas, add_alpha_noise, blur, gradient_v, mix, rgba, save, shade

W, H = 128, 64
FRAMES = 4


def surface_height(x, frame, base=9.0):
    """Wave height at pixel column x; periodic in W and in the frame loop."""
    t = 2 * math.pi * frame / FRAMES
    return (base + 2.4 * math.sin(2 * math.pi * x / W * 2 + t) + 1.6 * math.sin(2 * math.pi * x / W * 5 - t * 2 + 1.1)
            + 0.8 * math.sin(2 * math.pi * x / W * 9 + t * 3))


def liquid_frame(frame, top, mid, deep, glow, blobs, foam, seed, alpha=255):
    c = Canvas(W, H)
    layer = c.layer()
    for x in range(W * 4):
        xx = x / 4.0
        h = surface_height(xx, frame)
        layer.line([(xx, h), (xx, H)], (255, 255, 255, 255), 0.3)
    body = gradient_v(W * c.ss, H * c.ss, top, deep)
    out = Image.new("RGBA", layer.img.size, (0, 0, 0, 0))
    out.paste(body, (0, 0), layer.mask())
    w = type("W", (), {})()
    w.img = out
    c.composite(w)
    # Blobs / crust / bubbles drifting with the frame (positions periodic in x).
    rng = random.Random(seed)
    for i in range(blobs):
        bx = (rng.uniform(0, W) + frame * (W / FRAMES) * (1 if i % 2 else -1) * 0.5) % W
        by = rng.uniform(18, H - 8)
        r = rng.uniform(3, 7)
        for wrap in (-W, 0, W):
            c.ellipse(bx + wrap, by, r * 1.4, r, shade(mid, rng.uniform(0.7, 1.15)))
            c.ellipse(bx + wrap - r * 0.3, by - r * 0.25, r * 0.6, r * 0.35, (*glow[:3], 150))
    # Bright rim along the surface.
    for x in range(W * 3):
        xx = x / 3.0
        h = surface_height(xx, frame)
        c.line([(xx, h), (xx, h + 4.2)], foam, 1.0)
    img = c.result()
    if alpha < 255:
        a = img.getchannel("A").point(lambda v: v * alpha // 255)
        img.putalpha(a)
    return img


def strip(name, top, mid, deep, glow, blobs, foam, seed, alpha=255):
    frames = [liquid_frame(f, top, mid, deep, glow, blobs, foam, seed, alpha) for f in range(FRAMES)]
    out = Image.new("RGBA", (W * FRAMES, H))
    for i, f in enumerate(frames):
        out.paste(f, (i * W, 0))
    return out


def write_anim(path, texture, fps=6):
    doc = {"format": "yk.animation", "version": 2, "texture": texture, "columns": FRAMES, "rows": 1,
           "clips": [{"name": "flow", "first": 0, "count": FRAMES, "fps": fps}]}
    with open(path, "w") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def generate(out_dir):
    d = os.path.join(out_dir, "hazards")
    os.makedirs(d, exist_ok=True)
    styles = {
        "lava": dict(top="#ff8a1f", mid="#ffb31f", deep="#b3200f", glow=(255, 230, 120, 255), blobs=7,
                     foam=(255, 226, 120, 255), seed=4),
        "water": dict(top="#4aa8ff", mid="#66c2ff", deep="#1a4fb0", glow=(210, 245, 255, 255), blobs=6,
                      foam=(200, 240, 255, 255), seed=9, alpha=238),
        "goo": dict(top="#8fe04a", mid="#b8f26a", deep="#3a8a26", glow=(240, 255, 190, 255), blobs=8,
                    foam=(214, 255, 150, 255), seed=13),
    }
    for name, style in styles.items():
        save(strip(name, **style), os.path.join(d, name + ".png"))
        write_anim(os.path.join(d, name + ".ykanim"), "assets/hazards/%s.png" % name)


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
