"""Import the supplied classroom cutouts and build the second playable level.

Positions and collision bounds are pixels on the 1672 x 941 reference canvas.
The source artwork is visual only; all gameplay shapes are authored here.
"""
import json
from pathlib import Path
from zipfile import ZipFile

from authoring import Node, SceneBuilder, ref, write_json

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "Fireboy-Watergirl-Demo"
ARCHIVE = ROOT.parent.parent / "Downloads" / "classroom_level_assets.zip"
ART = PROJECT / "assets" / "classroom"
SCALE = 50.0
WIDTH, HEIGHT = 1672 / SCALE, 941 / SCALE


def xy(x, y):
    return (round(x / SCALE, 4), round(y / SCALE, 4))


def box(x0, y0, x1, y1):
    return xy((x0 + x1) / 2, (y0 + y1) / 2), xy(x1 - x0, y1 - y0)


def import_art():
    if ARCHIVE.exists():
        with ZipFile(ARCHIVE) as archive:
            prefix = "classroom_level_assets/"
            manifest_bytes = archive.read(prefix + "manifest.json")
            ART.mkdir(parents=True, exist_ok=True)
            (ART / "manifest.json").write_bytes(manifest_bytes)
            manifest = json.loads(manifest_bytes)
            for asset in manifest["assets"]:
                destination = ART / asset["file"]
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.read(prefix + asset["file"]))
    else:
        manifest = json.loads((ART / "manifest.json").read_text())
    for asset in manifest["assets"]:
        destination = ART / asset["file"]
        if not destination.is_file():
            raise FileNotFoundError(destination)
        write_json(str(destination) + ".ykmeta", {
            "format": "yk.texture", "version": 1,
            "pixelsPerUnit": SCALE, "filter": "nearest"})
    return {asset["name"]: asset for asset in manifest["assets"]}


