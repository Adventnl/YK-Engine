"""The two playable characters: Ember (a flame-crested spirit) and Tide (a droplet-crested spirit).

Original placeholder art drawn procedurally. Both share one sprite-sheet layout, so one animation
controller drives either of them:

    cell 96 x 96 px, 8 columns x 4 rows
    idle 0-3 | run 4-11 | jump 12-13 | fall 14-15 | land 16-17 | interact 18-20 | death 21-26
"""
import math

from PIL import Image

from common import Canvas, blur, fill_masked, gradient_v, mix, rgba, save, shade

CELL = 96
COLUMNS, ROWS = 8, 4
FEET_Y = 89.0
CX = 48.0

CLIPS = [
    # name, first, count, fps, loop
    ("idle", 0, 4, 5, True),
    ("run", 4, 8, 14, True),
    ("jump", 12, 2, 9, False),
    ("fall", 14, 2, 8, True),
    ("land", 16, 2, 22, False),
    ("interact", 18, 3, 10, False),
    ("death", 21, 6, 12, False),
]


class Palette:
    def __init__(self, name, body_top, body_bottom, outline, limb, crest_outer, crest_inner, spark):
        self.name = name
        self.body_top, self.body_bottom = body_top, body_bottom
        self.outline, self.limb = outline, limb
        self.crest_outer, self.crest_inner = crest_outer, crest_inner
        self.spark = spark


EMBER = Palette("ember", "#ff9a3c", "#e2401c", "#7d1d0e", "#c9331a", "#ff7a1a", "#ffe27a", "#ffd166")
TIDE = Palette("tide", "#66d0ff", "#2a73dc", "#123f86", "#215fc0", "#3fa9ff", "#c8f3ff", "#a6e8ff")


class Pose:
    def __init__(self, **kw):
        self.view = "front"      # "front" or "side" (facing right)
        self.sx = 1.0            # horizontal scale about the feet
        self.sy = 1.0            # vertical scale about the feet
        self.bob = 0.0           # body lift in px
        self.lean = 0.0          # forward lean of the head in px (side view)
        self.near_leg = (0.0, 0.0)  # (dx, lift) of the foot nearest the camera / right foot
        self.far_leg = (0.0, 0.0)
        self.near_arm = 0.0      # degrees; 0 hangs down, 90 sticks out forward/right, 180 straight up
        self.far_arm = 0.0
        self.look = 0.0          # pupil offset -1..1
        self.blink = False
        self.mouth = "smile"     # smile | open | oh
        self.crest = 0.0         # phase of the crest sway
        self.crest_lean = 0.0    # crest streaming: -1 back, 0 up, +1 forward
        self.crest_scale = 1.0
        self.alpha = 1.0
        self.flash = 0.0         # 0..1 white flash
        self.sparks = 0.0        # 0..1 sparks radiating from the body (death)
        for k, v in kw.items():
            setattr(self, k, v)


def egg_points(cx, cy, rx, ry, top_narrow=0.13, count=72):
    pts = []
    for i in range(count):
        t = 2 * math.pi * i / count
        pts.append((cx + rx * math.cos(t) * (1 + top_narrow * math.sin(t)), cy + ry * math.sin(t)))
    return pts


def rot(pt, origin, degrees):
    a = math.radians(degrees)
    dx, dy = pt[0] - origin[0], pt[1] - origin[1]
    return (origin[0] + dx * math.cos(a) - dy * math.sin(a), origin[1] + dx * math.sin(a) + dy * math.cos(a))


def limb(canvas, pal, x, y0, y1, w, dx=0.0, lift=0.0):
    """A stubby leg with a rounded foot; (dx, lift) moves the foot."""
    foot_y = y1 - lift
    foot_x = x + dx
    canvas.line([(x, y0), (foot_x, foot_y - 2)], pal.limb, w)
    canvas.ellipse(foot_x, foot_y - 2.5, w * 0.62, w * 0.42, pal.limb)
    canvas.ellipse(foot_x - 1.5, foot_y - 3.4, w * 0.3, w * 0.17, shade(pal.limb, 1.35))


def arm(canvas, pal, sx, sy, degrees, length=13.0, w=6.5, forward=1):
    """An arm hanging from the shoulder (sx, sy), rotated by `degrees` away from hanging down."""
    a = math.radians(degrees)
    tip = (sx + forward * math.sin(a) * length, sy + math.cos(a) * length)
    canvas.line([(sx, sy), tip], pal.limb, w)
    canvas.ellipse(tip[0], tip[1], w * 0.62, w * 0.62, shade(pal.limb, 1.12))


