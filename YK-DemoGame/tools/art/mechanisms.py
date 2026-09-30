"""Mechanism art: pressure plate, lever, gate, elevator, crate, checkpoint totem and exit portals.

Each state a mechanism can show is a frame of its sprite sheet. The game does not name frames: the
mechanism components publish parameters (pressed, on, open, reached, satisfied) and the small
animation controller next to each sheet decides what to show.
"""
import math
import os
import random

from PIL import Image

from common import (Canvas, add_alpha_noise, anim_doc, blur, controller_doc, gradient_radial, gradient_v, mix, rgba,
                    save, shade, write_json)

INK = "#111a17"
SLATE = "#26322d"
STONE = "#3d4a44"
STONE_LIGHT = "#5b6c64"
BRASS = "#b98a3a"
BRASS_LIGHT = "#e6bd66"
BRASS_DARK = "#6b4a1a"
RED = "#e2503a"
GREEN = "#5cf28a"


def lit(t):
    """The indicator color between 'off' (red) and 'on' (green)."""
    return mix(RED, GREEN, t)


def glow_dot(c, x, y, r, color):
    layer = c.layer()
    layer.ellipse(x, y, r * 2.2, r * 2.2, (*rgba(color)[:3], 90))
    layer.img = blur(layer.img, r * 1.1 * c.ss)
    c.composite(layer)
    c.ellipse(x, y, r, r, color)
    c.ellipse(x - r * 0.3, y - r * 0.3, r * 0.4, r * 0.4, (255, 255, 255, 160))


# ---------------------------------------------------------------------------------- pressure plate
PLATE_W, PLATE_H = 128, 32


def plate_frame(depth):
    c = Canvas(PLATE_W, PLATE_H)
    c.rrect(2, 15, PLATE_W - 2, PLATE_H, 5, SLATE, INK, 1.4)
    c.rrect(4, 17, PLATE_W - 4, 20, 2, STONE_LIGHT)
    c.rrect(12, 12, PLATE_W - 12, 22, 3, "#0d1512")
    top = 6 + depth * 6
    c.rrect(16, top, PLATE_W - 16, 24, 4, BRASS, BRASS_DARK, 1.3)
    c.rrect(18.5, top + 1.5, PLATE_W - 18.5, top + 5.5, 2.5, BRASS_LIGHT)
    for x in (24, PLATE_W - 24):
        c.ellipse(x, top + 8.5, 1.5, 1.5, BRASS_DARK)
    glow_dot(c, PLATE_W / 2, 27.5, 2.3, lit(depth))
    return c.result()


def plate(out_dir, texture_dir):
    frames = [plate_frame(d) for d in (0.0, 0.5, 1.0)]
    img = Image.new("RGBA", (PLATE_W * 3, PLATE_H))
    for i, f in enumerate(frames):
        img.paste(f, (i * PLATE_W, 0))
    save(img, os.path.join(out_dir, "plate.png"))
    plate_animation(out_dir, texture_dir)


def plate_animation(out_dir, texture_dir):
    """The clips and the state machine of the plate. The pad is a real body that sinks under a load;
    the plate publishes how far down it is (`pressAmount`, 0 up .. 1 fully down) and the brass cap
    follows it: up, half way, down, with a little hysteresis so that it cannot flicker between two
    frames while the pad is at rest."""
    write_json(os.path.join(out_dir, "plate.ykanim"), anim_doc(texture_dir + "plate.png", 3, 1, [
        {"name": "up", "frames": [0], "fps": 1},
        {"name": "mid", "frames": [1], "fps": 1},
        {"name": "down", "frames": [2], "fps": 1},
    ]))
    write_json(os.path.join(out_dir, "plate.ykctl"), controller_doc(
        [{"name": "pressAmount", "type": "float"}], "Up",
        [{"name": "Up", "clip": "up"}, {"name": "Mid", "clip": "mid"}, {"name": "Down", "clip": "down"}],
        [{"from": "Up", "to": "Mid", "when": [{"parameter": "pressAmount", "op": ">", "value": 0.25}]},
         {"from": "Mid", "to": "Down", "when": [{"parameter": "pressAmount", "op": ">", "value": 0.8}]},
         {"from": "Down", "to": "Mid", "when": [{"parameter": "pressAmount", "op": "<", "value": 0.75}]},
         {"from": "Mid", "to": "Up", "when": [{"parameter": "pressAmount", "op": "<", "value": 0.2}]}]))


