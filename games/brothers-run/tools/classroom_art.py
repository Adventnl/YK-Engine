"""Export generated classroom art at the game's native sprite resolution.

Generation originals and the exact built-in ImageGen prompts live in
source/classroom-generated. Exporting only crops transparent padding and uses
nearest-neighbour resizing; no high-resolution originals ship as textures.
"""
from pathlib import Path

from PIL import Image

from authoring import write_json

PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT / "source/classroom-generated"
OUTPUT = PROJECT / "assets/classroom/generated"


def prepare_classroom_art():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, size in (("desk", (384, 80)), ("bookcase", (96, 80)),
                       ("books", (192, 28)), ("background", (1672, 941))):
        image = Image.open(SOURCE / f"{name}.png").convert("RGBA")
        if name != "background":
            # Generated cutouts can contain nearly invisible alpha specks in
            # the padding. Ignore those when finding the solid sprite bounds.
            bounds = image.getchannel("A").point(lambda a: 255 if a > 16 else 0).getbbox()
            image = image.crop(bounds)
        image = image.resize(size, Image.Resampling.NEAREST)
        image.save(OUTPUT / f"{name}.png")
        write_json(str(OUTPUT / f"{name}.png") + ".ykmeta", {
            "format": "yk.texture", "version": 1,
            "pixelsPerUnit": 50, "filter": "nearest"})


if __name__ == "__main__":
    prepare_classroom_art()
