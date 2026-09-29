"""Writes every prefab of the demo game into ../prefabs.

A prefab here is an entity built only from the engine's generic components: nothing in this file
(or in the engine) knows what a "lava pool" or an "exit door" is. A Hazard component with
affectsTags ["Tide"] and lava-looking art is a lava pool; the rules are data.

Run:  python3 build_prefabs.py     (then `yk format ..` to write them in canonical form)
"""
import os
import sys

from authoring import Node, write_prefab

PROJECT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# Draw order of world sprites (SpriteRenderer.layer). Higher draws on top.
BACKGROUND_FAR, BACKGROUND_MID, DECOR_BACK = -100, -90, -20
GATE, TERRAIN, DECOR_FRONT, MECHANISM, PROP, CHARACTER, LIQUID, ITEM, PARTICLES, GLOW = 0, 10, 12, 20, 35, 40, 50, 55, 60, 70

SND = "assets/audio/"


def sound(name):
    return SND + name + ".wav"


def rgba(r, g, b, a=255):
    return "#%02x%02x%02x%02x" % (r, g, b, a)


# ---------------------------------------------------------------------------------------- effects
def emitter(node_name="Particles", at=(0, 0), **kw):
    n = Node(node_name, at)
    n.add("ParticleEmitter", **kw)
    return n


def glow(color, radius, intensity=0.6, flicker=0.0, speed=6, at=(0, 0), name="Glow"):
    n = Node(name, at)
    n.add("Light2D", color=color, intensity=intensity, radius=radius, flicker=flicker, flickerSpeed=speed,
          layer=GLOW)
    return n


def effect(name, **kw):
    """A one-shot particle effect that removes itself when its particles are gone."""
    n = Node(name)
    n.add("ParticleEmitter", loop=False, duration=0.05, rate=0, layer=PARTICLES, **kw)
    n.add("Lifetime", seconds=3.0, untilEmitterFinished=True)
    return n


def build_fx():
    out = {}
    out["dust"] = effect("Dust", burst=9, lifetime=[0.25, 0.5], area="Box", areaSize=[0.5, 0.05], direction=0,
                         spread=170, speed=[0.5, 1.5], gravity=[0, -1.2], drag=2.0, startSize=[0.13, 0.24],
                         endScale=1.7, startColor=rgba(215, 200, 170, 150), endColor=rgba(215, 200, 170, 0),
                         particleShape="Soft", blend="Alpha")
    for name, hot, cool in (("spark_red", rgba(255, 130, 150), rgba(255, 60, 90, 0)),
                            ("spark_blue", rgba(140, 210, 255), rgba(60, 130, 255, 0)),
                            ("spark_gold", rgba(255, 230, 140), rgba(255, 170, 60, 0))):
        out[name] = effect(name.replace("_", " ").title(), burst=14, lifetime=[0.4, 0.8], speed=[1.0, 3.0],
                           drag=2.5, startSize=[0.07, 0.15], endScale=0.0, startColor=hot, endColor=cool)
    out["death_ember"] = effect("Death Ember", burst=40, lifetime=[0.5, 1.2], speed=[1.5, 5.0], gravity=[0, -2.0],
                                drag=1.2, startSize=[0.12, 0.32], endScale=0.2,
                                startColor=rgba(255, 190, 80, 255), endColor=rgba(255, 60, 20, 0))
    out["death_tide"] = effect("Death Tide", burst=40, lifetime=[0.5, 1.2], speed=[1.5, 5.0], gravity=[0, 7.0],
                               drag=0.6, startSize=[0.10, 0.26], endScale=0.3,
                               startColor=rgba(170, 225, 255, 255), endColor=rgba(60, 130, 255, 0))
    out["respawn"] = effect("Respawn", burst=26, lifetime=[0.5, 1.0], area="Circle", areaSize=[0.8, 0.8],
                            speed=[0.5, 2.2], gravity=[0, -1.5], drag=1.0, startSize=[0.08, 0.2], endScale=0.0,
                            startColor=rgba(255, 245, 200, 255), endColor=rgba(255, 220, 120, 0))
    return {"fx/" + k: v for k, v in out.items()}


