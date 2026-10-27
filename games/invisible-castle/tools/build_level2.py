#!/usr/bin/env python3
"""Build chapter two with the supplied castle scenery and shared directional actors."""
import json
from pathlib import Path
from PIL import Image
from castle_reference import style_level_two

from build_frontend import HUD_DEFAULTS, decorate_level

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/level2'

def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')

def texture(name):
    return 'assets/level2/' + name + '.png'

def c(kind, **props):
    return dict(type=kind, properties=props)

def build_scene():
    entities = []
    def add(name, x, y, *parts, tags=None):
        eid = f'{len(entities) + 1:016x}'
        e = dict(id=eid, name=name, transform=dict(position=[x, y], rotation=0, scale=[1, 1]), components=list(parts))
        if tags:
            e['tags'] = tags
        entities.append(e)
        return eid
    def sprite(art, w, h, layer=10, **props):
        return c('SpriteRenderer', texture=texture(art), size=[w, h], layer=layer, **props)
    def solid(w, h, offset=(0, 0)):
        return c('Collider', size=[w, h], offset=list(offset), layer='Solid', friction=0)
    def floor(name, x, y, w, h, art):
        add(name, x + w / 2, y + h / 2, sprite(art, w, h, layer=-30, drawMode='Tiled', tileSize=[4, 4]))
    rooms = [('North cells', 1, 1, 9, 7, 'dorm_floor'), ('Common room', 1, 9, 9, 10, 'common_floor'),
             ('South cells', 1, 20, 9, 7, 'dorm_floor'), ('Canteen', 11, 1, 12, 8, 'canteen_floor'),
             ('Laundry', 24, 1, 9, 8, 'laundry_floor'), ('Control', 34, 1, 7, 8, 'control_floor'),
             ('Stores', 42, 1, 5, 8, 'store_floor'), ('Main corridor', 11, 10, 22, 8, 'corridor_floor'),
             ('Workshop', 11, 19, 12, 8, 'workshop_floor'), ('Infirmary', 24, 19, 9, 8, 'infirmary_floor'),
             ('Exercise yard', 34, 10, 13, 8, 'grass'), ('Utility', 34, 19, 7, 8, 'utility_floor'),
             ('Exit', 42, 19, 5, 8, 'corridor_floor')]
    floor('Castle foundation', 0, 0, 48, 28, 'corridor_floor')
    for room in rooms:
        floor(*room)
    floor('Exercise court', 39, 11, 7, 6, 'sand')
    # Continuous perimeter and connected rooms; each doorway is at least two cells wide.
    walls = {(x, y) for x in range(48) for y in (0, 27)} | {(x, y) for y in range(28) for x in (0, 47)}
    for x in (10, 33):
        for y in range(1, 27):
            if y not in ((12, 13, 14) if x == 10 else (13, 14, 22, 23)):
                walls.add((x, y))
    for x in (23, 41):
        for y in list(range(1, 9)) + list(range(19, 27)):
            if not (x == 41 and y in (22, 23)):
                walls.add((x, y))
    for x in range(1, 10):
        for y in (8, 19):
            if x not in (4, 5):
                walls.add((x, y))
    for y in (9, 18):
        for x in range(11, 47):
            if x not in (16, 17, 27, 28, 37, 38, 44, 45):
                walls.add((x, y))
    # North/south dormitory pairs separated by short piers with open circulation below.
    for y in list(range(1, 5)) + list(range(20, 24)):
        walls.add((5, y))
    for x in (44, 45):
        walls.discard((x, 27))
    for x, y in sorted(walls):
        vertical = x in (0, 10, 23, 33, 41, 47) and y not in (0, 9, 18, 27)
        art = 'wall_vertical' if vertical else ('wall_south' if y == 27 else 'wall')
        add(f'Masonry {x} {y}', x + .5, y + .5, sprite(art, 1, 1, layer=4), solid(1, 1))
    for x, y, w, h in [(4, 8, 2, 1), (4, 19, 2, 1), (10, 12, 1, 3), (33, 13, 1, 2), (33, 22, 1, 2), (41, 22, 1, 2)]:
        floor('Gold doorway threshold', x, y, w, h, 'door_tile')
    for y in (9, 18):
        for x in (16, 27, 37, 44):
            floor('Gold doorway threshold', x, y, 2, 1, 'door_tile')
    add('Blue corridor runner', 21, 14, sprite('runner', 3, 7, layer=-20))
    hero = add('Explorer', 3, 5.8, sprite('hero', 1, 1.375, offset=[0, -.4375], ySort=True),
               c('AnimatedSprite', animation='assets/level2/hero.ykanim', clip='idle_down', flipParameter=''),
               c('CharacterMotion', idleHeight=.035, stepHeight=.025, animateSteps=False, flipWithMovement=False),
               c('TopDownController', speed=4, acceleration=60), c('PlayerInput', actionSet='Player1'), c('Interactor'),
               c('RigidBody', type='Dynamic', gravityScale=0, linearDamping=0, fixedRotation=True, bullet=True),
               c('Collider', size=[.5, .35], layer='Player', friction=0), c('Inventory', slots=6), c('Crafter'), tags=['player'])
    for name, x, y, w, h, _ in rooms:
        add(name + ' location', 0, 0, c('LocationArea', actor=hero, title=name.upper(),
            boundsMin=[x - .5, y - .5], boundsMax=[x + w + .5, y + h + .5]))
    add('Player follow camera', 3, 5.8, c('Camera', mode='Follow', targets=[hero], orthographicHeight=12.083333,
         viewportInset=[0, 140], smoothTime=.08, clampToBounds=True, boundsMin=[0, 0], boundsMax=[48, 28]))
    def prop(name, art, x, y, w=1, h=1.5, parts=(), foot=.65):
        return add(name, x, y, sprite(art, w, h, offset=[0, -h / 2 + .25], ySort=True),
                   solid(w * .82, foot, (0, -foot / 2 + .25)), *parts)
    def search(name, art, x, y, item, flag, text, required=(), w=1, h=1.5):
        return prop(name, art, x, y, w, h, parts=[c('Interactable', prompt='E - SEARCH ' + name.upper(), range=1.8),
             c('Inventory', slots=2, startItems=[dict(item=item, count=1)]),
             c('Container', searchSeconds=2.2, hiddenSlots=2),
             c('SearchAction', title=name.upper(), duration=2.2, successFlag=flag, successText=text,
               requiredItems=list(required), lockedText='Bring the screwdriver from the workshop.', emptyText='Already searched. Your supplies are in your inventory.')])
    for x, y in [(2.6, 3.4), (7.5, 3.4), (2.6, 22.5), (7.5, 22.5)]:
        prop('Dormitory bed', 'bed', x, y, 2, 1.5)
        prop('Dormitory chair', 'chair', x + .8, y + 1.8, h=1.25, foot=.45)
    prop('Common room dining table', 'dining', 5, 13.5, 3, 2, foot=1.2)
    prop('Common room chest', 'chest', 2, 17.8, 1.5, 1.25)
    prop('Common room barrel', 'barrel', 8.5, 17.5)
    for x in (14, 20):
        prop('Canteen dining set', 'dining', x, 6.2, 3, 2, foot=1.2)
    prop('Kitchen hearth', 'hearth', 21.5, 3.2, 2, 2.5)
    prop('Kitchen shelves', 'kitchen_shelf', 13, 3, 2, 2)
    prop('Kitchen preparation table', 'prep', 17, 3, 3, 1.5)
    prop('Kitchen barrel', 'barrel', 12, 8)
    for x in (25.5, 28, 30.5):
        prop('Laundry wash basin', 'basin', x, 3.8, 1.25, 1.5)
    prop('Drying linen', 'linen_rail', 28, 2.5, 2, 2, foot=.3)
    prop('Laundry basket', 'basket', 25.5, 7)
    search('Linen table', 'folding', 30.5, 6.5, 'l2_linen', 'l2_linen_found', 'Clean linen found. Use it to insulate the spare fuse.', w=2)
    search('Security desk', 'computer', 37.5, 3.5, 'l2_keycard', 'l2_card_found', 'Security card found. The utility terminal will accept it.', w=3, h=2)
    prop('Security tower', 'tower', 40, 3.3, 1, 2)
    prop('Control chair', 'control_chair', 37, 5.2)
    prop('Control terminal', 'terminal', 35, 3.3)
    for y in (3, 5.5):
        prop('Store crates', 'crates', 45, y, 2, 2)
    search('Fuse cabinet', 'cabinet', 43.3, 7, 'l2_fuse', 'l2_fuse_found', 'Spare fuse found. Wrap it with linen at the workshop.', required=['l2_screwdriver'])
    search('Tool bench', 'workbench', 14, 22, 'l2_screwdriver', 'l2_tool_found', 'Screwdriver found. Pry open the stores fuse cabinet.', w=3)
    search('Tool chest', 'chest', 13, 25.6, 'l2_cutters', 'l2_cutters_found', 'Wire cutters found. Cut the exit chain after power is restored.', w=1.5, h=1.25)
    prop('Repair workbench', 'workbench', 20.5, 22, 3, parts=[
        c('CraftingStation', station='workbench', range=2), c('Interactable', prompt='E - WRAP FUSE / FUSE + LINEN', range=1.8),
        c('SearchAction', title='INSULATE THE FUSE', verb='WRAPPING THE CERAMIC CORE', recipe='l2_insulated_fuse', duration=2.5,
          successFlag='l2_fuse_made', successText='Insulated fuse ready. Take it and the security card to utility.', emptyText='Your insulated fuse is ready.')])
    prop('Workshop tools rack', 'tool_rack', 12, 20.7, 2, 2)
    prop('Workshop copper pipe', 'pipe', 22, 25.8, 1, 2)
    prop('Workshop crates', 'crates', 19, 26, 2, 2)
    for x in (26, 31):
        prop('Infirmary bed', 'hospital_bed', x, 22.3, 1.5, 2)
        prop('Bedside candle', 'nightstand', x, 24.8, 1, 1.25, foot=.45)
    prop('Medical cart', 'medical_cart', 28.6, 26, 1.5)
    prop('Privacy screen', 'screen', 28.5, 22, 1.5, 2, foot=.35)
    for x, y in [(36, 11.5), (36, 17)]:
        prop('Yard bench', 'yard_bench', x, y, 2, 1.25)
    prop('Yard stone planter', 'planter', 45.5, 17.3, 1.5)
    prop('Utility power generator', 'generator', 36.5, 21, 3, 2.5)
    prop('Utility copper pipe', 'pipe', 35, 25.7, 1, 2)
    prop('Utility cabinet', 'cabinet', 40, 25.7, 1, 2)
    prop('Power terminal', 'terminal', 39.5, 21.5, parts=[
        c('Interactable', prompt='E - RESTORE POWER / MATCH TERMINALS', range=1.8),
        c('WirePuzzle', requiredFlags=['l2_fuse_made', 'l2_card_found'], successFlag='l2_power_restored',
          missingText='Bring an insulated fuse and the security card. Find linen, a fuse and the control desk.')])
    # Cut-chain work remains unavailable until the terminal repair is actually solved.
    add('Green exit gate', 45, 27.5, sprite('gate', 2, 2.5, offset=[0, -.75], ySort=True), solid(2, 1),
        c('Interactable', prompt='E - CUT EXIT CHAIN / WIRE CUTTERS', range=1.8, requiredFlag='l2_power_restored'),
        c('SearchAction', title='CUT THE EXIT CHAIN', verb='CUTTING THE LAST LINK', duration=3,
          requiredItems=['l2_cutters'], lockedText='Find wire cutters in the workshop tool chest.',
          successFlag='l2_exit_open', successText='Gate open. Walk south through the green exit.', emptyText='The exit is open.'),
        c('StateGate', startsLocked=True, unlockFlag='l2_exit_open', persistFlag='l2_gate_open', openOffset=[-2.25, 0], animationSeconds=.45))
    prop('Emergency exit switch', 'exit_switch', 43, 25, parts=[c('Interactable', prompt='E - READ EXIT STATUS', range=1.5),
         c('Dialogue', speaker='Exit status', pages=['The green gate needs power from utility. Then cut the chain with wire cutters and walk south.'], charactersPerSecond=0)])
    for x, y in [(2, 10.8), (8.5, 10.8), (12, 17), (31.5, 17), (25, 26), (46, 21)]:
        prop('Terracotta fern', 'plant', x, y, foot=.45)
    for x, y in [(12, 11.6), (31, 11.6)]:
        prop('Corridor weapon rack', 'weapon_rack', x, y, 2, 2)
    for x, y in [(3, 1.5), (17, 1.5), (28, 1.5), (37, 1.5), (17, 19.5), (29, 19.5), (44, 19.5)]:
        add('Royal banner', x, y, sprite('banner', .8, 1.6, layer=8))
    for x, y in [(1.5, 8), (9.5, 8), (11.5, 9), (23.5, 9), (33.5, 9), (41.5, 9), (11.5, 18), (23.5, 18), (33.5, 18), (41.5, 18)]:
        add('Fixed wall torch', x, y - .4, c('SpriteRenderer', texture='assets/keep/torch.png', size=[.75, 1.125], layer=8),
            c('AnimatedSprite', animation='assets/keep/torch.ykanim', clip='burn', flipParameter=''))
    for label, x, y in [('NORTH CELLS', 7.8, 8.5), ('COMMON ROOM', 5, 10), ('SOUTH CELLS', 7.8, 19.5),
                         ('CANTEEN', 19.6, 9.4), ('LAUNDRY', 30.5, 9.4), ('CONTROL', 40.35, 9.4), ('STORES', 45.2, 7.65),
                         ('WORKSHOP', 20.5, 18.4), ('INFIRMARY', 30.5, 18.4), ('EXERCISE YARD', 42, 10.8),
                         ('UTILITY', 40.2, 18.4), ('EXIT', 45, 16.65), ('MAIN CORRIDOR', 24, 11)]:
        plaque = Image.open(ART / ('sign_' + label.lower().replace(' ', '_') + '.png'))
        add(label + ' plaque', x, y - .12, sprite('sign_' + label.lower().replace(' ', '_'),
            plaque.width / 24, .75, layer=18))
    def npc(name, role, x, y, pages, flag='', facing='down', route=(), wait=2.0):
        points = [add(name + ' routine ' + str(i), px, py) for i, (px, py) in enumerate(route)]
        add(name, x, y, sprite(role, 1, 1.375, offset=[0, -.4375], ySort=True),
            c('AnimatedSprite', animation='assets/level2/' + role + '.ykanim', clip='idle_' + facing, flipParameter=''),
            c('CharacterMotion', idleHeight=.035, stepHeight=.025, animateSteps=False, flipWithMovement=False),
            c('RigidBody', type='Dynamic', gravityScale=0, fixedRotation=True, linearDamping=0, allowSleep=False),
            c('Collider', size=[.5, .35], layer='Prop', friction=0),
            c('NpcPath', waypoints=points, speed=.85, waitSeconds=wait),
            c('Interactable', prompt='E - TALK TO ' + name.upper(), range=1.5, setFlag=flag),
            c('Dialogue', speaker=name, pages=pages, portrait=texture('portrait_' + role), charactersPerSecond=0))
    npc('Mara', 'companion', 7.5, 15.5, ['Welcome to the inner keep. The green exit is south of the east yard. Its power is cut, and a chain holds it shut.',
        'Find a screwdriver and wire cutters in the workshop. Pry open the stores for a fuse and take linen from the laundry. Wrap them at the repair bench.',
        'Steal the security card from control while the guard is away. Match the utility terminal wires, cut the exit chain, then walk south.'], 'l2_met_mara', 'left',
        route=[(7.5, 15.5), (7.5, 17), (5.5, 17), (7.5, 17)], wait=2.6)
    npc('Cook Bram', 'cook', 21, 8, ['Workshop supplies are south of the long corridor. The control guard cannot see through the walls.'],
        route=[(21, 8), (19, 8), (19, 7.5), (21, 7.5)], wait=3.1)
    npc('Ada', 'laundry_worker', 27, 7, ['Take clean linen from the folding table. Wrapped around the spare fuse, it will protect your hands.'],
        route=[(27, 7), (28, 7), (28, 5.5), (27, 5.5)], wait=2.2)
    npc('Tomas', 'craftsperson', 17, 25.2, ['Search both tool containers. The repair bench needs a spare fuse and clean linen.'],
        route=[(17, 25.2), (17, 23.5), (18, 23.5), (17, 23.5)], wait=3.5)
    npc('Nurse Iona', 'nurse', 28.5, 24.5, ['If the guard catches you, you return to your cell. Hidden tools stay with you. Step out of control before the warning fills.'],
        route=[(28.5, 24.5), (29.3, 24.5), (29.3, 23.5), (29.3, 24.5)], wait=2.8)
    npc('Rowan', 'yard_worker', 37.5, 15, ['Utility is south of this yard. On the terminal, W/S chooses a source and A/D chooses a destination. Match the letter and press E.'],
        route=[(37.5, 15), (40, 15), (40, 16), (40, 15)], wait=2.4)
    points = [add('Patrol marker ' + str(i), x, y) for i, (x, y) in enumerate([(35.5, 6), (39.5, 6), (37.5, 7.5), (37.5, 12), (35.5, 12), (37.5, 12), (37.5, 7.5)])]
    add('Control guard', 35.5, 6, sprite('guard', 1, 1.375, offset=[0, -.4375], ySort=True),
        c('AnimatedSprite', animation='assets/level2/guard.ykanim', clip='idle_down', flipParameter=''),
        c('CharacterMotion', idleHeight=.035, stepHeight=.025, animateSteps=False, flipWithMovement=False),
        c('NpcPath', waypoints=points, speed=1.6, waitSeconds=1), c('RigidBody', type='Kinematic', gravityScale=0, fixedRotation=True),
        solid(.5, .35), c('PatrolWatch', caughtText='Caught! Back to your cell. Your hidden supplies are safe.', actor=hero, restrictedMin=[34, 1], restrictedMax=[41, 9], returnPosition=[3, 5.8], range=3.2, catchSeconds=3.5),
        c('Interactable', prompt='E - TALK TO CONTROL GUARD', range=1.5),
        c('Dialogue', speaker='Control guard', portrait=texture('portrait_guard'), charactersPerSecond=0,
          pages=['Free time ends at dusk. Keep clear of the control desk. The room passages are clear.']))
    goal = add('South escape passage', 45, 27.6, c('Collider', size=[1.7, .65], isTrigger=True, layer='Sensor'),
               c('Goal', requiredTag='player', requiredFlag='l2_gate_open', enterOnComplete=True))
    add('Level two story', 0, 0, c('ObjectiveList',
        flags=['l2_met_mara', 'l2_tool_found', 'l2_cutters_found', 'l2_linen_found', 'l2_fuse_found', 'l2_fuse_made', 'l2_card_found', 'l2_power_restored', 'l2_exit_open'],
        instructions=['Talk to Mara in the west common room.', 'Search the tool bench in the south workshop.', 'Search the workshop tool chest for wire cutters.',
                      'Search the folding table in the north laundry.', 'Pry open the fuse cabinet in the northeast stores.', 'Wrap the fuse with linen at the repair workbench.',
                      'Search the security desk in control. Avoid the guard.', 'Match the utility terminal wires to restore power.', 'Cut the green exit chain with the wire cutters.'],
        completeText='Walk south through the open green gate.'),
        c('InventoryHud', **{**HUD_DEFAULTS, 'title': 'INVISIBLE CASTLE / LEVEL 2', 'completionTitle': 'THE INNER KEEP IS BEHIND YOU', 'completionText': 'The green gate opens. Your friends follow you out.'}, actor=hero),
        c('LevelFlow', goals=[goal], introDuration=0, restartOnDeath=False, completeDelay=30, completeMessage='LEVEL 2 ESCAPED'))
    scene = dict(format='yk.scene', version=1,
         settings=dict(name='Invisible Castle - Level 2: The Inner Keep', gravity=[0, 0], background='#16191fff'), entities=entities)
    style_level_two(scene)
    decorate_level(scene, 2)
    save(ROOT / 'scenes/level2.ykscene', scene)
    items = [('l2_screwdriver', 'Screwdriver', 'screwdriver'), ('l2_cutters', 'Cutters', 'cutters'), ('l2_linen', 'Linen', 'linen'),
             ('l2_fuse', 'Spare fuse', 'fuse'), ('l2_keycard', 'Keycard', 'keycard'), ('l2_insulated_fuse', 'Safe fuse', 'safe_fuse')]
    save(ROOT / 'data/level2.ykdata', dict(format='yk.data', version=1,
         items=[dict(id=i, displayName=n, icon=texture(a), tags=['escape']) for i, n, a in items],
         recipes=[dict(id='l2_insulated_fuse', displayName='Insulated fuse', station='workbench',
                       ingredients=[dict(item='l2_fuse'), dict(item='l2_linen')], output=dict(item='l2_insulated_fuse'))]))
    project = json.loads((ROOT / 'project.ykproj').read_text())
    project['startScene'] = 'scenes/menu.ykscene'
    project['textures']['pixelsPerUnit'] = 35
    project['window']['title'] = 'Invisible Castle'
    project['build']['exclude'] = ['assets/level2/source', 'assets/castle/source', 'assets/castle/reference', 'assets/castle/props']
    save(ROOT / 'project.ykproj', project)
    print('Level 2:', len(scene['entities']), 'editable entities;', len(rooms), 'areas; shared castle scenery and actors.')

if __name__ == '__main__':
    build_scene()