def draw_crest(canvas, pal, pose, base_x, base_y, style):
    """Flame (ember) or droplet (tide) on the head."""
    p = pose.crest
    lean = pose.crest_lean
    scale = pose.crest_scale
    if style == "flame":
        tongues = [(-9, 15, 0.0), (0, 25, 1.6), (9, 17, 3.1)]
        glow = canvas.layer()
        for ox, h, ph in tongues:
            hh = (h + 4.0 * math.sin(p + ph)) * scale
            tip_x = base_x + ox + lean * 7 + 3.0 * math.sin(p * 1.3 + ph)
            for grow, color in ((1.0, pal.crest_outer), (0.62, pal.crest_inner)):
                w = (8.5 if ox == 0 else 7.0) * (1.0 if grow == 1.0 else 0.55)
                top = base_y - hh * grow
                pts = [(base_x + ox - w, base_y + 2), (base_x + ox - w * 0.6, base_y - hh * 0.45 * grow),
                       (tip_x + (base_x + ox - tip_x) * (1 - grow) * 0.0, top),
                       (base_x + ox + w * 0.6, base_y - hh * 0.45 * grow), (base_x + ox + w, base_y + 2)]
                canvas.polygon(pts, color)
            glow.ellipse(base_x + ox, base_y - hh * 0.5, 9, hh * 0.6, (255, 180, 60, 90))
        canvas.composite(Layer_blur(glow, 3.0))
    else:  # droplet crest
        wob = 2.0 * math.sin(p)
        h = 26 * scale
        tip = (base_x + lean * 7 + wob, base_y - h)
        pts = [(base_x - 9, base_y + 2), (base_x - 8, base_y - 6), (tip[0] - 3, tip[1] + 9), tip,
               (tip[0] + 3, tip[1] + 9), (base_x + 8, base_y - 6), (base_x + 9, base_y + 2)]
        canvas.polygon(pts, pal.crest_outer)
        canvas.ellipse(base_x, base_y - 4, 9, 8, pal.crest_outer)
        canvas.polygon([(base_x - 4, base_y - 3), (tip[0] - 1.5, tip[1] + 12), (tip[0], tip[1] + 6),
                        (base_x + 3, base_y - 5)], rgba(pal.crest_inner, 215))
        canvas.ellipse(base_x - 3, base_y - 9, 2.6, 4.2, (255, 255, 255, 190))
        # Two small droplets swaying beside it.
        for side, phase in ((-1, 0.0), (1, 2.2)):
            dx = base_x + side * (13 + 1.5 * math.sin(p + phase)) + lean * 4
            dy = base_y - 8 - 3 * math.sin(p * 1.4 + phase)
            canvas.ellipse(dx, dy, 3.0 * scale, 3.6 * scale, pal.crest_outer)
            canvas.ellipse(dx - 0.8, dy - 1.2, 1.0, 1.4, (255, 255, 255, 200))


def Layer_blur(layer, radius):
    layer.img = blur(layer.img, radius * layer.ss)
    return layer


def draw_face(canvas, pal, pose, cx, cy, side_view):
    """Eyes and mouth on the body at (cx, cy) = center of the face area."""
    look = pose.look
    eyes = [(cx - 8.5, cy), (cx + 8.5, cy)] if not side_view else [(cx - 2.0, cy), (cx + 8.0, cy)]
    sizes = [(6.2, 8.2), (6.2, 8.2)] if not side_view else [(4.8, 7.8), (6.4, 8.4)]
    for (ex, ey), (rx, ry) in zip(eyes, sizes):
        if pose.blink:
            canvas.line([(ex - rx * 0.9, ey + 1), (ex + rx * 0.9, ey + 1)], (50, 30, 30, 255), 1.8)
            continue
        canvas.ellipse(ex, ey, rx, ry, (255, 255, 255, 255))
        canvas.ellipse(ex, ey, rx, ry, None, (40, 24, 24, 255), 1.0)
        px = ex + look * rx * 0.42
        canvas.ellipse(px, ey + 0.8, rx * 0.52, ry * 0.58, (34, 22, 30, 255))
        canvas.ellipse(px - rx * 0.16, ey - ry * 0.22, rx * 0.2, ry * 0.2, (255, 255, 255, 255))
    my = cy + 11.5
    mx = cx + (5.0 if side_view else 0.0)
    if pose.mouth == "smile":
        canvas.arc(mx, my - 3.0, 5.2, 3.6, 20, 160, (60, 22, 22, 255), 1.6)
    elif pose.mouth == "open":
        canvas.ellipse(mx, my, 3.2, 3.8, (60, 22, 22, 255))
        canvas.ellipse(mx, my + 1.6, 2.0, 1.4, (222, 110, 120, 255))
    else:
        canvas.ellipse(mx, my, 2.4, 2.8, (60, 22, 22, 255))


