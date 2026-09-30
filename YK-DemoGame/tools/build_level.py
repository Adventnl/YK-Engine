"""Writes the demo level (scenes/level01.ykscene) and the project file (project.ykproj).

The level is assembled from the prefabs in ../prefabs. This script only decides which prefab goes
where and how the mechanisms are wired together; the scene it writes is an ordinary scene file that
the editor opens, edits and saves like any other.

Coordinates are world units (1 unit = 1 meter), +X right, +Y down. The level is 44 x 19; the ground
surface is at y = 17 and the upper floor at y = 10.5.

                       exits   goo pit + shuttle    plates        (upper floor, y 10.5)
        ceiling  ------------------------------------------------------
                 |         |                                      |
                 | gate    |                          stairs -->  |
        ground   start hill | lava+shuttle | lever | water+ledges | crate  (y 17)
"""
import json
import os
import random

from authoring import Node, SceneBuilder, ref, write_json

PROJECT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = "prefabs/"

WIDTH, HEIGHT = 44.0, 19.0
GROUND, UPPER = 17.0, 10.5
CEILING = 1.5
HILL = 1.0  # height of the start hill
LAVA_SHUTTLE_TOP = 14.2  # its top plate; players wade beneath it


def write_project():
    document = {
        "format": "yk.project", "version": 1, "name": "Cinder Vale", "startScene": "scenes/level01.ykscene",
        "window": {"title": "Cinder Vale - a YK Engine demo", "width": 1280, "height": 720},
        "layers": [
            {"name": "Default", "interactsWith": ["Default"]},
            {"name": "Solid", "interactsWith": ["Player", "Prop"]},
            {"name": "Player", "interactsWith": ["Solid", "Sensor", "Prop"]},
            {"name": "Sensor", "interactsWith": ["Player", "Prop"]},
            {"name": "Prop", "interactsWith": ["Solid", "Player", "Sensor", "Prop"]},
        ],
        "textures": {"pixelsPerUnit": 64, "filter": "linear"},
    }
    write_json(os.path.join(PROJECT, "project.ykproj"), document)