# ---------------------------------------------------------------------------------- lever
LEVER_W, LEVER_H = 64, 96


def lever_frame(t):
    c = Canvas(LEVER_W, LEVER_H)
    c.rrect(8, 76, 56, 96, 6, STONE, INK, 1.4)
    c.rrect(12, 79, 52, 84, 2.5, STONE_LIGHT)
    c.rrect(22, 86, 42, 92, 3, "#0d1512")
    px, py = 32, 78
    a = math.radians(-32 + 64 * t)
    hx, hy = px + math.sin(a) * 42, py - math.cos(a) * 42
    c.line([(px, py), (hx, hy)], INK, 8)
    c.line([(px, py), (hx, hy)], "#98a59e", 5)
    c.line([(px - 1.2, py - 1), (hx - 1.2, hy - 1)], "#e1e9e4", 1.6)
    knob = lit(t)
    c.ellipse(hx, hy, 9, 9, INK)
    c.ellipse(hx, hy, 7.4, 7.4, knob)
    c.ellipse(hx - 2.4, hy - 2.6, 2.6, 2.6, (255, 255, 255, 170))
    c.ellipse(px, py, 6.5, 5.5, INK)
    c.ellipse(px, py, 5, 4.2, "#7c8a83")
    glow_dot(c, 47, 90, 1.8, lit(t))
    return c.result()


def lever(out_dir, texture_dir):
    img = Image.new("RGBA", (LEVER_W * 3, LEVER_H))
    for i, t in enumerate((0.0, 0.5, 1.0)):
        img.paste(lever_frame(t), (i * LEVER_W, 0))
    save(img, os.path.join(out_dir, "lever.png"))
    write_json(os.path.join(out_dir, "lever.ykanim"), anim_doc(texture_dir + "lever.png", 3, 1, [
        {"name": "off", "frames": [0], "fps": 1},
        {"name": "on", "frames": [2], "fps": 1},
        {"name": "switch_on", "frames": [0, 1, 2], "fps": 24, "loop": False},
        {"name": "switch_off", "frames": [2, 1, 0], "fps": 24, "loop": False},
    ]))
    write_json(os.path.join(out_dir, "lever.ykctl"), controller_doc(
        [{"name": "on", "type": "bool"}], "Off",
        [{"name": "Off", "clip": "off"}, {"name": "SwitchOn", "clip": "switch_on"}, {"name": "On", "clip": "on"},
         {"name": "SwitchOff", "clip": "switch_off"}],
        [{"from": "Off", "to": "SwitchOn", "when": [{"parameter": "on"}]},
         {"from": "SwitchOn", "to": "On", "exitTime": 1.0},
         {"from": "On", "to": "SwitchOff", "when": [{"parameter": "on", "value": False}]},
         {"from": "SwitchOff", "to": "Off", "exitTime": 1.0}]))


# ---------------------------------------------------------------------------------- gate
GATE_W, GATE_H = 64, 192


