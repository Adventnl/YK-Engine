"""Level building blocks: brick walls, the nine-slice platform, the one-way ledge and the ramp.

Textures that are meant to repeat are drawn so their edges match (periodic patterns), so a wall of
any size is built by tiling one small texture.
"""
import json
import math
import os
import random

import numpy as np
from PIL import Image, ImageChops, ImageFilter

from common import Canvas, add_alpha_noise, blur, gradient_v, mix, rgba, save, shade

STONE = "#4b5a4a"
STONE_DARK = "#2b3a34"
MORTAR = "#1d2a26"
MOSS = "#6ea63a"
MOSS_LIGHT = "#a2d65a"
MOSS_DARK = "#3f7327"


def brick_tile(size=128, rows=4, seed=3, base=STONE, dark=False):
    """A seamless running-bond brick texture (size x size)."""
    rng = random.Random(seed)
    c = Canvas(size, size)
    row_h = size / rows
    brick_w = size / 2
    mortar = 2.6
    for r in range(rows):
        offset = (r % 2) * brick_w / 2
        for b in range(-1, 3):
            x0 = b * brick_w + offset
            shade_f = rng.uniform(0.86, 1.12)
            color = shade(base, shade_f * (0.72 if dark else 1.0))
            # Draw wrapped copies so bricks crossing the edge tile seamlessly.
            for wrap in (-size, 0, size):
                c.rrect(x0 + mortar / 2 + wrap, r * row_h + mortar / 2, x0 + brick_w - mortar / 2 + wrap,
                        (r + 1) * row_h - mortar / 2, 3.0, color)
    layer = c.layer()
    # Mortar shows through as the (dark) background.
    bg = Canvas(size, size)
    bg.rect(0, 0, size, size, MORTAR)
    bg.composite_img = None
    base_img = bg.result()
    top = c.result()
    out = Image.alpha_composite(base_img, top)
    # Bevel: a light top edge and a dark bottom edge on each brick (cheap, from the row structure).
    arr = np.array(out).astype(float)
    ys = np.arange(size)[:, None]
    phase = (ys % row_h) / row_h
    light = np.clip(1 - phase / 0.16, 0, 1) * 22
    darken = np.clip((phase - 0.84) / 0.16, 0, 1) * 30
    arr[:, :, :3] += light[:, :, None]
    arr[:, :, :3] -= darken[:, :, None]
    out = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    return add_alpha_noise(out, 4.5, seed)


def periodic_wave(width, x, cycles, phase=0.0):
    return math.sin(2 * math.pi * (x * cycles / width) + phase)


