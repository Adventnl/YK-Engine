"""Import the supplied pixel art and build the single-screen co-op reference level.

The source manifests use pixels on a 1672 x 941 canvas.  Keeping that coordinate
system here makes each sprite easy to move without editing a flattened image.
Collision is authored separately from transparent sprite bounds.
"""
import json
import shutil
from pathlib import Path

from PIL import Image

from authoring import Node, SceneBuilder, ref, write_json

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "YK-DemoGame"
PROJECT = ROOT / "Fireboy-Watergirl-Demo"
SOURCE = ROOT / "game_assets_level1" / "game_assets_level1"
POSES = ROOT / "character_sprites" / "character_sprites"
OUT = PROJECT / "assets" / "reference"
SCALE = 50.0
WIDTH, HEIGHT = 1672 / SCALE, 941 / SCALE


def xy(x, y):
    return (round(x / SCALE, 4), round(y / SCALE, 4))


def box(x0, y0, x1, y1):
    return xy((x0 + x1) / 2, (y0 + y1) / 2), xy(x1 - x0, y1 - y0)


def sprite(name, path, position, size, layer, **extra):
    node = Node(name, xy(position[0] + size[0] / 2, position[1] + size[1] / 2))
    node.add("SpriteRenderer", texture=path, size=list(xy(*size)), layer=layer, **extra)
    return node


def import_art():
    # Gameplay prefabs and audio are shared source material, then copied into
    # this project's asset tree so export and playback need no external paths.
    shutil.copytree(LIBRARY / "assets", PROJECT / "assets", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("reference"))
    shutil.copytree(LIBRARY / "prefabs", PROJECT / "prefabs", dirs_exist_ok=True)
    (PROJECT / "scenes").mkdir(parents=True, exist_ok=True)
    manifest = json.loads((SOURCE / "manifest.json").read_text(encoding="utf-8"))
    for asset in manifest["assets"]:
        target = OUT / asset["file"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(SOURCE / asset["file"], target)
        write_json(str(target) + ".ykmeta", {"format": "yk.texture", "version": 1,
                                                  "pixelsPerUnit": SCALE, "filter": "nearest"})
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "Image_20261002095859_162_30.png", OUT / "room.png")
    write_json(str(OUT / "room.png.ykmeta"), {"format": "yk.texture", "version": 1,
                                                  "pixelsPerUnit": SCALE, "filter": "nearest"})
    return {asset["name"]: asset for asset in manifest["assets"]}