def gate_frame(open_):
    c = Canvas(GATE_W, GATE_H)
    c.rrect(2, 1, GATE_W - 2, GATE_H - 1, 7, SLATE, INK, 1.8)
    # Vertical slate panels.
    for i, x0 in enumerate((6, 34)):
        c.rrect(x0, 12, x0 + 24, GATE_H - 12, 3, shade(STONE, 0.95 + 0.06 * i))
        c.rect(x0 + 2, 14, x0 + 5, GATE_H - 14, (255, 255, 255, 26))
    # Metal caps.
    for y in (0, GATE_H - 12):
        c.rrect(2, y + 1, GATE_W - 2, y + 12, 3, "#57635d", INK, 1.2)
        for x in (12, 32, 52):
            c.ellipse(x, y + 6.5, 1.8, 1.8, "#1a2420")
    # Rune diamond, red while shut and green while open.
    color = GREEN if open_ else RED
    cx, cy = GATE_W / 2, GATE_H / 2
    glow = c.layer()
    glow.ellipse(cx, cy, 20, 20, (*rgba(color)[:3], 80))
    glow.img = blur(glow.img, 7 * c.ss)
    c.composite(glow)
    c.polygon([(cx, cy - 15), (cx + 11, cy), (cx, cy + 15), (cx - 11, cy)], INK)
    c.polygon([(cx, cy - 12), (cx + 8.5, cy), (cx, cy + 12), (cx - 8.5, cy)], color)
    c.polygon([(cx, cy - 12), (cx + 8.5, cy), (cx, cy)], mix(color, "#ffffff", 0.35))
    c.line([(cx - 18, cy - 24), (cx + 18, cy - 24)], (*rgba(color)[:3], 200), 1.6)
    c.line([(cx - 18, cy + 24), (cx + 18, cy + 24)], (*rgba(color)[:3], 200), 1.6)
    return add_alpha_noise(c.result(), 3.0, 21 + int(open_))


def gate(out_dir, texture_dir):
    img = Image.new("RGBA", (GATE_W * 2, GATE_H))
    for i, o in enumerate((False, True)):
        img.paste(gate_frame(o), (i * GATE_W, 0))
    save(img, os.path.join(out_dir, "gate.png"))
    write_json(os.path.join(out_dir, "gate.ykanim"), anim_doc(texture_dir + "gate.png", 2, 1, [
        {"name": "shut", "frames": [0], "fps": 1},
        {"name": "open", "frames": [1], "fps": 1},
    ]))
    write_json(os.path.join(out_dir, "gate.ykctl"), controller_doc(
        [{"name": "open", "type": "bool"}], "Shut",
        [{"name": "Shut", "clip": "shut"}, {"name": "Open", "clip": "open"}],
        [{"from": "Shut", "to": "Open", "when": [{"parameter": "open"}]},
         {"from": "Open", "to": "Shut", "when": [{"parameter": "open", "value": False}]}]))


# ---------------------------------------------------------------------------------- elevator
ELEV_W, ELEV_H = 192, 40


def elevator(out_dir, texture_dir):
    c = Canvas(ELEV_W, ELEV_H)
    c.rrect(2, 4, ELEV_W - 2, ELEV_H - 3, 9, "#2f3a36", INK, 1.8)
    plate = gradient_v(ELEV_W * c.ss, 16 * c.ss, "#8d9a93", "#5d6a64")
    layer = c.layer()
    layer.rrect(4, 5.5, ELEV_W - 4, 21, 6, (255, 255, 255, 255))
    out = Image.new("RGBA", layer.img.size, (0, 0, 0, 0))
    out.paste(plate.resize((ELEV_W * c.ss, 16 * c.ss)), (0, 5 * c.ss), layer.img.crop((0, 5 * c.ss, ELEV_W * c.ss, 21 * c.ss)).getchannel("A"))
    w = type("W", (), {})()
    w.img = out
    c.composite(w)
    c.line([(10, 8), (ELEV_W - 10, 8)], (255, 255, 255, 110), 1.6)
    for x in range(14, ELEV_W - 8, 22):
        c.ellipse(x, 14, 2.4, 2.4, "#26302c")
        c.ellipse(x - 0.6, 13.4, 0.9, 0.9, "#c1cbc5")
    # Underside light strip and drive gears.
    strip = c.layer()
    strip.rrect(22, 27, ELEV_W - 22, 32, 2.5, (110, 230, 255, 255))
    strip.img = blur(strip.img, 2.2 * c.ss)
    c.composite(strip)
    c.rrect(24, 28, ELEV_W - 24, 31, 1.5, "#bff3ff")
    for x in (14, ELEV_W - 14):
        c.ellipse(x, 28, 8, 8, INK)
        c.ellipse(x, 28, 6.4, 6.4, "#66736d")
        for k in range(6):
            a = k * math.pi / 3
            c.line([(x, 28), (x + math.cos(a) * 6, 28 + math.sin(a) * 6)], INK, 1.3)
        c.ellipse(x, 28, 2.2, 2.2, INK)
    save(add_alpha_noise(c.result(), 2.5, 5), os.path.join(out_dir, "elevator.png"))