def stacked_bricks(W, H, join0=24, brick_w=40, row_h=24, seed=11):
    """Bricks whose joins fall at join0 + k * brick_w and whose courses are row_h tall, so the
    region between the platform's caps repeats exactly every 2 * brick_w pixels."""
    rng = random.Random(seed)
    tones = [rng.uniform(0.9, 1.1) for _ in range(2)]
    c = Canvas(W, H)
    c.rect(0, 0, W, H, MORTAR)
    for row in range(-1, H // row_h + 2):
        for k in range(-1, W // brick_w + 3):
            x0, y0 = join0 + k * brick_w, row * row_h
            c.rrect(x0 + 1.4, y0 + 1.4, x0 + brick_w - 1.4, y0 + row_h - 1.4, 2.5,
                    shade(STONE, tones[k % 2]))
    out = c.result()
    arr = np.array(out).astype(float)
    ys = np.arange(H)[:, None]
    phase = (ys % row_h) / row_h
    arr[:, :, :3] += (np.clip(1 - phase / 0.18, 0, 1) * 20)[:, :, None]
    arr[:, :, :3] -= (np.clip((phase - 0.82) / 0.18, 0, 1) * 26)[:, :, None]
    return add_alpha_noise(Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA"), 4.0, seed)


def platform(path):
    """128 x 64 nine-slice: moss lip on top, brick body, dark underside. Border [24, 26, 24, 14]."""
    W, H = 128, 64
    top_h, bot_h, side = 26, 14, 24
    body = stacked_bricks(W, H)
    c = Canvas(W, H)
    # Base slab.
    slab = c.layer()
    slab.rrect(1.0, 4, W - 1.0, H - 1.5, 10, (255, 255, 255, 255))
    c.composite(_mask_paint(slab, body, c))
    # Underside shading.
    under = c.layer()
    under.rect(0, H - bot_h - 4, W, H, (0, 0, 0, 70))
    c.composite(_clip(under, slab))
    c.rect(0, H - 4, W, H, (0, 0, 0, 0))
    # Moss lip: periodic in the middle third [24, 104] (period 80) so it tiles.
    lip = c.layer()
    for x in range(0, W * 2):
        xx = x / 2.0
        # blade heights from a sum of sines whose periods divide 80 in the edge region
        h = 9 + 3.2 * periodic_wave(80, xx - 24, 4) + 2.2 * periodic_wave(80, xx - 24, 9, 1.3) + 1.2 * periodic_wave(80, xx - 24, 16, 0.4)
        lip.line([(xx, 12), (xx, 12 + h - 2)], MOSS, 1.05)
    # corners: rounded cap (left/right) so the lip wraps the ends
    lip.ellipse(15, 15, 15, 9, MOSS)
    lip.ellipse(W - 15, 15, 15, 9, MOSS)
    lip.rect(15, 5, W - 15, 15, MOSS)
    c.composite(_clip(lip, slab, expand=True))
    hi = c.layer()
    hi.rect(14, 4, W - 14, 8, MOSS_LIGHT)
    hi.rect(14, 8, W - 14, 10, mix(MOSS, MOSS_LIGHT, 0.5))
    c.composite(_clip(hi, lip))
    dark = c.layer()
    dark.rect(0, 20, W, 26, (0, 0, 0, 55))
    c.composite(_clip(dark, lip))
    img = c.result()
    img = _cleanup_periodic_edges(img, 24, W - 24)
    save(img, path)
    write_meta(path, {"border": [side, top_h, side, bot_h]})


def _mask_paint(layer, image, canvas):
    big = image.resize((canvas.w * canvas.ss, canvas.h * canvas.ss), Image.BICUBIC)
    out = Image.new("RGBA", layer.img.size, (0, 0, 0, 0))
    out.paste(big, (0, 0), layer.mask())
    w = type("W", (), {})()
    w.img = out
    return w


def _clip(layer, by, expand=False):
    a = ImageChops.multiply(layer.img.getchannel("A"), by.mask())
    img = layer.img.copy()
    img.putalpha(a)
    w = type("W", (), {})()
    w.img = img
    return w


def _cleanup_periodic_edges(img, x0, x1):
    return img


def write_meta(path, meta):
    doc = {"format": "yk.texture", "version": 1}
    doc.update(meta)
    with open(path + ".ykmeta", "w") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def ledge(path):
    """96 x 32 one-way stone-and-wood ledge. Border [12, 10, 12, 10]."""
    W, H = 96, 32
    c = Canvas(W, H)
    c.rrect(1, 3, W - 1, H - 3, 7, "#221a12")
    wood = gradient_v(W * c.ss, H * c.ss, "#9b7146", "#6a4a2c")
    layer = c.layer()
    layer.rrect(2.5, 4.5, W - 2.5, H - 5, 6, (255, 255, 255, 255))
    out = Image.new("RGBA", layer.img.size, (0, 0, 0, 0))
    out.paste(wood, (0, 0), layer.mask())
    w = type("W", (), {})()
    w.img = out
    c.composite(w)
    # plank seams (period 32 in the middle so the edge region tiles) and a top highlight
    for x in (28, 60):
        c.line([(x, 6), (x, H - 6)], (40, 26, 14, 200), 1.2)
    c.line([(12, 7), (W - 12, 7)], (255, 232, 190, 130), 1.6)
    for x in (7, W - 7):
        c.ellipse(x, H / 2, 3.0, 3.0, "#3b3f45")
        c.ellipse(x - 0.7, H / 2 - 0.8, 1.1, 1.1, "#9aa3ad")
    img = c.result()
    save(img, path)
    write_meta(path, {"border": [12, 10, 12, 10]})


def ramp(path):
    """128 x 128 wedge rising to the right, moss on the sloped top."""
    W = H = 128
    c = Canvas(W, H)
    tri = [(2, H - 2), (W - 2, H - 2), (W - 2, 4)]
    layer = c.layer()
    layer.polygon(tri, (255, 255, 255, 255))
    bricks = brick_tile(128, 4, seed=23)
    c.composite(_mask_paint(layer, bricks, c))
    under = c.layer()
    under.rect(0, H - 22, W, H, (0, 0, 0, 60))
    c.composite(_clip(under, layer))
    # moss along the slope
    moss = c.layer()
    for i in range(0, 256):
        t = i / 255.0
        x = 2 + t * (W - 4)
        y = (H - 2) - t * (H - 6)
        h = 6 + 2.0 * math.sin(i * 0.9) + 1.5 * math.sin(i * 2.3 + 1)
        moss.line([(x, y - 1), (x - 3.2, y + h)], MOSS, 1.3)
    moss.polygon([(2, H - 2), (2 + 4, H - 6), (W - 2, 4), (W - 2, 12)], mix(MOSS, MOSS_LIGHT, 0.4))
    c.composite(_clip(moss, layer))
    img = c.result()
    save(img, path)


def wall_textures(out_dir):
    save(brick_tile(128, 4, seed=5, base="#4c5b4d", dark=False), os.path.join(out_dir, "tiles", "brick.png"))
    save(brick_tile(128, 4, seed=8, base="#46554a", dark=True), os.path.join(out_dir, "tiles", "wall.png"))


def generate(out_dir):
    tiles = os.path.join(out_dir, "tiles")
    os.makedirs(tiles, exist_ok=True)
    wall_textures(out_dir)
    platform(os.path.join(tiles, "platform.png"))
    ledge(os.path.join(tiles, "ledge.png"))
    ramp(os.path.join(tiles, "ramp.png"))


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