# --------------------------------------------------------------------------------------- terrain
def build_terrain():
    out = {}
    platform = Node("Platform")
    platform.add("SpriteRenderer", texture="assets/tiles/platform.png", size=[4, 2], drawMode="Sliced",
                 sliceFill="Tile", layer=TERRAIN)
    # The moss lip starts a little below the sprite's top edge, so the collider does too.
    platform.add("Collider", size=[4, 1.94], offset=[0, 0.03], layer="Solid", friction=0.8)
    out["terrain/platform"] = platform

    for key, title, texture in (("brick_block", "Brick Block", "brick"), ("wall", "Wall", "wall")):
        block = Node(title)
        block.add("SpriteRenderer", texture="assets/tiles/%s.png" % texture, size=[2, 2], drawMode="Tiled",
                  layer=TERRAIN)
        block.add("Collider", size=[2, 2], layer="Solid", friction=0.4)
        out["terrain/" + key] = block

    ledge = Node("Ledge")
    ledge.add("SpriteRenderer", texture="assets/tiles/ledge.png", size=[3, 0.5], drawMode="Sliced",
              sliceFill="Tile", layer=TERRAIN)
    ledge.add("Collider", size=[3, 0.3], offset=[0, -0.05], oneWay=True, layer="Solid", friction=0.8)
    out["terrain/ledge"] = ledge

    ramp = Node("Ramp")
    ramp.add("SpriteRenderer", texture="assets/tiles/ramp.png", size=[2.4, 1.2], layer=TERRAIN)
    ramp.add("Collider", shape="Wedge", size=[2.4, 1.2], layer="Solid", friction=0.5)
    out["terrain/ramp"] = ramp
    return out


# --------------------------------------------------------------------------------------- hazards
def pool(title, key, tags, glow_color, bubble_start, bubble_end, glow_radius, flicker):
    n = Node(title)
    n.add("SpriteRenderer", texture="assets/hazards/%s.png" % key, size=[2, 1], drawMode="Tiled",
          tileSize=[2, 1], columns=4, rows=1, layer=LIQUID)
    n.add("AnimatedSprite", animation="assets/hazards/%s.ykanim" % key, clip="flow", flipParameter="")
    # Only the part below the surface hurts; something standing on a platform flush with the
    # surface, or leaping across, is clear of it.
    n.add("Collider", size=[1.9, 0.6], offset=[0, 0.2], isTrigger=True, layer="Sensor")
    n.add("Hazard", affectsTags=tags)
    n.child(glow(glow_color, glow_radius, 0.55, flicker, 5, (0, -0.15)))
    n.child(emitter("Bubbles", (0, -0.35), rate=2.4, lifetime=[0.5, 1.1], area="Box", areaSize=[1.7, 0.1],
                    direction=0, spread=30, speed=[0.4, 1.1], gravity=[0, -0.4], startSize=[0.06, 0.17],
                    endScale=0.2, startColor=bubble_start, endColor=bubble_end, layer=PARTICLES))
    return n


def build_hazards():
    return {
        "hazards/lava": pool("Lava", "lava", ["Tide"], rgba(255, 140, 40), rgba(255, 215, 130, 230),
                             rgba(255, 110, 30, 0), 2.8, 0.35),
        "hazards/water": pool("Water", "water", ["Ember"], rgba(90, 170, 255), rgba(200, 240, 255, 200),
                              rgba(120, 190, 255, 0), 2.4, 0.1),
        "hazards/goo": pool("Goo", "goo", [], rgba(150, 240, 90), rgba(230, 255, 170, 220),
                            rgba(150, 230, 90, 0), 2.4, 0.2),
    }


# ----------------------------------------------------------------------------------------- items
def gem(title, key, tag, variable, spark, glow_color):
    n = Node(title, tags=["gem"])
    n.add("SpriteRenderer", texture="assets/items/%s.png" % key, size=[0.7, 0.7], columns=4, rows=1, layer=ITEM)
    n.add("AnimatedSprite", animation="assets/items/%s.ykanim" % key, clip="shine", flipParameter="")
    n.add("Collider", shape="Circle", size=[0.55, 0.55], isTrigger=True, layer="Sensor")
    n.add("Collectible", collectorTags=[tag], variable=variable, value=1, sound=sound("collect"),
          collectEffect="prefabs/fx/%s.ykprefab" % spark)
    n.add("Oscillator", position=[0, 0.1], frequency=0.8)
    n.child(glow(glow_color, 1.3, 0.45, 0.0, 6, (0, 0)))
    return n


def build_items():
    return {
        "items/gem_red": gem("Red Gem", "gem_red", "Ember", "ember_gems", "spark_red", rgba(255, 90, 120)),
        "items/gem_blue": gem("Blue Gem", "gem_blue", "Tide", "tide_gems", "spark_blue", rgba(100, 180, 255)),
    }


