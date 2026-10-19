"""Shared, resizable castle objects. Geometry owns rendering, feet and contact shadows.

Wall runs repeat detailed masonry with permanent left/right returns. Props use authored
floor depths and visible-width limits, so transparent canvas margins never become solid.
"""
import json
from functools import lru_cache
from pathlib import Path
from PIL import Image, ImageOps
from castle_prop_repairs import export_prop_repairs, repaired_texture
from castle_masonry_art import export_masonry_materials

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'assets/castle/reference'
ASSETS = {a['id']: a for a in json.loads((REFERENCE / 'manifest.json').read_text())['assets']}
PPU = 35
# Use the actual viewport resolution. A 25px/unit intermediate discarded almost a
# third of the supplied 35px/unit artwork before magnifying the damage on screen.
WORLD_DENSITY = 0


def component(kind, **properties):
    return dict(type=kind, properties=properties)


def material(key):
    return Image.open(REFERENCE / ASSETS[key]['file']).convert('RGBA')


def export_wall_materials():
    export_prop_repairs()
    directory = ROOT / 'assets/castle/materials'
    directory.mkdir(exist_ok=True)
    # Borders are fixed-width end returns; the middle and coping repeat without stretching.
    cap, face, pier = (material(k) for k in ('module_inner_cap', 'module_inner_face', 'module_wall_end_pier'))
    wall = Image.new('RGBA', (53, 84))
    wall.alpha_composite(cap, (9, 0))
    wall.alpha_composite(face, (9, 26))
    wall.alpha_composite(pier.crop((0, 0, 9, 84)), (0, 0))
    wall.alpha_composite(ImageOps.mirror(pier.crop((0, 0, 9, 84))), (44, 0))
    wall.save(directory / 'wall-run.png')
    column = material('module_vertical_light')
    column.save(directory / 'wall-column.png')
    # A free end needs a front elevation, rather than a chopped-off coping tile.
    # Keep that elevation fixed while the stone coping above it repeats.
    end = Image.new('RGBA', (column.width, column.height + 35))
    end.alpha_composite(column)
    end.alpha_composite(pier.crop((0, pier.height - 35, pier.width, pier.height)), (0, column.height))
    end.save(directory / 'wall-column-end.png')
    for name, border in [('wall-run', [9, 26, 9, 4]), ('wall-column', [4, 3, 4, 3]),
                         ('wall-column-end', [4, 3, 4, 35])]:
        (directory / (name + '.png.ykmeta')).write_text(json.dumps(dict(
            format='yk.texture', version=1, pixelsPerUnit=PPU, filter='nearest', border=border), indent=2) + '\n')
    runner = REFERENCE / ASSETS['corridor_runner']['file']
    Path(str(runner) + '.ykmeta').write_text(json.dumps(dict(
        format='yk.texture', version=1, pixelsPerUnit=PPU, filter='nearest', border=[4, 4, 4, 4]), indent=2) + '\n')
    export_masonry_materials()


def wall_parts(width, height, vertical=False, free_end=False):
    """A complete wall run, including a front elevation at a free column end."""
    return [component('SpriteRenderer', texture='assets/castle/materials/hq-' +
                      (('wall-column-end' if free_end else 'wall-column') if vertical else 'wall-run') + '.png',
                      size=[width / PPU, height / PPU], offset=[0, -height / PPU / 2],
                      drawMode='Sliced', sliceFill='Tile', layer=10, ySort=True,
                      shadowSize=[(width + 6) / PPU, 12 / PPU], shadowOffset=[2 / PPU, 3 / PPU],
                      shadowColor='#080e2448'),
            component('Collider', size=[width / PPU, height / PPU],
                      offset=[0, -height / PPU / 2], layer='Solid', friction=0)]


def subtract_interval(start, end, holes):
    result = [(start, end)]
    for left, right in holes:
        result = [(a, b) for x, y in result for a, b in ((x, min(y, left)), (max(x, right), y)) if b > a]
    return result


