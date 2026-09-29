"""Scenery that dresses a level: grass, hanging vines, ferns, glow mushrooms, a wall torch and
crystal clusters. Each sheet holds a few variants of one plant, so a level can scatter them without
every copy looking alike (the sprite's `frame` picks the variant).
"""
import math
import os
import random

from PIL import Image

from common import Canvas, add_alpha_noise, blur, mix, rgba, save, shade, write_json

LEAF_DARK = "#2f6b2a"
LEAF = "#57a534"
LEAF_LIGHT = "#9ad552"
LEAF_TIP = "#c6ee7a"


def blade(c, x, y, height, lean, width, color, tip):
    """A curved tapering blade rooted at (x, y), growing up (negative y) and leaning by `lean` px."""
    steps = 14
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        bx = x + lean * t * t
        by = y - height * t
        w = width * (1 - t) ** 0.8 * (1 - 0.25 * t)
        left.append((bx - w / 2, by))
        right.append((bx + w / 2, by))
    pts = left + right[::-1]
    c.polygon(pts, color)
    # Lighter tip half.
    tip_pts = left[steps // 2:] + right[steps // 2:][::-1]
    c.polygon(tip_pts, tip)


def grass_variant(seed, w=64, h=40, flowers=False):
    rng = random.Random(seed)
    c = Canvas(w, h)
    for layer_color, tip, count, hmin, hmax in ((LEAF_DARK, LEAF, 9, 16, 30), (LEAF, LEAF_LIGHT, 8, 12, 26),
                                                 (LEAF_LIGHT, LEAF_TIP, 5, 8, 18)):
        for _ in range(count):
            x = rng.uniform(8, w - 8)
            blade(c, x, h + 1, rng.uniform(hmin, hmax), rng.uniform(-9, 9), rng.uniform(4.5, 7), layer_color, tip)
    if flowers:
        for fx, col in ((20, "#ffd166"), (44, "#ff8fa2")):
            fy = h - rng.uniform(22, 30)
            c.line([(fx, h), (fx + rng.uniform(-3, 3), fy)], LEAF_DARK, 1.6)
            for k in range(5):
                a = k * 2 * math.pi / 5
                c.ellipse(fx + math.cos(a) * 3.2, fy + math.sin(a) * 3.2, 2.4, 2.4, col)
            c.ellipse(fx, fy, 1.8, 1.8, "#fff3c4")
    return c.result()


def grass(out_dir):
    variants = [grass_variant(3), grass_variant(11), grass_variant(19, flowers=True)]
    img = Image.new("RGBA", (64 * 3, 40))
    for i, v in enumerate(variants):
        img.paste(v, (i * 64, 0))
    save(img, os.path.join(out_dir, "grass.png"))


def vine_variant(seed, w=64, h=160):
    rng = random.Random(seed)
    c = Canvas(w, h)
    x = w / 2
    pts = []
    for i in range(0, 41):
        t = i / 40
        pts.append((x + math.sin(t * 5 + seed) * 6 + math.sin(t * 11 + seed * 2) * 2, 4 + t * (h * (0.78 + 0.14 * rng.random()))))
    c.line(pts, LEAF_DARK, 4.4)
    c.line(pts, mix(LEAF_DARK, LEAF, 0.55), 2.2)
    for i in range(3, len(pts), 3):
        px, py = pts[i]
        side = 1 if (i // 3) % 2 else -1
        size = rng.uniform(7, 12) * (1.0 - 0.35 * i / len(pts))
        ang = side * rng.uniform(0.5, 0.9)
        tip = (px + math.sin(ang) * size * 1.5, py + math.cos(ang) * size * 0.9)
        mid = ((px + tip[0]) / 2 + side * 2, (py + tip[1]) / 2 - 3)
        mid2 = ((px + tip[0]) / 2 - side * 2.2, (py + tip[1]) / 2 + 3)
        c.polygon([(px, py), mid, tip, mid2], rng.choice([LEAF, LEAF_LIGHT, LEAF]), LEAF_DARK, 0.9)
        c.line([(px, py), tip], LEAF_DARK, 0.9)
    return c.result()


def vines(out_dir):
    img = Image.new("RGBA", (128, 160))
    for i, seed in enumerate((2, 7)):
        img.paste(vine_variant(seed), (i * 64, 0))
    save(img, os.path.join(out_dir, "vines.png"))


def fern(out_dir):
    W, H = 96, 64
    c = Canvas(W, H)
    rng = random.Random(5)
    for k, ang in enumerate((-72, -48, -24, 0, 24, 48, 72)):
        a = math.radians(ang)
        length = 46 - abs(ang) * 0.22
        pts = [(W / 2, H - 2)]
        for i in range(1, 13):
            t = i / 12
            droop = 0.45 * t * t * (1 if ang >= 0 else -1) * abs(math.sin(a))
            pts.append((W / 2 + math.sin(a + droop) * length * t, H - 2 - math.cos(a + droop) * length * t * 0.95))
        c.line(pts, LEAF_DARK, 1.8)
        for i in range(2, 13):
            px, py = pts[i]
            size = 11 * (1 - i / 14)
            for side in (-1, 1):
                dx = math.cos(a) * side * size
                dy = math.sin(a) * side * size * 0.6 + size * 0.35
                col = mix(LEAF, LEAF_LIGHT, i / 12 * 0.8 + rng.uniform(-0.1, 0.1))
                c.polygon([(px, py), (px + dx, py + dy - 1.2), (px + dx * 0.4, py + dy * 0.4 + 2)], col)
    save(c.result(), os.path.join(out_dir, "fern.png"))


def mushroom(c, x, base_y, height, cap_w, cap_color, glow_color):
    stem_w = cap_w * 0.28
    c.rrect(x - stem_w / 2, base_y - height, x + stem_w / 2, base_y, stem_w / 2.2, "#e8dcc5", "#7a6a55", 1.1)
    c.rrect(x - stem_w / 2 + 1, base_y - height + 2, x - stem_w / 2 + 3, base_y - 2, 1, (255, 255, 255, 120))
    cap_h = cap_w * 0.55
    top = base_y - height - cap_h * 0.7
    c.polygon([(x - cap_w / 2, base_y - height + 2)] +
              [(x + math.cos(math.pi + math.pi * i / 20) * cap_w / 2, base_y - height + 2 + math.sin(math.pi + math.pi * i / 20) * cap_h) for i in range(21)] +
              [(x + cap_w / 2, base_y - height + 2)], cap_color, shade(cap_color, 0.5), 1.4)
    for dx, dy, r in ((-0.22, 0.42, 0.13), (0.1, 0.28, 0.16), (0.28, 0.5, 0.1)):
        c.ellipse(x + dx * cap_w, base_y - height - cap_h * dy + 2, cap_w * r, cap_w * r * 0.7, glow_color)


def mushrooms(out_dir):
    img = Image.new("RGBA", (128, 48))
    specs = [
        [(18, 14, 26, "#d8477a", "#ffd9e6"), (42, 9, 18, "#e26a99", "#ffe3ee")],
        [(22, 12, 22, "#3fb7c2", "#d5fbff"), (40, 16, 28, "#37a5b8", "#d5fbff"), (52, 7, 14, "#59cdd6", "#ecffff")],
    ]
    for i, group in enumerate(specs):
        c = Canvas(64, 48)
        for x, height, cap_w, cap, spot in group:
            mushroom(c, x, 46, height, cap_w, cap, spot)
        img.paste(c.result(), (i * 64, 0))
    save(img, os.path.join(out_dir, "mushrooms.png"))


def torch(out_dir):
    W, H = 32, 72
    c = Canvas(W, H)
    cx = W / 2
    # Wall bracket and bowl (the flame is a particle emitter and a glow, not part of the sprite).
    c.rrect(cx - 3, 30, cx + 3, 70, 2, "#4a3a2e", "#1c1410", 1.2)
    c.rrect(cx - 1.5, 32, cx - 0.2, 66, 0.6, (255, 255, 255, 60))
    c.polygon([(cx - 12, 22), (cx + 12, 22), (cx + 8, 38), (cx - 8, 38)], "#5d5046", "#1c1410", 1.5)
    c.rrect(cx - 13, 19, cx + 13, 25, 2.5, "#7a6a5d", "#1c1410", 1.3)
    c.rect(cx - 10, 27, cx + 10, 29, (0, 0, 0, 60))
    c.ellipse(cx, 20.5, 10, 3, "#ff9a3c")
    c.ellipse(cx, 20, 6, 1.8, "#ffe27a")
    c.rrect(cx - 9, 62, cx + 9, 68, 2, "#5d5046", "#1c1410", 1.2)
    save(c.result(), os.path.join(out_dir, "torch.png"))


def crystal_variant(seed, base, light, dark, w=64, h=64):
    rng = random.Random(seed)
    c = Canvas(w, h)
    glow = c.layer()
    glow.ellipse(w / 2, h - 22, 24, 22, (*rgba(light)[:3], 70))
    glow.img = blur(glow.img, 6 * c.ss)
    c.composite(glow)
    specs = [(w / 2, 58, 12, 44), (w / 2 - 15, 60, 9, 28), (w / 2 + 15, 60, 9, 34), (w / 2 - 6, 61, 7, 22)]
    for i, (x, y, half, height) in enumerate(specs):
        lean = rng.uniform(-5, 5)
        pts = [(x - half, y), (x - half + 1, y - height * 0.72), (x + lean, y - height), (x + half - 1, y - height * 0.72), (x + half, y)]
        c.polygon(pts, dark, shade(dark, 0.5), 1.5)
        c.polygon([pts[0], pts[1], pts[2], (x + lean * 0.3, y)], base)
        c.polygon([pts[2], pts[3], pts[4], (x + lean * 0.3, y)], shade(base, 0.82))
        c.polygon([pts[1], pts[2], (x + lean * 0.6 - half * 0.2, y - height * 0.55)], light)
    return c.result()


def crystals(out_dir):
    img = Image.new("RGBA", (128, 64))
    img.paste(crystal_variant(4, "#3fc3d6", "#c4f7ff", "#1c6e86"), (0, 0))
    img.paste(crystal_variant(9, "#5f9bff", "#d0e2ff", "#2a4fa8"), (64, 0))
    save(img, os.path.join(out_dir, "crystals.png"))


def generate(out_dir):
    d = os.path.join(out_dir, "decor")
    os.makedirs(d, exist_ok=True)
    grass(d)
    vines(d)
    fern(d)
    mushrooms(d)
    torch(d)
    crystals(d)


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