# ---------------------------------------------------------------------------------- crate
def crate(out_dir, texture_dir):
    S = 64
    c = Canvas(S, S)
    c.rrect(1, 1, S - 1, S - 1, 4, "#5e3f22", "#2a1a0c", 2.0)
    rng = random.Random(7)
    for i in range(4):
        y0 = 4 + i * 14
        c.rrect(4, y0, S - 4, y0 + 13, 2, shade("#9c7143", rng.uniform(0.88, 1.08)), "#3c2612", 1.1)
        c.line([(8, y0 + 3), (S - 12, y0 + 3)], (255, 226, 180, 70), 1.0)
    # Metal frame and diagonal brace.
    c.line([(6, 6), (S - 6, S - 6)], "#3a3f45", 6)
    c.line([(6, 6), (S - 6, S - 6)], "#7d8791", 3)
    for rect in ((1, 1, S - 1, 9), (1, S - 9, S - 1, S - 1), (1, 1, 9, S - 1), (S - 9, 1, S - 1, S - 1)):
        c.rrect(*rect, 2, "#59626b", "#1f2328", 1.2)
    for x, y in ((5, 5), (S - 5, 5), (5, S - 5), (S - 5, S - 5)):
        c.ellipse(x, y, 1.8, 1.8, "#c4ccd3")
    save(add_alpha_noise(c.result(), 3.0, 8), os.path.join(out_dir, "crate.png"))


# ---------------------------------------------------------------------------------- checkpoint
TOTEM_W, TOTEM_H = 64, 96


def totem_frame(on):
    c = Canvas(TOTEM_W, TOTEM_H)
    cx = TOTEM_W / 2
    c.rrect(14, 84, 50, 96, 4, STONE, INK, 1.4)
    c.rrect(21, 40, 43, 88, 4, SLATE, INK, 1.6)
    c.rrect(24, 44, 27, 84, 1.5, (255, 255, 255, 30))
    for y in (52, 64, 76):
        c.line([(23, y), (41, y)], INK, 1.3)
    color = "#ffd166" if on else "#6f7d76"
    if on:
        glow = c.layer()
        glow.ellipse(cx, 28, 24, 24, (255, 220, 110, 100))
        glow.img = blur(glow.img, 8 * c.ss)
        c.composite(glow)
    c.polygon([(cx, 6), (cx + 12, 26), (cx, 46), (cx - 12, 26)], INK)
    c.polygon([(cx, 9.5), (cx + 9.3, 26), (cx, 42), (cx - 9.3, 26)], color)
    c.polygon([(cx, 9.5), (cx + 9.3, 26), (cx, 26)], mix(color, "#ffffff", 0.45 if on else 0.2))
    c.polygon([(cx, 26), (cx + 9.3, 26), (cx, 42)], shade(color, 0.78))
    c.line([(cx - 12, 46), (cx + 12, 46)], "#7c8a83", 3)
    return c.result()


