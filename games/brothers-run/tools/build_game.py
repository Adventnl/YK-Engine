"""Shared, grid-aligned authoring for both co-op levels and the front end.

Level 1 uses clean pieces of the original reference kit; Level 2 uses generated
classroom terrain. Switches, pickups and moving platforms are independent
objects, never baked into terrain. Coordinates use the original 1672 x 941 canvas.
"""
import json
from pathlib import Path

from PIL import Image

from authoring import Node, SceneBuilder, ref, write_json

PROJECT = Path(__file__).resolve().parents[1]
ART = PROJECT / "assets/reference"
SCALE = 50
WIDTH, HEIGHT = 1672, 941


def xy(x, y):
    return [round(x / SCALE, 4), round(y / SCALE, 4)]


def prepare_kit():
    """Extract prop-free surfaces and the two mechanical parts of a button."""
    target = ART / "kit"
    target.mkdir(exist_ok=True)
    bridge = Image.open(ART / "png/terrain/upper_left_block_bridge.png")
    for i, (left, right) in enumerate(((3, 47), (54, 103), (117, 161),
                                      (176, 219), (236, 271))):
        bridge.crop((left, 17, right, 55)).save(target / f"block_{i}.png")
    button = Image.open(ART / "png/props/left_red_button.png")
    button.crop((0, 15, 59, 30)).save(target / "button_housing.png")
    button.crop((13, 0, 51, 15)).save(target / "button_cap.png")
    platform = Image.open(ART / "png/platforms/left_moving_platform.png")
    platform.crop((2, 12, 145, 46)).save(target / "platform.png")
    # The reference includes stray pixels and a large transparent right margin.
    Image.open(ART / "png/props/center_lever.png").crop((3, 0, 60, 56)).save(
        target / "lever.png")
    for color, filename in (("red", "red_pit"), ("blue", "blue_pit"),
                            ("green", "upper_green_pit")):
        source = Image.open(ART / f"png/terrain/{filename}.png")
        # Leftmost liquid cells contain no decorative gems or raised end wall.
        bounds = (48, 16, 108, 53) if color != "green" else (58, 12, 118, 48)
        source.crop(bounds).save(target / f"{color}_liquid.png")
    for path in target.glob("*.png"):
        write_json(str(path) + ".ykmeta", {"format": "yk.texture", "version": 1,
                   "pixelsPerUnit": SCALE, "filter": "nearest"})


def sprite(name, texture, x, y, w, h, layer=10, **extra):
    return Node(name, xy(x + w / 2, y + h / 2)).add(
        "SpriteRenderer", texture=(texture if texture.startswith("assets/")
                                   else "assets/reference/" + texture),
        size=xy(w, h), layer=layer, **extra)


def text(scene, name, value, anchor, offset, scale=2, color="#d6e4edff", layer=4):
    scene.add(Node(name).add("UiText", text=value, anchor=anchor, offset=offset,
                            scale=scale, color=color, layer=layer))


def action(scene, name, input_action, destination):
    scene.add(Node(name).add("EventAction", inputAction=input_action, waitForRelease=True, once=True,
                            changeScene=destination))


def room(name):
    scene = SceneBuilder(str(PROJECT), name, background="#17283bff")
    scene.add(Node("Camera", xy(WIDTH / 2, HEIGHT / 2)).add(
        "Camera", mode="Fixed", orthographicHeight=HEIGHT / SCALE))
    scene.add(sprite("Room Background", "room.png", 0, 0, WIDTH, HEIGHT, -100))
    # Quiet the backdrop so the original saturated play pieces read clearly.
    scene.add(Node("Background Shade", xy(WIDTH / 2, HEIGHT / 2)).add(
        "SpriteRenderer", size=xy(WIDTH, HEIGHT), color="#08131f55", layer=-90))
    for group in ("Terrain", "Hazards", "Mechanisms", "Collectibles", "Characters"):
        scene.group(group)
    return scene