class Level:
    def __init__(self):
        self.s = SceneBuilder(PROJECT, "Cinder Vale - Room 1", background="#08141aff")
        self.solid_tops = []  # (x0, x1, y) surfaces decor may grow on

    # ---- terrain pieces --------------------------------------------------------------------
    def platform(self, name, x0, x1, top, thickness=2.0, decor=True):
        w = x1 - x0
        self.s.place(P + "terrain/platform.ykprefab", name, ((x0 + x1) / 2, top + thickness / 2), parent="Terrain",
                     overrides={"SpriteRenderer": {"size": [w, thickness]},
                                "Collider": {"size": [w, thickness - 0.06], "offset": [0, 0.03]}})
        if decor:
            self.solid_tops.append((x0, x1, top))

    def block(self, name, kind, x0, y0, x1, y1, parent="Terrain"):
        w, h = x1 - x0, y1 - y0
        self.s.place(P + "terrain/%s.ykprefab" % kind, name, ((x0 + x1) / 2, (y0 + y1) / 2), parent=parent,
                     overrides={"SpriteRenderer": {"size": [w, h]}, "Collider": {"size": [w, h]}})

    def ledge(self, name, x0, x1, top):
        w = x1 - x0
        self.s.place(P + "terrain/ledge.ykprefab", name, ((x0 + x1) / 2, top + 0.2), parent="Terrain",
                     overrides={"SpriteRenderer": {"size": [w, 0.5]}, "Collider": {"size": [w, 0.3]}})
        self.solid_tops.append((x0, x1, top))

    def ramp(self, name, x0, x1, base, rise, rising_right=True):
        w = x1 - x0
        self.s.place(P + "terrain/ramp.ykprefab", name, ((x0 + x1) / 2, base - rise / 2), parent="Terrain",
                     scale=(1 if rising_right else -1, 1),
                     overrides={"SpriteRenderer": {"size": [w, rise]}, "Collider": {"size": [w, rise]}})

    def pool(self, kind, first_x, count, top):
        """`count` two-unit pieces of a liquid, side by side, with its surface at `top`."""
        for i in range(count):
            self.s.place(P + "hazards/%s.ykprefab" % kind, "%s %d" % (kind.title(), i + 1),
                         (first_x + 2 * i + 1, top + 0.5), parent="Hazards")

    def build_terrain(self):
        s = self.s
        s.group("Terrain")
        self.block("Ceiling", "wall", 0, 0, WIDTH, CEILING)
        self.block("Left Wall", "wall", 0, 0, 1.5, HEIGHT)
        self.block("Right Wall", "wall", WIDTH - 1.5, 0, WIDTH, HEIGHT)
        # Ground floor: start area, beyond the lava, beyond the gate and water, and the far side.
        self.platform("Ground Start", 1.5, 8.0, GROUND)
        self.platform("Ground Middle", 14.0, 25.0, GROUND)
        self.platform("Ground Far", 31.0, WIDTH - 1.5, GROUND)
        self.block("Lava Basin", "brick_block", 8.0, GROUND + 1, 14.0, HEIGHT)
        self.block("Water Basin", "brick_block", 25.0, GROUND + 1, 31.0, HEIGHT)
        # A hill in the start area: a ramp up to a flat top that ends at the lava pit's edge, the
        # take-off point for the platform hovering over it.
        self.ramp("Hill Up", 4.0, 6.0, GROUND, HILL)
        self.block("Hill Top", "brick_block", 6.0, GROUND - HILL, 8.0, GROUND)
        self.solid_tops.append((6.0, 8.0, GROUND - HILL))
        # Above the gate the wall reaches the ceiling; the gate fills its doorway.
        self.block("Gate Wall", "wall", 18.5, CEILING, 19.5, GROUND - 3.0)
        # Jump-through ledges: a route over the water, and stairs up to the upper floor.
        self.ledge("Water Ledge A", 25.3, 27.8, GROUND - 1.8)
        self.ledge("Water Ledge B", 28.3, 30.8, GROUND - 1.8)
        self.ledge("Stair 1", 40.4, 42.4, GROUND - 1.8)
        self.ledge("Stair 2", 38.0, 40.2, GROUND - 3.6)
        self.ledge("Stair 3", 35.8, 37.8, GROUND - 5.4)
        # Upper floor: the exits' side, the goo pit's far side.
        self.platform("Upper Exit Floor", 19.5, 26.0, UPPER, thickness=1.0)
        self.platform("Upper Plate Floor", 32.0, 35.8, UPPER, thickness=1.0)
        self.block("Goo Basin", "brick_block", 26.0, UPPER + 1, 32.0, UPPER + 2)

    def build_hazards(self):
        self.s.group("Hazards")
        self.pool("lava", 8.0, 3, GROUND)
        self.pool("water", 25.0, 3, GROUND)
        self.pool("goo", 26.0, 3, UPPER)

    # ---- mechanisms ------------------------------------------------------------------------
    def build_mechanisms(self):
        s = self.s
        s.group("Mechanisms")
        s.place(P + "mechanisms/lever.ykprefab", "Lever", (15.6, GROUND - 0.75), parent="Mechanisms",
                overrides={"Lever": {"targets": [ref("Gate")]}})
        s.place(P + "mechanisms/gate.ykprefab", "Gate", (19.0, GROUND - 1.5), parent="Mechanisms")
        # The lava crossing hovers above the wading route; the goo crossing runs only while a plate
        # on either side of it is held.
        s.place(P + "mechanisms/shuttle.ykprefab", "Shuttle Lava", (9.5, LAVA_SHUTTLE_TOP + 0.23), parent="Mechanisms",
                overrides={"MovingPlatform": {"travel": [3, 0], "speed": 1.8, "pause": 1.4}})
        s.place(P + "mechanisms/shuttle.ykprefab", "Shuttle Goo", (27.5, UPPER + 0.23), parent="Mechanisms",
                overrides={"MovingPlatform": {"travel": [3, 0], "speed": 2.0, "pause": 0.6, "requireSignal": True}})
        s.place(P + "mechanisms/plate.ykprefab", "Plate Near", (33.4, UPPER + 0.016), parent="Mechanisms",
                overrides={"PressurePlate": {"targets": [ref("Shuttle Goo")]}})
        s.place(P + "mechanisms/plate.ykprefab", "Plate Far", (25.0, UPPER + 0.016), parent="Mechanisms",
                overrides={"PressurePlate": {"targets": [ref("Shuttle Goo")]}})
        s.place(P + "mechanisms/checkpoint.ykprefab", "Checkpoint", (23.5, GROUND - 0.75), parent="Mechanisms")
        s.place(P + "mechanisms/crate.ykprefab", "Crate", (33.0, GROUND - 0.5), parent="Mechanisms")
        s.group("Exits")
        s.place(P + "mechanisms/exit_ember.ykprefab", "Ember Exit", (21.4, UPPER - 1.25), parent="Exits")
        s.place(P + "mechanisms/exit_tide.ykprefab", "Tide Exit", (23.6, UPPER - 1.25), parent="Exits")

    def build_gems(self):
        s = self.s
        s.group("Gems")
        red = P + "items/gem_red.ykprefab"
        blue = P + "items/gem_blue.ykprefab"
        for n, (x, y) in enumerate([(6.6, 15.3), (10.0, GROUND + 0.5), (12.0, GROUND + 0.5), (29.5, 13.9),
                                    (34.9, 9.6), (28.4, 8.6)], 1):
            s.place(red, "Red Gem %d" % n, (x, y), parent="Gems")
        for n, (x, y) in enumerate([(7.3, 15.3), (27.0, GROUND + 0.5), (29.0, GROUND + 0.5), (11.0, 13.0),
                                    (34.9, 9.6), (29.6, 8.6)], 1):
            s.place(blue, "Blue Gem %d" % n, (x, y), parent="Gems")

    def build_characters(self):
        s = self.s
        s.group("Characters")
        y = GROUND - 0.575
        s.place(P + "characters/ember.ykprefab", "Ember", (2.6, y), parent="Characters")
        s.place(P + "characters/tide.ykprefab", "Tide", (3.4, y), parent="Characters")
        s.place(P + "level/spawn_point.ykprefab", "Ember Spawn", (2.6, y), parent="Characters",
                overrides={"SpawnPoint": {"character": ref("Ember")}})
        s.place(P + "level/spawn_point.ykprefab", "Tide Spawn", (3.4, y), parent="Characters",
                overrides={"SpawnPoint": {"character": ref("Tide")}})

    # ---- decoration ------------------------------------------------------------------------
    def build_decor(self):
        s = self.s
        rng = random.Random(2024)
        s.group("Decor")
        keep_clear = [(15.6, 1.0), (19.0, 1.2), (23.5, 1.0), (33.4, 1.6), (25.0, 1.6), (21.4, 1.4), (23.6, 1.4),
                      (33.0, 1.0), (27.0, 0), (2.6, 1.0), (3.4, 1.0), (7.0, 0.9)]

        def clear(x):
            return all(abs(x - cx) > radius for cx, radius in keep_clear)

        count = {"grass": 0, "fern": 0, "mushrooms": 0, "crystals": 0}

        def put(kind, x, y, frame=None, glow_color=None):
            count[kind] += 1
            overrides = {}
            if frame is not None:
                overrides["SpriteRenderer"] = {"frame": frame}
            child = {"Glow": {"Light2D": {"color": glow_color}}} if glow_color else None
            s.place(P + "decor/%s.ykprefab" % kind, "%s %d" % (kind.title(), count[kind]), (x, y), parent="Decor",
                    overrides=overrides or None, child_overrides=child)

        for x0, x1, top in self.solid_tops:
            x = x0 + 0.4
            while x < x1 - 0.3:
                if clear(x):
                    roll = rng.random()
                    if roll < 0.62:
                        put("grass", x, top + 0.05, rng.randrange(3))
                    elif roll < 0.76:
                        put("fern", x, top + 0.05)
                    elif roll < 0.88:
                        variant = rng.randrange(2)
                        put("mushrooms", x, top + 0.05, variant,
                            "#ff96c8ff" if variant == 0 else "#78e6f0ff")
                    else:
                        put("crystals", x, top + 0.05, rng.randrange(2))
                x += rng.uniform(1.1, 2.0)
        # Vines hang from the ceiling and from the underside of the upper floor.
        vines = 0
        for x in [3.0, 6.5, 10.0, 12.5, 16.0, 21.0, 26.5, 30.0, 34.0, 38.0, 41.0]:
            vines += 1
            s.place(P + "decor/vines.ykprefab", "Vines %d" % vines, (x + rng.uniform(-0.4, 0.4), CEILING - 0.1),
                    parent="Decor", overrides={"SpriteRenderer": {"frame": rng.randrange(2)}})
        for x in [22.0, 24.4, 33.0, 34.8]:
            vines += 1
            s.place(P + "decor/vines.ykprefab", "Vines %d" % vines, (x, UPPER + 1.0), parent="Decor",
                    overrides={"SpriteRenderer": {"frame": rng.randrange(2)}})
        # Wall torches light the way.
        for n, (x, y) in enumerate([(2.1, 12.5), (13.0, 9.0), (18.1, 12.0), (19.9, 8.0), (27.0, 5.5), (37.0, 6.0),
                                    (42.0, 12.0)], 1):
            s.place(P + "decor/torch.ykprefab", "Torch %d" % n, (x, y), parent="Decor")

    # ---- the level itself ------------------------------------------------------------------
    def build_scene_parts(self):
        s = self.s
        camera = Node("Camera", (WIDTH / 2, HEIGHT / 2))
        camera.add("Camera", mode="FitTargets", orthographicHeight=13, targets=[ref("Ember"), ref("Tide")],
                   padding=4, smoothTime=0.3, minHeight=11, maxHeight=HEIGHT, clampToBounds=True,
                   boundsMin=[0, 0], boundsMax=[WIDTH, HEIGHT])
        s.add(camera)
        s.place(P + "level/backdrop.ykprefab", "Backdrop", (WIDTH / 2, HEIGHT / 2))
        audio = Node("Audio")
        audio.child(Node("Ambience")).add("AudioSource", sound="assets/audio/ambience.wav", volume=0.5, loop=True,
                                          playOnStart=True)
        audio.child(Node("Music")).add("AudioSource", sound="assets/audio/music.wav", volume=0.3, loop=True,
                                       playOnStart=True)
        s.add(audio)
        rules = Node("Level Flow")
        rules.add("LevelFlow", goals=[ref("Ember Exit"), ref("Tide Exit")], completeSound="assets/audio/complete.wav",
                  completeDelay=4.0)
        s.add(rules)
        s.place(P + "level/hud.ykprefab", "HUD", (0, 0))

    def build(self):
        self.build_scene_parts()
        self.build_terrain()
        self.build_hazards()
        self.build_mechanisms()
        self.build_gems()
        self.build_characters()
        self.build_decor()
        self.s.write("scenes/level01.ykscene")
        print("level01: %d entities" % len(self.s.records))


