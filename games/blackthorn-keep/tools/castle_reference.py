"""Shared castle presentation from the user's editable layers and original animated doors.

The supplied asset manifest is data, not build instructions. Coordinates use 35 pixels
per tile; complete high-density artwork, collision and passages are authored here.
Both chapter builders call these functions, so rebuilding cannot restore the old style.
"""
import json
import math
from copy import deepcopy
from pathlib import Path

from PIL import Image, ImageDraw
from art_import import generated_objects, silhouette
from castle_geometry import (export_wall_materials, wall_parts, column_parts, add_wall_copings, subtract_interval,
                             prop_definition, prop_sprite, footprint, ground_prop, prop_colliders, WORLD_DENSITY)
from castle_pixel_art import pixel_texture, pixel_image, write_pixel, BANNER_SIZE, PPU, silhouette_colliders
from castle_layout import (PLACEMENTS, OMIT_PROPS, CHAPTER_ONE_PLACEMENTS,
                           compact_architecture, furnish_compact_rooms, furnish_chapter_one)
from castle_masonry import build_masonry

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/castle'
REFERENCE = ART / 'reference'
MANIFEST = json.loads((REFERENCE / 'manifest.json').read_text())
ASSETS = {a['id']: a for a in MANIFEST['assets']}
VIEW_HEIGHT = 9.2
BOUNDS = [1591 / PPU, 883 / PPU]
DOOR_FRAMES = 8
FRONT_DOORS = [('North cell door', 148, 260, 69, 60), ('North east cell door', 321, 260, 63, 60),
               ('South cell door', 144, 656, 61, 57), ('South east cell door', 320, 656, 61, 57),
               ('Canteen door', 614, 380, 58, 86), ('Laundry door', 961, 380, 52, 86),
               ('Control door', 1330, 383, 64, 92), ('Stores door', 1523, 383, 58, 91),
               ('Workshop door', 614, 556, 58, 90), ('Infirmary door', 961, 556, 52, 90),
               ('Utility door', 1523, 643, 57, 60), ('Exit anteroom door', 1523, 738, 55, 48)]
DOOR_SIZE = (56, 60)


def c(kind, **properties):
    return dict(type=kind, properties=properties)


def part(entity, kind):
    return next((p['properties'] for p in entity['components'] if p['type'] == kind), None)


def point(x, y):
    return [(x - 40) / PPU, (y - 25) / PPU]


def texture(asset):
    if asset.startswith('tile_'):
        return pixel_texture(asset)
    return 'assets/castle/reference/' + ASSETS[asset]['file']


SIDE_GATE_WIDTH = 18


def side_door_art(depth):
    """Compact locked grille entirely contained in a vertical wall opening."""
    path = ART / 'doors' / f'locked-side-{depth}.png'
    master = Image.open(ART / 'source/repairs/consistency/gate-locked-side.png').convert('RGBA')
    bounds = master.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
    height = depth
    closed = pixel_image(master.crop(bounds), (SIDE_GATE_WIDTH, height))
    sheet = Image.new('RGBA', (SIDE_GATE_WIDTH * DOOR_FRAMES, height))
    # Slide into the north wall pocket without squashing the bars or padlock.
    # The final cell is empty: unlocking clears the complete passage.
    for frame in range(DOOR_FRAMES):
        shift = round(height * frame / (DOOR_FRAMES - 1))
        if shift < height:
            sheet.alpha_composite(closed.crop((0, shift, SIDE_GATE_WIDTH, height)),
                                  (frame * SIDE_GATE_WIDTH, 0))
    write_pixel(sheet, path)
    return path.relative_to(ROOT).as_posix()