def import_characters():
    # The source has one image per pose.  Reuse the engine's existing animation
    # controller, with short pose sequences placed in its expected clip slots.
    frames = ["front", "three_quarter", "front", "three_quarter",
              "side", "walk", "side", "run", "side", "walk", "run", "walk",
              "jump", "jump", "jump", "side", "walk", "front",
              "three_quarter", "back", "three_quarter_back",
              "front", "three_quarter", "side", "three_quarter_back", "back", "front"]
    clips = [("idle", 0, 4, 4), ("run", 4, 8, 12), ("jump", 12, 2, 8),
             ("fall", 14, 2, 8), ("land", 16, 2, 14),
             ("interact", 18, 3, 9), ("death", 21, 6, 10)]
    for color in ("orange", "blue"):
        sheet = Image.new("RGBA", (8 * 160, 4 * 240))
        for index, pose in enumerate(frames):
            frame = Image.open(POSES / f"{color}_{pose}.png").convert("RGBA")
            x = (index % 8) * 160 + (160 - frame.width) // 2
            y = (index // 8) * 240 + 239 - frame.height
            sheet.alpha_composite(frame, (x, y))
        sheet.save(OUT / f"{color}_motion.png")
        write_json(str(OUT / f"{color}_motion.png.ykmeta"),
                   {"format": "yk.texture", "version": 1, "pixelsPerUnit": SCALE,
                    "filter": "nearest", "columns": 8, "rows": 4})
        write_json(OUT / f"{color}_motion.ykanim", {
            "format": "yk.animation", "version": 2,
            "texture": f"assets/reference/{color}_motion.png", "columns": 8, "rows": 4,
            "clips": [{"name": name, "first": first, "count": count, "fps": fps,
                       "loop": name not in ("jump", "land", "interact", "death")}
                      for name, first, count, fps in clips]})


def build():
    assets = import_art()
    import_characters()
    scene = SceneBuilder(str(PROJECT), "Fireboy and Watergirl - Level One", background="#17283bff")
    scene.add(Node("Camera", xy(836, 470.5)).add("Camera", mode="Fixed",
                   orthographicHeight=HEIGHT, clampToBounds=True,
                   boundsMin=[0, 0], boundsMax=[WIDTH, HEIGHT]))
    scene.add(Node("Room", xy(836, 470.5), locked=True).add(
        "SpriteRenderer", texture="assets/reference/room.png", size=[WIDTH, HEIGHT], layer=-100))
    scene.group("Artwork")
    scene.group("Terrain")
    scene.group("Hazards")
    scene.group("Mechanisms")
    scene.group("Collectibles")
    scene.group("Characters")

    def art(name, layer=10, parent="Artwork"):
        asset = assets[name]
        node = sprite(name.replace("_", " ").title(),
                      "assets/reference/" + asset["file"].replace("\\", "/"),
                      asset["position_px"], asset["size_px"], layer)
        scene.add(node, parent=parent)
        return node

    def solid(name, x0, y0, x1, y1, one_way=False):
        at, size = box(x0, y0, x1, y1)
        scene.add(Node(name, at).add("Collider", size=list(size), oneWay=one_way,
                                      layer="Solid", friction=0.8), parent="Terrain")

    def hazard(name, x0, y0, x1, y1, tags):
        at, size = box(x0, y0, x1, y1)
        scene.add(Node(name, at).add("Collider", size=list(size), isTrigger=True, layer="Sensor")
                  .add("Hazard", affectsTags=tags), parent="Hazards")

    # Each source cutout is rendered once.  The source art includes decorative
    # rims and attached details, so physical surfaces use the measured flat tops.
    for name in ("ground_left", "ground_center", "ground_right",
                 "tall_pillar_with_red_diamond", "upper_left_block_bridge",
                 "upper_right_block_bridge"):
        art(name)
    for name in ("red_pit", "blue_pit", "upper_green_pit"):
        art(name, layer=50)
    for name in ("left_moving_platform", "middle_button_platform", "middle_dino_platform",
                 "upper_right_small_platform", "crystal_platform",
                 "far_right_high_platform", "far_right_low_platform"):
        art(name, layer=20)
    for name in ("center_pedestal", "green_dinosaur"):
        node = art(name, layer=32)
        if name == "green_dinosaur":
            node.add("Oscillator", rotation=5, frequency=0.7)

    # Floor and pit basins.  A friendly element can drop into its basin and jump out.
    solid("Left Floor", 3, 724, 191, 813)
    solid("Left Step", 190, 673, 248, 813)
    solid("Red Basin", 246, 775, 496, 815)
    solid("Center Floor", 496, 725, 900, 816)
    solid("Blue Basin", 901, 775, 1191, 815)
    solid("Right Floor", 1191, 725, 1668, 816)
    solid("Left Boundary", 0, 122, 4, 815)
    solid("Right Boundary", 1667, 122, 1672, 815)
    solid("Upper Left Bridge", 680, 402, 902, 445)
    solid("Upper Right Bridge", 1248, 402, 1419, 452)
    solid("Upper Green Basin", 902, 472, 1247, 503)
    for name, bounds in (
        ("Button Platform", (934, 603, 1038, 648)),
        ("Dino Platform", (1083, 614, 1243, 650)),
        ("Upper Right Step", (1410, 452, 1513, 497)),
        ("Crystal Step", (1479, 582, 1588, 614)),
        ("Far High Step", (1584, 547, 1656, 583)),
        ("Far Low Step", (1554, 640, 1667, 691))):
        solid(name, *bounds, one_way=True)
    hazard("Red Liquid", 251, 729, 493, 775, ["Tide"])
    hazard("Blue Liquid", 907, 729, 1188, 775, ["Ember"])
    hazard("Green Liquid", 955, 427, 1190, 478, [])

    # Left button runs a lift across the red pit while it is held.  Ember can
    # keep it down for Tide, then walk through the red liquid herself.
    button = assets["left_red_button"]
    plate = Node("Left Button", xy(219, 667))
    plate.add("SpriteRenderer", texture="assets/reference/" + button["file"],
              size=list(xy(*button["size_px"])), layer=34)
    plate.add("Collider", size=[0.9, 0.22], isTrigger=True, layer="Sensor")
    plate.add("PressurePlate", targets=[ref("Red Shuttle")], sensing="Region",
              pressDepth=0.06, pressedColor="#ffb9aaff",
              pressSound="assets/audio/plate_down.wav",
              releaseSound="assets/audio/plate_up.wav")
    scene.add(plate, parent="Mechanisms")
    # A moving copy of the striped platform uses the cutout already in the scene.
    # Hide the static art copy during motion by making it the shuttle itself.
    platform_art = next(r for r in scene.records if r["name"] == "Left Moving Platform")
    platform_art["components"].append({"type": "RigidBody", "properties": {"type": "Kinematic"}})
    platform_art["components"].append({"type": "Collider", "properties": {
        "size": list(xy(140, 25)), "offset": [0, -0.16], "layer": "Solid", "friction": 1.0}})
    platform_art["components"].append({"type": "MovingPlatform", "properties": {
        "travel": list(xy(90, 0)), "speed": 1.5, "pause": 0.9, "requireSignal": True}})
    platform_art["name"] = "Red Shuttle"
    scene.ids_by_name["Red Shuttle"] = scene.ids_by_name.pop("Left Moving Platform")

    # Lever starts the upper crossing; a second switch can be operated from the
    # central platform.  Both controls use the art from the supplied set.
    lever_asset = assets["center_lever"]
    lever = sprite("Center Lever", "assets/reference/" + lever_asset["file"],
                   lever_asset["position_px"], lever_asset["size_px"], 35)
    lever.add("Collider", size=[1.6, 1.2], isTrigger=True, layer="Sensor")
    lever.add("Lever", interactAction="Interact", cooldown=0.4,
              onColor="#ffe0acff",
              targets=[ref("Green Shuttle")], sound="assets/audio/lever.wav")
    scene.add(lever, parent="Mechanisms")
    # The green bridge is a slim moving strip; its image is a second use of the
    # striped platform so it remains visually part of the same kit.
    shuttle = Node("Green Shuttle", xy(977, 400))
    shuttle.add("SpriteRenderer", texture="assets/reference/png/platforms/left_moving_platform.png",
                size=[2.94, 0.66], layer=25)
    shuttle.add("RigidBody", type="Kinematic")
    shuttle.add("Collider", size=[2.8, 0.38], layer="Solid", friction=1.0)
    shuttle.add("MovingPlatform", travel=list(xy(195, 0)), speed=1.8,
                pause=0.7, requireSignal=True)
    scene.add(shuttle, parent="Mechanisms")
    for name, x, y in (("Middle Button", 989, 595), ("Upper Button", 1465, 445)):
        switch = Node(name, xy(x, y))
        switch.add("Collider", size=[0.9, 0.2], isTrigger=True, layer="Sensor")
        switch.add("TriggerZone", targets=[ref("Green Shuttle")])
        scene.add(switch, parent="Mechanisms")

    checkpoint = Node("Checkpoint", xy(875, 690))
    checkpoint.add("Collider", size=[0.9, 1.2], isTrigger=True, layer="Sensor")
    checkpoint.add("Checkpoint", respawnOffset=[0, -0.6], sound="assets/audio/checkpoint.wav")
    scene.add(checkpoint, parent="Mechanisms")
    crate_asset = assets["wooden_crate"]
    crate = sprite("Crate", "assets/reference/" + crate_asset["file"],
                   crate_asset["position_px"], crate_asset["size_px"], 36)
    crate.add("RigidBody", type="Dynamic", fixedRotation=True)
    crate.add("Collider", size=[1.85, 0.95], layer="Prop", friction=0.9, density=0.8)
    scene.add(crate, parent="Mechanisms")

    for color, name, tag in (("red", "Ember Exit", "Ember"),
                             ("blue", "Tide Exit", "Tide")):
        asset = assets[f"{color}_door"]
        door = sprite(name, "assets/reference/" + asset["file"],
                      asset["position_px"], asset["size_px"], 40)
        door.add("Collider", size=[1.25, 1.75], offset=[0, 0.15],
                 isTrigger=True, layer="Sensor")
        door.add("Goal", requiredTag=tag, sound="assets/audio/exit.wav")
        scene.add(door, parent="Mechanisms")

    for name in ("red_orb_left", "blue_orb_left", "red_orb_upper",
                 "blue_orb_upper", "cyan_orb_upper", "pink_orb_middle",
                 "blue_crystal_right"):
        asset = assets[name]
        red = name.startswith("red") or name.startswith("pink")
        node = sprite(name.replace("_", " ").title(), "assets/reference/" + asset["file"],
                      asset["position_px"], asset["size_px"], 55)
        node.add("Collider", shape="Circle", size=list(xy(*asset["size_px"])),
                 isTrigger=True, layer="Sensor")
        node.add("Collectible", collectorTags=["Ember" if red else "Tide"],
                 variable="ember_gems" if red else "tide_gems", value=1,
                 sound="assets/audio/collect.wav",
                 collectEffect="prefabs/fx/spark_red.ykprefab" if red else "prefabs/fx/spark_blue.ykprefab")
        node.add("Oscillator", position=[0, 0.12], frequency=0.9)
        scene.add(node, parent="Collectibles")

    for who, color, px in (("Ember", "orange", 95), ("Tide", "blue", 145)):
        y = 724 / SCALE - 0.96
        scene.place(f"prefabs/characters/{who.lower()}.ykprefab", who, (px / SCALE, y),
                    parent="Characters", overrides={
                        "SpriteRenderer": {"texture": f"assets/reference/{color}_motion.png",
                                           "size": [1.6, 2.4], "offset": [0, -0.23],
                                           "columns": 8, "rows": 4},
                        "AnimatedSprite": {"animation": f"assets/reference/{color}_motion.ykanim"},
                        "Collider": {"size": [0.68, 1.92]},
                        "PlatformerController": {"jumpHeight": 4.2, "moveSpeed": 5.8}})
        scene.place("prefabs/level/spawn_point.ykprefab", f"{who} Spawn", (px / SCALE, y),
                    parent="Characters", overrides={"SpawnPoint": {"character": ref(who)}})

    flow = Node("Level Flow")
    flow.add("LevelFlow", goals=[ref("Ember Exit"), ref("Tide Exit")],
             completeSound="assets/audio/complete.wav", completeDelay=4.0,
             nextScene="scenes/reference_level.ykscene", continueAction="Continue")
    scene.add(flow)
    scene.place("prefabs/level/hud.ykprefab", "HUD", (0, 0), child_overrides={
        "Ember Score": {"UiPanel": {"size": [290, 52]}},
        "Tide Score": {"UiPanel": {"size": [290, 52]}},
        "Ember Gem Icon": {"UiImage": {"texture": "assets/reference/png/collectibles/red_orb_left.png",
                                       "columns": 1, "rows": 1, "frame": 0}},
        "Tide Gem Icon": {"UiImage": {"texture": "assets/reference/png/collectibles/blue_orb_left.png",
                                      "columns": 1, "rows": 1, "frame": 0}},
        "Ember Gems": {"UiText": {"text": "FIREBOY {ember_gems:0}/{ember_gems_total:0}"}},
        "Tide Gems": {"UiText": {"text": "WATERGIRL {tide_gems:0}/{tide_gems_total:0}"}},
        "Controls": {"UiText": {"text": "FIREBOY A D W JUMP S USE    WATERGIRL ARROWS UP JUMP DOWN USE    R RESTART  P PAUSE"}},
    })
    audio = Node("Audio")
    audio.child(Node("Ambience")).add("AudioSource", sound="assets/audio/ambience.wav",
                                       volume=0.35, loop=True, playOnStart=True)
    audio.child(Node("Music")).add("AudioSource", sound="assets/audio/music.wav",
                                    volume=0.28, loop=True, playOnStart=True)
    scene.add(audio)
    scene.write("scenes/reference_level.ykscene")
    project_path = PROJECT / "project.ykproj"
    project = json.loads((LIBRARY / "project.ykproj").read_text(encoding="utf-8"))
    project["name"] = "Fireboy and Watergirl"
    project["window"].update({"title": "Fireboy and Watergirl - Level One", "width": 1280,
                              "height": 720})
    project["startScene"] = "scenes/reference_level.ykscene"
    write_json(project_path, project)
    print(f"reference level: {len(scene.records)} entities, {len(assets)} supplied assets")


if __name__ == "__main__":
    build()
