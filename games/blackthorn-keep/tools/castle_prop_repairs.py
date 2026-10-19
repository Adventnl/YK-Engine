"""Complete, high-detail prop sprites exported from transparent ImageGen masters.

The reference extraction is preserved verbatim.  These replacements repair objects
whose source silhouettes included neighboring furniture, floor pixels, or cropped
legs.  A scene rebuild always uses the repaired assets, rather than restoring the
old extraction.  Cropping and sizing here only package the generated artwork.
"""
import json
from pathlib import Path

from PIL import Image

from art_import import generated_objects

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/castle/source/repairs'
OUTPUT = ROOT / 'assets/castle/props'
REFERENCE = ROOT / 'assets/castle/reference'
ASSETS = {asset['id']: asset for asset in json.loads((REFERENCE / 'manifest.json').read_text())['assets']}
ASSETS['drain_grate'] = dict(width=64, height=48)
DENSITY = 4
PROP_SIZES = {'kitchen_preparation_table': (140, 40), 'control_computer_desk': (126, 64),
              'store_upper_crates': (86, 50), 'infirmary_nightstand_west': (32, 47),
              'yard_exercise_fence': (196, 202), 'arena_court_markings': (154, 136),
              'workshop_workbench': (140, 65), 'utility_energy_device_pipes': (49, 150),
              'workshop_tools_rack_west': (55, 96), 'workshop_pipe_west': (15, 80),
              'workshop_chest_southeast': (27, 64), 'infirmary_privacy_screen': (28, 70)}
# Individually generated masters for the remaining low-detail scene extractions.
CONSISTENT = {
    'corridor_crate_west': 'crate', 'store_upper_crates': 'crate-pair',
    'yard_bench_north': 'bench', 'infirmary_nightstand_west': 'candle-nightstand',
    'yard_small_stool': 'stool', 'utility_green_console': 'green-console',
    'utility_box': 'exit-switch', 'control_tower': 'control-tower',
    'control_console_2': 'control-console', 'laundry_switch': 'laundry-switch',
    'infirmary_terminal': 'medical-terminal', 'yard_exercise_fence': 'fence',
}

# Repeated basins and candle stands share complete silhouettes, rather than four
# differently damaged scene extractions of the same object.
REPAIRED_ALIASES = {
    'laundry_basin_2': 'laundry_basin_1',
    'laundry_basin_3': 'laundry_basin_1',
    'laundry_basin_4': 'laundry_basin_1',
    'infirmary_candle_stool': 'workshop_candle_stool',
    'south_dorm_candle_stool': 'workshop_candle_stool',
}


def repaired_texture(key):
    """Return a complete replacement path, or the preserved reference asset."""
    key = REPAIRED_ALIASES.get(key, key)
    repaired = {name for batch in json.loads((SOURCE / 'layout.json').read_text()) for name in batch}
    if key in repaired or key in CONSISTENT or key == 'corridor_runner':
        return f'assets/castle/props/{key}.png'
    return 'assets/castle/reference/' + ASSETS[key]['file']