# ------------------------------------------------------------------------------------ characters
def character(title, tag, key, action_set, death, aura, trail):
    n = Node(title, tags=[tag])
    # The 96 px cell is 1.5 units; the feet are 7 px above its bottom, and the capsule's bottom
    # edge is 0.55 below the entity's origin, so the art moves up 0.09 to stand on it.
    n.add("SpriteRenderer", texture="assets/characters/%s.png" % key, size=[1.5, 1.5], offset=[0, -0.09],
          columns=8, rows=4, layer=CHARACTER)
    n.add("AnimatedSprite", animation="assets/characters/%s.ykanim" % key,
          controller="assets/characters/character.ykctl")
    n.add("RigidBody", type="Dynamic", fixedRotation=True, allowSleep=False)
    n.add("Collider", shape="Capsule", size=[0.6, 1.15], layer="Player", friction=0)
    n.add("PlayerInput", actionSet=action_set)
    n.add("PlatformerController", jumpSound=sound("jump"), landSound=sound("land"),
          jumpEffect="prefabs/fx/dust.ykprefab", landEffect="prefabs/fx/dust.ykprefab")
    n.add("Killable", respawnDelay=1.4, deathDuration=0.9, deathSound=sound("death"),
          respawnSound=sound("respawn"), deathEffect="prefabs/fx/%s.ykprefab" % death,
          respawnEffect="prefabs/fx/respawn.ykprefab")
    n.child(glow(aura, 2.2, 0.35, 0.15, 4, (0, -0.1), "Aura"))
    n.child(emitter("Trail", (0, 0.1), **trail))
    return n


def build_characters():
    ember_trail = dict(rate=14, lifetime=[0.4, 0.8], area="Box", areaSize=[0.4, 0.5], direction=0, spread=40,
                       speed=[0.4, 1.0], gravity=[0, -1.5], startSize=[0.08, 0.18], endScale=0.1,
                       startColor=rgba(255, 175, 70, 220), endColor=rgba(255, 60, 20, 0), layer=PARTICLES)
    tide_trail = dict(rate=10, lifetime=[0.5, 0.9], area="Box", areaSize=[0.4, 0.5], direction=0, spread=60,
                      speed=[0.2, 0.7], gravity=[0, 3.0], startSize=[0.06, 0.12], endScale=0.2,
                      startColor=rgba(160, 225, 255, 210), endColor=rgba(80, 160, 255, 0), layer=PARTICLES)
    return {
        "characters/ember": character("Ember", "Ember", "ember", "Player1", "death_ember",
                                      rgba(255, 140, 60), ember_trail),
        "characters/tide": character("Tide", "Tide", "tide", "Player2", "death_tide",
                                     rgba(90, 180, 255), tide_trail),
    }


