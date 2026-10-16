"""Shared castle presentation from the user's editable layers and original animated doors.

The supplied asset manifest is data, not build instructions. Artwork stays at its native
35 pixels per tile; collision footprints and connecting passages are authored here.
Both chapter builders call these functions, so rebuilding cannot restore the old style.
"""
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/castle'
REFERENCE = ART / 'reference'
MANIFEST = json.loads((REFERENCE / 'manifest.json').read_text())
ASSETS = {a['id']: a for a in MANIFEST['assets']}
PPU = 35
VIEW_HEIGHT = 8.4
BOUNDS = [1591 / PPU, 883 / PPU]
DOOR_FRAMES = 8


def c(kind, **properties):
    return dict(type=kind, properties=properties)


def part(entity, kind):
    return next((p['properties'] for p in entity['components'] if p['type'] == kind), None)


def point(x, y):
    return [(x - 40) / PPU, (y - 25) / PPU]


def texture(asset):
    return 'assets/castle/reference/' + ASSETS[asset]['file']


def door_art():
    """Slice the generated closed master and hinge its leaves into an eight-frame strip."""
    master = Image.open(ART / 'source/double-door.png').convert('RGBA')
    master = master.crop(master.getbbox()).resize((112, 84), Image.Resampling.NEAREST)
    # Keep the masonry jamb fixed while two separate leaves rotate about the outside hinges.
    interior = (16, 13, 96, 83)
    jamb = master.copy()
    ImageDraw.Draw(jamb).rectangle(interior, fill=(0, 0, 0, 0))
    left = master.crop((16, 13, 56, 83))
    right = master.crop((56, 13, 96, 83))
    sheet = Image.new('RGBA', (112 * DOOR_FRAMES, 84))
    for frame in range(DOOR_FRAMES):
        amount = frame / (DOOR_FRAMES - 1)
        width = max(3, round(40 * math.cos(amount * math.pi / 2)))
        face = Image.new('RGBA', (112, 84))
        for leaf, x in [(left, 16), (right, 96 - width)]:
            panel = leaf.resize((width, 70), Image.Resampling.NEAREST)
            face.alpha_composite(panel, (x, 13 + round(2 * amount)))
        face.alpha_composite(jamb)
        sheet.alpha_composite(face, (frame * 112, 0))
    sheet.save(ART / 'double-door.png')


def set_character(entity, role):
    sprite = part(entity, 'SpriteRenderer')
    sprite.update(texture='assets/level2/' + role + '.png', size=[1.3, 1.7875],
                  offset=[0, -.75], layer=10, ySort=True,
                  shadowSize=[1.0, .42], shadowOffset=[.08, .06], shadowColor='#080e2496')
    animation = part(entity, 'AnimatedSprite')
    if animation:
        animation['animation'] = 'assets/level2/' + role + '.ykanim'
    if not part(entity, 'CharacterMotion'):
        entity['components'].append(c('CharacterMotion', idleHeight=.018, stepHeight=.02,
                                      animateSteps=False, flipWithMovement=False))
    if not part(entity, 'TopDownController'):
        body = part(entity, 'RigidBody')
        if body:
            body.update(type='Kinematic', gravityScale=0, fixedRotation=True)
        part(entity, 'Collider')['layer'] = 'Solid'
    dialogue = part(entity, 'Dialogue')
    if dialogue:
        dialogue['portrait'] = 'assets/level2/portrait_' + role + '.png'
    # Load explicit dependencies before controllers, so no default collider is inserted ahead
    # of the authored feet collider (Collider deliberately allows multiple shapes).
    order = {'SpriteRenderer': 0, 'Collider': 1, 'RigidBody': 2, 'PlayerInput': 3,
             'Inventory': 4, 'Crafter': 5, 'AnimatedSprite': 6, 'CharacterMotion': 7,
             'NpcPath': 8, 'TopDownController': 8, 'Interactor': 9}
    entity['components'].sort(key=lambda p: order.get(p['type'], 20))


def set_door(entity, width, depth=.55, automatic=True, height=None):
    entity['components'] = [p for p in entity['components'] if p['type'] != 'AnimatedSprite']
    sprite = part(entity, 'SpriteRenderer')
    sprite.clear()
    sprite.update(texture='assets/castle/double-door.png', size=[width + .32, height or 1.9],
                  layer=11, offset=[0, -.18], columns=DOOR_FRAMES, ySort=True)
    collider = part(entity, 'Collider')
    collider.update(size=[width, depth], offset=[0, 0], layer='Solid', friction=0)
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