def render_body(pal, pose, style):
    """Draws the character in the given pose, upright, on a 96 x 96 cell (supersampled)."""
    c = Canvas(CELL, CELL)
    side = pose.view == "side"
    bob = pose.bob
    body_cx = CX + (2.0 if side else 0.0)
    body_cy = 66.0 - bob
    rx = 19.0 if not side else 17.0
    ry = 21.5

    # Far limbs first (behind the body).
    far_leg_x = CX + (4 if side else 9)
    limb(c, pal, far_leg_x, body_cy + 12, FEET_Y - bob * 0.0, 8.5, *pose.far_leg)
    raised = side and pose.near_arm > 100
    if side:
        arm(c, pal, body_cx - 3, body_cy + 6, pose.far_arm, 11, 5.6, forward=1)
        if raised:  # A raised near arm would cover the face: draw it behind the body.
            arm(c, pal, body_cx + 4, body_cy + 6, pose.near_arm, 12, 6.2, forward=1)
    else:
        arm(c, pal, body_cx - rx - 1, body_cy + 1, -pose.far_arm if pose.far_arm else 6, 12.5, 6.4, forward=1)

    # The crest sits behind the head so the body overlaps its base.
    head_top = body_cy - ry
    draw_crest(c, pal, pose, body_cx + pose.lean * 0.9, head_top + 5, style)

    # Body: dark outline, gradient fill, highlights.
    shape = egg_points(body_cx + pose.lean * 0.5, body_cy, rx, ry)
    outline = c.layer()
    outline.polygon(shape, pal.outline)
    grown = [(x + (x - (body_cx + pose.lean * 0.5)) * 0.085, y + (y - body_cy) * 0.085) for x, y in shape]
    outline.polygon(grown, pal.outline)
    c.composite(outline)
    fill = c.layer()
    fill.polygon(shape, (255, 255, 255, 255))
    body_img = gradient_v(CELL * c.ss, CELL * c.ss, pal.body_top, pal.body_bottom)
    c.composite(_masked(fill, body_img))
    shine = c.layer()
    shine.ellipse(body_cx - rx * 0.35 + pose.lean * 0.3, body_cy - ry * 0.45, rx * 0.42, ry * 0.3, (255, 255, 255, 105))
    c.composite(_clipped(shine, fill.mask()))
    rim = c.layer()
    rim.ellipse(body_cx + rx * 0.05, body_cy + ry * 1.05, rx * 1.05, ry * 0.45, (0, 0, 0, 80))
    c.composite(_clipped(rim, fill.mask()))

    draw_face(c, pal, pose, body_cx + pose.lean * 0.5, body_cy - 4, side)

    # Near limbs in front.
    near_leg_x = CX + (-2 if side else -9)
    limb(c, pal, near_leg_x, body_cy + 12, FEET_Y, 8.5, *pose.near_leg)
    if side:
        if not raised:
            arm(c, pal, body_cx + 3, body_cy + 7, pose.near_arm, 11.5, 6.2, forward=1)
    else:
        arm(c, pal, body_cx + rx + 1, body_cy + 1, pose.near_arm if pose.near_arm else 6, 12.5, 6.4, forward=-1)
    return c


def _masked(layer, image):
    out = Image.new("RGBA", layer.img.size, (0, 0, 0, 0))
    out.paste(image, (0, 0), layer.mask())
    wrapper = type("W", (), {})()
    wrapper.img = out
    return wrapper


def _clipped(layer, mask):
    from PIL import ImageChops
    a = ImageChops.multiply(layer.img.getchannel("A"), mask)
    img = layer.img.copy()
    img.putalpha(a)
    wrapper = type("W", (), {})()
    wrapper.img = img
    return wrapper


def to_cell(canvas, pose):
    """Scale about the feet, apply flash/alpha, and downsample to the final cell."""
    img = canvas.img
    ss = canvas.ss
    px, py = CX * ss, FEET_Y * ss
    sx, sy = pose.sx, pose.sy
    coeffs = (1 / sx, 0, px - px / sx, 0, 1 / sy, py - py / sy)
    img = img.transform(img.size, Image.AFFINE, coeffs, resample=Image.BICUBIC)
    if pose.flash > 0:
        white = Image.new("RGBA", img.size, (255, 255, 255, int(255 * pose.flash)))
        flashed = Image.alpha_composite(img, white)
        flashed.putalpha(img.getchannel("A"))
        img = flashed
    if pose.alpha < 1.0:
        a = img.getchannel("A").point(lambda v: int(v * pose.alpha))
        img.putalpha(a)
    return img.resize((CELL, CELL), Image.LANCZOS)


