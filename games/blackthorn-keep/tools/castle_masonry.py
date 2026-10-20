"""Build both keeps from joined stone footprints on one native pixel grid.

The plan is a union, so a T junction has one surface and no internal end caps.
Every coping is 35px wide and every wall projects 49px upward. Export small,
reusable native tiles; collision is partitioned from that same plan.
"""
import hashlib
import math

from PIL import Image, ImageChops, ImageDraw

from castle_geometry import ROOT, PPU, component, material
from castle_pixel_art import write_pixel

THICKNESS = 35
ELEVATION = 49
OUTPUT = ROOT / 'assets/castle/masonry'


def rectangles(mask):
    """Disjoint rectangles covering an occupancy mask, without padding."""
    pixels = mask.load()
    active, result = {}, []
    for y in range(mask.height + 1):
        runs, start = [], None
        for x in range(mask.width + 1):
            occupied = y < mask.height and x < mask.width and pixels[x, y]
            if occupied and start is None:
                start = x
            elif not occupied and start is not None:
                runs.append((start, x))
                start = None
        current = {run: active.pop(run, y) for run in runs}
        result.extend((left, top, right, y) for (left, right), top in active.items())
        active = current
    return result


def build_masonry(scene, chapter):
    """Convert authoring runs into connected modular geometry after layout mapping."""
    walls = [e for e in scene['entities'] if e['name'].startswith('Masonry ')]
    horizontal, vertical = [], []
    for entity in walls:
        if entity['name'].endswith('coping'):
            continue
        sprite = next((p['properties'] for p in entity['components']
                       if p['type'] == 'SpriteRenderer'), None)
        if not sprite:
            continue
        is_column = 'wall-column' in sprite['texture']
        # Joined chapter-one columns can own several visible pieces but only
        # one continuous collider. Use that physical run once.
        for shape in entity['components']:
            if shape['type'] != 'Collider':
                continue
            p = shape['properties']
            x, y = entity['transform']['position']
            dx, dy = p.get('offset', [0, 0])
            w, h = p['size']
            left, top, right, bottom = [round(v * PPU) for v in
                (x + dx - w / 2, y + dy - h / 2, x + dx + w / 2, y + dy + h / 2)]
            if is_column:
                left = round((left + right - THICKNESS) / 2)
                vertical.append((left, top, left + THICKNESS, bottom))
            else:
                horizontal.append((left, top, right, bottom))

    # The extracted room facades differed by up to six pixels along the same
    # wall. Give each shared row one front edge before joining any columns.
    band_bottom = {}
    for left, top, right, bottom in horizontal:
        band_bottom[top] = max(bottom, band_bottom.get(top, bottom))
    horizontal = [(left, top, right, band_bottom[top])
                  for left, top, right, bottom in horizontal]
    # Back floors reach the unified coping edge. A taller source facade could
    # otherwise leave a narrow background strip between its room and the cap.
    for entity in scene['entities']:
        if not entity['name'].startswith('Floor '):
            continue
        sprite = next(p['properties'] for p in entity['components'] if p['type'] == 'SpriteRenderer')
        x, y = entity['transform']['position']
        w, h = sprite['size']
        dx, dy = sprite.get('offset', [0, 0])
        bottom = (y + dy + h / 2) * PPU
        candidates = [d - THICKNESS - ELEVATION for a, b, c, d in horizontal
                      if min((x + dx + w / 2) * PPU, c) > max((x + dx - w / 2) * PPU, a)
                      and 0 < d - THICKNESS - ELEVATION - bottom <= 12]
        if candidates:
            extension = (max(candidates) + 1 - bottom) / PPU
            sprite['size'][1] += extension
            sprite['offset'] = [dx, dy + extension / 2]
    plan = [(left, bottom - THICKNESS, right, bottom)
            for left, top, right, bottom in horizontal]
    for left, top, right, bottom in vertical:
        # Columns formerly stopped at the back of a projected facade. Continue
        # their footprint to its front edge before unioning; never draw an end
        # face across a connecting wall.
        for a, b, c, d in horizontal:
            if min(right, c) > max(left, a) and b - 1 <= bottom <= d:
                bottom = d
        plan.append((left, top, right, bottom))

    # A side gate occupies the same raised plane as the adjoining column.
    # Keeping its leaves at floor level hid them behind the south wall, leaving
    # only the red strip visible in the reported east-gate screenshot.
    for entity in scene['entities']:
        if 'gate' not in entity.get('tags', []):
            continue
        collider = next(p['properties'] for p in entity['components'] if p['type'] == 'Collider')
        w, h = collider['size']
        if h <= w:
            continue
        x, y = entity['transform']['position']
        left, top, right, bottom = [round(v * PPU) for v in
                                   (x - w / 2, y - h / 2, x + w / 2, y + h / 2)]
        plan.extend([(left, top, right, top + 9), (left, bottom - 9, right, bottom)])
        sprite = next(p['properties'] for p in entity['components'] if p['type'] == 'SpriteRenderer')
        sprite['offset'][1] -= ELEVATION / PPU

    # Mount whole fixtures on the exposed face, below the shared 35px coping.
    # Original art used a 26px coping, which left brackets buried in the stone.
    for entity in scene['entities']:
        sprite = next((p['properties'] for p in entity['components']
                       if p['type'] == 'SpriteRenderer'), None)
        if not sprite or not (sprite['texture'].endswith(('/control_banner.png', '/laundry_switch.png'))
                              or entity['name'] == 'Fixed wall torch'):
            continue
        x, y = entity['transform']['position']
        dx, dy = sprite.get('offset', [0, 0])
        w, h = sprite['size']
        supports = [r for r in horizontal
                    if r[0] <= (x + dx - w / 2) * PPU + 1
                    and r[2] >= (x + dx + w / 2) * PPU - 1]
        if supports:
            left, top, right, bottom = min(supports,
                key=lambda r: abs(r[3] - ELEVATION / 2 - (y + dy) * PPU))
            if abs(bottom - ELEVATION / 2 - (y + dy) * PPU) < 2 * PPU:
                entity['transform']['position'][1] = (bottom - ELEVATION / 2) / PPU - dy
                sprite['sortOffset'] = bottom / PPU - entity['transform']['position'][1] + .01

    origin_x = math.floor(min(r[0] for r in plan) / PPU) * PPU
    origin_y = math.floor((min(r[1] for r in plan) - ELEVATION) / PPU) * PPU
    width = math.ceil((max(r[2] for r in plan) - origin_x) / PPU) * PPU
    height = math.ceil((max(r[3] for r in plan) - origin_y) / PPU) * PPU
    solid = Image.new('L', (width, height))
    draw = ImageDraw.Draw(solid)
    for left, top, right, bottom in plan:
        draw.rectangle((left - origin_x, top - origin_y,
                        right - origin_x - 1, bottom - origin_y - 1), fill=255)
    top_mask = Image.new('L', solid.size)
    top_mask.paste(solid, (0, -ELEVATION))
    projected = Image.new('L', solid.size)
    for shift in range(ELEVATION + 1):
        moved = Image.new('L', solid.size)
        moved.paste(top_mask, (0, shift))
        projected = ImageChops.lighter(projected, moved)
    face_mask = ImageChops.subtract(projected, top_mask)

    # The same stone courses run through corners in either direction. Only the
    # union's exposed edges receive a bevel, so joins never get a bright seam.
    column = Image.open(ROOT / 'assets/castle/materials/wall-column.png').convert('RGBA')
    face = material('module_inner_face')
    stone = column.crop((4, 3, column.width - 4, column.height - 3))

    def fill(tile):
        canvas = Image.new('RGBA', solid.size)
        for y in range(0, height, tile.height):
            for x in range(0, width, tile.width):
                canvas.paste(tile, (x, y))
        return canvas

    coping = fill(stone)
    coping.putalpha(top_mask)
    pixels, occupancy = coping.load(), top_mask.load()
    for y in range(height):
        for x in range(width):
            if not occupancy[x, y]:
                continue
            if x == 0 or y == 0 or not occupancy[x - 1, y] or not occupancy[x, y - 1]:
                pixels[x, y] = (173, 216, 247, 255)
            elif x == width - 1 or y == height - 1 or not occupancy[x + 1, y] or not occupancy[x, y + 1]:
                pixels[x, y] = (51, 84, 124, 255)
    facade = fill(face.crop((0, 0, face.width, face.height - 4)))
    facade.putalpha(face_mask)

    scene['entities'] = [e for e in scene['entities'] if e not in walls]
    next_id = max(int(e['id'], 16) for e in scene['entities']) + 1
    OUTPUT.mkdir(parents=True, exist_ok=True)
    tiles, tile_indices = [], {}
    texture_path = OUTPUT / f'chapter-{chapter}.png'
    columns = 16

    def add(name, position, parts, tag):
        nonlocal next_id
        scene['entities'].append(dict(id=f'{next_id:016x}', name=name,
            transform=dict(position=position, rotation=0, scale=[1, 1]),
            components=parts, tags=['masonry', tag]))
        next_id += 1

    for kind, canvas, depth in [('top', coping, ELEVATION), ('face', facade, 0)]:
        for y in range(0, height, PPU):
            for x in range(0, width, PPU):
                tile = canvas.crop((x, y, x + PPU, y + PPU))
                if not tile.getchannel('A').getbbox():
                    continue
                digest = hashlib.sha256(tile.tobytes()).hexdigest()[:20]
                if digest not in tile_indices:
                    tile_indices[digest] = len(tiles)
                    tiles.append(tile)
                add(f'Masonry {kind} block {x // PPU} {y // PPU}',
                    [(origin_x + x + PPU / 2) / PPU, (origin_y + y + PPU) / PPU],
                    [component('SpriteRenderer', texture=texture_path.relative_to(ROOT).as_posix(),
                        size=[1, 1], offset=[0, -.5], layer=10, ySort=True,
                        sortOffset=depth / PPU, drawMode='Tiled', tileSize=[1, 1],
                        columns=columns, rows=1, frame=tile_indices[digest])], 'wall_' + kind)
    rows = math.ceil(len(tiles) / columns)
    atlas = Image.new('RGBA', (columns * PPU, rows * PPU))
    for index, tile in enumerate(tiles):
        atlas.paste(tile, (index % columns * PPU, index // columns * PPU))
    write_pixel(atlas, texture_path)
    for entity in scene['entities']:
        if not entity.get('tags', []) or 'masonry' not in entity['tags']:
            continue
        for part in entity['components']:
            if part['type'] == 'SpriteRenderer':
                part['properties']['rows'] = rows
    for index, (left, top, right, bottom) in enumerate(rectangles(solid)):
        add(f'Masonry solid {index}',
            [(origin_x + (left + right) / 2) / PPU,
             (origin_y + (top + bottom) / 2) / PPU],
            [component('Collider', size=[(right - left) / PPU, (bottom - top) / PPU],
                       layer='Solid', friction=0)], 'wall_solid')