def door_art(width=64, height=72, side=False):
    """Bake gates on the native grid, retaining fixed jambs and animated leaves."""
    output = ART / 'doors'
    output.mkdir(exist_ok=True)
    if side:
        return side_door_art(height)
    path = output / f'front-{width}x{height}.png'
    density = 4
    width, height = width * density, height * density
    master = Image.open(ART / 'source/repairs/consistency/gate.png').convert('RGBA')
    bounds = master.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
    master = master.crop(bounds).resize((64 * density, 64 * density), Image.Resampling.LANCZOS)
    # The new master's jambs are eight pixels wide, with a nine-pixel lintel.
    panel = master.crop((9 * density, 10 * density, 55 * density, 63 * density))
    jamb = Image.new('RGBA', (width, height))
    jamb.alpha_composite(master.crop((0, 0, 64 * density, 9 * density)).resize(
        (width, 9 * density), Image.Resampling.LANCZOS))
    for left, x in [(True, 0), (False, width - 8 * density)]:
        strip = master.crop((0 if left else 56 * density, 9 * density,
                             8 * density if left else 64 * density, 64 * density))
        jamb.alpha_composite(strip.resize((8 * density, height - 9 * density),
                                          Image.Resampling.LANCZOS), (x, 9 * density))
    interior = (8 * density, 9 * density, width - 8 * density, height)
    x1, y1, x2, y2 = interior
    iw, ih = x2 - x1, y2 - y1
    closed = panel.resize((iw, ih), Image.Resampling.LANCZOS)
    sheet = Image.new('RGBA', (width * DOOR_FRAMES, height))
    for frame in range(DOOR_FRAMES):
        amount = frame / (DOOR_FRAMES - 1)
        face = Image.new('RGBA', (width, height))
        extent = max(1, round(iw / 2 * math.cos(amount * math.pi / 2)))
        for leaf, x in [(closed.crop((0, 0, iw // 2, ih)), x1),
                        (closed.crop((iw // 2, 0, iw, ih)), x2 - extent)]:
            face.alpha_composite(leaf.resize((extent, ih), Image.Resampling.LANCZOS), (x, y1))
        face.alpha_composite(jamb)
        sheet.alpha_composite(face, (frame * width, 0))
    write_pixel(pixel_image(sheet, (sheet.width // density, sheet.height // density)), path)
    return path.relative_to(ROOT).as_posix()


def character_art():
    """Keep the existing strides/searches and give the hero separate standing cells."""
    directory = ROOT / 'assets/level2'
    source = Image.open(directory / 'source/hero_idle.png').convert('RGBA')
    frames = [silhouette(f) for f in generated_objects(source, 2, 4, alpha_threshold=160)]
    scale = min(28 / max(f.width for f in frames), 38 / max(f.height for f in frames))
    sheet = Image.new('RGBA', (320, 176))
    walk = Image.open(directory / 'hero.png').convert('RGBA')
    for row in range(4):
        sheet.alpha_composite(walk.crop((0, row * 44, 256, row * 44 + 44)), (0, row * 44))
        for col in range(2):
            frame = frames[row * 2 + col]
            frame = frame.resize((round(frame.width * scale), round(frame.height * scale)), Image.Resampling.NEAREST)
            sheet.alpha_composite(frame, (256 + col * 32 + (32 - frame.width) // 2,
                                         row * 44 + 42 - frame.height))
    sheet.save(directory / 'hero-polished.png')
    animation = json.loads((directory / 'hero.ykanim').read_text())
    animation.update(texture='assets/level2/hero-polished.png', columns=10)
    for clip in animation['clips']:
        row = ['down', 'left', 'right', 'up'].index(clip['name'].split('_')[1])
        if clip['name'].startswith('idle_'):
            clip.update(frames=[row * 10 + 8, row * 10 + 9], durations=[2.8, .16])
        else:
            clip['frames'] = [row * 10 + f % 8 for f in clip['frames']]
            if clip['name'].startswith('walk_'):
                clip['fps'] = 7
    (directory / 'hero-polished.ykanim').write_text(json.dumps(animation, indent=2) + '\n')


def banner_texture(key):
    return pixel_texture('control_banner')


def set_character(entity, role):
    sprite = part(entity, 'SpriteRenderer')
    sprite.update(texture='assets/level2/' + ('hero-polished' if role == 'hero' else role) + '.png', size=[32 / PPU, 44 / PPU],
                  offset=[0, -20 / PPU], layer=10, ySort=True,
                  shadowSize=[.65, .24], shadowOffset=[.03, 0], shadowColor='#080e2460')
    animation = part(entity, 'AnimatedSprite')
    if animation:
        animation['animation'] = 'assets/level2/' + ('hero-polished' if role == 'hero' else role) + '.ykanim'
    if not part(entity, 'CharacterMotion'):
        entity['components'].append(c('CharacterMotion', idleHeight=.018, stepHeight=.02,
                                      animateSteps=False, flipWithMovement=False))
    if not part(entity, 'TopDownController'):
        body = part(entity, 'RigidBody')
        if body:
            body.update(type='Kinematic', gravityScale=0, fixedRotation=True)
        part(entity, 'Collider')['layer'] = 'Solid'
    else:
        part(entity, 'TopDownController')['speed'] = 3
    # Transform anchors the visible soles. Keep the whole feet box above that
    # anchor; centering it here leaves an invisible four-pixel border in front.
    part(entity, 'Collider').update(size=[16 / PPU, 8 / PPU], offset=[0, -4 / PPU])
    motion = part(entity, 'CharacterMotion')
    motion.update(idleHeight=.008, stepHeight=0, animateSteps=False, flipWithMovement=False)
    dialogue = part(entity, 'Dialogue')
    if dialogue:
        dialogue['portrait'] = 'assets/level2/portrait_' + role + '.png'
    # Load explicit dependencies before controllers, so no default collider is inserted ahead
    # of the authored feet collider (Collider deliberately allows multiple shapes).
    order = {'SpriteRenderer': 0, 'Collider': 1, 'RigidBody': 2, 'PlayerInput': 3,
             'Inventory': 4, 'Crafter': 5, 'AnimatedSprite': 6, 'CharacterMotion': 7,
             'NpcPath': 8, 'TopDownController': 8, 'Interactor': 9}
    entity['components'].sort(key=lambda p: order.get(p['type'], 20))


def set_door(entity, width, depth=.55, automatic=True, height=None, side=False):
    entity['components'] = [p for p in entity['components'] if p['type'] != 'AnimatedSprite']
    sprite = part(entity, 'SpriteRenderer')
    sprite.clear()
    pixels = [round(width * PPU), round((height or 1.9) * PPU)]
    sprite.update(texture=door_art(*pixels, side=side), size=[pixels[0] / PPU, pixels[1] / PPU],
                  layer=10, offset=[0, 0], sortOffset=0 if side else pixels[1] / PPU / 2,
                  columns=DOOR_FRAMES, ySort=True)
    if side:
        # Keep the edge and west-facing lock inside the actual gap. Stone
        # above and below owns the depth; the gate needs no extra elevation.
        sprite.update(size=[SIDE_GATE_WIDTH / PPU, pixels[1] / PPU],
                      offset=[(SIDE_GATE_WIDTH - pixels[0]) / (2 * PPU), 0])
    collider = part(entity, 'Collider')
    collider.update(size=[width, pixels[1] / PPU if side else depth], offset=[0, 0], layer='Solid', friction=0)
    gate = part(entity, 'StateGate')
    if gate is None:
        gate = c('StateGate')['properties']
        entity['components'].append(dict(type='StateGate', properties=gate))
    gate.update(openFrames=DOOR_FRAMES, animationSeconds=.32, stopWhenBlocked=True)
    if automatic:
        gate.update(startsOpen=False, proximityRange=2.25, closeDelay=.7)
        # Passage doors need no button and never steal a nearby container's interaction.
        entity['components'] = [p for p in entity['components'] if p['type'] != 'Interactable']
    entity['tags'] = list(dict.fromkeys(entity.get('tags', []) + ['room_door' if automatic else 'gate']))


def style_level_two(scene):
    character_art()
    old = {e['name']: e for e in scene['entities']}
    used = []
    next_id = max(int(e['id'], 16) for e in scene['entities']) + 1

    def add(name, at, *components):
        nonlocal next_id
        entity = dict(id=f'{next_id:016x}', name=name,
                      transform=dict(position=list(at), rotation=0, scale=[1, 1]),
                      components=list(components))
        next_id += 1
        used.append(entity)
        return entity

    # Door cuts are explicit physical passages through the supplied reconstructed masonry.
    cuts = [(394, 429, 426, 511), (791, 429, 825, 511),
            (1130, 429, 1163, 511), (1413, 429, 1447, 511),
            (1298, 336, 1362, 430), (1494, 336, 1552, 430),
            (231, 158, 262, 216), (223, 735, 254, 793)]
    front_cuts = [(x - DOOR_SIZE[0] // 2, math.ceil(y + h / 2) - DOOR_SIZE[1],
                   x + DOOR_SIZE[0] // 2, math.ceil(y + h / 2))
                  for _, x, y, _, h in FRONT_DOORS]
    for i, (x1, y1, x2, y2) in enumerate(cuts):
        add('Stone passage ' + str(i), point((x1 + x2) / 2, (y1 + y2) / 2),
            c('SpriteRenderer', texture=texture('tile_dormitory_north' if i == 6 else
                                               'tile_dormitory_south' if i == 7 else
                                               'tile_west_common_room' if i == 0 else 'tile_main_corridor'),
              size=[(x2 - x1) / PPU, (y2 - y1) / PPU], layer=-31,
              drawMode='Tiled', tileSize=[1, 1]))
    export_wall_materials()
    # Floors repeat on the shared native grid rather than stretching extracted room canvases.
    for asset in MANIFEST['assets']:
        if asset['category'] != 'floors':
            continue
        x, y = asset['origin_px']
        w, h = asset['width'], asset['height']
        # Room materials continue through their north/south doorway to the
        # front edge of the shared wall. The corridor begins at that edge,
        # rather than painting strips and fingers into the adjoining rooms.
        if asset['id'] in ('floor_canteen', 'floor_laundry',
                           'floor_control_room', 'floor_store_room'):
            h = 429 - y
        elif asset['id'] == 'floor_main_corridor':
            y, h = 429, 601 - 429
        elif asset['id'] in ('floor_exercise_yard', 'floor_utility_room'):
            y, h = 429, y + h - 429
        tile = 'tile_' + asset['id'][6:]
        if tile not in ASSETS:
            tile = 'tile_main_corridor'
        add(asset['id'].replace('_', ' ').title(), point(x + w / 2, y + h),
            c('SpriteRenderer', texture=texture(tile), size=[w / PPU, h / PPU],
              offset=[0, -h / PPU / 2], layer=-30, drawMode='Tiled', tileSize=[1, 1]))

    # A wall is a length with two returns and a solid footprint. Resize the run and its
    # masonry repeats. Openings split geometry, so both sides get complete finished ends.
    horizontal, vertical = {}, {}
    wall_runs = []
    column_runs = []
    for asset in MANIFEST['assets']:
        if asset['category'] != 'walls':
            continue
        x, y = asset['origin_px']
        w, h = asset['width'], asset['height']
        if asset['id'].startswith('wall_exit_anteroom_wall_'):
            # Move this divider's finished front edge south, making both
            # service bays deep enough for the complete 42px terminal.
            h += 24
        is_column = (asset['id'] in ('wall_outer_west', 'wall_outer_east', 'wall_control_room_pier')
                     or 'divider_' in asset['id'] or '_partition_' in asset['id'])
        group, key, span = ((vertical, (x, w), (y, y + h)) if is_column
                            else (horizontal, (y, h), (x, x + w)))
        group.setdefault(key, []).append(span)
    for (y, h), spans in horizontal.items():
        start, end = min(a for a, _ in spans), max(b for _, b in spans)
        holes = [(x1, x2) for x1, y1, x2, y2 in cuts + front_cuts
                 if y1 < y + h and y2 > y]
        if y == 823:
            holes.append((1482, 1556))  # Locked escape gate retains its own geometry.
        for i, (left, right) in enumerate(subtract_interval(start, end, holes)):
            wall_runs.append((left, y, right, y + h))
            add(f'Masonry run {y} {i}', point((left + right) / 2, y + h),
                *wall_parts(right - left, h))
    for (x, w), spans in vertical.items():
        start, end = min(a for a, _ in spans), max(b for _, b in spans)
        holes = [(y1, y2) for x1, y1, x2, y2 in cuts + front_cuts
                 if x1 < x + w and x2 > x]
        # Split solid columns at facades. column_parts joins their visible
        # coping planes above the projected brick face without changing physics.
        crosswalls = [(top, bottom) for left, top, right, bottom in wall_runs
                      if left <= x and right >= x + w]
        # A thirteen-pixel remnant above the north cell facade was the apparent
        # floating tile in the old view. Continue its doorway to the next wall.
        holes = [(min([a] + [bottom for top, bottom in crosswalls if 0 <= a - bottom < 20]),
                  max([b] + [top for top, bottom in crosswalls if 0 <= top - b < 20]))
                 for a, b in holes]
        holes += crosswalls
        for i, (top, bottom) in enumerate(subtract_interval(start, end, holes)):
            column_runs.append((x, top, x + w, bottom))
            add(f'Masonry column {x} {i}', point(x + w / 2, bottom),
                *column_parts(w, top, bottom, crosswalls))

    bindings = {'laundry_folding_table': 'Linen table', 'control_computer_desk': 'Security desk',
                'store_lower_crate': 'Fuse cabinet', 'workshop_tools_rack_west': 'Tool bench',
                'workshop_middle_crate': 'Tool chest', 'workshop_workbench': 'Repair workbench',
                'utility_green_console': 'Power terminal', 'utility_box': 'Emergency exit switch'}
    # The power terminal is named in the builder; keep its reusable puzzle components.
    terminal = next(e for e in scene['entities'] if part(e, 'WirePuzzle') is not None)
    old['Power terminal'] = terminal
    for asset in MANIFEST['assets']:
        if asset['category'] != 'props' or asset['id'] == 'green_exit_gate' or asset['id'] in OMIT_PROPS:
            continue
        key = asset['id']
        x, y = asset['origin_px']
        original_w, original_h = asset['width'], asset['height']
        canonical, w, h = prop_definition(key)
        x += (original_w - w) / 2
        y += original_h - h
        x, y = PLACEMENTS.get(key, (x, y))
        banner_base = None
        if 'banner' in key:
            # Attach the entire flag to an actual facade, including its brackets.
            # Short parapets get a proportionately smaller flag, never a full-size
            # banner suspended over the floor beyond the supporting wall.
            center = asset['origin_px'][0] + original_w / 2
            source_top = asset['origin_px'][1]
            if key in ('exit_banner_west', 'exit_banner_east'):
                # The shallow service parapet has no room for a readable banner.
                # Frame the locked escape gate on the full-height outer facade.
                source_top = 851
            if key == 'dormitory_wall_banner':
                center, source_top = 198, 53
            supports = [(left, top, right, bottom) for left, top, right, bottom in wall_runs
                        if left <= center <= right and top - 20 <= source_top <= bottom + 20]
            if not supports:
                raise ValueError(f'Banner has no supporting wall: {key}')
            left, top, right, bottom = min(supports, key=lambda r: abs(source_top - (r[1] + 26)))
            w, h = BANNER_SIZE
            if bottom - top - min(26, bottom - top) < h + 4:
                raise ValueError(f'Wall face is too short for a standard banner: {key}')
            center = max(left + w / 2 + 2, min(right - w / 2 - 2, center))
            x, y = center - w / 2, top + min(26, bottom - top) + 2
            banner_base = bottom
        if key == 'laundry_switch':
            # This is a wall plate, not a loose object on the laundry floor.
            center = x + w / 2
            supports = [(left, top, right, bottom) for left, top, right, bottom in wall_runs
                        if left <= center <= right and 0 <= top - y <= 100]
            left, top, right, bottom = min(supports, key=lambda r: r[1] - y)
            y = top + 30
            banner_base = bottom
        flat = key in ('corridor_runner', 'arena_court_markings')
        hanging = ('banner' in key or key in ('laundry_switch', 'workshop_rack_top_crate'))
        if not flat and not hanging and key != 'yard_exercise_fence':
            feet = footprint(key)
            fw, fh = [v * PPU for v in feet['size']]
            # Projected artwork may overlap a facade; its floor contact may not enter stone.
            # Resolve in front of or behind the wall, preserving the nearest side of the room.
            for left, top, right, bottom in wall_runs:
                fx = x + w / 2
                fy = y + h - 2 + feet['offset'][1] * PPU
                if (min(fx + fw / 2, right) - max(fx - fw / 2, left) > 2 and
                        min(fy + fh / 2, bottom) - max(fy - fh / 2, top) > 2):
                    shifts = [bottom - (fy - fh / 2) + 2, top - (fy + fh / 2) - 2]
                    y += min(shifts, key=abs)
            # Full silhouettes need lateral clearance too. A bed can have clear
            # feet while its wooden side is still sliced by a column in front.
            for left, top, right, bottom in column_runs:
                if (y < bottom and y + h > top and x < right and x + w > left):
                    x = left - w - 2 if x + w / 2 < (left + right) / 2 else right + 2
        x, y = round(x), round(y)
        name = bindings.get(key)
        entity = old.get(name) if name else None
        if entity:
            used.append(entity)
            entity['components'] = [p for p in entity['components'] if p['type'] not in
                                    ('SpriteRenderer', 'Collider', 'AnimatedSprite')]
            entity['transform']['position'] = point(x + w / 2, y + h - 2)
        else:
            entity = add(key.replace('_', ' ').title(), point(x + w / 2, y + h - 2))
        properties = prop_sprite(key)
        properties.update(ySort=not flat, layer=-20 if flat else 10)
        entity['components'].insert(0, c('SpriteRenderer', **properties))
        if 'banner' in key:
            part(entity, 'SpriteRenderer').update(texture=banner_texture(key),
                size=[w / PPU, h / PPU], offset=[0, (-h / 2 + 2) / PPU])
            # A banner belongs to its supporting wall plane. Its hanging tip must
            # never force it in front of a player already standing on the floor.
            part(entity, 'SpriteRenderer')['sortOffset'] = (banner_base - (y + h - 2) + .5) / PPU
        if key == 'laundry_switch':
            part(entity, 'SpriteRenderer')['sortOffset'] = (banner_base - (y + h - 2) + .5) / PPU
        if key in ('utility_green_console', 'utility_box'):
            # Keep the entire controls above the foreground coping, with their
            # interaction points reachable in the narrow service alcove.
            entity['transform']['position'][1] -= 12 / PPU
            part(entity, 'SpriteRenderer')['sortOffset'] = 12 / PPU
        if key == 'workshop_rack_top_crate':
            # This crate rests on the rack; it has neither a second floor collider nor shadow.
            rack = ASSETS['workshop_tools_rack_east']
            part(entity, 'SpriteRenderer')['sortOffset'] = (
                rack['origin_px'][1] + rack['height'] - (y + h - 2) + .1) / PPU
        if not flat and not hanging:
            feet = footprint(key)
            entity['components'].extend(prop_colliders(key))
            ground_prop(part(entity, 'SpriteRenderer'), feet)
        if 'candle' in key or key in ('kitchen_fireplace', 'canteen_dining_set_west'):
            entity['components'].append(c('Light2D', color='#ffb945ff', radius=1.15,
                                          intensity=.17, offset=[0, -.6], flicker=.16, layer=12))

    positions = {'Explorer': (172, 196), 'Mara': (313, 488), 'Cook Bram': (776, 282),
                 'Ada': (1036, 288), 'Tomas': (618, 678), 'Nurse Iona': (947, 775),
                 'Rowan': (1330, 576), 'Control guard': (1235, 240)}
    roles = dict(zip(positions, ['hero', 'companion', 'cook', 'laundry_worker', 'craftsperson',
                                'nurse', 'yard_worker', 'guard']))
    routes = {'Mara': [(313, 488), (313, 548), (280, 548), (313, 548)],
              'Cook Bram': [(776, 282), (776, 307), (776, 282), (776, 270)],
              'Ada': [(1036, 288), (996, 288), (996, 241), (1036, 241)],
              'Tomas': [(618, 678), (660, 678), (660, 646), (618, 646)],
              'Nurse Iona': [(947, 775), (990, 775), (990, 765), (947, 765)],
              'Rowan': [(1330, 576), (1330, 526), (1320, 526), (1330, 526)],
              'Control guard': [(1235, 240), (1235, 300), (1330, 300), (1330, 455),
                                (1360, 455), (1330, 455), (1330, 300), (1235, 300)]}
    for name, at in positions.items():
        entity = old[name]
        entity['transform']['position'] = point(*at)
        set_character(entity, roles[name])
        path = part(entity, 'NpcPath')
        if path:
            path['waypoints'] = [add(name + ' routine ' + str(i), point(*p))['id']
                                 for i, p in enumerate(routes[name])]
        used.append(entity)
    watch = part(old['Control guard'], 'PatrolWatch')
    watch.update(restrictedMin=point(1162, 99), restrictedMax=point(1414, 337),
                 returnPosition=point(*positions['Explorer']))
    part(old['Control guard'], 'Dialogue')['pages'] = [
        'Free time ends at dusk. Keep clear of the control desk. The room passages are clear.']

    gate = old['Green exit gate']
    gate['transform']['position'] = point(1519, 850)
    set_door(gate, 74 / PPU, automatic=False, height=2.45)
    # Bottom-align the frame with the outer facade. The centered 86px gate used
    # to stop fifteen pixels above the wall base, exposing the black background.
    part(gate, 'SpriteRenderer')['offset'][1] = 15 / PPU
    part(gate, 'SpriteRenderer')['color'] = '#c4f4d4ff'
    gate['components'].append(c('Light2D', color='#55ed7aff', radius=1.55, intensity=.18, layer=12))
    used.append(gate)
    goal = old['South escape passage']
    goal['transform']['position'] = point(1519, 884)
    part(goal, 'Collider')['size'] = [1.7, .55]
    used.append(goal)
    for name in ('Player follow camera', 'Level two story'):
        used.append(old[name])
    camera = part(old['Player follow camera'], 'Camera')
    camera.update(orthographicHeight=VIEW_HEIGHT, pixelsPerUnit=WORLD_DENSITY, boundsMax=BOUNDS)
    old['Player follow camera']['transform']['position'] = point(*positions['Explorer'])
    for asset in MANIFEST['assets']:
        if asset['category'] != 'floors' or asset['id'] == 'floor_exercise_arena':
            continue
        x, y = asset['origin_px']
        title = asset['id'][6:].replace('_', ' ').upper()
        add(title + ' location', [0, 0], c('LocationArea', actor=old['Explorer']['id'], title=title,
            boundsMin=point(x, y), boundsMax=point(x + asset['width'], y + asset['height'])))
    scene['entities'] = used
    add_wall_copings(scene)
    compact_architecture(scene)
    furnish_compact_rooms(scene)
    build_masonry(scene, 2)
    scene['settings']['background'] = '#101823ff'
    print('Reference keep:', len(used), 'entities, native 35px tiles,', BOUNDS, 'world bounds')


def style_level_one(scene):
    character_art()
    export_wall_materials()
    entities = scene['entities']
    floor_map = {'floor': 'tile_dormitory_north', 'corridor_floor': 'tile_main_corridor',
                 'work_floor': 'tile_workshop', 'office_floor': 'tile_control_room'}
    prop_map = {'bed': 'dorm_bed_nw', 'shelf': 'kitchen_shelf_utensils', 'barrel': 'store_barrel',
                'cabinet': 'kitchen_small_cabinet', 'bench': 'workshop_tools_rack_west',
                'craft': 'west_common_table', 'chest': 'west_storage_chest',
                'laundry': 'laundry_basin_1', 'desk': 'west_common_table',
                'table': 'west_common_table', 'plant': 'west_plant',
                'statue': 'yard_stone_planter', 'crates': 'store_middle_crates',
                'banner': 'control_banner', 'rug': 'corridor_runner'}
    # Use the same single-tile wall materials as chapter two, with the facade facing the player.
    wall_map = {'wall': 'module_inner_face', 'wall_vertical': 'module_vertical_light',
                'wall_south': 'module_outer_south_face'}
    keep = []
    for entity in entities:
        if entity['name'] in CHAPTER_ONE_PLACEMENTS:
            entity['transform']['position'] = list(CHAPTER_ONE_PLACEMENTS[entity['name']])
        sprite = part(entity, 'SpriteRenderer')
        if sprite:
            name = Path(sprite.get('texture', '')).stem
            if name.startswith('sign_'):
                continue
            if name in floor_map:
                sprite.update(texture=texture(floor_map[name]), tileSize=[1, 1])
            elif name == 'drain':
                sprite.update(texture=pixel_texture('drain_grate'), size=[64 / PPU, 48 / PPU],
                              offset=[0, -22 / PPU])
                entity['components'] = [p for p in entity['components'] if p['type'] != 'Collider']
                entity['components'].extend(silhouette_colliders('drain_grate'))
            elif name in wall_map:
                # One shared resizable wall object; expose returns only at actual openings.
                vertical = name == 'wall_vertical'
                height = PPU if vertical else 84
                pieces = wall_parts(PPU, height, vertical)
                pieces[0]['properties']['offset'][1] += .5
                pieces[0]['properties']['sortOffset'] = .5
                pieces[1]['properties'].update(size=[1, 1], offset=[0, 0])
                entity['components'] = pieces
                sprite = part(entity, 'SpriteRenderer')
            elif name in prop_map:
                asset = prop_map[name]
                canonical, pw, ph = prop_definition(asset)
                source = ASSETS[canonical]
                width, height = pw / PPU, ph / PPU
                if name in ('banner', 'rug'):
                    sprite['texture'] = prop_sprite(asset)['texture'] if name == 'rug' else texture(asset)
                    if name == 'rug':
                        sprite.update(size=[sprite['size'][0], 45 / PPU], drawMode='Sliced', sliceFill='Tile')
                    else:
                        asset = ASSETS['control_banner']
                        height = BANNER_SIZE[1] / PPU
                        sprite.update(texture=banner_texture(asset['id']),
                                      size=[BANNER_SIZE[0] / PPU, height], offset=[0, .25 - height / 2],
                                      layer=10, ySort=True)
                        entity['transform']['position'][1] = .75
                        sprite['sortOffset'] = .251
                else:
                    # A consistent pixels-per-tile ratio avoids stretching every prop to 1 x 1.5.
                    sprite.update(**prop_sprite(asset))
                    collider = part(entity, 'Collider')
                    if collider:
                        ground_prop(sprite, footprint(asset))
                        entity['components'] = [p for p in entity['components'] if p['type'] != 'Collider']
                        entity['components'].extend(prop_colliders(asset))
        role = {'Explorer': 'hero', 'Mara': 'companion', 'Old prisoner': 'yard_worker', 'Keep guard': 'guard'}.get(entity['name'])
        if role:
            set_character(entity, role)
        camera = part(entity, 'Camera')
        if camera:
            # The north facade projects above the first floor cell. Include the
            # whole coping in the camera bounds instead of cutting it at y=0.
            camera.update(orthographicHeight=VIEW_HEIGHT, pixelsPerUnit=WORLD_DENSITY,
                          boundsMin=[0, -1.4])
        if part(entity, 'StateGate') and entity['name'] != 'Drain grate':
            collider = part(entity, 'Collider')
            side = collider['size'][1] > collider['size'][0]
            set_door(entity, collider['size'][0], collider['size'][1], automatic=False,
                     height=collider['size'][1] if side else None, side=side)
        if entity['name'] == 'Fixed wall torch':
            entity['components'].append(c('Light2D', radius=1.25, color='#ffb24dff', intensity=.16,
                                          offset=[0, -.25], flicker=.18, layer=12))
        if entity['name'] == 'Brass doorway threshold':
            # A flush strip of brass, rather than a stretched picture of upright bars.
            sprite.update(texture='builtin:white', size=[sprite['size'][0], .08],
                          color='#b99855a0', drawMode='Simple')
        keep.append(entity)
    # Keep the entire prop, including frame edges, clear of side walls. The
    # source canvas is deliberately used here instead of the narrower feet box.
    sidewalls = []
    frontwalls = []
    for wall in keep:
        sprite = part(wall, 'SpriteRenderer')
        if wall['name'].startswith('Masonry ') and sprite and sprite['texture'].endswith('wall-column.png'):
            x, y = wall['transform']['position']
            sidewalls.append((x - .5, y - .5, x + .5, y + .5))
        elif wall['name'].startswith('Masonry ') and sprite and sprite['texture'].endswith('wall-run.png'):
            x, y = wall['transform']['position']
            w, h = sprite['size']
            cy = y + sprite['offset'][1]
            frontwalls.append((x - w / 2, cy - h / 2, x + w / 2, cy + h / 2,
                               y + sprite.get('sortOffset', 0)))
    for prop in keep:
        sprite = part(prop, 'SpriteRenderer')
        if (not sprite or not part(prop, 'Collider') or prop['name'].startswith('Masonry ')
                or not sprite['texture'].startswith(('assets/castle/pixel/', 'assets/castle/props/', 'assets/castle/reference/'))):
            continue
        x, y = prop['transform']['position']
        w, h = sprite['size']
        dx, dy = sprite.get('offset', [0, 0])
        for left, top, right, bottom in sidewalls:
            if (y + dy - h / 2 < bottom and y + dy + h / 2 > top
                    and x + dx - w / 2 < right and x + dx + w / 2 > left):
                center = left - w / 2 - 2 / PPU if x + dx < (left + right) / 2 else right + w / 2 + 2 / PPU
                x = center - dx
        for left, top, right, bottom, depth in frontwalls:
            if (y + sprite.get('sortOffset', 0) < depth and x + dx - w / 2 < right
                    and x + dx + w / 2 > left and y + dy - h / 2 < bottom and y + dy + h / 2 > top):
                y = top - dy - h / 2 - 2 / PPU
        prop['transform']['position'] = [x, y]
    # Full silhouettes can reach stone above the old shallow feet boxes. Resolve
    # their actual compound bounds against physical cells before merging walls.
    stone = []
    for wall in keep:
        if not wall['name'].startswith('Masonry '):
            continue
        wx, wy = wall['transform']['position']
        for shape in wall['components']:
            if shape['type'] == 'Collider':
                p = shape['properties']
                dx, dy = p.get('offset', [0, 0])
                w, h = p['size']
                stone.append((wx + dx - w / 2, wy + dy - h / 2,
                              wx + dx + w / 2, wy + dy + h / 2))
    for prop in keep:
        sprite = part(prop, 'SpriteRenderer')
        if (not sprite or not sprite['texture'].startswith('assets/castle/pixel/')
                or part(prop, 'StateGate')):
            continue
        shapes = [p['properties'] for p in prop['components'] if p['type'] == 'Collider']
        if not shapes:
            continue
        x, y = prop['transform']['position']
        lo = [min(p.get('offset', [0, 0])[i] - p['size'][i] / 2 for p in shapes) for i in (0, 1)]
        hi = [max(p.get('offset', [0, 0])[i] + p['size'][i] / 2 for p in shapes) for i in (0, 1)]
        for _ in range(8):
            moved = False
            for left, top, right, bottom in stone:
                if min(x + hi[0], right) - max(x + lo[0], left) <= .001 or min(y + hi[1], bottom) - max(y + lo[1], top) <= .001:
                    continue
                options = [(left - x - hi[0] - 2 / PPU, 0), (right - x - lo[0] + 2 / PPU, 0),
                           (0, top - y - hi[1] - 2 / PPU), (0, bottom - y - lo[1] + 2 / PPU)]
                dx, dy = min(options, key=lambda v: abs(v[0]) + abs(v[1]))
                x, y = x + dx, y + dy
                moved = True
            if not moved:
                break
        prop['transform']['position'] = [x, y]
    # Merge adjacent front-facing cells into extendable runs with one pair of returns.
    rows = {}
    for entity in keep:
        sprite = part(entity, 'SpriteRenderer')
        if entity['name'].startswith('Masonry ') and sprite['texture'].endswith('wall-run.png'):
            x, y = entity['transform']['position']
            rows.setdefault(y, []).append((x, entity))
    removed = set()
    for y, cells in rows.items():
        cells.sort(key=lambda item: item[0])
        runs = []
        for x, entity in cells:
            if not runs or x - runs[-1][-1][0] > 1.01:
                runs.append([])
            runs[-1].append((x, entity))
        for run in runs:
            width = len(run)
            first = run[0][1]
            first['transform']['position'][0] = (run[0][0] + run[-1][0]) / 2
            part(first, 'SpriteRenderer')['size'][0] = width
            part(first, 'SpriteRenderer')['shadowSize'][0] = width + .18
            part(first, 'Collider')['size'][0] = width
            removed.update(entity['id'] for _, entity in run[1:])
    keep = [entity for entity in keep if entity['id'] not in removed]
    # Join vertical cells too. Their old 35px quads restarted stone joints at
    # every cell and had no finished ends. Clip only the hidden intersection
    # with a horizontal facade; the original continuous physics stays intact.
    columns = {}
    facades = []
    for entity in keep:
        sprite = part(entity, 'SpriteRenderer')
        if not entity['name'].startswith('Masonry ') or not sprite:
            continue
        x, y = entity['transform']['position']
        if sprite['texture'].endswith('wall-column.png'):
            columns.setdefault(x, []).append((y, entity))
        elif sprite['texture'].endswith('wall-run.png'):
            w, h = sprite['size']
            cy = y + sprite['offset'][1]
            facades.append((x - w / 2, cy - h / 2, x + w / 2, cy + h / 2))
    removed = set()
    next_id = max(int(e['id'], 16) for e in keep) + 1
    joined = []
    for x, cells in columns.items():
        cells.sort(key=lambda item: item[0])
        runs = []
        for y, entity in cells:
            if not runs or y - runs[-1][-1][0] > 1.01:
                runs.append([])
            runs[-1].append((y, entity))
        for run in runs:
            top, bottom = run[0][0] - .5, run[-1][0] + .5
            crossings = [(a, b) for left, a, right, b in facades if left < x + .5 and right > x - .5]
            visible = subtract_interval(top, bottom, crossings)
            removed.update(entity['id'] for _, entity in run)
            for index, (a, b) in enumerate(visible):
                entity = deepcopy(run[0][1])
                entity.update(id=f'{next_id:016x}', name=f'Masonry joined column {x} {a}')
                next_id += 1
                entity['transform']['position'] = [x, b]
                entity['components'] = column_parts(PPU, a * PPU, b * PPU,
                                                    [(start * PPU, end * PPU) for start, end in crossings])
                if index == 0:
                    part(entity, 'Collider').update(size=[1, bottom - top],
                                                    offset=[0, (top + bottom) / 2 - b])
                else:
                    entity['components'] = [p for p in entity['components'] if p['type'] != 'Collider']
                joined.append(entity)
    scene['entities'] = [entity for entity in keep if entity['id'] not in removed] + joined
    add_wall_copings(scene)
    furnish_chapter_one(scene)
    build_masonry(scene, 1)
    scene['settings']['background'] = '#101823ff'
