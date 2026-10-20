"""One native pixel grid for castle scenery and its exact solid silhouettes.

ImageGen masters remain editable source art. Exports use 35 pixels per world unit,
nearest sampling, a shared palette and binary alpha; no independently scaled detail.
Collision is baked once from that same alpha, as merged, non-overlapping pixel runs.
"""
import json
from functools import lru_cache
from pathlib import Path

from PIL import Image, PngImagePlugin

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/castle/source/pixel-unified'
OUTPUT = ROOT / 'assets/castle/pixel'
PPU = 35
BANNER_SIZE = (24, 28)
ASSETS = {a['id']: a for a in json.loads(
    (ROOT / 'assets/castle/reference/manifest.json').read_text())['assets']}

# Dimensions are design data in native pixels, relative to the 32 x 44 actor cell.
SIZES = {
    'dorm_bed_nw': (38, 72), 'west_vertical_bed_upper': (38, 72),
    'infirmary_bed_west': (38, 72), 'dorm_chair_nw_inner': (24, 32),
    'infirmary_nightstand_west': (24, 32), 'canteen_dining_set_west': (104, 72),
    'canteen_dining_set_east': (104, 72), 'west_common_table': (78, 48),
    'workshop_workbench': (92, 52), 'yard_small_stool': (22, 24),
    'yard_bench_north': (72, 34), 'west_storage_chest': (38, 30),
    'kitchen_small_cabinet': (30, 46), 'kitchen_side_cabinet': (30, 46),
    'workshop_chest_southeast': (30, 46), 'infirmary_privacy_screen': (48, 42),
    'utility_energy_device_pipes': (44, 64), 'utility_green_console': (28, 42),
    'utility_box': (28, 36), 'control_computer_desk': (96, 56),
    'control_tower': (28, 56), 'control_console_1': (30, 40),
    'control_console_2': (30, 40), 'control_chair': (26, 36),
    'corridor_weapon_rack_east': (46, 54), 'workshop_tools_rack_west': (48, 54),
    'infirmary_medical_cart': (36, 44), 'infirmary_terminal': (28, 40),
    'laundry_linen_rail': (76, 52), 'store_barrel': (30, 40),
    'corridor_crate_west': (32, 36), 'store_upper_crates': (64, 36),
    'workshop_rack_top_crate': (32, 36), 'west_plant': (28, 42),
    'yard_stone_planter': (46, 46), 'kitchen_fireplace': (48, 64),
    'kitchen_shelf_utensils': (56, 56), 'kitchen_shelf_east': (56, 56),
    'kitchen_jug_shelf': (56, 56), 'kitchen_preparation_table': (92, 44),
    'laundry_basin_1': (44, 40), 'laundry_basket': (30, 30),
    'laundry_folding_table': (80, 48), 'workshop_candle_stool': (24, 32),
    'workshop_pipe_west': (12, 56), 'laundry_switch': (18, 22),
    'yard_exercise_fence': (150, 154), 'arena_court_markings': (120, 100),
    'corridor_runner': (660, 45), 'drain_grate': (64, 48),
}

# Exactly twelve objects in each generated atlas, in reading order.
ATLASES = {
    'furniture': ['dorm_bed_nw', 'infirmary_bed_west', 'dorm_chair_nw_inner',
                  'infirmary_nightstand_west', 'canteen_dining_set_west',
                  'west_common_table', 'workshop_workbench', 'yard_small_stool',
                  'yard_bench_north', 'west_storage_chest', 'kitchen_small_cabinet',
                  'infirmary_privacy_screen'],
    'equipment': ['utility_energy_device_pipes', 'utility_green_console', 'utility_box',
                  'control_computer_desk', 'control_tower', 'control_console_1',
                  'control_chair', 'corridor_weapon_rack_east', 'workshop_tools_rack_west',
                  'infirmary_medical_cart', 'infirmary_terminal', 'laundry_linen_rail'],
    'domestic': ['store_barrel', 'corridor_crate_west', 'store_upper_crates', 'west_plant',
                 'yard_stone_planter', 'kitchen_fireplace', 'kitchen_shelf_utensils',
                 'kitchen_preparation_table', 'laundry_basin_1', 'laundry_basket',
                 'laundry_folding_table', 'workshop_candle_stool'],
}
ART_ALIASES = {
    'west_vertical_bed_upper': 'dorm_bed_nw',
    'canteen_dining_set_east': 'canteen_dining_set_west',
    'kitchen_side_cabinet': 'kitchen_small_cabinet',
    'workshop_chest_southeast': 'kitchen_small_cabinet',
    'control_console_2': 'control_console_1',
    'kitchen_shelf_east': 'kitchen_shelf_utensils',
    'kitchen_jug_shelf': 'kitchen_shelf_utensils',
    'workshop_rack_top_crate': 'corridor_crate_west',
}