def add_sparks(cell, pal, amount, seed):
    """Death sparks: small bright dots radiating from the body."""
    import random
    rng = random.Random(seed)
    c = Canvas(CELL, CELL)
    for i in range(14):
        ang = rng.uniform(0, 2 * math.pi)
        dist = 6 + amount * rng.uniform(14, 38)
        x, y = CX + math.cos(ang) * dist, 60 + math.sin(ang) * dist * 0.85
        r = max(0.6, (1.0 - amount * 0.55) * rng.uniform(1.6, 3.2))
        c.ellipse(x, y, r, r, pal.spark)
        c.ellipse(x, y, r * 0.5, r * 0.5, (255, 255, 255, 230))
    out = Image.alpha_composite(cell, c.result())
    return out


def frames_for(pal, style):
    frames = []

    def add(pose, spark=None):
        canvas = render_body(pal, pose, style)
        cell = to_cell(canvas, pose)
        if spark is not None:
            cell = add_sparks(cell, pal, spark, len(frames) * 7 + 3)
        frames.append(cell)

    # idle: facing the camera, breathing and blinking.
    for i in range(4):
        t = 2 * math.pi * i / 4
        add(Pose(view="front", sy=1.0 + 0.022 * math.sin(t), sx=1.0 - 0.012 * math.sin(t),
                 crest=t * 1.0, blink=(i == 3), near_arm=8 + 3 * math.sin(t), far_arm=8 - 3 * math.sin(t),
                 look=0.0, mouth="smile"))
    # run: side view, legs alternating, body bobbing twice per cycle.
    for i in range(8):
        phi = 2 * math.pi * i / 8
        add(Pose(view="side", lean=4.5, bob=2.6 * abs(math.sin(phi)),
                 near_leg=(10 * math.sin(phi), max(0.0, 7 * math.cos(phi))),
                 far_leg=(-10 * math.sin(phi), max(0.0, -7 * math.cos(phi))),
                 near_arm=-45 * math.sin(phi) + 10, far_arm=45 * math.sin(phi) + 10,
                 look=0.9, crest=phi * 1.6, crest_lean=-0.9, mouth="open" if i % 4 == 1 else "smile"))
    # jump: takeoff stretch, then rising with tucked legs and raised arms.
    add(Pose(view="side", sx=0.9, sy=1.14, lean=2.0, near_leg=(0, 0), far_leg=(-2, 0), near_arm=-25, far_arm=-10,
             look=0.6, crest_lean=-0.3, crest_scale=1.15, mouth="open"))
    add(Pose(view="side", sx=0.96, sy=1.06, bob=6.0, lean=3.0, near_leg=(5, 8), far_leg=(-4, 6), near_arm=140,
             far_arm=120, look=0.7, crest_lean=-0.5, crest_scale=1.2, crest=1.0, mouth="open"))
    # fall: legs dangling and spread, arms up, crest streaming upward.
    for i in range(2):
        f = 1 if i else -1
        add(Pose(view="side", sx=0.98, sy=1.03, bob=4.0, lean=1.0, near_leg=(8, 1 + i), far_leg=(-6, 2 - i),
                 near_arm=115 + 12 * f, far_arm=100 - 12 * f, look=0.4, crest_lean=0.4, crest_scale=0.95,
                 crest=2.0 * i, mouth="oh"))
    # land: a squash, then recovering.
    add(Pose(view="front", sx=1.22, sy=0.76, near_leg=(7, 0), far_leg=(-7, 0), near_arm=55, far_arm=55,
             mouth="oh", crest_scale=0.7))
    add(Pose(view="front", sx=1.08, sy=0.92, near_arm=25, far_arm=25, mouth="smile", crest_scale=0.9))
    # interact: reaching forward and pulling.
    for i, (arm_angle, lean) in enumerate(((60, 3.0), (100, 6.5), (70, 4.0))):
        add(Pose(view="side", lean=lean, near_arm=arm_angle, far_arm=20, look=0.9, near_leg=(2, 0),
                 far_leg=(-3, 0), crest=i * 1.3, crest_lean=-0.3, mouth="open" if i == 1 else "smile"))
    # death: flash, squash, puff into sparks.
    add(Pose(view="front", sy=1.1, sx=0.94, flash=0.75, mouth="oh", crest_scale=1.3, near_arm=70, far_arm=70))
    add(Pose(view="front", sy=0.7, sx=1.32, flash=0.35, mouth="oh", crest_scale=0.6), spark=0.15)
    add(Pose(view="front", sy=0.55, sx=1.3, alpha=0.85, mouth="oh", crest_scale=0.0), spark=0.4)
    add(Pose(view="front", sy=0.42, sx=1.05, alpha=0.6, mouth="oh", crest_scale=0.0), spark=0.7)
    add(Pose(view="front", sy=0.28, sx=0.7, alpha=0.3, mouth="oh", crest_scale=0.0), spark=0.9)
    add(Pose(view="front", sy=0.1, sx=0.3, alpha=0.0), spark=1.0)
    return frames


