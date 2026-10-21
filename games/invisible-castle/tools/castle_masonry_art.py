"""Package the original native-pixel masonry at the same 35 PPU as the actors.

The editable cap, brick face and returns are assembled by castle_geometry. The old
4x painted exports introduced subpixel detail and noise; native nine-slices keep
stone joints and pixel sizes consistent in both directions and both chapters.
"""
from pathlib import Path
from PIL import Image
from castle_pixel_art import write_pixel

ROOT = Path(__file__).resolve().parents[1]
MATERIALS = ROOT / 'assets/castle/materials'
BORDERS = {'wall-run': [9, 26, 9, 4], 'wall-column': [4, 3, 4, 3],
           'wall-column-end': [4, 3, 4, 35]}


def export_masonry_materials():
    paths = []
    for name, border in BORDERS.items():
        path = MATERIALS / ('hq-' + name + '.png')
        with Image.open(MATERIALS / (name + '.png')) as image:
            write_pixel(image.convert('RGBA'), path, border=border)
        paths.append(path)
    return paths


if __name__ == '__main__':
    for path in export_masonry_materials():
        print('Exported', path)
