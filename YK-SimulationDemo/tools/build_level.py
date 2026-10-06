#!/usr/bin/env python3
"""Build the small, data-only Night Shift escape scene."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
entities = []
next_id = 1


def add(name, x, y, *components, active=True, tags=None):
    global next_id
    entity_id = f"{next_id:016x}"
    next_id += 1
    entity = {
        "id": entity_id,
        "name": name,
        "transform": {"position": [x, y], "rotation": 0, "scale": [1, 1]},
        "components": list(components),
    }
    if not active:
        entity["active"] = False
    if tags:
        entity["tags"] = tags
    entities.append(entity)
    return entity_id


def component(kind, **properties):
    return {"type": kind, "properties": properties}


def sprite(w, h, color, layer=0, shape="Rectangle", y_sort=False, offset=(0, 0)):
    return component("SpriteRenderer", shape=shape, size=[w, h], offset=list(offset),
                     color=color, layer=layer, ySort=y_sort)


def collider(w, h, layer="Solid", trigger=False, offset=(0, 0)):
    return component("Collider", shape="Box", size=[w, h], offset=list(offset),
                     isTrigger=trigger, layer=layer)


def rect(name, x, y, w, h, color, layer=0, solid=False):
    parts = [sprite(w, h, color, layer)]
    if solid:
        parts.append(collider(w, h))
    return add(name, x, y, *parts)


def text(name, message, anchor, x, y, scale, color, layer=20, active=True):
    return add(name, 0, 0, component("UiText", text=message, anchor=anchor,
                                    offset=[x, y], scale=scale, color=color,
                                    shadow=True, layer=layer), active=active)


def object_with_action(name, x, y, w, h, color, prompt, flag, *, required="",
                       once=True, hide=False, extra=()):
    return add(name, x, y, sprite(w, h, color, 8, y_sort=True),
               component("Interactable", prompt=prompt, range=1.45,
                         requiredFlag=required, setFlag=flag, once=once,
                         hideWhenUsed=hide), *extra)


# The map is 28 x 18 world units. Each locked crossing spans the entire opening in its wall.
rect("Asphalt foundation", 14, 9, 28, 18, "#101e2aff", -30)
rect("Cell floor", 4.7, 9, 8.8, 17, "#253747ff", -20)
rect("Service floor", 15, 9, 11.4, 17, "#293b44ff", -20)
rect("Outside road", 24.5, 9, 6.1, 17, "#273341ff", -20)
rect("Cell route", 5.1, 9, 7.4, 2.4, "#324b55ff", -15)
rect("Service route", 15, 9, 11.4, 2.4, "#3b5154ff", -15)
rect("Exit route", 24.5, 9, 6.1, 2.4, "#44504bff", -15)
for x in (2.2, 4.6, 7.0, 11.2, 13.6, 16.0, 18.4, 23.2, 25.6):
    rect(f"Floor seam {x}", x, 9, 0.055, 2.4, "#62737b55", -14)
for y in (3.0, 5.0, 13.0, 15.0):
    rect(f"Cell floor joint {y}", 4.7, y, 8.8, 0.055, "#54667566", -14)
    rect(f"Service floor joint {y}", 15.0, y, 11.4, 0.055, "#61747755", -14)

player = add(
    "Runner", 3, 9,
    sprite(0.76, 1.13, "#f4bd68ff", 10, y_sort=True, offset=(0, -0.26)),
    component("RigidBody", type="Dynamic", gravityScale=0, linearDamping=8,
              fixedRotation=True, allowSleep=False),
    collider(0.54, 0.46, "Player", offset=(0, 0.26)),
    component("PlayerInput", actionSet="Player1"),
    component("TopDownController", speed=4.4, acceleration=38),
    component("Interactor", action="Interact"), tags=["player"])
add("Camera", 14, 9, component("Camera", primary=True, mode="Fixed",
                               orthographicHeight=18.8, targets=[player],
                               clampToBounds=False))

# A short intro gives the player the objective before the clock starts.
exit_goal = add("Escape zone", 25.9, 9, sprite(1.75, 2.3, "#4ead85cc", 3),
                collider(1.65, 2.1, "Sensor", trigger=True),
                component("Goal", requiredTag="player", enterOnComplete=True,
                          exitDuration=0.75, satisfiedColor="#98f3b4ff"))
add("Escape rules", 0, 0, component("LevelFlow", goals=[exit_goal],
                                    introDuration=1.4,
                                    introMessage="NIGHT SHIFT  /  FIND A WAY OUT",
                                    completeDelay=999, completeMessage="ESCAPED!",
                                    restartSet="Global", restartAction="Restart",
                                    lockInputOnComplete=True))

# Outer boundary and two continuous room barriers. The only traversable gaps hold gates.
for name, x, y, w, h in [
    ("North wall", 14, 0.3, 28, 0.6),
    ("South wall", 14, 17.7, 28, 0.6),
    ("West wall", 0.3, 9, 0.6, 18),
    ("East wall", 27.7, 9, 0.6, 18),
    ("Cell wall north", 9, 3.95, 0.7, 7.9),
    ("Cell wall south", 9, 14.05, 0.7, 7.9),
    ("Yard wall north", 21, 3.95, 0.7, 7.9),
    ("Yard wall south", 21, 14.05, 0.7, 7.9),
]:
    rect(name, x, y, w, h, "#708996ff", 6, solid=True)

# Beds and storage make the cell feel inhabited without obstructing the central corridor.
rect("Bunk frame", 2.5, 3.2, 2.4, 1.15, "#152733ff", 1)
rect("Bunk mattress", 2.5, 3.15, 2.1, 0.9, "#647981ff", 2)
rect("Bunk pillow", 1.72, 3.15, 0.42, 0.72, "#ccd5c7ff", 3)
rect("Desk", 5.35, 5.55, 1.65, 0.95, "#6d5849ff", 4, solid=True)
rect("Desk paper", 5.15, 5.51, 0.55, 0.35, "#d3d0aeff", 5)
rect("Locker", 6.3, 12.5, 1.2, 1.55, "#566d79ff", 4, solid=True)
rect("Locker handle", 6.65, 12.45, 0.09, 0.42, "#f3c77cff", 5)
rect("Cell light", 4.4, 1.5, 3.7, 0.24, "#d4cb95aa", 2)

object_with_action("Keycard", 6.4, 11.75, 0.58, 0.38, "#f0d67aff",
                   "E  Take guard keycard", "keycard", hide=True)
object_with_action("Wire", 4.65, 6.4, 0.58, 0.42, "#ed9c65ff",
                   "E  Take loose wire", "wire", hide=True)

def gate(name, x, flag, kept, prompt, color):
    return add(name, x, 9, sprite(0.7, 2.25, color, 8),
               collider(0.7, 2.25),
               component("StateGate", startsOpen=False, startsLocked=True,
                         unlockFlag=flag, persistFlag=kept, animationSeconds=0.32),
               component("Interactable", prompt=prompt, range=1.6))

cell_gate = gate("Cell gate", 9, "keycard", "cell_gate_open",
                 "E  Cell gate - keycard required", "#e4a75cff")
rect("Cell gate frame left", 8.65, 7.95, 0.24, 0.45, "#b6c5c2ff", 9)
rect("Cell gate frame right", 8.65, 10.05, 0.24, 0.45, "#b6c5c2ff", 9)

# The player must bring the wire to this panel before the yard gate can open.
rect("Maintenance bench", 14.4, 4.8, 2.8, 0.9, "#485d63ff", 3, solid=True)
object_with_action("Control panel", 14.3, 5.75, 0.95, 0.72, "#67cad0ff",
                   "E  Repair control panel", "power", required="wire")
rect("Warning stripe one", 14.4, 5.35, 2.3, 0.11, "#e8b857ff", 5)
rect("Crate A", 11.6, 14.5, 1.1, 1.1, "#746b56ff", 4, solid=True)
rect("Crate B", 18.3, 13.6, 1.3, 1.3, "#746b56ff", 4, solid=True)
rect("Supply drum", 17.8, 4.5, 1.0, 1.0, "#526c77ff", 4, solid=True)
rect("Service lamp", 17, 1.6, 4.4, 0.22, "#b9d4b6aa", 2)

yard_gate = gate("Yard gate", 21, "power", "yard_gate_open",
                 "E  Outer gate - restore power", "#f0bd63ff")
for y in (3.2, 5.4, 12.6, 14.8):
    rect(f"Fence rail {y}", 24.4, y, 5.8, 0.08, "#839f9cff", 4)
for x in (22.0, 23.4, 24.8, 26.2):
    rect(f"Fence post {x}", x, 3.2, 0.09, 4.1, "#829e9aff", 5)
    rect(f"South fence post {x}", x, 14.8, 0.09, 4.1, "#829e9aff", 5)
rect("Exit glow", 25.9, 9, 2.2, 2.75, "#76cfa955", 2)

# Compact objective and control HUD. Numeric flags are intentional so progress is unambiguous.
add("HUD panel", 0, 0, component("UiPanel", anchor="TopLeft", size=[456, 141],
                                  offset=[18, 18], color="#08151fe0", layer=10))
text("Title", "NIGHT SHIFT  /  ESCAPE", "TopLeft", 32, 28, 2.3, "#ffcf82ff")
text("Step one", "01  KEYCARD {keycard:0}   WIRE {wire:0}", "TopLeft", 32, 62,
     1.7, "#e1e9e3ff")
text("Step two", "02  REPAIR PANEL {power:0}", "TopLeft", 32, 88,
     1.7, "#e1e9e3ff")
text("Step three", "03  OPEN GATE  >  GREEN EXIT", "TopLeft", 32, 114,
     1.7, "#b5e4c5ff")
add("Controls panel", 0, 0, component("UiPanel", anchor="BottomLeft", size=[455, 39],
                                       offset=[18, 17], color="#08151fc9", layer=10))
text("Controls", "WASD / ARROWS   MOVE     E   USE     R   RESTART",
     "BottomLeft", 29, 27, 1.35, "#d2dbd8ff")
text("Interaction prompt", "{interaction_prompt}", "Bottom", 0, 31, 2.3,
     "#ffe2a1ff")
text("Intro and result", "{level_message}", "Center", 0, 0, 3.2,
     "#fff0bdff", layer=40)

scene = {
    "format": "yk.scene", "version": 1,
    "settings": {"name": "Night Shift", "gravity": [0, 0],
                 "background": "#101e2aff"},
    "entities": entities,
}
(ROOT / "scenes" / "night_shift.ykscene").write_text(
    json.dumps(scene, indent=2) + "\n", encoding="utf-8")
print(f"Wrote {len(entities)} entities; player {player}, gates {cell_gate}/{yard_gate}, exit {exit_goal}")