# Shared material ramps, rather than one independently quantized palette per prop.
COLORS = '''141b28 222a36 303b49 435267 5e7385 8497a1 b4c1bd e4e6d5
241c1c 3d2923 58392a 71482e 8c5d39 a77747 c39659 ddb678
3c3b32 565349 726d5e 938a74 b5a88b d4c6a5 ece1c7 f9f0d9
193342 234e62 2f6c88 4799b5 65c3d5 9ae4dc 2c3e63 405c88
273528 3a4c31 52663b 71854b 98a35e c0bc77 3f2927 694132
93533b b56e45 d99857 f0c477 5c252e 943b3b c05745 e08250
30383e 4b5255 686f6e 8a9090 aeb4aa d2d4c5 885f29 b68b40'''.split()
PALETTE = Image.new('P', (1, 1))
_rgb = [int(color[i:i + 2], 16) for color in COLORS for i in (0, 2, 4)]
PALETTE.putpalette(_rgb + _rgb[:3] * (256 - len(COLORS)))


def pixel_image(image, size):
    """Package source artwork at its final pixel size, preserving clear silhouettes."""
    image = image.convert('RGBA').resize(size, Image.Resampling.NEAREST)
    alpha = image.getchannel('A').point(lambda a: 255 if a >= 128 else 0)
    image = image.convert('RGB').quantize(palette=PALETTE, dither=Image.Dither.NONE).convert('RGBA')
    image.putalpha(alpha)
    return image


def write_pixel(image, path, **meta):
    path.parent.mkdir(parents=True, exist_ok=True)
    info = PngImagePlugin.PngInfo()
    info.add_text('Art standard', '35 PPU; nearest; binary alpha; props use shared 64-color palette')
    image.save(path, pnginfo=info)
    Path(str(path) + '.ykmeta').write_text(json.dumps(dict(
        format='yk.texture', version=1, pixelsPerUnit=PPU, filter='nearest', **meta), indent=2) + '\n')


