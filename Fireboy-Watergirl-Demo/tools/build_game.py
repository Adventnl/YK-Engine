"""Shared, grid-aligned authoring for both co-op levels and the front end.

Only clean pieces of the original reference kit are reused: switches, pickups,
collision surfaces and moving platforms are independent objects, never baked
into terrain. Coordinates are pixels on the original 1672 x 941 canvas.
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
        "SpriteRenderer", texture="assets/reference/" + texture,
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

    def deck(self, name, x, y, w, rows=1, one_way=True):
        assert w % 48 == 0
        self.solid(name, x, y, w, rows * 40, one_way)
        for row in range(rows):
            for col in range(w // 48):
                self.scene.add(sprite(f"{name} Block {row}-{col}",
                    f"kit/block_{(col + row * 2) % 5}.png", x + col * 48,
                    y + row * 40, 48, 40), parent="Terrain")

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
                 oneWay=True)
        node.add("MovingPlatform", travel=xy(*travel), speed=speed, pause=1.4,
                 requireSignal=True)
        self.scene.add(node, parent="Mechanisms")

    def plate(self, name, x, floor, target):
        root = sprite(name, "kit/button_housing.png", x - 29.5, floor - 15,
                      59, 15, 32)
        root.add("PressurePlate", pad=ref(name + " Cap"), sensing="Weight",
                 targets=[ref(target)], latch=True, pressDepth=0.16,
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
        node = sprite(name, "png/props/center_lever.png", x - 26, floor - 60,
                      52, 60, 32)
        node.add("Collider", size=xy(100, 100), isTrigger=True, layer="Sensor")
        node.add("Lever", interactAction="Interact", targets=[ref(target)],
                 sound="assets/audio/lever.wav", onColor="#b6ffb9ff")
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

    def finish(self, exits, spawns):
        for tag, x in zip(("Ember", "Tide"), exits[0]):
            floor = exits[1]
            color = "red" if tag == "Ember" else "blue"
            node = sprite(tag + " Exit", f"png/props/{color}_door.png",
                          x - 42, floor - 112, 84, 112, 30)
            node.add("Collider", size=xy(48, 94), isTrigger=True, layer="Sensor")
            node.add("Goal", requiredTag=tag, sound="assets/audio/exit.wav")
            self.scene.add(node, parent="Mechanisms")
        for tag, color, x in (("Ember", "orange", 104), ("Tide", "blue", 200)):
            at = xy(x, spawns - 48)
            self.scene.place(f"prefabs/characters/{tag.lower()}.ykprefab", tag, at,
                parent="Characters", overrides={
                    "SpriteRenderer": {"texture": f"assets/reference/{color}_motion.png",
                        "size": [1.6, 2.4], "offset": [0, -0.23], "columns": 8, "rows": 4},
                    "AnimatedSprite": {"animation": f"assets/reference/{color}_motion.ykanim"},
                    "Collider": {"size": [1.12, 1.92]},
                    "PlatformerController": {"jumpHeight": 4.2, "moveSpeed": 5.8}})
            self.scene.place("prefabs/level/spawn_point.ykprefab", tag + " Spawn", at,
                parent="Characters", overrides={"SpawnPoint": {"character": ref(tag)}})
        self.solid("Left Wall", 16, 120, 16, 784)
        self.solid("Right Wall", 1616, 120, 16, 784)
        self.scene.add(Node("Level Flow").add("LevelFlow",
            goals=[ref("Ember Exit"), ref("Tide Exit")], completeDelay=3,
            nextScene="scenes/level02.ykscene" if self.number == 1 else "scenes/menu.ykscene",
            completeMessage="LEVEL 1 COMPLETE" if self.number == 1 else "BOTH LEVELS COMPLETE!",
            continueAction="Continue", completeSound="assets/audio/complete.wav"))
        self.scene.place("prefabs/level/hud.ykprefab", "HUD", (0, 0), child_overrides={
            "Ember Score": {"UiPanel": {"size": [290, 52]}},
            "Tide Score": {"UiPanel": {"size": [290, 52]}},
            "Ember Gems": {"UiText": {"text": "FIREBOY {ember_gems:0}/{ember_gems_total:0}"}},
            "Tide Gems": {"UiText": {"text": "WATERGIRL {tide_gems:0}/{tide_gems_total:0}"}},
            "Controls": {"UiText": {"text": "A/D/W + ARROWS  |  S/DOWN USE  |  R RETRY  P PAUSE  M MENU", "scale": 1.5}},
        })
        text(self.scene, "Level Number", f"LEVEL {self.number}  /  2", "Top", [0, 76], 2)
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
    level.deck("Bonus Ledge", 176, 632, 192)
    level.deck("Right Step", 1376, 632, 240)
    level.deck("Upper Right Landing", 1088, 472, 240)
    level.pool("Green Pool", 800, 472, 288, "green")
    level.deck("Upper Left Landing", 512, 472, 288)
    level.deck("Exit Deck", 176, 280, 432)
    level.mover("Red Shuttle", 384, 716, 144, (64, 0))
    level.plate("Red Crossing Button", 272, 824, "Red Shuttle")
    level.mover("Blue Shuttle", 1008, 716, 144, (64, 0))
    level.plate("Blue Crossing Button", 896, 824, "Blue Shuttle")
    level.mover("Green Shuttle", 816, 392, 144, (112, 0))
    level.lever("Upper Crossing Lever", 1232, 472, "Green Shuttle")
    for at in ((488, 790), (744, 762), (1496, 570), (200, 218)):
        level.gem("Ember", *at)
    for at in ((248, 570), (1112, 790), (1160, 410), (520, 218)):
        level.gem("Tide", *at)
    level.finish(((280, 424), 280), 824)


def level_two():
    level = Level(2)
    level.deck("Starting Floor", 32, 824, 480, rows=2, one_way=False)
    level.pool("Long Green Pool", 512, 824, 624, "green")
    level.deck("Lift Landing", 1136, 824, 480, rows=2, one_way=False)
    level.mover("Long Shuttle", 528, 696, 192, (400, 0), 2)
    level.plate("Shuttle Button", 400, 824, "Long Shuttle")
    level.mover("Elevator", 1376, 672, 192, (0, -264), 1.8)
    level.lever("Elevator Lever", 1232, 824, "Elevator")
    level.deck("Upper Right Landing", 1136, 408, 192)
    level.pool("Upper Blue Pool", 896, 408, 240, "blue")
    level.deck("Upper Middle Landing", 608, 408, 288)
    level.pool("Upper Red Pool", 368, 408, 240, "red")
    level.deck("Exit Deck", 80, 408, 288)
    level.mover("Blue Shuttle", 912, 328, 144, (64, 0))
    level.plate("Blue Crossing Button", 1240, 408, "Blue Shuttle")
    level.mover("Red Shuttle", 384, 328, 144, (64, 0))
    level.plate("Red Crossing Button", 800, 408, "Red Shuttle")
    for at in ((304, 762), (792, 634), (1480, 346), (488, 374)):
        level.gem("Ember", *at)
    for at in ((640, 634), (1056, 634), (1016, 374), (680, 346)):
        level.gem("Tide", *at)
    level.finish(((184, 296), 408), 824)


def menus():
    for select in (False, True):
        scene = room("Level Select" if select else "Main Menu")
        # Deliberately screen-space: the title and controls remain centered
        # when an exported game is resized.
        scene.add(Node("Menu Scrim").add("UiImage", fillScreen=True,
                   color="#091422b8", layer=0))
        text(scene, "Title", "FIREBOY & WATERGIRL", "Center", [0, -205], 5,
             "#fff0c9ff")
        text(scene, "Subtitle", "A TWO-PLAYER CO-OP ADVENTURE", "Center", [0, -147], 2)
        for color, x, frame in (("orange", 460, 0), ("blue", 1100, 0)):
            scene.add(sprite(color + " Hero", f"{color}_motion.png", x, 365,
                             112, 168, 15, columns=8, rows=4, frame=frame))
        if select:
            text(scene, "Prompt", "CHOOSE YOUR LEVEL", "Center", [0, -70], 3)
            for n, y, caption, accent in ((1, 0, "CROSSINGS & CLIMB", "#efad75ff"),
                                           (2, 92, "SHUTTLE & LIFT", "#71d9ffff")):
                scene.add(Node(f"Level {n} Card").add("UiPanel", anchor="Center",
                           size=[460, 76], offset=[0, y], color="#152e42f0", layer=2))
                text(scene, f"Level {n} Label", f"[{n}]  LEVEL {n}", "Center", [0, y - 14],
                     3, accent)
                text(scene, f"Level {n} Detail", caption, "Center", [0, y + 16], 1.5)
                action(scene, f"Select Level {n}", f"Level{n}", f"scenes/level0{n}.ykscene")
            action(scene, "Default Level", "Continue", "scenes/level01.ykscene")
            action(scene, "Back", "Menu", "scenes/menu.ykscene")
            text(scene, "Menu Hint", "1 / ENTER: LEVEL 1    2: LEVEL 2    M: BACK", "Center", [0, 180], 2)
        else:
            scene.add(Node("Play Card").add("UiPanel", anchor="Center", size=[360, 80],
                       offset=[0, 0], color="#23546aff", layer=2))
            text(scene, "Play Label", "PLAY  [ENTER]", "Center", [0, 0], 3, "#fff0c9ff")
            text(scene, "Play Hint", "TWO LEVELS  /  ONE KEYBOARD", "Center", [0, 80], 2)
            action(scene, "Play", "Continue", "scenes/levels.ykscene")
        text(scene, "Fireboy Controls", "FIREBOY: A D MOVE / W JUMP / S USE", "Bottom", [0, 100], 2,
             "#efad75ff")
        text(scene, "Watergirl Controls", "WATERGIRL: ARROWS MOVE / UP JUMP / DOWN USE", "Bottom", [0, 70], 2,
             "#71d9ffff")
        text(scene, "Coop Hint", "REACH BOTH MATCHING DOORS TOGETHER", "Bottom", [0, 32], 1.5)
        scene.write("scenes/levels.ykscene" if select else "scenes/menu.ykscene")


def configure_project():
    path = PROJECT / "project.ykproj"
    project = json.loads(path.read_text())
    project["name"] = "Fireboy and Watergirl"
    project["startScene"] = "scenes/menu.ykscene"
    project["window"].update(title="Fireboy and Watergirl", width=1280, height=720)
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
    project["build"].update(productName="Fireboy and Watergirl", executable="FireboyWatergirl",
                            identifier="com.ykengine.fireboywatergirl")
    write_json(path, project)


def build():
    prepare_kit()
    level_one()
    level_two()
    menus()
    configure_project()
    legacy = PROJECT / "scenes/reference_level.ykscene"
    if legacy.exists():
        legacy.unlink()


if __name__ == "__main__":
    build()
