#!/usr/bin/env python3
"""Export detailed ImageGen masonry masters at four times the castle's native density.

The hand-painted source images and exact prompts live in assets/castle/source/masonry-hq.
This exporter only fits nine-slice regions and conditions repeating texture edges; it does
not draw replacement stone. Run directly, or call export_masonry_materials() when authoring.
"""
import json
from pathlib import Path

from PIL import Image, PngImagePlugin

ROOT = Path(__file__).resolve().parents[1]
MASTERS = ROOT / 'assets/castle/source/masonry-hq'
MATERIALS = ROOT / 'assets/castle/materials'
PIXELS_PER_UNIT = 140

# Source boundaries follow the finished cap, returns, face and plinth in each master.
# Target boundaries preserve the physical proportions of the original 35 PPU materials.
MATERIAL_DEFINITIONS = {
    'wall-run': dict(source_x=(0, 171, 827, 996), source_y=(0, 320, 1464, 1579),
                     target_x=(0, 36, 176, 212), target_y=(0, 104, 320, 336),
                     border=[36, 104, 36, 16],
                     # Remove the master's narrow recess above the coping center.
                     regions={(0, 1): (171, 31, 827, 320)}),
    'wall-column': dict(source_x=(0, 176, 944, 1122), source_y=(0, 64, 1342, 1402),
                        target_x=(0, 16, 96, 112), target_y=(0, 12, 128, 140),
                        border=[16, 12, 16, 12]),
    'wall-column-end': dict(source_x=(0, 64, 731, 793), source_y=(0, 68, 925, 1983),
                            target_x=(0, 16, 96, 112), target_y=(0, 12, 140, 280),
                            border=[16, 12, 16, 140]),
}


def periodic_edges(image, axis, feather=8):
    """Match opposing seam pixels with a short, smooth blend into the painted texture."""
    result = image.copy()
    pixels = result.load()
    original = image.load()
    along = image.width if axis == 0 else image.height
    across = image.height if axis == 0 else image.width
    feather = min(feather, along // 2)
    for distance in range(feather):
        weight = (1 - distance / max(1, feather - 1)) ** 2
        for coordinate in range(across):
            first = (distance, coordinate) if axis == 0 else (coordinate, distance)
            last = (along - 1 - distance, coordinate) if axis == 0 else (coordinate, along - 1 - distance)
            a, b = original[first], original[last]
            average = tuple((x + y) / 2 for x, y in zip(a, b))
            pixels[first] = tuple(round(x * (1 - weight) + mean * weight) for x, mean in zip(a, average))
            pixels[last] = tuple(round(x * (1 - weight) + mean * weight) for x, mean in zip(b, average))
    return result


def export_material(name, definition):
    source = Image.open(MASTERS / (name + '-master.png')).convert('RGB')
    sx, sy = definition['source_x'], definition['source_y']
    tx, ty = definition['target_x'], definition['target_y']
    if source.size != (sx[-1], sy[-1]):
        raise ValueError(f'{name}: master size changed; review its nine-slice boundaries')
    output = Image.new('RGB', (tx[-1], ty[-1]))
    for row in range(3):
        for column in range(3):
            bounds = definition.get('regions', {}).get((row, column),
                         (sx[column], sy[row], sx[column + 1], sy[row + 1]))
            region = source.crop(bounds)
            region = region.resize((tx[column + 1] - tx[column], ty[row + 1] - ty[row]),
                                   Image.Resampling.LANCZOS)
            if column == 1:
                region = periodic_edges(region, 0)
            if row == 1:
                region = periodic_edges(region, 1)
            output.paste(region, (tx[column], ty[row]))
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('Source', 'Built-in ImageGen; assets/castle/source/masonry-hq/' + name + '-master.png')
    metadata.add_text('Prompt', (MASTERS / (name + '.prompt.txt')).read_text())
    metadata.add_text('Export', 'tools/castle_masonry_art.py; independently fitted nine-slices with matched repeating edges')
    destination = MATERIALS / ('hq-' + name + '.png')
    output.save(destination, pnginfo=metadata)
    Path(str(destination) + '.ykmeta').write_text(json.dumps(dict(
        format='yk.texture', version=1, pixelsPerUnit=PIXELS_PER_UNIT,
        filter='linear', border=definition['border']), indent=2) + '\n')
    return destination


def export_masonry_materials():
    MATERIALS.mkdir(parents=True, exist_ok=True)
    return [export_material(name, definition) for name, definition in MATERIAL_DEFINITIONS.items()]


if __name__ == '__main__':
    for path in export_masonry_materials():
        print('Exported', path)