class Practice(Level):
    """A small room for trying the controls: ramps, a jump-through ledge, a crate, a plate that
    opens a gate, some gems and the two exits. It is also what the level's tests switch scenes to."""

    def __init__(self):
        super().__init__()
        self.s = SceneBuilder(PROJECT, "Practice Room", background="#08141aff")

    def build(self):
        s = self.s
        width, height, floor = 26.0, 14.0, 11.0
        camera = Node("Camera", (width / 2, height / 2))
        camera.add("Camera", mode="FitTargets", orthographicHeight=height, targets=[ref("Ember"), ref("Tide")],
                   padding=3, smoothTime=0.3, minHeight=9, maxHeight=height, clampToBounds=True,
                   boundsMin=[0, 0], boundsMax=[width, height])
        s.add(camera)
        s.place(P + "level/backdrop.ykprefab", "Backdrop", (width / 2, height / 2))
        s.group("Terrain")
        self.block("Ceiling", "wall", 0, 0, width, CEILING)
        self.block("Left Wall", "wall", 0, 0, 1.5, height)
        self.block("Right Wall", "wall", width - 1.5, 0, width, height)
        self.platform("Floor", 1.5, width - 1.5, floor, thickness=height - floor)
        self.ramp("Ramp Up", 5.0, 7.4, floor, 1.2)
        self.block("Ramp Top", "brick_block", 7.4, floor - 1.2, 9.0, floor)
        self.ramp("Ramp Down", 9.0, 11.4, floor, 1.2, rising_right=False)
        self.ledge("Ledge", 12.5, 15.5, floor - 2.2)
        self.platform("Shelf", 17.0, 24.5, floor - 4.0, thickness=1.0)
        s.group("Mechanisms")
        s.place(P + "mechanisms/crate.ykprefab", "Crate", (16.2, floor - 0.5), parent="Mechanisms")
        s.place(P + "mechanisms/plate.ykprefab", "Plate", (18.5, floor + 0.016), parent="Mechanisms",
                overrides={"PressurePlate": {"targets": [ref("Gate")]}})
        s.place(P + "mechanisms/gate.ykprefab", "Gate", (21.0, floor - 1.5), parent="Mechanisms")
        s.place(P + "mechanisms/checkpoint.ykprefab", "Checkpoint", (12.0, floor - 0.75), parent="Mechanisms")
        s.group("Exits")
        s.place(P + "mechanisms/exit_ember.ykprefab", "Ember Exit", (22.4, floor - 1.25), parent="Exits")
        s.place(P + "mechanisms/exit_tide.ykprefab", "Tide Exit", (24.0, floor - 1.25), parent="Exits")
        s.group("Gems")
        s.place(P + "items/gem_red.ykprefab", "Red Gem 1", (8.2, floor - 2.2), parent="Gems")
        s.place(P + "items/gem_blue.ykprefab", "Blue Gem 1", (14.0, floor - 3.4), parent="Gems")
        s.group("Characters")
        y = floor - 0.575
        s.place(P + "characters/ember.ykprefab", "Ember", (2.8, y), parent="Characters")
        s.place(P + "characters/tide.ykprefab", "Tide", (3.8, y), parent="Characters")
        s.place(P + "level/spawn_point.ykprefab", "Ember Spawn", (2.8, y), parent="Characters",
                overrides={"SpawnPoint": {"character": ref("Ember")}})
        s.place(P + "level/spawn_point.ykprefab", "Tide Spawn", (3.8, y), parent="Characters",
                overrides={"SpawnPoint": {"character": ref("Tide")}})
        rules = Node("Level Flow")
        rules.add("LevelFlow", goals=[ref("Ember Exit"), ref("Tide Exit")], completeSound="assets/audio/complete.wav")
        s.add(rules)
        s.place(P + "level/hud.ykprefab", "HUD", (0, 0))
        s.write("scenes/practice.ykscene")
        print("practice: %d entities" % len(s.records))


def main():
    write_project()
    Level().build()
    Practice().build()


if __name__ == "__main__":
    main()