# ---------------------------------------------------------------------------------- mechanisms
def build_mechanisms():
    out = {}
    plate = Node("Plate")
    plate.add("SpriteRenderer", texture="assets/mechanisms/plate.png", size=[2, 0.5], columns=3, rows=1,
              layer=MECHANISM)
    plate.add("AnimatedSprite", animation="assets/mechanisms/plate.ykanim",
              controller="assets/mechanisms/plate.ykctl", flipParameter="")
    plate.add("Collider", size=[1.7, 0.3], offset=[0, 0.1], isTrigger=True, layer="Sensor")
    plate.add("PressurePlate", pressSound=sound("plate_down"), releaseSound=sound("plate_up"))
    out["mechanisms/plate"] = plate

    lever = Node("Lever")
    lever.add("SpriteRenderer", texture="assets/mechanisms/lever.png", size=[1, 1.5], columns=3, rows=1,
              layer=MECHANISM)
    lever.add("AnimatedSprite", animation="assets/mechanisms/lever.ykanim",
              controller="assets/mechanisms/lever.ykctl", flipParameter="")
    lever.add("Collider", size=[0.9, 1.2], offset=[0, 0.15], isTrigger=True, layer="Sensor")
    lever.add("Lever", interactAction="Interact", cooldown=0.4, sound=sound("lever"))
    out["mechanisms/lever"] = lever

    gate = Node("Gate")
    gate.add("SpriteRenderer", texture="assets/mechanisms/gate.png", size=[1, 3], columns=2, rows=1, layer=GATE)
    gate.add("AnimatedSprite", animation="assets/mechanisms/gate.ykanim",
             controller="assets/mechanisms/gate.ykctl", flipParameter="")
    gate.add("RigidBody", type="Kinematic")
    gate.add("Collider", size=[0.9, 3], layer="Solid", friction=0.4)
    gate.add("Door", openOffset=[0, -3.2], speed=4, openSound=sound("gate_open"), closeSound=sound("gate_close"))
    out["mechanisms/gate"] = gate

    shuttle = Node("Shuttle")
    shuttle.add("SpriteRenderer", texture="assets/mechanisms/elevator.png", size=[3, 0.625], layer=MECHANISM)
    shuttle.add("RigidBody", type="Kinematic")
    # The platform's top plate is 0.23 above the entity's origin. The body is thin so that something
    # walking underneath (through a pool, say) clears it.
    shuttle.add("Collider", size=[3, 0.4], offset=[0, -0.03], layer="Solid", friction=1.0)
    shuttle.add("MovingPlatform", travel=[3, 0], speed=2, pause=0.8)
    out["mechanisms/shuttle"] = shuttle

    crate = Node("Crate", tags=["crate"])
    crate.add("SpriteRenderer", texture="assets/mechanisms/crate.png", size=[1, 1], layer=PROP)
    crate.add("RigidBody", type="Dynamic", fixedRotation=True)
    crate.add("Collider", size=[0.96, 0.96], layer="Prop", friction=0.9, density=0.8)
    out["mechanisms/crate"] = crate

    totem = Node("Checkpoint")
    totem.add("SpriteRenderer", texture="assets/mechanisms/checkpoint.png", size=[1, 1.5], columns=2, rows=1,
              layer=MECHANISM)
    totem.add("AnimatedSprite", animation="assets/mechanisms/checkpoint.ykanim",
              controller="assets/mechanisms/checkpoint.ykctl", flipParameter="")
    totem.add("Collider", size=[0.9, 1.3], offset=[0, 0.1], isTrigger=True, layer="Sensor")
    totem.add("Checkpoint", respawnOffset=[0, -0.3], sound=sound("checkpoint"))
    out["mechanisms/checkpoint"] = totem

    for key, tag, title, color, spark_a, spark_b in (
            ("exit_ember", "Ember", "Ember Exit", rgba(255, 150, 60), rgba(255, 220, 120, 230), rgba(255, 120, 30, 0)),
            ("exit_tide", "Tide", "Tide Exit", rgba(100, 190, 255), rgba(210, 245, 255, 230), rgba(90, 160, 255, 0))):
        door = Node(title)
        door.add("SpriteRenderer", texture="assets/mechanisms/%s.png" % key, size=[2, 2.5], columns=4, rows=2,
                 layer=MECHANISM)
        door.add("AnimatedSprite", animation="assets/mechanisms/%s.ykanim" % key,
                 controller="assets/mechanisms/%s.ykctl" % key, flipParameter="")
        door.add("Collider", size=[1.1, 1.9], offset=[0, 0.3], isTrigger=True, layer="Sensor")
        door.add("Goal", requiredTag=tag, sound=sound("exit"))
        door.child(glow(color, 2.6, 0.55, 0.15, 3, (0, 0.1)))
        door.child(emitter("Motes", (0, 0.8), rate=6, lifetime=[0.8, 1.5], area="Box", areaSize=[0.9, 0.2],
                           direction=0, spread=20, speed=[0.4, 0.9], gravity=[0, -0.3], startSize=[0.05, 0.12],
                           endScale=0.0, startColor=spark_a, endColor=spark_b, layer=PARTICLES))
        out["mechanisms/" + key] = door

    spawn = Node("Spawn Point")
    spawn.add("SpawnPoint")
    out["level/spawn_point"] = spawn
    return out


# ---------------------------------------------------------------------------------------- decor
def plant(title, texture, cell, columns, offset_y, layer, sway, frequency, glow_spec=None):
    n = Node(title)
    n.add("SpriteRenderer", texture="assets/decor/%s.png" % texture, size=list(cell), offset=[0, offset_y],
          columns=columns, rows=1, layer=layer)
    if sway:
        n.add("Oscillator", rotation=sway, frequency=frequency)
    if glow_spec:
        n.child(glow(*glow_spec[:2], **glow_spec[2]))
    return n