def checkpoint(out_dir, texture_dir):
    img = Image.new("RGBA", (TOTEM_W * 2, TOTEM_H))
    for i, on in enumerate((False, True)):
        img.paste(totem_frame(on), (i * TOTEM_W, 0))
    save(img, os.path.join(out_dir, "checkpoint.png"))
    write_json(os.path.join(out_dir, "checkpoint.ykanim"), anim_doc(texture_dir + "checkpoint.png", 2, 1, [
        {"name": "dormant", "frames": [0], "fps": 1},
        {"name": "lit", "frames": [1], "fps": 1},
    ]))
    write_json(os.path.join(out_dir, "checkpoint.ykctl"), controller_doc(
        [{"name": "reached", "type": "bool"}], "Dormant",
        [{"name": "Dormant", "clip": "dormant"}, {"name": "Lit", "clip": "lit"}],
        [{"from": "Dormant", "to": "Lit", "when": [{"parameter": "reached"}]}]))


# ---------------------------------------------------------------------------------- exit portals
EXIT_W, EXIT_H = 128, 160
EXIT_FRAMES = 4


def exit_frame(frame, active, palette):
    deep, mid, bright, stone_tint = palette
    c = Canvas(EXIT_W, EXIT_H)
    cx = EXIT_W / 2
    inner_left, inner_right, inner_top, inner_bottom = 26, EXIT_W - 26, 30, EXIT_H - 6

    def arch_points(inset=0.0, steps=48):
        r = (inner_right - inner_left) / 2 - inset
        pts = [(inner_left + inset, inner_bottom)]
        for i in range(steps + 1):
            a = math.pi - math.pi * i / steps
            pts.append((cx + math.cos(a) * r, inner_top + r + inset * 0 - math.sin(a) * r))
        pts.append((inner_right - inset, inner_bottom))
        return pts

    # Portal interior: swirling glow clipped to the arch.
    interior = c.layer()
    interior.polygon(arch_points(0), (255, 255, 255, 255))
    glow_amount = 1.0 if active else 0.45
    bg = gradient_radial(EXIT_W * c.ss, EXIT_H * c.ss, mix(bright, deep, 0.15), deep, 0.5, 0.62, 0.6)
    swirl = Canvas(EXIT_W, EXIT_H)
    phase = 2 * math.pi * frame / EXIT_FRAMES
    for ring in range(5):
        r = 8 + ring * 9
        start = math.degrees(phase * (1 if ring % 2 else -1)) + ring * 47
        swirl.arc(cx, 92, r, r * 1.25, start, start + 190, (*rgba(bright)[:3], int(150 * glow_amount)), 3.2)
    swirl_img = blur(swirl.img, 1.4 * c.ss)
    base = Image.alpha_composite(bg, swirl_img)
    if not active:
        arr = base.copy()
        arr.putalpha(arr.getchannel("A").point(lambda v: int(v * 0.72)))
        base = arr
    painted = Image.new("RGBA", interior.img.size, (0, 0, 0, 0))
    painted.paste(base, (0, 0), interior.mask())
    w = type("W", (), {})()
    w.img = painted
    c.composite(w)
    if active:
        sparkle = Canvas(EXIT_W, EXIT_H)
        rng = random.Random(3)
        for i in range(9):
            sx = cx + rng.uniform(-22, 22)
            sy = 44 + ((rng.uniform(0, 100) - frame * 18) % 100)
            sparkle.ellipse(sx, sy, 1.8, 1.8, (*rgba(bright)[:3], 230))
        clip = interior.mask()
        sp = sparkle.img
        sp.putalpha(Image.eval(sp.getchannel("A"), lambda v: v))
        cut = Image.new("RGBA", sp.size, (0, 0, 0, 0))
        cut.paste(sp, (0, 0), clip)
        w2 = type("W", (), {})()
        w2.img = cut
        c.composite(w2)
    # Stone frame around the opening.
    frame_layer = c.layer()
    frame_layer.polygon(arch_points(-13), (255, 255, 255, 255))
    cut_out = c.layer()
    cut_out.polygon(arch_points(0), (255, 255, 255, 255))
    stone = Canvas(EXIT_W, EXIT_H)
    stone.rect(0, 0, EXIT_W, EXIT_H, mix(stone_tint, "#000000", 0.05))
    for row in range(0, 9):
        for k in range(-1, 4):
            x0 = k * 34 + (row % 2) * 17 - 6
            stone.rrect(x0 + 1, 12 + row * 17, x0 + 33, 12 + row * 17 + 16, 2.5,
                        shade(stone_tint, 0.88 + 0.16 * ((row + k) % 3) / 2))
    stone_img = add_alpha_noise(stone.result(), 4, 12 + frame)
    st = Image.new("RGBA", frame_layer.img.size, (0, 0, 0, 0))
    st.paste(stone_img.resize(frame_layer.img.size, Image.BICUBIC), (0, 0), frame_layer.mask())
    # Carve the opening out of the stone.
    st_alpha = st.getchannel("A")
    hole = cut_out.mask().point(lambda v: 255 - v)
    from PIL import ImageChops
    st.putalpha(ImageChops.multiply(st_alpha, hole))
    w3 = type("W", (), {})()
    w3.img = st
    c.composite(w3)
    # Keystone and edge lines.
    c.polygon(arch_points(-13), None, INK, 2.2)
    c.polygon(arch_points(0), None, INK, 1.6)
    c.polygon([(cx - 9, 8), (cx + 9, 8), (cx + 7, 24), (cx - 7, 24)], shade(stone_tint, 1.25), INK, 1.4)
    c.ellipse(cx, 16, 3.2, 3.2, bright if active else "#5b6c64")
    # Threshold step.
    c.rrect(inner_left - 12, EXIT_H - 6, inner_right + 12, EXIT_H, 2.5, shade(stone_tint, 0.8), INK, 1.4)
    return c.result()


