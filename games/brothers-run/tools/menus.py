"""Screen-space front end: real clickable controls and miniature level maps."""
import json

from authoring import Node, SceneBuilder

INK = "#0c1926ff"
PANEL = "#172f40ff"
CREAM = "#fff0d4ff"
MUTED = "#a5bccbff"
FIRE = "#ffb16bff"
WATER = "#70dcffff"


def panel(scene, name, x, y, w, h, color=PANEL, layer=2):
    node = Node(name).add("UiPanel", anchor="Center", offset=[x, y],
                          size=[w, h], color=color, layer=layer)
    scene.add(node)
    return node


def label(scene, name, value, x, y, scale=2, color=CREAM, layer=8):
    scene.add(Node(name).add("UiText", text=value, anchor="Center", offset=[x, y],
                            scale=scale, color=color, layer=layer))


def image(scene, name, texture, x, y, w, h, layer=5, **extra):
    scene.add(Node(name).add("UiImage", texture="assets/reference/" + texture,
                            anchor="Center", offset=[x, y], size=[w, h], layer=layer, **extra))


def button(scene, name, title, x, y, w, h, destination, accent=WATER):
    # The accent edge and label are separate drawing elements; the whole card is clickable.
    node = Node(name).add("UiPanel", anchor="Center", offset=[x, y], size=[w, h],
                          color=PANEL, layer=2)
    node.add("UiButton", changeScene=destination, hoverColor="#2c5268ff",
             pressedColor="#36647aff")
    scene.add(node)
    panel(scene, name + " Accent", x - w / 2 + 2, y, 4, h, accent, 3)
    label(scene, name + " Label", title, x, y, 2.5)


def shortcut(scene, name, action, destination):
    scene.add(Node(name).add("EventAction", inputAction=action, waitForRelease=True,
                            once=True, changeScene=destination))


def miniature(scene, project, number, x, y):
    """Draw the actual authored solid layout, rather than an unrelated stock thumbnail."""
    background = ("assets/classroom/generated/background.png" if number == 2
                  else "assets/reference/room.png")
    scene.add(Node(f"Map {number} Room").add("UiImage", texture=background,
        anchor="Center", offset=[x, y], size=[414, 218], layer=4, color="#ffffff88"))
    data = json.loads((project / f"scenes/level0{number}.ykscene").read_text())
    factor = 414 / 1672
    for entity in data["entities"]:
        components = {c["type"]: c["properties"] for c in entity["components"]}
        collider = components.get("Collider", {})
        if entity["name"].endswith("Wall"):
            continue
        if collider.get("layer") != "Solid" or "PressurePlate" in components:
            continue
        if entity.get("parent") and entity["name"].endswith(" Cap"):
            continue
        px, py = entity["transform"]["position"]
        w, h = collider["size"]
        color = "#e7b064ff" if "MovingPlatform" in components else "#6f927fff"
        panel(scene, f"Map {number} {entity['name']}",
              x + (px * 50 - 836) * factor, y + (py * 50 - 470.5) * factor,
              w * 50 * factor, max(3, h * 50 * factor), color, 5)


def build_menus(project):
    for select in (False, True):
        scene = SceneBuilder(str(project), "Level Select" if select else "Main Menu",
                             background=INK)
        scene.add(Node("Backdrop").add("UiImage", texture="assets/reference/room.png",
                                       fillScreen=True, color="#4b6475ff", layer=-5))
        scene.add(Node("Scrim").add("UiImage", fillScreen=True, color="#07131de5", layer=-4))
        panel(scene, "Top Rule", 0, -303, 1040, 2, "#456273ff", -1)
        label(scene, "Series", "EMBER AND TIDE  /  TWO BROTHERS. ONE RACE.", 0, -278, 1.5, MUTED)
        if select:
            label(scene, "Heading", "CHOOSE YOUR CHALLENGE", 0, -218, 3.5)
            label(scene, "Intro", "TWO ROUTES. TWO PLAYERS. ONE WAY OUT TOGETHER.", 0, -170, 1.5, MUTED)
            for number, x, title, detail, accent in (
                    (1, -246, "01  THE CROSSINGS", "POOLS / SWITCHES / THE CLIMB", FIRE),
                    (2, 246, "02  THE CLASSROOM", "BOOKS / DESKS / LIFTS", WATER)):
                destination = f"scenes/level0{number}.ykscene"
                button(scene, f"Level {number} Card", "", x, 40, 460, 358, destination, accent)
                miniature(scene, project, number, x, -22)
                label(scene, f"Chapter {number}", title, x, 106, 2.5, accent)
                label(scene, f"Detail {number}", detail, x, 140, 1.5, MUTED)
                label(scene, f"Launch {number}", f"PLAY LEVEL {number}  >", x, 202, 2, accent)
                shortcut(scene, f"Key {number}", f"Level{number}", destination)
            button(scene, "Back Button", "<  BACK", 0, 259, 196, 42, "scenes/menu.ykscene")
            shortcut(scene, "Default", "Continue", "scenes/level01.ykscene")
            shortcut(scene, "Back", "Menu", "scenes/menu.ykscene")
        else:
            label(scene, "Fire Title", "BROTHERS", -238, -180, 5.5, FIRE)
            label(scene, "Water Title", "RUN", -238, -119, 4.5, WATER)
            label(scene, "Tagline", "TWO BROTHERS. TWO ROOMS. ONE RACE.", -238, -50, 1.5, MUTED)
            label(scene, "Tagline Two", "DODGE THE POOLS. BE FIRST TO THE EXIT.", -238, -24, 1.5, MUTED)
            button(scene, "Play Button", "START THE RACE  >", -238, 66, 430, 78,
                   "scenes/levels.ykscene", FIRE)
            label(scene, "Keyboard Hint", "CLICK TO BEGIN  /  ENTER", -238, 132, 1.5, MUTED)
            panel(scene, "Hero Stage", 267, 83, 382, 18, "#456273ff", 3)
            for color, x in (("orange", 166), ("blue", 356)):
                image(scene, color + " Hero", f"{color}_motion.png", x, -35, 144, 216,
                      columns=8, rows=4)
            image(scene, "Fire Gem", "png/collectibles/red_orb_left.png", 167, -200, 30, 32)
            image(scene, "Water Gem", "png/collectibles/blue_orb_left.png", 357, -176, 30, 32)
            label(scene, "Chapter Count", "2 CHAPTERS  /  LOCAL CO-OP", 267, 132, 1.5, MUTED)
            shortcut(scene, "Play", "Continue", "scenes/levels.ykscene")
            panel(scene, "Control Divider", 0, 200, 1040, 2, "#456273ff")
            label(scene, "Fire Controls", "EMBER", -260, 238, 2, FIRE)
            label(scene, "Fire Keys", "A D MOVE   W JUMP   S USE", -260, 270, 1.5, MUTED)
            label(scene, "Water Controls", "TIDE", 260, 238, 2, WATER)
            label(scene, "Water Keys", "ARROWS MOVE   UP JUMP   DOWN USE", 260, 270, 1.5, MUTED)
        label(scene, "Footer", "ONE EXIT  /  FIRST BROTHER TO REACH IT WINS THE RACE", 0, 320, 1.5, MUTED)
        scene.write("scenes/levels.ykscene" if select else "scenes/menu.ykscene")