def build():
    assets = import_art()
    scene = SceneBuilder(str(PROJECT), "Fireboy and Watergirl - Classroom", background="#000000ff")
    scene.add(Node("Camera", xy(836, 470.5)).add(
        "Camera", mode="Fixed", orthographicHeight=HEIGHT, clampToBounds=True,
        boundsMin=[0, 0], boundsMax=[WIDTH, HEIGHT]))
    for group in ("Artwork", "Terrain", "Hazards", "Mechanisms", "Collectibles", "Characters"):
        scene.group(group)

    def art(name, layer=10, parent="Artwork", entity_name=None):
        asset = assets[name]
        at, size = box(*asset["source_position_px"],
                       asset["source_position_px"][0] + asset["size_px"][0],
                       asset["source_position_px"][1] + asset["size_px"][1])
        node = Node(entity_name or "Art " + name.replace("_", " ").title(), at)
        node.add("SpriteRenderer", texture="assets/classroom/" + asset["file"],
                 size=list(size), layer=layer)
        scene.add(node, parent=parent)
        return node

    def sync_art(node):
        # SceneBuilder flattens a node when it is added. Gameplay components
        # attached after placement must also be copied to its scene record.
        record = next(r for r in scene.records if r["name"] == node.name)
        record["components"] = node.components

    def solid(name, x0, y0, x1, y1, one_way=False):
        at, size = box(x0, y0, x1, y1)
        scene.add(Node(name, at).add("Collider", size=list(size),
                  oneWay=one_way, layer="Solid", friction=0.8), parent="Terrain")

    def hazard(name, x0, y0, x1, y1, affected):
        at, size = box(x0, y0, x1, y1)
        scene.add(Node(name, at)
                  .add("Collider", size=list(size), isTrigger=True, layer="Sensor")
                  .add("Hazard", affectsTags=affected), parent="Hazards")

    def plate(name, x0, y0, x1, y1, target, latch=False, tags=()):
        at, size = box(x0, y0, x1, y1)
        scene.add(Node(name, at)
                  .add("Collider", size=list(size), isTrigger=True, layer="Sensor")
                  .add("PressurePlate", targets=[ref(target)], sensing="Region",
                       activatorTags=list(tags), latch=latch, pressDepth=0.04,
                       pressSound="assets/audio/plate_down.wav",
                       releaseSound="assets/audio/plate_up.wav"), parent="Mechanisms")

    def mover(name, asset_name, collider, travel, speed, collider_offset=(0, -6)):
        node = art(asset_name, 30, "Mechanisms", name)
        node.add("RigidBody", type="Kinematic")
        node.add("Collider", size=list(xy(*collider)), offset=list(xy(*collider_offset)),
                 layer="Solid", friction=1.0)
        node.add("MovingPlatform", travel=list(xy(*travel)), speed=speed,
                 pause=0.7, requireSignal=True)
        sync_art(node)

    # The cutouts preserve the exact composition. Some props and basin gems are
    # already baked into their desk/basin cutouts, so they are not drawn twice.
    for name in ("left_desk_with_ramp", "orange_hazard_basin", "middle_desk",
                 "purple_hazard_basin", "upper_left_desk", "upper_blue_basin",
                 "upper_right_desk"):
        art(name, 10 if "basin" not in name else 18)
    art("purple_bookcase_top", 24)
    art("red_exit_door", 45, "Mechanisms", "Ember Exit")
    art("blue_exit_door", 45, "Mechanisms", "Tide Exit")
    art("pencil_cup_upper_right", 46)

    # Lower desks, ramps, liquid basins, and the upper exit route.
    solid("Left Desk", 10, 674, 180, 814)
    for name, bounds in (("Ramp Low", (178, 654, 200, 814)),
                         ("Ramp Mid", (200, 633, 220, 814)),
                         ("Ramp High", (220, 612, 243, 814)),
                         ("Red Button Block", (243, 632, 304, 814))):
        solid(name, *bounds)
    solid("Orange Basin Floor", 306, 733, 549, 815)
    solid("Middle Desk", 549, 674, 925, 814)
    solid("Purple Basin Floor", 926, 773, 1663, 815)
    solid("Upper Left Desk", 915, 395, 1225, 451)
    solid("Blue Basin Floor", 1226, 445, 1431, 494)
    solid("Upper Right Desk", 1431, 395, 1663, 462)
    solid("Bookcase Upper", 753, 355, 807, 520)
    solid("Left Boundary", 0, 260, 8, 815)
    solid("Right Boundary", 1664, 260, 1672, 815)
    hazard("Orange Liquid", 310, 678, 546, 733, ["Tide"])
    hazard("Purple Liquid", 933, 715, 1660, 773, [])
    hazard("Blue Liquid", 1230, 393, 1427, 445, ["Ember"])

    # The red button carries Watergirl over the orange basin. Fireboy can cross
    # through orange liquid and rejoin her on the middle desk.
    mover("Left Cyan Conveyor", "left_cyan_conveyor", (145, 23), (90, 0), 1.5)
    plate("Left Red Button", 251, 611, 293, 635, "Left Cyan Conveyor", tags=("Ember",))

    # Lever retracts the striped lower bookcase panel so both characters can
    # reach the orange elevator. The yellow button then calls that elevator.
    panel = art("blue_striped_vertical_panel", 22, "Mechanisms", "Bookcase Panel")
    panel.add("RigidBody", type="Kinematic")
    panel.add("Collider", size=list(xy(52, 149)), layer="Solid")
    panel.add("Door", openOffset=list(xy(0, -157)), speed=4,
              openSound="assets/audio/gate_open.wav",
              closeSound="assets/audio/gate_close.wav")
    sync_art(panel)
    at, size = box(619, 603, 678, 671)
    scene.add(Node("Gold Lever", at)
              .add("Collider", size=list(size), isTrigger=True, layer="Sensor")
              .add("Lever", interactAction="Interact", cooldown=0.4,
                   targets=[ref("Bookcase Panel")], sound="assets/audio/lever.wav"),
              parent="Mechanisms")
    # Its visual overhang can pass the upper desk; the rider surface stays left
    # of the desk's solid edge while rising.
    mover("Orange Elevator", "middle_orange_conveyor", (80, 22), (0, -235),
          1.8, collider_offset=(-25, -6))
    plate("Middle Yellow Button", 685, 646, 743, 674, "Orange Elevator", latch=True)

    # The upper button sends the cyan conveyor over the blue channel. Both
    # exit doors use their own trigger, matching the two-player goal flow.
    mover("Upper Cyan Conveyor", "upper_cyan_conveyor", (131, 18), (94, 0), 1.6)
    plate("Upper Yellow Button", 1010, 375, 1066, 399,
          "Upper Cyan Conveyor", latch=True)
    for name, tag, bounds in (("Ember Exit", "Ember", (1455, 273, 1516, 382)),
                              ("Tide Exit", "Tide", (1547, 273, 1608, 382))):
        record = next(r for r in scene.records if r["name"] == name)
        at, size = box(*bounds)
        record["components"].append({"type": "Collider", "properties": {
            "size": list(size), "offset": list(xy(at[0] * SCALE - record["transform"]["position"][0] * SCALE,
                                               at[1] * SCALE - record["transform"]["position"][1] * SCALE)),
            "isTrigger": True, "layer": "Sensor"}})
        record["components"].append({"type": "Goal", "properties": {
            "requiredTag": tag, "sound": "assets/audio/exit.wav"}})

    # Four free-standing glowing gems are collectibles. The two basin gems are
    # part of the supplied basin images and remain visible as scenery.
    for name, tag, variable in (("blue_gem_left", "Tide", "tide_gems"),
                                ("red_gem_column", "Ember", "ember_gems"),
                                ("blue_gem_upper", "Tide", "tide_gems"),
                                ("red_gem_upper", "Ember", "ember_gems")):
        node = art(name, 55, "Collectibles")
        asset = assets[name]
        node.add("Collider", shape="Circle", size=list(xy(*asset["size_px"])),
                 isTrigger=True, layer="Sensor")
        node.add("Collectible", collectorTags=[tag], variable=variable, value=1,
                 sound="assets/audio/collect.wav",
                 collectEffect=f"prefabs/fx/spark_{'red' if tag == 'Ember' else 'blue'}.ykprefab")
        node.add("Oscillator", position=[0, 0.1], frequency=0.9)
        sync_art(node)

    for who, color, px in (("Ember", "orange", 80), ("Tide", "blue", 137)):
        y = 674 / SCALE - 0.96
        scene.place(f"prefabs/characters/{who.lower()}.ykprefab", who,
                    (px / SCALE, y), parent="Characters", overrides={
                        "SpriteRenderer": {"texture": f"assets/reference/{color}_motion.png",
                                           "size": [1.6, 2.4], "offset": [0, -0.23],
                                           "columns": 8, "rows": 4},
                        "AnimatedSprite": {"animation": f"assets/reference/{color}_motion.ykanim"},
                        "Collider": {"size": [0.68, 1.92]},
                        "PlatformerController": {"jumpHeight": 4.2, "moveSpeed": 5.8}})
        scene.place("prefabs/level/spawn_point.ykprefab", f"{who} Spawn",
                    (px / SCALE, y), parent="Characters",
                    overrides={"SpawnPoint": {"character": ref(who)}})

    scene.add(Node("Level Flow").add(
        "LevelFlow", goals=[ref("Ember Exit"), ref("Tide Exit")],
        completeSound="assets/audio/complete.wav", completeDelay=4.0,
        continueAction="Continue"))
    scene.place("prefabs/level/hud.ykprefab", "HUD", (0, 0), child_overrides={
        "Ember Gems": {"UiText": {"text": "FIREBOY {ember_gems:0}/{ember_gems_total:0}"}},
        "Tide Gems": {"UiText": {"text": "WATERGIRL {tide_gems:0}/{tide_gems_total:0}"}},
        "Controls": {"UiText": {"text": "CLASSROOM  A/D/W/S + ARROWS/DOWN   R RESTART  P PAUSE"}},
    })
    audio = Node("Audio")
    audio.child(Node("Ambience")).add("AudioSource", sound="assets/audio/ambience.wav",
                                       volume=0.35, loop=True, playOnStart=True)
    audio.child(Node("Music")).add("AudioSource", sound="assets/audio/music.wav",
                                    volume=0.28, loop=True, playOnStart=True)
    scene.add(audio)
    scene.write("scenes/level02.ykscene")
    print(f"level02 classroom: {len(scene.records)} entities, {len(assets)} supplied sprites")


if __name__ == "__main__":
    build()
