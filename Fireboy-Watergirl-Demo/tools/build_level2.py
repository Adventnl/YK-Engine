"""Build the Sky Foundry: a new art set and a vertical co-op route for Level Two."""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

from authoring import Node, SceneBuilder, ref, write_json

PROJECT = Path(__file__).resolve().parents[1]
ART = PROJECT / "assets" / "sky_foundry"
WIDTH, HEIGHT = 33.44, 18.82
GROUND, UPPER = 16.0, 10.2
PREFABS = "prefabs/"


def save_art(name, image, ppu=64):
    ART.mkdir(parents=True, exist_ok=True)
    path = ART / f"{name}.png"
    image.save(path)
    write_json(str(path) + ".ykmeta", {"format": "yk.texture", "version": 1,
                                       "pixelsPerUnit": ppu, "filter": "linear"})


def make_art():
    rng = random.Random(2026)
    sky = Image.new("RGB", (1672, 941))
    pixels = sky.load()
    for y in range(941):
        t = y / 940
        for x in range(1672):
            glow = max(0, 1-abs(x-1180)/900) * max(0, 1-abs(y-300)/800)
            pixels[x, y] = (int(10+28*t+16*glow), int(20+12*t+8*glow),
                            int(46+32*t+28*glow))
    d = ImageDraw.Draw(sky, "RGBA")
    for _ in range(140):
        x, y = rng.randrange(30, 1640), rng.randrange(75, 720)
        r = rng.choice((1, 1, 2, 3))
        d.ellipse((x-r, y-r, x+r, y+r), fill=(180, 230, 255, rng.randrange(60, 180)))
    d.ellipse((1130, 70, 1540, 480), outline=(95, 224, 245, 90), width=5)
    d.ellipse((1180, 120, 1490, 430), outline=(252, 177, 94, 100), width=3)
    d.ellipse((1250, 190, 1420, 360), fill=(255, 220, 169, 110))
    for i in range(17):
        x, h = i*106-25, rng.randrange(95, 285)
        d.rectangle((x, 795-h, x+76, 941), fill=(9, 20, 42, 150))
        d.polygon([(x+12, 795-h), (x+38, 750-h), (x+64, 795-h)],
                  fill=(9, 20, 42, 150))
        for yy in range(820-h, 760, 32):
            d.rectangle((x+18, yy, x+25, yy+8), fill=(62, 222, 236, 100))
            d.rectangle((x+51, yy, x+58, yy+8), fill=(255, 164, 92, 75))
    save_art("sky", sky, 50)

    for name, fill, trim in (("deck", (20, 38, 63, 255), (71, 226, 231, 255)),
                             ("wall", (29, 29, 59, 255), (155, 108, 218, 255))):
        im = Image.new("RGBA", (256, 128), fill)
        p = ImageDraw.Draw(im)
        p.rectangle((3, 3, 252, 124), outline=trim, width=4)
        p.rectangle((8, 11, 247, 22), fill=trim)
        for x in (60, 128, 196):
            p.line((x, 32, x, 118), fill=(9, 18, 40, 180), width=4)
            p.ellipse((x-4, 59, x+4, 67), fill=trim)
        save_art(name, im)

    for name, trim in (("lift", (255, 185, 91, 255)),
                       ("bridge", (78, 236, 237, 255))):
        im = Image.new("RGBA", (256, 60))
        p = ImageDraw.Draw(im)
        p.rounded_rectangle((3, 8, 252, 54), radius=12, fill=(22, 34, 65, 255),
                            outline=trim, width=5)
        p.rectangle((20, 20, 235, 29), fill=trim)
        for x in range(38, 226, 46):
            p.polygon([(x, 41), (x+17, 41), (x+27, 34), (x+10, 34)],
                      fill=(212, 228, 255, 150))
        save_art(name, im)

    for name, base, line in (("ember_flow", (244, 87, 44), (255, 209, 105, 255)),
                             ("tide_flow", (34, 120, 229), (115, 238, 255, 255)),
                             ("void_flow", (148, 53, 191), (252, 126, 231, 255))):
        im = Image.new("RGBA", (256, 72))
        p = ImageDraw.Draw(im)
        for y in range(8, 72):
            t = (y-8)/64
            p.line((0, y, 255, y), fill=tuple(int(base[i]*(1-t)+18*t)
                                               for i in range(3))+(245,))
        p.line([(x, 10+int(5*math.sin(x/18))) for x in range(256)], fill=line, width=6)
        for x, y in ((29, 44), (91, 58), (152, 32), (212, 49)):
            p.ellipse((x-5, y-5, x+5, y+5), outline=line, width=2)
        save_art(name, im)

    im = Image.new("RGBA", (128, 34))
    p = ImageDraw.Draw(im)
    p.rounded_rectangle((2, 10, 126, 32), radius=7, fill=(25, 38, 65, 255),
                        outline=(137, 172, 206, 255), width=3)
    p.rounded_rectangle((15, 3, 113, 19), radius=7, fill=(255, 182, 77, 255),
                        outline=(255, 236, 174, 255), width=3)
    save_art("pad", im)
    im = Image.new("RGBA", (88, 132))
    p = ImageDraw.Draw(im)
    p.rounded_rectangle((17, 78, 71, 127), radius=8, fill=(25, 35, 63, 255),
                        outline=(80, 219, 227, 255), width=4)
    p.line((44, 84, 53, 44), fill=(225, 231, 244, 255), width=8)
    p.ellipse((37, 18, 70, 51), fill=(255, 174, 84, 255),
              outline=(255, 237, 171, 255), width=4)
    save_art("console", im)
    im = Image.new("RGBA", (90, 300))
    p = ImageDraw.Draw(im)
    p.rounded_rectangle((12, 2, 78, 298), radius=12, fill=(33, 31, 77, 255),
                        outline=(168, 117, 237, 255), width=6)
    for y in range(28, 285, 35):
        p.line((24, y, 66, y-9), fill=(102, 235, 242, 240), width=6)
    save_art("seal", im)
    for name, rim in (("fire_portal", (255, 155, 77, 255)),
                      ("water_portal", (84, 218, 255, 255))):
        im = Image.new("RGBA", (150, 220))
        p = ImageDraw.Draw(im)
        p.rounded_rectangle((12, 10, 138, 215), radius=54, fill=(19, 32, 64, 240),
                            outline=rim, width=12)
        p.rounded_rectangle((33, 33, 117, 195), radius=38, fill=(*rim[:3], 60),
                            outline=(*rim[:3], 190), width=3)
        p.polygon([(75, 54), (85, 104), (75, 169), (65, 104)],
                  fill=(*rim[:3], 180))
        save_art(name, im)