def atlas_cells(sheet, columns=4, rows=3):
    """The widest transparent gutters split cells, including disconnected candle flames.

    Equal mathematical cells can clip wide tables. Tiny alpha gaps inside an object
    (chair legs, a flame) are not atlas gutters. Reject ambiguous or touching masters.
    """
    alpha = sheet.getchannel('A').point(lambda a: 255 if a >= 128 else 0)

    def splits(mask, count, axis):
        length = mask.width if axis == 0 else mask.height
        occupied = [mask.crop((i, 0, i + 1, mask.height) if axis == 0 else
                              (0, i, mask.width, i + 1)).getbbox() is not None for i in range(length)]
        lo, hi = occupied.index(True), length - occupied[::-1].index(True)
        gaps, start = [], None
        for i in range(lo, hi):
            if not occupied[i] and start is None:
                start = i
            elif occupied[i] and start is not None:
                gaps.append((start, i))
                start = None
        chosen = sorted(sorted(gaps, key=lambda g: g[1] - g[0], reverse=True)[:count - 1])
        if len(chosen) != count - 1 or min(b - a for a, b in chosen) < 12:
            raise ValueError('Generated atlas has ambiguous gutters')
        cuts = [lo] + [(a + b) // 2 for a, b in chosen] + [hi]
        return list(zip(cuts, cuts[1:]))

    cells = []
    for top, bottom in splits(alpha, rows, 1):
        for left, right in splits(alpha.crop((0, top, sheet.width, bottom)), columns, 0):
            cell = sheet.crop((left, top, right, bottom))
            bounds = cell.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
            cells.append(cell.crop(bounds))
    return cells


def pixel_texture(key):
    return f'assets/castle/pixel/{key}.png'


def export_pixel_props():
    from castle_prop_repairs import repaired_texture

    masters = {}
    for name, keys in ATLASES.items():
        with Image.open(SOURCE / (name + '.png')) as sheet:
            masters.update(zip(keys, atlas_cells(sheet.convert('RGBA')), strict=True))
    for alias, target in ART_ALIASES.items():
        masters[alias] = masters[target]
    keys = set(SIZES) | {'control_banner'}
    for key in sorted(keys):
        if key in masters:
            image = masters[key]
        elif 'banner' in key:
            image = Image.open(ROOT / 'assets/castle/source/repairs/consistency/banner.png').convert('RGBA')
        else:
            image = Image.open(ROOT / repaired_texture(key)).convert('RGBA')
        asset = ASSETS.get(key, {})
        size = BANNER_SIZE if 'banner' in key else SIZES.get(key, (asset.get('width'), asset.get('height')))
        bounds = image.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
        image = image.crop(bounds)
        # A rug is a flat tiled material; all furniture preserves its generated aspect ratio.
        scale = min((size[0] - 2) / image.width, (size[1] - 2) / image.height)
        fitted = pixel_image(image, (max(1, round(image.width * scale)), max(1, round(image.height * scale))))
        canvas = Image.new('RGBA', size)
        canvas.alpha_composite(fitted, ((size[0] - fitted.width) // 2, size[1] - fitted.height - 1))
        write_pixel(canvas, ROOT / pixel_texture(key), **(
            {'border': [4, 4, 4, 4]} if key == 'corridor_runner' else {}))
    for key, asset in ASSETS.items():
        if not key.startswith('tile_'):
            continue
        with Image.open(ROOT / 'assets/castle/reference' / asset['file']) as image:
            # These source tiles already are native 35px pixel art. Preserve the
            # subtle grout contrast instead of collapsing it into palette speckles.
            write_pixel(image.convert('RGBA'), OUTPUT / (key + '.png'))
    silhouette_rectangles.cache_clear()


@lru_cache(maxsize=None)
def silhouette_rectangles(key):
    """Exact opaque-pixel union; merge identical scanline runs vertically, never pad."""
    with Image.open(ROOT / pixel_texture(key)) as image:
        alpha = image.getchannel('A')
        width, height = image.size
        pixels = alpha.load()
        active, rectangles = {}, []
        for y in range(height + 1):
            runs, start = [], None
            for x in range(width + 1):
                solid = y < height and x < width and pixels[x, y] >= 128
                if solid and start is None:
                    start = x
                elif not solid and start is not None:
                    runs.append((start, x))
                    start = None
            current = {}
            for run in runs:
                current[run] = active.pop(run, y)
            rectangles.extend((left, top, right, y) for (left, right), top in active.items())
            active = current
    return tuple(sorted(rectangles, key=lambda r: (-(r[2] - r[0]) * (r[3] - r[1]), r)))


def silhouette_colliders(key):
    with Image.open(ROOT / pixel_texture(key)) as image:
        width, height = image.size
    return [dict(type='Collider', properties=dict(
        size=[(right - left) / PPU, (bottom - top) / PPU],
        offset=[((left + right) / 2 - width / 2) / PPU,
                ((top + bottom) / 2 - height + 2) / PPU], layer='Solid', friction=0))
        for left, top, right, bottom in silhouette_rectangles(key)]


def silhouette_bounds(key):
    with Image.open(ROOT / pixel_texture(key)) as image:
        left, top, right, bottom = image.getchannel('A').getbbox()
        width, height = image.size
    return dict(size=[(right - left) / PPU, (bottom - top) / PPU],
                offset=[((left + right) / 2 - width / 2) / PPU,
                        ((top + bottom) / 2 - height + 2) / PPU], layer='Solid', friction=0)