# Repeated roles use identical silhouettes and dimensions in both chapters.
ALIASES = {
    'dorm_bed_ne': 'dorm_bed_nw', 'south_dorm_bed_west': 'dorm_bed_nw', 'south_dorm_bed_east': 'dorm_bed_nw',
    'west_vertical_bed_lower': 'west_vertical_bed_upper',
    'dorm_chair_ne_top': 'dorm_chair_nw_inner', 'dorm_chair_nw': 'dorm_chair_nw_inner',
    'dorm_chair_ne_inner': 'dorm_chair_nw_inner', 'south_dorm_chair': 'dorm_chair_nw_inner',
    'corridor_chair_west': 'dorm_chair_nw_inner', 'corridor_chair_east': 'dorm_chair_nw_inner',
    'kitchen_chair': 'dorm_chair_nw_inner', 'utility_chair': 'dorm_chair_nw_inner',
    'west_common_chair_left': 'dorm_chair_nw_inner', 'west_common_chair_right': 'dorm_chair_nw_inner',
    'store_middle_crates': 'corridor_crate_west', 'store_lower_crate': 'corridor_crate_west',
    'workshop_middle_crate': 'corridor_crate_west', 'utility_crate': 'corridor_crate_west',
    'workshop_crate_by_rack': 'corridor_crate_west', 'workshop_crate_southwest': 'corridor_crate_west',
    'kitchen_barrel_north': 'store_barrel', 'kitchen_barrel_south': 'store_barrel',
    'workshop_barrel_west': 'store_barrel', 'workshop_barrel_east': 'store_barrel',
    'workshop_small_barrel': 'store_barrel', 'west_barrel': 'store_barrel',
    'control_plant': 'west_plant', 'corridor_plant': 'west_plant',
    'infirmary_plant_west': 'west_plant', 'infirmary_plant_east': 'west_plant',
    'infirmary_bed_east': 'infirmary_bed_west',
    'infirmary_nightstand_east': 'infirmary_nightstand_west',
    'workshop_tools_rack_east': 'workshop_tools_rack_west',
    'laundry_basin_2': 'laundry_basin_1', 'laundry_basin_3': 'laundry_basin_1',
    'laundry_basin_4': 'laundry_basin_1',
    'infirmary_candle_stool': 'workshop_candle_stool', 'south_dorm_candle_stool': 'workshop_candle_stool',
}
# Explicit scale choices replace accidental dimensions from extracted image canvases.
SIZES = {'kitchen_preparation_table': (140, 40), 'control_computer_desk': (126, 64),
         'workshop_workbench': (140, 65), 'utility_energy_device_pipes': (49, 150),
         'workshop_tools_rack_west': (55, 96), 'workshop_pipe_west': (15, 80),
         'workshop_chest_southeast': (27, 64), 'infirmary_privacy_screen': (28, 70)}


def prop_definition(key):
    canonical = ALIASES.get(key, key)
    asset = ASSETS[canonical]
    width, height = SIZES.get(canonical, (asset['width'], asset['height']))
    return canonical, width, height


def prop_sprite(key):
    canonical, width, height = prop_definition(key)
    return dict(texture=repaired_texture(canonical),
                size=[width / PPU, height / PPU], offset=[0, (-height / 2 + 2) / PPU],
                layer=10, ySort=True)


@lru_cache(maxsize=None)
def visible_width(key):
    """Generated objects retain padding; that padding must not become solid."""
    path = repaired_texture(key)
    if not path.startswith('assets/castle/props/'):
        return 1
    with Image.open(ROOT / path) as image:
        bounds = image.getchannel('A').point(lambda alpha: 255 if alpha >= 64 else 0).getbbox()
        return (bounds[2] - bounds[0]) / image.width


def footprint(key):
    """Projected floor contact, independent of the height drawn above it."""
    canonical, w, h = prop_definition(key)
    if 'bed' in canonical:
        depth = h - (18 if 'vertical' in canonical or 'infirmary' in canonical else 22)
    elif 'dining_set' in canonical:
        depth = h - 28
    elif 'energy_device' in canonical:
        # Full pipe assembly rests on the floor; the old .6-unit box let actors walk through it.
        depth = h - 34
    elif 'table' in canonical or 'workbench' in canonical or 'desk' in canonical:
        depth = min(32, h * .5)
    elif 'crate' in canonical or 'barrel' in canonical or 'basin' in canonical:
        depth = h * .52
    else:
        depth = min(24, h * .4)
    contact_width = min(w - 10, w * visible_width(canonical) - 4)
    return dict(size=[max(12, contact_width) / PPU, depth / PPU], offset=[0, (-depth / 2 + 2) / PPU],
                layer='Solid', friction=0)


def ground_prop(sprite, collider):
    width, depth = collider['size']
    sprite.update(shadowSize=[width + .18, max(.22, depth * .48)],
                  shadowOffset=[.06, .04], shadowColor='#080e2450')