def export_prop_repairs():
    """Package each entire generated object with clear padding at 4x density.

    Transparent gutters define the cells: generated layouts need not align with
    mathematical thirds.  Guard checks reject a master whose artwork touches an
    outside edge instead of silently chopping its silhouette.
    The alpha channel is retained through the resize; no backgrounds are inferred
    or removed here.
    """
    OUTPUT.mkdir(exist_ok=True)
    batches = json.loads((SOURCE / 'layout.json').read_text())
    for batch_index, names in enumerate(batches, 1):
        sheet = Image.open(SOURCE / f'prop-repair-master-{batch_index}.png').convert('RGBA')
        outer = sheet.getchannel('A').point(lambda value: 255 if value >= 32 else 0).getbbox()
        if outer is None or min(outer[0], outer[1], sheet.width - outer[2], sheet.height - outer[3]) < 8:
            raise ValueError(f'Repaired atlas touches canvas boundary: batch {batch_index}')
        objects = generated_objects(sheet, 3, 3, alpha_threshold=32)
        for key, cell in zip(names, objects, strict=True):
            bounds = cell.getchannel('A').point(lambda value: 255 if value >= 32 else 0).getbbox()
            if bounds is None:
                raise ValueError(f'Empty repaired prop: {key}')
            # Keep the extra transparent edge supplied by generated_objects, so
            # antialiased contours remain complete during alpha-aware resizing.
            sprite = cell.crop((max(0, bounds[0] - 1), max(0, bounds[1] - 1),
                                min(cell.width, bounds[2] + 1), min(cell.height, bounds[3] + 1)))
            asset = ASSETS[key]
            w, h = PROP_SIZES.get(key, (asset['width'], asset['height']))
            size = w * DENSITY, h * DENSITY
            scale = min((size[0] - 8) / sprite.width, (size[1] - 8) / sprite.height)
            sprite = sprite.resize((max(1, round(sprite.width * scale)), max(1, round(sprite.height * scale))),
                                   Image.Resampling.LANCZOS)
            canvas = Image.new('RGBA', size)
            canvas.alpha_composite(sprite, ((size[0] - sprite.width) // 2, size[1] - sprite.height - 4))
            path = OUTPUT / f'{key}.png'
            canvas.save(path)
            Path(str(path) + '.ykmeta').write_text(json.dumps(dict(
                format='yk.texture', version=1, pixelsPerUnit=35 * DENSITY, filter='linear'), indent=2) + '\n')
    for key, master in CONSISTENT.items():
        image = Image.open(SOURCE / 'consistency' / (master + '.png')).convert('RGBA')
        bounds = image.getchannel('A').point(lambda a: 255 if a >= 32 else 0).getbbox()
        if bounds is None or min(bounds[0], bounds[1], image.width - bounds[2], image.height - bounds[3]) < 4:
            raise ValueError(f'Incomplete individual prop master: {key}')
        sprite = image.crop((bounds[0] - 2, bounds[1] - 2, bounds[2] + 2, bounds[3] + 2))
        # Preserve the complete silhouette and its own aspect ratio. Source scene
        # canvases are placement data, never a reason to stretch a replacement.
        w, h = PROP_SIZES.get(key, (ASSETS[key]['width'], ASSETS[key]['height']))
        size = w * DENSITY, h * DENSITY
        scale = min((size[0] - 8) / sprite.width, (size[1] - 8) / sprite.height)
        sprite = sprite.resize((max(1, round(sprite.width * scale)), max(1, round(sprite.height * scale))),
                               Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', size)
        canvas.alpha_composite(sprite, ((size[0] - sprite.width) // 2, size[1] - sprite.height - 4))
        path = OUTPUT / f'{key}.png'
        canvas.save(path)
        Path(str(path) + '.ykmeta').write_text(json.dumps(dict(
            format='yk.texture', version=1, pixelsPerUnit=35 * DENSITY, filter='linear'), indent=2) + '\n')
    export_runner_repair()


def export_runner_repair():
    """Keep the runner's full cloth and finished ends inside its authored plane.

    This flat nine-slice material fills its complete 1320 x 90 texture.  Its
    source master contains one isolated intact runner; no floor is reconstructed
    or removed during packaging.
    """
    OUTPUT.mkdir(exist_ok=True)
    master = Image.open(SOURCE / 'corridor-runner-master.png').convert('RGBA')
    bounds = master.getchannel('A').point(lambda value: 255 if value >= 32 else 0).getbbox()
    if bounds is None:
        raise ValueError('Empty repaired corridor runner')
    if min(bounds[0], bounds[1], master.width - bounds[2], master.height - bounds[3]) < 2:
        raise ValueError(f'Repaired corridor runner touches master boundary: {bounds}')
    runner = master.crop(bounds).resize((1320, 90), Image.Resampling.LANCZOS)
    path = OUTPUT / 'corridor_runner.png'
    runner.save(path)
    Path(str(path) + '.ykmeta').write_text(json.dumps(dict(
        format='yk.texture', version=1, pixelsPerUnit=70, filter='linear',
        border=[8, 8, 8, 8]), indent=2) + '\n')


if __name__ == '__main__':
    export_prop_repairs()