def build():
    make_art()
    scene = SceneBuilder(str(PROJECT), "Fireboy and Watergirl - Sky Foundry",
                         background="#0e1938ff")
    scene.add(Node("Camera", (WIDTH/2, HEIGHT/2)).add(
        "Camera", mode="Fixed", orthographicHeight=HEIGHT, clampToBounds=True,
        boundsMin=[0, 0], boundsMax=[WIDTH, HEIGHT]))
    scene.add(Node("Sky", (WIDTH/2, HEIGHT/2), locked=True).add(
        "SpriteRenderer", texture="assets/sky_foundry/sky.png",
        size=[WIDTH, HEIGHT], layer=-100))
    for group in ("Terrain", "Hazards", "Mechanisms", "Collectibles", "Characters"):
        scene.group(group)

    def solid(name, x0, y0, x1, y1, art="deck"):
        w, h = x1-x0, y1-y0
        scene.add(Node(name, ((x0+x1)/2, (y0+y1)/2))
                  .add("SpriteRenderer", texture=f"assets/sky_foundry/{art}.png",
                       size=[w, h], layer=10)
                  .add("Collider", size=[w, h], layer="Solid", friction=0.8),
                  parent="Terrain")

    def hazard(name, art, x0, y0, x1, y1, tags):
        w, h = x1-x0, y1-y0
        scene.add(Node(name, ((x0+x1)/2, (y0+y1)/2))
                  .add("SpriteRenderer", texture=f"assets/sky_foundry/{art}.png",
                       size=[w, h], layer=50)
                  .add("Collider", size=[w-0.08, h-0.1], isTrigger=True,
                       layer="Sensor")
                  .add("Hazard", affectsTags=tags), parent="Hazards")

    def plate(name, x, top, target):
        scene.add(Node(name, (x, top-0.05))
                  .add("SpriteRenderer", texture="assets/sky_foundry/pad.png",
                       size=[1.8, 0.48], layer=30)
                  .add("Collider", size=[1.7, 0.35], isTrigger=True, layer="Sensor")
                  .add("PressurePlate", targets=[ref(target)], sensing="Region",
                       pressDepth=0.06, pressedColor="#ffda83ff",
                       pressSound="assets/audio/plate_down.wav",
                       releaseSound="assets/audio/plate_up.wav"), parent="Mechanisms")

    def mover(name, x, y, art, travel, speed):
        scene.add(Node(name, (x, y))
                  .add("SpriteRenderer", texture=f"assets/sky_foundry/{art}.png",
                       size=[2.8, 0.66], layer=25)
                  .add("RigidBody", type="Kinematic")
                  .add("Collider", size=[2.7, 0.4], offset=[0, -0.1],
                       layer="Solid", friction=1.0)
                  .add("MovingPlatform", travel=travel, speed=speed,
                       pause=0.6, requireSignal=True), parent="Mechanisms")

    # A rising route across floating docks, a lift shaft, and a coolant channel.
    solid("Left Rail", 0, 0, 0.9, HEIGHT, "wall")
    solid("Right Rail", WIDTH-0.9, 0, WIDTH, HEIGHT, "wall")
    solid("Launch Dock", 0.9, GROUND, 6.4, HEIGHT)
    solid("Ember Basin", 6.4, GROUND+1, 10.4, HEIGHT, "wall")
    solid("Control Dock", 10.4, GROUND, 17.6, HEIGHT)
    hazard("Ember Current", "ember_flow", 6.4, GROUND, 10.4, GROUND+1, ["Tide"])
    solid("Reactor Basin", 17.6, 18.0, WIDTH-0.9, HEIGHT, "wall")
    hazard("Reactor Void", "void_flow", 17.6, 17.0, WIDTH-0.9, 18.0, [])
    solid("Lift Shaft Cap", 14.15, 8.5, 15.05, 13.0, "wall")
    solid("Observatory Deck", 18.3, UPPER, 24.3, UPPER+0.9)
    solid("Coolant Basin", 24.3, UPPER+1, 28.3, UPPER+1.9, "wall")
    solid("Exit Deck", 28.3, UPPER, WIDTH-0.9, UPPER+0.9)
    hazard("Coolant Channel", "tide_flow", 24.3, UPPER, 28.3, UPPER+1,
           ["Ember"])

    # Switches and pads create four distinct activations in the new route.
    mover("Ember Shuttle", 7.1, 14.4, "bridge", [2.6, 0], 1.7)
    plate("Launch Plate", 4.4, GROUND, "Ember Shuttle")
    scene.add(Node("Shaft Seal", (14.6, 14.5))
              .add("SpriteRenderer", texture="assets/sky_foundry/seal.png",
                   size=[0.9, 3], layer=35)
              .add("RigidBody", type="Kinematic")
              .add("Collider", size=[0.85, 3], layer="Solid")
              .add("Door", openOffset=[0, -3.2], speed=4,
                   openSound="assets/audio/gate_open.wav",
                   closeSound="assets/audio/gate_close.wav"), parent="Mechanisms")
    scene.add(Node("Shaft Console", (12.1, 15.25))
              .add("SpriteRenderer", texture="assets/sky_foundry/console.png",
                   size=[0.9, 1.35], layer=35)
              .add("Collider", size=[1.2, 1.2], isTrigger=True, layer="Sensor")
              .add("Lever", interactAction="Interact", cooldown=0.4,
                   targets=[ref("Shaft Seal")], sound="assets/audio/lever.wav"),
              parent="Mechanisms")
    mover("Sky Lift", 16.4, 15.55, "lift", [0, -5.1], 1.5)
    plate("Lift Call", 12.9, GROUND, "Sky Lift")
    plate("Lift Return", 20.1, UPPER, "Sky Lift")
    scene.add(Node("Coolant Console", (21.8, UPPER-0.75))
              .add("SpriteRenderer", texture="assets/sky_foundry/console.png",
                   size=[0.9, 1.35], layer=35)
              .add("Collider", size=[1.2, 1.2], isTrigger=True, layer="Sensor")
              .add("Lever", interactAction="Interact", cooldown=0.4,
                   targets=[ref("Coolant Bridge")], sound="assets/audio/lever.wav"),
              parent="Mechanisms")
    mover("Coolant Bridge", 24.9, 9.96, "bridge", [2.7, 0], 1.8)
    scene.place(PREFABS + "mechanisms/checkpoint.ykprefab", "Shaft Checkpoint",
                (11.3, GROUND-0.75), parent="Mechanisms")

    for who, color, x in (("Ember", "orange", 2.2), ("Tide", "blue", 3.1)):
        y = GROUND-0.96
        scene.place(PREFABS + f"characters/{who.lower()}.ykprefab", who,
                    (x, y), parent="Characters", overrides={
                        "SpriteRenderer": {"texture": f"assets/reference/{color}_motion.png",
                                           "size": [1.6, 2.4], "offset": [0, -0.23],
                                           "columns": 8, "rows": 4},
                        "AnimatedSprite": {"animation": f"assets/reference/{color}_motion.ykanim"},
                        "Collider": {"size": [0.68, 1.92]},
                        "PlatformerController": {"jumpHeight": 4.2, "moveSpeed": 5.8}})
        scene.place(PREFABS + "level/spawn_point.ykprefab", f"{who} Spawn",
                    (x, y), parent="Characters",
                    overrides={"SpawnPoint": {"character": ref(who)}})

    for who, art, x in (("Ember", "fire_portal", 29.4),
                        ("Tide", "water_portal", 31.2)):
        scene.add(Node(f"{who} Exit", (x, UPPER-1.1))
                  .add("SpriteRenderer", texture=f"assets/sky_foundry/{art}.png",
                       size=[1.5, 2.2], layer=40)
                  .add("Collider", size=[1.2, 1.8], isTrigger=True, layer="Sensor")
                  .add("Goal", requiredTag=who, sound="assets/audio/exit.wav"),
                  parent="Mechanisms")

    for color, points in (("red", [(8.2, 16.5), (15.9, 12.6), (22.2, 9.2)]),
                          ("blue", [(8.3, 13.5), (18.9, 9.2), (26.3, 10.8)])):
        for number, point in enumerate(points, 1):
            scene.place(PREFABS + f"items/gem_{color}.ykprefab",
                        f"{color.title()} Gem {number}", point,
                        parent="Collectibles")

    scene.add(Node("Level Flow").add(
        "LevelFlow", goals=[ref("Ember Exit"), ref("Tide Exit")],
        completeSound="assets/audio/complete.wav", completeDelay=4.0,
        continueAction="Continue"))
    scene.place(PREFABS + "level/hud.ykprefab", "HUD", (0, 0), child_overrides={
        "Ember Gems": {"UiText": {"text": "FIREBOY {ember_gems:0}/{ember_gems_total:0}"}},
        "Tide Gems": {"UiText": {"text": "WATERGIRL {tide_gems:0}/{tide_gems_total:0}"}},
        "Controls": {"UiText": {"text": "SKY FOUNDRY  A/D/W/S + ARROWS/DOWN   R RESTART  P PAUSE"}},
    })
    audio = Node("Audio")
    audio.child(Node("Ambience")).add("AudioSource", sound="assets/audio/ambience.wav",
                                       volume=0.35, loop=True, playOnStart=True)
    audio.child(Node("Music")).add("AudioSource", sound="assets/audio/music.wav",
                                    volume=0.28, loop=True, playOnStart=True)
    scene.add(audio)
    scene.write("scenes/level02.ykscene")
    print(f"level02: {len(scene.records)} entities")


if __name__ == "__main__":
    build()