def build_decor():
    out = {}
    out["decor/grass"] = plant("Grass", "grass", (1, 0.625), 3, -0.31, DECOR_FRONT, 2.2, 0.5)
    out["decor/vines"] = plant("Vines", "vines", (1, 2.5), 2, 1.25, DECOR_BACK, 2.0, 0.3)
    out["decor/fern"] = plant("Fern", "fern", (1.5, 1), 1, -0.47, DECOR_FRONT, 1.5, 0.4)
    out["decor/mushrooms"] = plant("Mushrooms", "mushrooms", (1, 0.75), 2, -0.34, DECOR_FRONT, 0, 0,
                                   (rgba(255, 150, 200), 1.6, dict(intensity=0.35, at=(0, -0.3))))
    out["decor/crystals"] = plant("Crystals", "crystals", (1, 1), 2, -0.44, DECOR_FRONT, 0, 0,
                                  (rgba(110, 220, 240), 2.2, dict(intensity=0.5, at=(0, -0.45))))
    torch = Node("Torch")
    torch.add("SpriteRenderer", texture="assets/decor/torch.png", size=[0.5, 1.125], layer=DECOR_BACK + 5)
    torch.child(glow(rgba(255, 170, 80), 3.4, 0.8, 0.5, 9, (0, -0.3)))
    torch.child(emitter("Flame", (0, -0.28), rate=26, lifetime=[0.3, 0.6], area="Circle", areaSize=[0.18, 0.1],
                        direction=0, spread=25, speed=[0.5, 1.1], gravity=[0, -1.2], startSize=[0.14, 0.28],
                        endScale=0.1, startColor=rgba(255, 205, 90, 235), endColor=rgba(255, 80, 10, 0),
                        layer=PARTICLES))
    out["decor/torch"] = torch
    return out


# ------------------------------------------------------------------------------------ level parts
def build_level_parts():
    out = {}
    backdrop = Node("Backdrop", locked=True)  # Too big to be picked by accident.
    far = Node("Far Wall")
    far.add("SpriteRenderer", texture="assets/backgrounds/bg_far.png", size=[72, 40.5], layer=BACKGROUND_FAR,
            parallax=[0.15, 0.15])
    mid = Node("Columns")
    mid.add("SpriteRenderer", texture="assets/backgrounds/bg_mid.png", size=[96, 22], drawMode="Tiled",
            tileSize=[16, 22], layer=BACKGROUND_MID, parallax=[0.45, 0.45])
    backdrop.child(far)
    backdrop.child(mid)
    out["level/backdrop"] = backdrop

    hud = Node("HUD")
    hud.child(Node("Vignette")).add("UiImage", texture="assets/backgrounds/vignette.png", fillScreen=True,
                                    layer=-5)
    for side, tag, anchor, icon, color, variable in (
            ("Ember", "EMBER", "TopLeft", "gem_red", rgba(255, 176, 110), "ember_gems"),
            ("Tide", "TIDE", "TopRight", "gem_blue", rgba(140, 205, 255), "tide_gems")):
        panel = Node(side + " Score")
        panel.add("UiPanel", anchor=anchor, size=[236, 52], offset=[16, 16], color=rgba(6, 14, 18, 170), layer=1)
        hud.child(panel)
        icon_node = Node(side + " Gem Icon")
        icon_node.add("UiImage", texture="assets/items/%s.png" % icon, columns=4, rows=1, frame=0, anchor=anchor,
                      size=[40, 40], offset=[24, 22], layer=2)
        hud.child(icon_node)
        label = Node(side + " Gems")
        label.add("UiText", text="%s  {%s:0}/{%s_total:0}" % (tag, variable, variable), anchor=anchor,
                  offset=[74, 32], scale=2, color=color, layer=3)
        hud.child(label)
    clock = Node("Timer")
    clock.add("UiText", text="TIME {level_time}", anchor="Top", offset=[0, 30], scale=2,
              color=rgba(235, 240, 235), layer=3)
    hud.child(clock)
    message = Node("Message")
    message.add("UiText", text="{level_message}", anchor="Center", offset=[0, 0], scale=6,
                color=rgba(255, 240, 170), layer=6)
    hud.child(message)
    help_text = Node("Controls")
    help_text.add("UiText", text="EMBER: A D MOVE  W JUMP  S USE      TIDE: ARROWS MOVE  UP JUMP  DOWN USE      R RESTART",
                  anchor="Bottom", offset=[0, 16], scale=1, color=rgba(190, 205, 205, 200), layer=3)
    hud.child(help_text)
    out["level/hud"] = hud
    return out


def main():
    prefabs = {}
    for build in (build_fx, build_terrain, build_hazards, build_items, build_characters, build_mechanisms,
                  build_decor, build_level_parts):
        prefabs.update(build())
    for key, root in sorted(prefabs.items()):
        write_prefab(PROJECT, "prefabs/%s.ykprefab" % key, root)
    print("wrote %d prefabs" % len(prefabs))


if __name__ == "__main__":
    main()
