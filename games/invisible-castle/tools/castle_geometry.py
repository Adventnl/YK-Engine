"""Shared, resizable castle objects. Geometry owns rendering, feet and contact shadows.

Wall runs repeat native masonry with permanent left/right returns. Props use the
actual opaque sprite silhouette, so transparent canvas margins never become solid.
"""
import json
from pathlib import Path
from PIL import Image, ImageOps
from castle_pixel_art import (export_pixel_props, pixel_texture, SIZES, BANNER_SIZE, PPU,
                              silhouette_bounds, silhouette_colliders)
from castle_masonry_art import export_masonry_materials

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'assets/castle/reference'
ASSETS = {a['id']: a for a in json.loads((REFERENCE / 'manifest.json').read_text())['assets']}
# Actors and scenery share a native raster; window resolution cannot add prop detail.
WORLD_DENSITY = PPU
COPING_DEPTH = 26


def component(kind, **properties):
    return dict(type=kind, properties=properties)


def material(key):
    return Image.open(REFERENCE / ASSETS[key]['file']).convert('RGBA')


def export_wall_materials():
    export_pixel_props()
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


def column_parts(width, top, bottom, crossings):
    """Join the coping planes while retaining the original solid footprint.

    Facades include a projected brick face below their coping. A column coming
    toward the viewer must continue over that face, starting at the coping edge.
    At its far end it overlaps the next coping, making a continuous T junction.
    """
    above = next(((a, b) for a, b in crossings if abs(top - b) < .01), None)
    below = next(((a, b) for a, b in crossings if abs(bottom - a) < .01), None)
    art_top = above[0] + min(COPING_DEPTH, above[1] - above[0]) if above else top
    art_bottom = below[0] + min(COPING_DEPTH, below[1] - below[0]) if below else bottom
    parts = wall_parts(width, bottom - top, vertical=True, free_end=below is None)
    sprite = parts[0]['properties']
    sprite.update(size=[width / PPU, (art_bottom - art_top) / PPU],
                  offset=[0, ((art_top + art_bottom) / 2 - bottom) / PPU])
    if below:
        # The junction must draw above the facade's cap, which sorts at its base.
        sprite['sortOffset'] = (below[1] - bottom + .1) / PPU
    return parts


def add_wall_copings(scene):
    """Use one stone surface in both directions, including the south perimeter."""
    next_id = max(int(e['id'], 16) for e in scene['entities']) + 1
    copings = []
    for entity in scene['entities']:
        sprite = next((c['properties'] for c in entity['components'] if c['type'] == 'SpriteRenderer'), None)
        if not sprite or not sprite['texture'].endswith('/hq-wall-run.png'):
            continue
        x, y = entity['transform']['position']
        w, h = sprite['size']
        dx, dy = sprite.get('offset', [0, 0])
        depth = min(COPING_DEPTH / PPU, h)
        center_y = y + dy - h / 2 + depth / 2
        copings.append(dict(id=f'{next_id:016x}', name=entity['name'] + ' coping',
            transform=dict(position=[x + dx, center_y], rotation=90, scale=[1, 1]),
            components=[component('SpriteRenderer', texture='assets/castle/materials/hq-wall-column.png',
                size=[depth, w], layer=sprite['layer'], ySort=True,
                sortOffset=y + sprite.get('sortOffset', 0) - center_y + .01 / PPU,
                drawMode='Sliced', sliceFill='Tile')]))
        next_id += 1
    scene['entities'].extend(copings)


def subtract_interval(start, end, holes):
    result = [(start, end)]
    for left, right in holes:
        result = [(a, b) for x, y in result for a, b in ((x, min(y, left)), (max(x, right), y)) if b > a]
    return result


# Repeated roles use identical silhouettes and dimensions in both chapters.
ALIASES = {
    'corridor_weapon_rack_west': 'corridor_weapon_rack_east',
    'yard_bench_south': 'yard_bench_north',
    'utility_small_stool': 'yard_small_stool', 'workshop_stool': 'yard_small_stool',
    'infirmary_stool': 'yard_small_stool',
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


def prop_definition(key):
    canonical = ALIASES.get(key, key)
    asset = ASSETS[canonical]
    width, height = SIZES.get(canonical, (asset['width'], asset['height']))
    if 'banner' in canonical:
        width, height = BANNER_SIZE
    return canonical, width, height


def prop_sprite(key):
    canonical, width, height = prop_definition(key)
    return dict(texture=pixel_texture(canonical),
                size=[width / PPU, height / PPU], offset=[0, (-height / 2 + 2) / PPU],
                layer=10, ySort=True)


def footprint(key):
    """Envelope for layout clearance only; physical collision uses the opaque union."""
    return silhouette_bounds(prop_definition(key)[0])


def prop_colliders(key):
    return silhouette_colliders(prop_definition(key)[0])


def ground_prop(sprite, collider):
    width, depth = collider['size']
    sprite.update(shadowSize=[width * .8, max(.18, min(.3, depth * .15))],
                  shadowOffset=[.06, .04], shadowColor='#080e2450')