class Level:
    def __init__(self, number):
        self.number = number
        self.scene = room(f"Level {number}")

    def solid(self, name, x, y, w, h=40, one_way=False):
        node = Node(name, xy(x + w / 2, y + h / 2))
        node.add("Collider", size=xy(w, h), layer="Solid", friction=0.8,
                 oneWay=one_way)
        self.scene.add(node, parent="Terrain")

    def deck(self, name, x, y, w, rows=1, one_way=False):
        self.solid(name, x, y, w, rows * 40, one_way)
        for row in range(rows):
            for col in range((w + 47) // 48):
                self.scene.add(sprite(f"{name} Block {row}-{col}",
                    f"kit/block_{(col + row * 2) % 5}.png", x + col * 48,
                    y + row * 40, min(48, w - col * 48), 40), parent="Terrain")

    def pool(self, name, x, y, w, color):
        self.deck(name + " Basin", x, y + 48, w, one_way=False)
        node = sprite(name, f"kit/{color}_liquid.png", x, y, w, 48, 12,
                      drawMode="Tiled", tileSize=xy(60, 48))
        node.add("Collider", size=xy(w - 6, 38), offset=xy(0, 5),
                 isTrigger=True, layer="Sensor")
        node.add("Hazard", affectsTags={"red": ["Tide"], "blue": ["Ember"],
                                       "green": []}[color])
        self.scene.add(node, parent="Hazards")

    def mover(self, name, x, y, w, travel, speed=1.6):
        node = sprite(name, "kit/platform.png", x, y, w, 28, 25)
        node.add("RigidBody", type="Kinematic")
        node.add("Collider", size=xy(w, 28), layer="Solid", friction=1,
                 oneWay=False)
        node.add("MovingPlatform", travel=xy(*travel), speed=speed, pause=1.4,
                 requireSignal=True)
        self.scene.add(node, parent="Mechanisms")

    def plate(self, name, x, floor, target):
        root = sprite(name, "kit/button_housing.png", x - 29.5, floor - 15,
                      59, 15, 32)
        root.add("PressurePlate", pad=ref(name + " Cap"), sensing="Weight",
                 targets=[ref(t) for t in ([target] if isinstance(target, str) else target)],
                 latch=True, pressDepth=0.16,
                 pressSpeed=0.8, acceleration=5, pressedColor="#ffffffff",
                 pressSound="assets/audio/plate_down.wav",
                 releaseSound="assets/audio/plate_up.wav")
        cap = root.child(Node(name + " Cap", xy(0, -7.5)))
        # The solid cap has sloped shoulders down into the mounting surface.
        # Its flat top matches the visible cap; the housing stays fixed.
        cap.add("SpriteRenderer", texture="assets/reference/kit/button_cap.png",
                size=xy(38, 15), layer=31)
        cap.add("RigidBody", type="Kinematic")
        cap.add("Collider", size=xy(70, 40), offset=xy(0, 12.5), chamfer=xy(17.5, 18),
                layer="Solid", friction=0.8)
        self.scene.add(root, parent="Mechanisms")

    def lever(self, name, x, floor, target):
        node = sprite(name, "kit/lever.png", x - 32, floor - 64,
                      64, 64, 32)
        node.add("Collider", size=xy(100, 100), isTrigger=True, layer="Sensor")
        node.add("Lever", interactAction="Interact", targets=[ref(target)],
                 sound="assets/audio/lever.wav", onColor="#ffffffff", flipSprite=True)
        self.scene.add(node, parent="Mechanisms")

    def lift(self, name, x, y, travel):
        """An elevator has a visible shaft, end stops, and a solid riding deck."""
        top, bottom = y + travel, y + 28
        for side in (x + 12, x + 176):
            self.scene.add(Node(name + f" Rail {side}", xy(side, (top + bottom) / 2)).add(
                "SpriteRenderer", size=xy(8, bottom - top + 48), color="#60788aff", layer=5),
                parent="Mechanisms")
            for stop in (top, y):
                self.scene.add(Node(name + f" Stop {side} {stop}", xy(side, stop)).add(
                    "SpriteRenderer", size=xy(20, 8), color="#73d9eaff", layer=6),
                    parent="Mechanisms")
        self.mover(name, x, y, 192, (0, travel), 1.8)

    def gate(self, name, x, floor):
        node = Node(name, xy(x, floor - 54))
        node.add("SpriteRenderer", size=xy(24, 108), color="#8ea5b5ff", layer=24)
        node.add("RigidBody", type="Kinematic")
        node.add("Collider", size=xy(24, 108), layer="Solid")
        node.add("Door", openOffset=xy(0, 140), speed=2.4)
        for y in (-40, -20, 0, 20, 40):
            node.child(Node(name + f" Stripe {y}", xy(0, y))).add(
                "SpriteRenderer", size=xy(24, 6), color="#ffd16fff", layer=25)
        self.scene.add(node, parent="Mechanisms")

    def gem(self, tag, x, y):
        color = "red" if tag == "Ember" else "blue"
        name = f"{tag} Gem {x} {y}"
        node = sprite(name, f"png/collectibles/{color}_orb_left.png",
                      x - 19, y - 20, 38, 40, 45)
        node.add("Collider", shape="Circle", size=xy(32, 32),
                 isTrigger=True, layer="Sensor")
        node.add("Collectible", collectorTags=[tag],
                 variable="ember_gems" if tag == "Ember" else "tide_gems",
                 sound="assets/audio/collect.wav", value=1,
                 collectEffect=f"prefabs/fx/spark_{color}.ykprefab")
        node.add("Oscillator", position=[0, 0.06], frequency=0.65)
        self.scene.add(node, parent="Collectibles")

    def finish(self, exit_position, spawns):
        x, floor = exit_position
        node = sprite("Shared Exit", "png/props/red_door.png",
                      x - 42, floor - 112, 84, 112, 30)
        node.add("Collider", size=xy(48, 94), isTrigger=True, layer="Sensor")
        node.add("Goal", requiredTag="Player", sound="assets/audio/exit.wav")
        self.scene.add(node, parent="Mechanisms")
        for tag, color, x in (("Ember", "orange", 104), ("Tide", "blue", 200)):
            at = xy(x, spawns - 48)
            self.scene.place(f"prefabs/characters/{tag.lower()}.ykprefab", tag, at,
                parent="Characters", tags=[tag, "Player"], overrides={
                    "SpriteRenderer": {"texture": f"assets/reference/{color}_motion.png",
                        "size": [1.6, 2.4], "offset": [0, -0.23], "columns": 8, "rows": 4},
                    "AnimatedSprite": {"animation": f"assets/reference/{color}_motion.ykanim"},
                    "Collider": {"size": [1.12, 1.92]},
                    "PlatformerController": {"jumpHeight": 4.2, "moveSpeed": 5.8}})
            self.scene.place("prefabs/level/spawn_point.ykprefab", tag + " Spawn", at,
                parent="Characters", overrides={"SpawnPoint": {"character": ref(tag)}})
        self.deck("Left Corner", 0, 824, 32, rows=3)
        self.deck("Right Corner", 1616, 824, WIDTH - 1616, rows=3)
        self.deck("Foundation", 0, 904, WIDTH)
        self.solid("Left Wall", -32, 0, 32, HEIGHT)
        self.solid("Right Wall", WIDTH, 0, 32, HEIGHT)
        self.scene.add(Node("Level Flow").add("LevelFlow",
            goals=[ref("Shared Exit")], completeDelay=3,
            nextScene="scenes/level02.ykscene" if self.number == 1 else "scenes/menu.ykscene",
            completeMessage="LEVEL 1 COMPLETE" if self.number == 1 else "BOTH LEVELS COMPLETE!",
            continueAction="Continue", completeSound="assets/audio/complete.wav"))
        self.scene.place("prefabs/level/hud.ykprefab", "HUD", (0, 0), child_overrides={
            "Ember Score": {"UiPanel": {"size": [290, 52]}},
            "Tide Score": {"UiPanel": {"size": [290, 52]}},
            "Ember Gems": {"UiText": {"text": "EMBER {ember_gems:0}/{ember_gems_total:0}"}},
            "Tide Gems": {"UiText": {"text": "TIDE {tide_gems:0}/{tide_gems_total:0}"}},
            "Controls": {"UiText": {"text": "A/D/W + ARROWS  |  S/DOWN USE  |  R RETRY  P PAUSE  M MENU", "scale": 1.5}},
        })
        caption = f"LEVEL {self.number}  /  2"
        if self.number == 2:
            caption += "  -  BUTTONS STAY ON"
        text(self.scene, "Level Number", caption, "Top", [0, 76], 2)
        action(self.scene, "Return to Menu", "Menu", "scenes/menu.ykscene")
        self.scene.add(Node("Music").add("AudioSource", sound="assets/audio/music.wav",
                                        volume=0.22, loop=True, playOnStart=True))
        self.scene.write(f"scenes/level0{self.number}.ykscene")


def level_one():
    level = Level(1)
    level.deck("Starting Floor", 32, 824, 336, rows=2, one_way=False)
    level.pool("Red Pool", 368, 824, 240, "red")
    level.deck("Middle Floor", 608, 824, 384, rows=2, one_way=False)
    level.pool("Blue Pool", 992, 824, 240, "blue")
    level.deck("Right Floor", 1232, 824, 384, rows=2, one_way=False)
    level.deck("Bonus Ledge", 32, 632, 144)
    level.deck("Right Low Step", 1328, 728, 96)
    level.deck("Right Step", 1424, 632, 96)
    level.deck("Right High Step", 1520, 536, 96)
    level.deck("Upper Right Landing", 1088, 472, 240)
    level.pool("Green Pool", 800, 472, 288, "green")
    level.deck("Upper Left Landing", 608, 472, 192)
    level.deck("Exit Step", 512, 376, 96)
    level.deck("Exit Deck", 176, 280, 336)
    level.mover("Red Shuttle", 384, 716, 144, (64, 0))
    level.plate("Red Crossing Button", 272, 824, "Red Shuttle")
    level.mover("Blue Shuttle", 1008, 716, 144, (64, 0))
    level.plate("Blue Crossing Button", 896, 824, "Blue Shuttle")
    level.mover("Green Shuttle", 816, 392, 144, (112, 0))
    level.lever("Upper Crossing Lever", 1232, 472, "Green Shuttle")
    for at in ((488, 790), (744, 762), (1568, 474), (200, 218)):
        level.gem("Ember", *at)
    for at in ((104, 570), (1112, 790), (1160, 410), (472, 218)):
        level.gem("Tide", *at)
    level.finish((176 + 336 / 2, 280), 824)


class ClassroomLevel(Level):
    """Classroom visuals on the same solid surfaces and mechanisms as before."""

    ART = "assets/classroom/generated/"

    def __init__(self):
        from classroom_art import prepare_classroom_art
        prepare_classroom_art()
        super().__init__(2)
        self.scene.set_property("Room Background", "SpriteRenderer",
                                texture=self.ART + "background.png")

    def deck(self, name, x, y, w, rows=1, one_way=False):
        self.solid(name, x, y, w, rows * 40, one_way)
        # Cabinets form the floor; shallow cupboards and books form the upper
        # lanes. Every visual stays inside its original collision rectangle.
        if rows > 1:
            pieces = [("desk", min(w, 384))]
            remaining = w - pieces[0][1]
            while remaining:
                width = min(96, remaining)
                pieces.append(("bookcase", width))
                remaining -= width
        else:
            material = "desk" if name in ("Upper Left Landing", "Upper Right Landing", "Exit Deck") else "books"
            pieces = [(material, min(192, w - start)) for start in range(0, w, 192)]
        offset = 0
        for index, (material, width) in enumerate(pieces):
            self.scene.add(sprite(f"{name} Classroom {index}",
                self.ART + material + ".png", x + offset, y, width, rows * 40),
                parent="Terrain")
            offset += width

    def mover(self, name, x, y, w, travel, speed=1.6):
        super().mover(name, x, y, w, travel, speed)
        self.scene.set_property(name, "SpriteRenderer", texture=self.ART + "books.png")


def level_two():
    level = ClassroomLevel()
    # Three distinct lanes: cross right, return left, then climb to the exit.
    # The long crossings exceed a full running jump, so their controls matter.
    level.deck("Starting Floor", 32, 824, 480, rows=2, one_way=False)
    level.pool("Long Green Pool", 512, 824, 624, "green")
    level.deck("Lift Landing", 1136, 824, 240, rows=2)
    level.deck("Lift Far Bank", 1568, 824, 48, rows=2)
    level.mover("Long Shuttle", 528, 696, 192, (400, 0), 2.6)
    level.plate("Shuttle Button", 400, 824, "Long Shuttle")

    # The lift runs beside the landing, with 48 px of horizontal clearance.
    level.lift("Elevator", 1376, 824, -336)
    level.lever("Elevator Lever", 1232, 824, "Elevator")
    level.deck("Upper Right Landing", 1136, 488, 192)
    level.pool("Upper Blue Pool", 512, 488, 624, "blue")
    level.deck("Upper Left Landing", 224, 488, 288)
    level.mover("Blue Shuttle", 944, 448, 192, (-416, 0), 2.6)
    level.gate("Landing Gate", 1152, 488)
    level.plate("Blue Crossing Button", 1240, 488, ["Blue Shuttle", "Landing Gate"])

    # Two readable 120 px steps lead out of the return lane. The shorter red
    # crossing is the final jump/ride challenge, with a wide, clear exit deck.
    level.lift("Return Elevator", 32, 488, -240)
    level.plate("Return Lift Button", 304, 488, "Return Elevator")
    level.deck("Final Approach", 272, 248, 240, one_way=False)
    level.pool("Upper Red Pool", 512, 248, 384, "red")
    level.deck("Exit Deck", 896, 248, 480, one_way=False)
    level.mover("Red Shuttle", 528, 224, 144, (208, 0), 1.8)
    level.plate("Red Crossing Button", 432, 248, "Red Shuttle")
    for name in ("Shuttle Button", "Blue Crossing Button", "Red Crossing Button", "Return Lift Button"):
        level.scene.set_property(name, "PressurePlate", pressedColor="#91ffc5ff")
    for at in ((304, 762), (792, 634), (1200, 426), (704, 214)):
        level.gem("Ember", *at)
    for at in ((640, 634), (1056, 634), (824, 454), (128, 266)):
        level.gem("Tide", *at)
    level.finish((896 + 480 / 2, 248), 824)


def menus():
    from menus import build_menus
    build_menus(PROJECT)


def configure_project():
    path = PROJECT / "project.ykproj"
    project = json.loads(path.read_text())
    project["name"] = "Brothers Run"
    project["startScene"] = "scenes/menu.ykscene"
    project["window"].update(title="Brothers Run", width=1280, height=720)
    global_set = next(s for s in project["input"]["sets"] if s["name"] == "Global")
    global_set["actions"] = [a for a in global_set["actions"]
                             if a["name"] not in ("Menu", "Level1", "Level2")]
    global_set["actions"] += [
        {"name": "Menu", "bindings": ["Key:M", "Pad:Back"]},
        {"name": "Level1", "bindings": ["Key:Num1", "Pad:South"]},
        {"name": "Level2", "bindings": ["Key:Num2", "Pad:North"]},
    ]
    players = next(layer for layer in project["layers"] if layer["name"] == "Player")
    if "Player" not in players["interactsWith"]:
        players["interactsWith"].append("Player")
    project["build"].update(productName="Brothers Run", executable="BrothersRun",
                            identifier="com.ykengine.brothersrun")
    write_json(path, project)


def build():
    prepare_kit()
    level_one()
    level_two()
    menus()
    configure_project()



if __name__ == "__main__":
    build()