def generate(out_dir):
    import json
    import os
    for pal, style in ((EMBER, "flame"), (TIDE, "drop")):
        frames = frames_for(pal, style)
        image, rows = _sheet(frames)
        save(image, os.path.join(out_dir, "characters", pal.name + ".png"))
        with open(os.path.join(out_dir, "characters", pal.name + ".ykanim"), "w") as f:
            json.dump(animation_document(pal.name, rows), f, indent=2)
            f.write("\n")
    write_controller(out_dir)


def _sheet(frames):
    rows = (len(frames) + COLUMNS - 1) // COLUMNS
    out = Image.new("RGBA", (COLUMNS * CELL, ROWS * CELL), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        out.paste(frame, ((i % COLUMNS) * CELL, (i // COLUMNS) * CELL))
    return out, ROWS


def animation_document(name, rows):
    clips = []
    for clip_name, first, count, fps, loop in CLIPS:
        entry = {"name": clip_name, "first": first, "count": count, "fps": fps}
        if not loop:
            entry["loop"] = False
        clips.append(entry)
    # Footstep events on the two contact frames of the run cycle.
    for clip in clips:
        if clip["name"] == "run":
            clip["events"] = [{"frame": 0, "name": "footstep", "sound": "assets/audio/step.wav"},
                              {"frame": 4, "name": "footstep", "sound": "assets/audio/step.wav"}]
    return {"format": "yk.animation", "version": 2, "texture": "assets/characters/%s.png" % name,
            "columns": COLUMNS, "rows": rows, "clips": clips}


def write_controller(out_dir):
    import json
    import os
    controller = {
        "format": "yk.animator", "version": 1,
        "parameters": [
            {"name": "speed", "type": "float"}, {"name": "speedRatio", "type": "float"},
            {"name": "velocityY", "type": "float"}, {"name": "grounded", "type": "bool", "default": True},
            {"name": "jumped", "type": "trigger"}, {"name": "landed", "type": "trigger"},
            {"name": "interact", "type": "trigger"}, {"name": "dead", "type": "bool"}],
        "entry": "Idle",
        "states": [
            {"name": "Idle", "clip": "idle"},
            {"name": "Run", "clip": "run", "speedParameter": "speedRatio"},
            {"name": "Jump", "clip": "jump"}, {"name": "Fall", "clip": "fall"},
            {"name": "Land", "clip": "land"}, {"name": "Interact", "clip": "interact"},
            {"name": "Death", "clip": "death"}],
        "transitions": [
            {"from": "*", "to": "Death", "when": [{"parameter": "dead"}]},
            {"from": "*", "to": "Jump", "when": [{"parameter": "jumped"}, {"parameter": "dead", "value": False}]},
            {"from": "Death", "to": "Idle", "when": [{"parameter": "dead", "value": False}]},
            {"from": "Jump", "to": "Fall", "when": [{"parameter": "velocityY", "op": ">", "value": 0.5}]},
            {"from": "Idle", "to": "Fall", "when": [{"parameter": "grounded", "value": False}]},
            {"from": "Run", "to": "Fall", "when": [{"parameter": "grounded", "value": False}]},
            {"from": "Interact", "to": "Fall", "when": [{"parameter": "grounded", "value": False}]},
            {"from": "Fall", "to": "Land", "when": [{"parameter": "landed"}]},
            {"from": "Fall", "to": "Idle", "when": [{"parameter": "grounded"}]},
            {"from": "Land", "to": "Idle", "exitTime": 1.0},
            {"from": "Idle", "to": "Interact", "when": [{"parameter": "interact"}]},
            {"from": "Run", "to": "Interact", "when": [{"parameter": "interact"}]},
            {"from": "Interact", "to": "Idle", "exitTime": 1.0},
            {"from": "Idle", "to": "Run", "when": [{"parameter": "speed", "op": ">", "value": 0.4}]},
            {"from": "Run", "to": "Idle", "when": [{"parameter": "speed", "op": "<=", "value": 0.4}]}]}
    path = os.path.join(out_dir, "characters", "character.ykctl")
    with open(path, "w") as f:
        json.dump(controller, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