def exits(out_dir, texture_dir):
    palettes = {
        "exit_ember": ("#8a1d0a", "#ff7a1a", "#ffd166", "#5a463f"),
        "exit_tide": ("#0c3a8c", "#3fa9ff", "#c8f3ff", "#3f4f5f"),
    }
    for name, palette in palettes.items():
        img = Image.new("RGBA", (EXIT_W * EXIT_FRAMES, EXIT_H * 2))
        for row, active in enumerate((False, True)):
            for f in range(EXIT_FRAMES):
                img.paste(exit_frame(f, active, palette), (f * EXIT_W, row * EXIT_H))
        save(img, os.path.join(out_dir, name + ".png"))
        write_json(os.path.join(out_dir, name + ".ykanim"), anim_doc(
            texture_dir + name + ".png", EXIT_FRAMES, 2, [
                {"name": "waiting", "first": 0, "count": 4, "fps": 4},
                {"name": "active", "first": 4, "count": 4, "fps": 8},
            ]))
        write_json(os.path.join(out_dir, name + ".ykctl"), controller_doc(
            [{"name": "satisfied", "type": "bool"}], "Waiting",
            [{"name": "Waiting", "clip": "waiting"}, {"name": "Active", "clip": "active"}],
            [{"from": "Waiting", "to": "Active", "when": [{"parameter": "satisfied"}]},
             {"from": "Active", "to": "Waiting", "when": [{"parameter": "satisfied", "value": False}]}]))


def generate(out_dir):
    d = os.path.join(out_dir, "mechanisms")
    os.makedirs(d, exist_ok=True)
    texture_dir = "assets/mechanisms/"
    plate(d, texture_dir)
    lever(d, texture_dir)
    gate(d, texture_dir)
    elevator(d, texture_dir)
    crate(d, texture_dir)
    checkpoint(d, texture_dir)
    exits(d, texture_dir)


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
