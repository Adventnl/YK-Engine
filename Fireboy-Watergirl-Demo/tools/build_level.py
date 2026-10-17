"""Reimport the original pixel art and character poses, then rebuild both levels."""
import json
import shutil
from pathlib import Path

from PIL import Image

from authoring import write_json

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "YK-DemoGame"
PROJECT = ROOT / "Fireboy-Watergirl-Demo"
SOURCE = ROOT / "game_assets_level1" / "game_assets_level1"
POSES = ROOT / "character_sprites" / "character_sprites"
OUT = PROJECT / "assets" / "reference"
SCALE = 50.0


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
    import_art()
    import_characters()
    from build_game import build as build_game
    build_game()


if __name__ == "__main__":
    build()
