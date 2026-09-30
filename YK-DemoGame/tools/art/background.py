"""Backdrop layers for the cave: a far wall of light and dust, a mid layer of columns and
stalactites that repeats sideways, and a screen vignette. The layers are separate textures so the
game view can scroll them at different speeds (SpriteRenderer.parallax)."""
import math
import os
import random

import numpy as np
from PIL import Image, ImageChops, ImageFilter

from common import Canvas, add_alpha_noise, blur, gradient_v, mix, rgba, save, write_texture_meta

FAR_W, FAR_H = 1024, 576
MID_W, MID_H = 1024, 1408


def far(out_dir):
    rng = random.Random(21)
    base = gradient_v(FAR_W, FAR_H, "#08141a", "#0f2730")
    lower = gradient_v(FAR_W, FAR_H // 2, (0, 0, 0, 0), (28, 74, 80, 120))
    base.alpha_composite(lower, (0, FAR_H // 2))
    c = Canvas(FAR_W, FAR_H, ss=2)
    # Soft slanted light shafts.
    shafts = Image.new("RGBA", (FAR_W, FAR_H), (0, 0, 0, 0))
    for x0, width, alpha in ((150, 90, 34), (420, 140, 26), (760, 80, 30)):
        layer = Canvas(FAR_W, FAR_H, ss=2)
        layer.polygon([(x0, -10), (x0 + width, -10), (x0 + width + 260, FAR_H + 10), (x0 + 260, FAR_H + 10)],
                      (190, 235, 235, alpha))
        shafts.alpha_composite(blur(layer.result(), 14))
    base.alpha_composite(shafts)
    # Distant arches: pale silhouettes that fade into the haze.
    arches = Canvas(FAR_W, FAR_H, ss=2)
    for cx, half, top in ((150, 120, 250), (520, 170, 200), (900, 130, 270)):
        pts = [(cx - half, FAR_H)]
        for i in range(0, 41):
            a = math.pi - math.pi * i / 40
            pts.append((cx + math.cos(a) * half, top + half - math.sin(a) * half * 1.15))
        pts.append((cx + half, FAR_H))
        arches.polygon(pts, (18, 46, 54, 120))
        arches.polygon([(x + 0, y) for x, y in [(cx - half + 26, FAR_H)] +
                        [(cx + math.cos(math.pi - math.pi * i / 40) * (half - 26), top + 26 + half - math.sin(math.pi - math.pi * i / 40) * (half - 26) * 1.15) for i in range(41)] +
                        [(cx + half - 26, FAR_H)]], (10, 26, 32, 130))
    base.alpha_composite(blur(arches.result(), 3.5))
    # Dust motes.
    for _ in range(90):
        x, y, r = rng.uniform(0, FAR_W), rng.uniform(0, FAR_H), rng.uniform(0.8, 2.4)
        c.ellipse(x, y, r, r, (200, 240, 235, rng.randint(25, 85)))
    base.alpha_composite(blur(c.result(), 0.6))
    save(base.convert("RGB"), os.path.join(out_dir, "bg_far.png"))


def mid(out_dir):
    """Columns and stalactites, periodic across the width (columns at 1/4 and 3/4)."""
    rng = random.Random(9)
    c = Canvas(MID_W, MID_H, ss=2)
    body = (12, 36, 42, 245)
    edge = (6, 22, 27, 255)
    for cx, half in ((MID_W * 0.25, 86), (MID_W * 0.75, 74)):
        for wrap in (-MID_W, 0, MID_W):
            x = cx + wrap
            c.polygon([(x - half, -10), (x + half, -10), (x + half - 10, MID_H + 10), (x - half + 10, MID_H + 10)], body, edge, 3)
            c.polygon([(x - half - 30, 170), (x + half + 30, 170), (x + half, 130), (x - half, 130)], (16, 46, 53, 250), edge, 3)
            c.polygon([(x - half - 30, MID_H - 170), (x + half + 30, MID_H - 170), (x + half, MID_H - 130), (x - half, MID_H - 130)], (16, 46, 53, 250), edge, 3)
            for k in range(6):
                y = rng.uniform(260, MID_H - 260)
                c.line([(x - half * 0.6, y), (x - half * 0.1, y + rng.uniform(-12, 12)), (x + half * 0.5, y + rng.uniform(-6, 14))], (6, 20, 24, 150), 2.4)
    # Stalactites hanging from the top, wrapping seamlessly.
    for i in range(14):
        x = rng.uniform(0, MID_W)
        h = rng.uniform(70, 230)
        w = rng.uniform(22, 60)
        for wrap in (-MID_W, 0, MID_W):
            c.polygon([(x + wrap - w / 2, -4), (x + wrap + w / 2, -4), (x + wrap + rng.uniform(-6, 6), h)], (14, 42, 49, 250), edge, 2)
    img = c.result()
    arr = np.array(img).astype(float)
    ys = np.linspace(0, 1, MID_H)[:, None]
    fade = np.clip(1.15 - 0.55 * ys, 0.0, 1.0)  # Deeper toward the top, lighter (hazier) toward the bottom.
    arr[:, :, :3] *= fade[:, :, None] * 0.35 + 0.65
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    # A distant, soft layer needs only half the resolution; its .ykmeta says so (pixelsPerUnit 32),
    # so it still covers 16 x 22 world units.
    img = add_alpha_noise(img, 2.0, 4).resize((MID_W // 2, MID_H // 2), Image.LANCZOS)
    path = os.path.join(out_dir, "bg_mid.png")
    save(img, path)
    write_texture_meta(path, {"pixelsPerUnit": 32})


def vignette(out_dir):
    W, H = 512, 288
    ys, xs = np.mgrid[0:H, 0:W]
    nx, ny = (xs / W - 0.5) * 2, (ys / H - 0.5) * 2
    d = np.sqrt((nx * 0.92) ** 2 + (ny * 1.05) ** 2)
    alpha = np.clip((d - 0.55) / 0.75, 0, 1) ** 1.6 * 170
    arr = np.zeros((H, W, 4), np.uint8)
    arr[:, :, 0], arr[:, :, 1], arr[:, :, 2] = 2, 8, 12
    arr[:, :, 3] = alpha.astype(np.uint8)
    save(Image.fromarray(arr, "RGBA"), os.path.join(out_dir, "vignette.png"))


def generate(out_dir):
    d = os.path.join(out_dir, "backgrounds")
    os.makedirs(d, exist_ok=True)
    far(d)
    mid(d)
    vignette(d)


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
