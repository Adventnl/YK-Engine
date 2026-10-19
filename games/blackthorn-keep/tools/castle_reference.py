"""Shared castle presentation from the user's editable layers and original animated doors.

The supplied asset manifest is data, not build instructions. Artwork stays at its native
35 pixels per tile; collision footprints and connecting passages are authored here.
Both chapter builders call these functions, so rebuilding cannot restore the old style.
"""
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw
from art_import import generated_objects, silhouette

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/castle'
REFERENCE = ART / 'reference'
MANIFEST = json.loads((REFERENCE / 'manifest.json').read_text())
ASSETS = {a['id']: a for a in MANIFEST['assets']}
PPU = 35
VIEW_HEIGHT = 11.6
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
    return 'assets/castle/reference/' + ASSETS[asset]['file']


def door_art(width=64, height=72, side=False):
    """Bake doors on the scenery's pixel grid, with fixed jambs and isolated leaves.

    Each opening gets an integer-pixel atlas: the runtime never squashes a detailed
    112px door into a narrow opening. Frame pixels cannot retain fragments of a leaf.
    """
    output = ART / 'doors'
    output.mkdir(exist_ok=True)
    path = output / f'{"side" if side else "front"}-{width}x{height}.png'
    master = Image.open(ART / 'source/door-simple.png').convert('RGBA')
    bounds = master.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
    master = master.crop(bounds).resize((64, 64), Image.Resampling.NEAREST)
    # The new master's jambs are eight pixels wide, with a nine-pixel lintel.
    panel = master.crop((9, 10, 55, 63))
    jamb = Image.new('RGBA', (width, height))
    if side:
        cap = Image.open(REFERENCE / ASSETS['module_inner_cap']['file']).convert('RGBA')
        cap = cap.resize((width, 9), Image.Resampling.NEAREST)
        jamb.alpha_composite(cap)
        jamb.alpha_composite(cap, (0, height - 9))
        interior = (0, 9, width, height - 9)
    else:
        jamb.alpha_composite(master.crop((0, 0, 64, 9)).resize((width, 9), Image.Resampling.NEAREST))
        for left, x in [(True, 0), (False, width - 8)]:
            strip = master.crop((0 if left else 56, 9, 8 if left else 64, 64))
            jamb.alpha_composite(strip.resize((8, height - 9), Image.Resampling.NEAREST), (x, 9))
        interior = (8, 9, width - 8, height)
    x1, y1, x2, y2 = interior
    iw, ih = x2 - x1, y2 - y1
    closed = panel.resize((iw, ih), Image.Resampling.NEAREST)
    # Side frames are separate scene pieces so the top jamb shares the supporting
    # wall's depth instead of covering banners with a whole doorway-sized quad.
    frame_height = ih if side else height
    sheet = Image.new('RGBA', (width * DOOR_FRAMES, frame_height))
    for frame in range(DOOR_FRAMES):
        amount = frame / (DOOR_FRAMES - 1)
        face = Image.new('RGBA', (width, height))
        if side:
            extent = max(1, round(ih / 2 * math.cos(amount * math.pi / 2)))
            for leaf, y in [(closed.crop((0, 0, iw, ih // 2)), y1),
                            (closed.crop((0, ih // 2, iw, ih)), y2 - extent)]:
                face.alpha_composite(leaf.resize((iw, extent), Image.Resampling.NEAREST), (x1, y))
        else:
            extent = max(1, round(iw / 2 * math.cos(amount * math.pi / 2)))
            for leaf, x in [(closed.crop((0, 0, iw // 2, ih)), x1),
                            (closed.crop((iw // 2, 0, iw, ih)), x2 - extent)]:
                face.alpha_composite(leaf.resize((extent, ih), Image.Resampling.NEAREST), (x, y1))
        if side:
            face = face.crop((0, y1, width, y2))
        else:
            face.alpha_composite(jamb)
        sheet.alpha_composite(face, (frame * width, 0))
    sheet.save(path)
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


def banner_art():
    """Export clean cloth silhouettes; source extractions contain attached floor pixels."""
    directory = ART / 'fixtures'
    directory.mkdir(exist_ok=True)
    master = silhouette(Image.open(ART / 'source/banner-clean.png').convert('RGBA'))
    for key, asset in ASSETS.items():
        if 'banner' not in key:
            continue
        w, h = asset['width'], asset['height']
        scale = min(min(w - 4, 44) / master.width, min(h - 4, 68) / master.height)
        flag = master.resize((round(master.width * scale), round(master.height * scale)), Image.Resampling.NEAREST)
        image = Image.new('RGBA', (w, h))
        image.alpha_composite(flag, ((w - flag.width) // 2, 2))
        image.save(directory / (key + '.png'))


def banner_texture(key):
    return 'assets/castle/fixtures/' + key + '.png'


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
    part(entity, 'Collider').update(size=[.62, .32], offset=[0, 0])
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
    sprite.update(texture=door_art(*pixels, side=side), size=[pixels[0] / PPU, (pixels[1] - 18 if side else pixels[1]) / PPU],
                  layer=10, offset=[0, 0], sortOffset=0 if side else pixels[1] / PPU / 2,
                  columns=DOOR_FRAMES, ySort=True)
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
    character_art()
    banner_art()
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
    cuts = [(394, 423, 426, 511), (791, 423, 825, 511),
            (1130, 423, 1163, 511), (1413, 423, 1447, 511),
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
              size=[(x2 - x1) / PPU, (y2 - y1) / PPU], layer=-29,
              drawMode='Tiled', tileSize=[1, 1]))
    derived = ART / 'walls'
    derived.mkdir(exist_ok=True)

    def wall_pieces(name, image, x, y, vertical=None, collidable=True):
        # A vertical wall's depth changes along its length. Giving the whole column
        # its bottom-most depth makes every section cover actors and wall fixtures.
        w, h = image.size
        is_vertical = h > w if vertical is None else vertical
        cell_height = PPU if is_vertical else h
        rows = math.ceil(h / cell_height)
        atlas = Image.new('RGBA', (w, rows * cell_height))
        atlas.alpha_composite(image)
        path = derived / f'{name}.png'
        atlas.save(path)
        for top in range(0, atlas.height, cell_height):
            bottom = top + cell_height
            piece = atlas.crop((0, top, w, bottom))
            if not piece.getchannel('A').getbbox():
                continue
            ph = bottom - top
            parts = [c('SpriteRenderer', texture=path.relative_to(ROOT).as_posix(),
                       size=[w / PPU, ph / PPU], offset=[0, -ph / PPU / 2],
                       rows=rows, frame=top // cell_height, layer=10, ySort=True,
                       sortOffset=(piece.getchannel('A').getbbox()[3] - ph) / PPU - .001 if is_vertical else 0)]
            for rx, ry, rw, rh in wall_rectangles(piece) if collidable else []:
                parts.append(c('Collider', layer='Solid', friction=0,
                               size=[rw / PPU, rh / PPU],
                               offset=[(rx + rw / 2 - w / 2) / PPU, (ry + rh / 2 - ph) / PPU]))
            add(name.replace('_', ' ').title() + f' section {top}', point(x + w / 2, y + bottom), *parts)

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
            if h > w:
                # Reconstructed columns contain opaque scraps of floor and runner
                # in their old gaps. Build clean stone from the supplied module;
                # passages are exclusively the authored cuts below.
                tile = Image.open(REFERENCE / ASSETS['module_vertical_light']['file']).convert('RGBA')
                tile = tile.resize((w, PPU), Image.Resampling.NEAREST)
                image = Image.new('RGBA', (w, h))
                for py in range(0, h, PPU):
                    image.alpha_composite(tile.crop((0, 0, w, min(PPU, h - py))), (0, py))
            draw = ImageDraw.Draw(image)
            for x1, y1, x2, y2 in cuts + front_cuts:
                if x1 < x + w and x2 > x and y1 < y + h and y2 > y:
                    draw.rectangle((x1 - x, y1 - y, x2 - x - 1, y2 - y - 1), fill=(0, 0, 0, 0))
            wall_pieces(asset['id'], image, x, y)
        else:
            add(asset['id'].replace('_', ' ').title(), point(x + w / 2, y + h), *parts)
    # All ordinary front doors share one silhouette and one native pixel scale.
    # Complete the larger source apertures with stone surrounds. Their lintels
    # are overhead artwork; only the side piers have ground collision.
    for (name, x, y, w, h), hole in zip(FRONT_DOORS, front_cuts):
        left = min(math.floor(x - w / 2), hole[0])
        right = max(math.ceil(x + w / 2), hole[2])
        top = math.ceil(y + h / 2) - h
        samples = [a for a in MANIFEST['assets'] if a['category'] == 'walls'
                   and a['height'] == h and a['width'] > h]
        source = max(samples, key=lambda a: a['width'])
        stone = Image.open(REFERENCE / source['file']).convert('RGBA')
        strip = stone.crop((stone.width // 2 - 17, 0, stone.width // 2 + 18, h))
        surround = Image.new('RGBA', (right - left, h))
        for px in range(0, surround.width, PPU):
            surround.alpha_composite(strip.crop((0, 0, min(PPU, surround.width - px), h)), (px, 0))
        ImageDraw.Draw(surround).rectangle((hole[0] - left, hole[1] - top,
                                           hole[2] - left - 1, hole[3] - top - 1), fill=(0, 0, 0, 0))
        wall_pieces(name.lower().replace(' ', '_') + '_surround', surround, left, top,
                    vertical=False, collidable=False)
        for edge, start, end in [('west', left, hole[0]), ('east', hole[2], right)]:
            if end > start:
                add(name + ' ' + edge + ' pier', point((start + end) / 2, top + h / 2),
                    c('Collider', size=[(end - start) / PPU, h / PPU], layer='Solid', friction=0))
    # Extraction seams are stone joints, not tiny unwalkable floor slits.
    for name, x, y, w, h in [('control_joint', 1131, 252, 31, 2)]:
        tile = Image.open(REFERENCE / ASSETS['module_vertical_light']['file']).convert('RGBA')
        seal = Image.new('RGBA', (w, h))
        for px in range(0, w, tile.width):
            seal.alpha_composite(tile.crop((0, 0, min(tile.width, w - px), h)), (px, 0))
        wall_pieces(name, seal, x, y)

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
        if key == 'yard_stone_planter':
            x -= 14
            y += 66
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
                                       layer=-20 if flat else 10))
        if 'banner' in key:
            part(entity, 'SpriteRenderer')['texture'] = banner_texture(key)
            # A banner belongs to its supporting wall plane. Its hanging tip must
            # never force it in front of a player already standing on the floor.
            supports = [a for a in MANIFEST['assets'] if a['category'] == 'walls'
                        and a['width'] > a['height']
                        and a['origin_px'][0] <= x + w / 2 <= a['origin_px'][0] + a['width']
                        and a['origin_px'][1] <= y <= a['origin_px'][1] + a['height']]
            if supports:
                base = max(a['origin_px'][1] + a['height'] for a in supports)
                part(entity, 'SpriteRenderer')['sortOffset'] = (base - (y + h - 2) + .5) / PPU
        if key in ('utility_green_console', 'utility_box'):
            # Wall-mounted controls are reached in front of the facade, not through its body.
            entity['transform']['position'][1] -= 12 / PPU
            part(entity, 'SpriteRenderer')['offset'][1] += 12 / PPU
            part(entity, 'SpriteRenderer')['sortOffset'] = 12 / PPU
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
        'Free time ends at dusk. Keep clear of the control desk. The room passages are clear.']

    # Ordinary passages stay open, with no door sprites, gates or hidden jamb colliders.
    # Keep the stone caps at the ends of the east/west wall openings.
    for name, x1, y1, x2, y2 in [
            ('Common corridor door', *cuts[0]), ('Middle corridor door', *cuts[1]),
            ('Yard corridor door', *cuts[2]), ('Utility corridor door', *cuts[3]),
            ('North cell connecting door', *cuts[6]), ('South cell connecting door', *cuts[7])]:
        w = x2 - x1
        for suffix, top in [('north', y1 - 9), ('south', y2)]:
            add(name + ' ' + suffix + ' cap', point((x1 + x2) / 2, top + 9),
                c('SpriteRenderer', texture=texture('module_vertical_light'),
                  size=[w / PPU, 9 / PPU], offset=[0, -4.5 / PPU],
                  drawMode='Tiled', tileSize=[w / PPU, 1], layer=10, ySort=True,
                  sortOffset=-9 / PPU if suffix == 'north' else 0))
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
    character_art()
    banner_art()
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
        sprite = part(entity, 'SpriteRenderer')
        if sprite:
            name = Path(sprite.get('texture', '')).stem
            if name.startswith('sign_'):
                continue
            if name in floor_map:
                sprite.update(texture=texture(floor_map[name]), tileSize=[1, 1])
            elif name in wall_map:
                source = ASSETS[wall_map[name]]
                height = source['height'] / PPU
                sprite.update(texture=texture(wall_map[name]), size=[1, height],
                              offset=[0, .5 - height / 2], layer=10, ySort=True, sortOffset=.5)
            elif name in prop_map:
                asset = prop_map[name]
                source = ASSETS[asset]
                width, height = source['width'] / PPU, source['height'] / PPU
                if name in ('banner', 'rug'):
                    sprite['texture'] = texture(asset)
                    if name == 'rug':
                        sprite['size'][1] = .55
                    else:
                        asset = ASSETS['dormitory_wall_banner']
                        height = asset['height'] / PPU
                        sprite.update(texture=banner_texture(asset['id']),
                                      size=[asset['width'] / PPU, height], offset=[0, .25 - height / 2],
                                      layer=10, ySort=True)
                        entity['transform']['position'][1] = 1
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
            side = collider['size'][1] > collider['size'][0]
            set_door(entity, collider['size'][0], collider['size'][1], automatic=False,
                     height=collider['size'][1] if side else None, side=side)
        if entity['name'] == 'Fixed wall torch':
            entity['components'].append(c('Light2D', radius=1.25, color='#ffb24dff', intensity=.16,
                                          offset=[0, -.25], flicker=.18, layer=12))
        keep.append(entity)
    # The existing wall openings remain clear for ordinary room travel.
    scene['entities'] = keep
    scene['settings']['background'] = '#101823ff'