def wall_rectangles(image, cell=4):
    """Merge a coarse opaque mask into rectangles, retaining every authored passage."""
    width, height = image.size
    mask = image.getchannel('A')
    active = {}
    rectangles = []
    for y in range(0, height + cell, cell):
        runs = []
        start = None
        for x in range(0, width + cell, cell):
            opaque = (y < height and x < width and
                      mask.getpixel((min(x + cell // 2, width - 1),
                                     min(y + cell // 2, height - 1))) > 192)
            if opaque and start is None:
                start = x
            if not opaque and start is not None:
                runs.append((start, min(x, width)))
                start = None
        following = {}
        for run in runs:
            following[run] = active.pop(run, [run[0], y, run[1] - run[0], 0])
            following[run][3] += min(cell, height - y)
        rectangles.extend(active.values())
        active = following
    return rectangles


def style_level_two(scene):
    door_art()
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
    cuts = [(394, 443, 426, 507), (791, 443, 825, 507),
            (1130, 443, 1163, 507), (1413, 443, 1447, 507),
            (1298, 336, 1362, 430), (1494, 336, 1552, 430)]
    for i, (x1, y1, x2, y2) in enumerate(cuts):
        add('Stone passage ' + str(i), point((x1 + x2) / 2, (y1 + y2) / 2),
            c('SpriteRenderer', texture=texture('tile_main_corridor'),
              size=[(x2 - x1) / PPU, (y2 - y1) / PPU], layer=-29,
              drawMode='Tiled', tileSize=[1, 1]))
    derived = ART / 'walls'
    derived.mkdir(exist_ok=True)
    for asset in MANIFEST['assets']:
        if asset['category'] not in ('floors', 'walls'):
            continue
        x, y = asset['origin_px']
        w, h = asset['width'], asset['height']
        sprite = c('SpriteRenderer', texture=texture(asset['id']), size=[w / PPU, h / PPU],
                   offset=[0, -h / PPU / 2], layer=-30 if asset['category'] == 'floors' else 10,
                   ySort=asset['category'] == 'walls')
        parts = [sprite]
        if asset['category'] == 'walls':
            image = Image.open(REFERENCE / asset['file']).convert('RGBA')
            draw = ImageDraw.Draw(image)
            for x1, y1, x2, y2 in cuts:
                if x1 < x + w and x2 > x and y1 < y + h and y2 > y:
                    draw.rectangle((x1 - x, y1 - y, x2 - x - 1, y2 - y - 1), fill=(0, 0, 0, 0))
            image.save(derived / (asset['id'] + '.png'))
            sprite['properties']['texture'] = 'assets/castle/walls/' + asset['id'] + '.png'
            for rx, ry, rw, rh in wall_rectangles(image):
                parts.append(c('Collider', layer='Solid', friction=0,
                               size=[rw / PPU, rh / PPU],
                               offset=[(rx + rw / 2 - w / 2) / PPU, (ry + rh / 2 - h) / PPU]))
        add(asset['id'].replace('_', ' ').title(), point(x + w / 2, y + h), *parts)

    bindings = {'laundry_folding_table': 'Linen table', 'control_computer_desk': 'Security desk',
                'store_lower_crate': 'Fuse cabinet', 'workshop_tools_rack_west': 'Tool bench',
                'workshop_middle_crate': 'Tool chest', 'workshop_workbench': 'Repair workbench',
                'utility_green_console': 'Power terminal', 'utility_box': 'Emergency exit switch'}
    # The power terminal is named in the builder; keep its reusable puzzle components.
    terminal = next(e for e in scene['entities'] if part(e, 'WirePuzzle') is not None)
    old['Power terminal'] = terminal
    for asset in MANIFEST['assets']:
        if asset['category'] != 'props' or asset['id'] == 'green_exit_gate':
            continue
        key = asset['id']
        x, y = asset['origin_px']
        w, h = asset['width'], asset['height']
        name = bindings.get(key)
        entity = old.get(name) if name else None
        if entity:
            used.append(entity)
            entity['components'] = [p for p in entity['components'] if p['type'] not in
                                    ('SpriteRenderer', 'Collider', 'AnimatedSprite')]
            entity['transform']['position'] = point(x + w / 2, y + h - 2)
        else:
            entity = add(key.replace('_', ' ').title(), point(x + w / 2, y + h - 2))
        flat = key in ('corridor_runner', 'arena_court_markings')
        hanging = 'banner' in key or 'linen_rail' in key
        entity['components'].insert(0, c('SpriteRenderer', texture=texture(key), size=[w / PPU, h / PPU],
                                       offset=[0, (-h / 2 + 2) / PPU], ySort=not flat,
                                       layer=-20 if flat else (12 if hanging else 10)))
        if key in ('utility_green_console', 'utility_box'):
            # Wall-mounted controls are reached in front of the facade, not through its body.
            entity['transform']['position'][1] -= 12 / PPU
            part(entity, 'SpriteRenderer')['offset'][1] += 12 / PPU
            part(entity, 'SpriteRenderer')['layer'] = 12
            hanging = True
        if not flat and not hanging:
            depth = min(32, h * (.68 if 'bed' in key or 'dining_set' in key else .33))
            entity['components'].append(c('Collider', layer='Solid', friction=0,
                size=[max(10, w - 12) / PPU, depth / PPU], offset=[0, -depth / PPU / 2]))
        if key == 'yard_exercise_fence':
            entity['components'] = [p for p in entity['components'] if p['type'] != 'Collider']
            for cw, ch, ox, oy in [(w - 15, 9, 0, -h + 12), (w - 15, 9, 0, -4),
                                   (9, h - 18, -w / 2 + 8, -h / 2),
                                   (9, 65, w / 2 - 8, -h + 42), (9, 65, w / 2 - 8, -36)]:
                entity['components'].append(c('Collider', size=[cw / PPU, ch / PPU],
                    offset=[ox / PPU, oy / PPU], layer='Solid', friction=0))
        if key == 'utility_energy_device_pipes':
            part(entity, 'Collider').update(size=[1.2, .6], offset=[.5, -.3])
        if 'candle' in key or key in ('kitchen_fireplace', 'canteen_dining_set_west'):
            entity['components'].append(c('Light2D', color='#ffb945ff', radius=1.15,
                                          intensity=.17, offset=[0, -.6], flicker=.16, layer=12))

    positions = {'Explorer': (172, 196), 'Mara': (313, 488), 'Cook Bram': (776, 264),
                 'Ada': (1036, 288), 'Tomas': (618, 678), 'Nurse Iona': (947, 775),
                 'Rowan': (1352, 576), 'Control guard': (1235, 240)}
    roles = dict(zip(positions, ['hero', 'companion', 'cook', 'laundry_worker', 'craftsperson',
                                'nurse', 'yard_worker', 'guard']))
    routes = {'Mara': [(313, 488), (313, 548), (280, 548), (313, 548)],
              'Cook Bram': [(776, 264), (776, 307), (776, 264), (776, 242)],
              'Ada': [(1036, 288), (996, 288), (996, 241), (1036, 241)],
              'Tomas': [(618, 678), (660, 678), (660, 646), (618, 646)],
              'Nurse Iona': [(947, 775), (990, 775), (990, 765), (947, 765)],
              'Rowan': [(1352, 576), (1360, 535), (1370, 535), (1360, 535)],
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
        'Free time ends at dusk. Keep clear of the control desk. Passage doors open as you approach.']

    doors = [('North cell door', 148, 260, 69, 60), ('North east cell door', 321, 260, 63, 60),
             ('South cell door', 144, 656, 61, 57), ('South east cell door', 320, 656, 61, 57),
             ('Canteen door', 614, 380, 58, 86), ('Laundry door', 961, 380, 52, 86),
             ('Control door', 1330, 383, 64, 92), ('Stores door', 1523, 383, 58, 91),
             ('Workshop door', 614, 556, 58, 90), ('Infirmary door', 961, 556, 52, 90),
             ('Utility door', 1523, 643, 57, 60), ('Exit anteroom door', 1523, 738, 55, 48)]
    for name, x, y, w, h in doors:
        door = old.get(name)
        if door is None:
            door = add(name, point(x, y), c('SpriteRenderer'), c('Collider'), c('StateGate'))
        else:
            used.append(door)
            door['transform']['position'] = point(x, y)
        set_door(door, w / PPU, height=h / PPU)
    gate = old['Green exit gate']
    gate['transform']['position'] = point(1519, 850)
    set_door(gate, 74 / PPU, automatic=False, height=2.45)
    part(gate, 'SpriteRenderer')['color'] = '#7ce6a0ff'
    gate['components'].append(c('Light2D', color='#55ed7aff', radius=1.55, intensity=.18, layer=12))
    used.append(gate)
    goal = old['South escape passage']
    goal['transform']['position'] = point(1519, 884)
    part(goal, 'Collider')['size'] = [1.7, .55]
    used.append(goal)
    for name in ('Player follow camera', 'Level two story'):
        used.append(old[name])
    camera = part(old['Player follow camera'], 'Camera')
    camera.update(orthographicHeight=VIEW_HEIGHT, boundsMax=BOUNDS)
    old['Player follow camera']['transform']['position'] = point(*positions['Explorer'])
    for asset in MANIFEST['assets']:
        if asset['category'] != 'floors' or asset['id'] == 'floor_exercise_arena':
            continue
        x, y = asset['origin_px']
        title = asset['id'][6:].replace('_', ' ').upper()
        add(title + ' location', [0, 0], c('LocationArea', actor=old['Explorer']['id'], title=title,
            boundsMin=point(x, y), boundsMax=point(x + asset['width'], y + asset['height'])))
    scene['entities'] = used
    scene['settings']['background'] = '#101823ff'
    print('Reference keep:', len(used), 'entities, native 35px tiles,', BOUNDS, 'world bounds')


def style_level_one(scene):
    door_art()
    entities = scene['entities']
    next_id = max(int(e['id'], 16) for e in entities) + 1
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
        sprite = part(entity, 'SpriteRenderer')
        if sprite:
            name = Path(sprite.get('texture', '')).stem
            if name.startswith('sign_'):
                continue
            if name in floor_map:
                sprite.update(texture=texture(floor_map[name]), tileSize=[1, 1])
            elif name in wall_map:
                sprite['texture'] = texture(wall_map[name])
            elif name in prop_map:
                asset = prop_map[name]
                source = ASSETS[asset]
                width, height = source['width'] / PPU, source['height'] / PPU
                if name in ('banner', 'rug'):
                    sprite['texture'] = texture(asset)
                    if name == 'rug':
                        sprite['size'][1] = .55
                else:
                    # A consistent pixels-per-tile ratio avoids stretching every prop to 1 x 1.5.
                    sprite.update(texture=texture(asset), size=[width, height],
                                  offset=[0, -height / 2 + .12], layer=10, ySort=True)
                    collider = part(entity, 'Collider')
                    if collider:
                        depth = min(.8, height * .38)
                        collider.update(size=[width * .78, depth], offset=[0, -depth / 2 + .12])
        role = {'Explorer': 'hero', 'Mara': 'companion', 'Old prisoner': 'yard_worker', 'Keep guard': 'guard'}.get(entity['name'])
        if role:
            set_character(entity, role)
        camera = part(entity, 'Camera')
        if camera:
            camera['orthographicHeight'] = VIEW_HEIGHT
        if part(entity, 'StateGate') and entity['name'] != 'Drain grate':
            collider = part(entity, 'Collider')
            set_door(entity, collider['size'][0], collider['size'][1], automatic=False)
        if entity['name'] == 'Fixed wall torch':
            entity['components'].append(c('Light2D', radius=1.25, color='#ffb24dff', intensity=.16,
                                          offset=[0, -.25], flicker=.18, layer=12))
        keep.append(entity)
    # Doorways stay where the chapter's puzzles and patrols expect them.
    for name, x, y in [('Cell door', 4, 7.5), ('Stores door', 10, 7.5), ('Office door', 17, 7.5),
                       ('Workshop door', 8, 11.5), ('Laundry door', 16, 11.5), ('Archive door', 26, 7.5)]:
        door = dict(id=f'{next_id:016x}', name=name,
                    transform=dict(position=[x, y], rotation=0, scale=[1, 1]),
                    components=[c('SpriteRenderer'), c('Collider'), c('StateGate')])
        next_id += 1
        set_door(door, 2)
        keep.append(door)
    scene['entities'] = keep
    scene['settings']['background'] = '#101823ff'
