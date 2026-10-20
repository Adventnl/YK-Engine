"""Intentional chapter-two furniture positions, in native reference pixels (top left).

Keep the castle architecture and progression, while giving each room a usable aisle.
Furniture is placed as a whole object; fitting a chair into a wall opening is forbidden.
"""
OMIT_PROPS = {
    'dorm_chair_nw_inner', 'dorm_chair_ne_inner', 'corridor_chair_west',
    'corridor_chair_east', 'yard_small_stool', 'utility_chair',
    'workshop_rack_top_crate',
    # The common room is a sitting room, and the kitchen needs one coherent work run.
    'west_vertical_bed_upper', 'west_vertical_bed_lower', 'west_barrel',
    'kitchen_jug_shelf', 'kitchen_chair', 'kitchen_side_cabinet', 'kitchen_barrel_south',
    'laundry_basin_4', 'infirmary_candle_stool', 'infirmary_stool',
    'workshop_small_barrel', 'workshop_crate_by_rack', 'workshop_barrel_east',
    'workshop_stool', 'utility_small_stool',
    'canteen_dining_set_east', 'corridor_plant',
    'south_dorm_candle_stool', 'south_dorm_chair', 'infirmary_plant_west',
    'yard_bench_south',
}

# Chapter one uses world-unit anchors rather than reference-image top-left pixels.
CHAPTER_ONE_PLACEMENTS = {
    'Your straw bed': (2, 3.15), 'Cellmate bed': (5, 3.15),
    'Stores shelf': (7.9, 2.6), 'Stores barrel': (11.2, 2.15),
    'Locked cabinet': (9.6, 2.32), 'Stores crates': (7.8, 4.4),
    'Stores fern': (11.2, 4.4),
    'Office shelves': (13.9, 2.6), 'Warden desk': (16.5, 2.35),
    'Office chest': (19, 1.85),
    'Tool bench': (3, 13.65), 'Workshop shelf': (5.2, 13.65),
    'Rope workbench': (7.85, 15.3), 'Workshop barrel': (1.6, 15.4),
    'Workshop crates': (9.48, 15.4), 'Laundry chest': (12.5, 15.2),
    'East tower shelf': (24.6, 2.6), 'East archive shelf': (21.9, 2.6),
    'East tower chest': (29.8, 1.85), 'East archive desk': (27.4, 4),
    'East tower barrel': (30.3, 5.4), 'Tower armor': (29.8, 9.4),
    'Drain service barrel': (22.5, 13),
}


def furnish_chapter_one(scene):
    """Wall storage, a serving corner, work aisles and an unobstructed east gate."""
    scene['entities'] = [e for e in scene['entities']
                         if e['name'] not in ('Terracotta fern', 'Common room armor', 'Common room table')]
    basins = iter((11.8, 15, 18.8))
    for entity in scene['entities']:
        name = entity['name']
        if name in CHAPTER_ONE_PLACEMENTS:
            entity['transform']['position'] = list(CHAPTER_ONE_PLACEMENTS[name])
        if name == 'Folded linen':
            entity['transform']['position'] = [next(basins), 13.15]
        if name == 'Common room heraldic rug':
            entity['transform']['position'] = [4, 9.1]
            next(p['properties'] for p in entity['components']
                 if p['type'] == 'SpriteRenderer')['size'] = [4.9, 45 / 35]
        if name == 'Office heraldic rug':
            entity['transform']['position'] = [16.5, 2.65]

PLACEMENTS = {
    'dorm_bed_nw': (88, 110), 'dorm_chair_nw': (174, 128),
    'dorm_bed_ne': (280, 110), 'dorm_chair_ne_top': (348, 128),
    'south_dorm_bed_west': (88, 705), 'south_dorm_bed_east': (310, 705),
    'south_dorm_candle_stool': (136, 770), 'south_dorm_chair': (358, 750),
    'west_common_table': (124, 350), 'west_common_chair_left': (132, 408),
    'west_common_chair_right': (174, 408),
    'west_vertical_bed_upper': (90, 456), 'west_vertical_bed_lower': (90, 540),
    'west_storage_chest': (346, 540), 'west_barrel': (352, 586),
    'kitchen_barrel_north': (435, 110), 'kitchen_shelf_utensils': (480, 109),
    'kitchen_small_cabinet': (552, 110), 'kitchen_jug_shelf': (610, 109),
    'kitchen_shelf_east': (680, 109), 'kitchen_fireplace': (742, 105),
    'kitchen_preparation_table': (550, 182), 'kitchen_side_cabinet': (750, 210),
    'kitchen_barrel_south': (435, 185), 'kitchen_chair': (700, 188),
    'canteen_dining_set_west': (480, 255), 'canteen_dining_set_east': (650, 255),
    'laundry_linen_rail': (915, 105), 'laundry_basin_1': (836, 175),
    'laundry_basin_2': (910, 175), 'laundry_basin_3': (984, 175),
    'laundry_basin_4': (1058, 175), 'laundry_basket': (835, 240),
    'laundry_folding_table': (885, 265),
    'control_computer_desk': (1220, 114), 'control_tower': (1360, 119),
    'control_chair': (1180, 135), 'control_console_1': (1280, 205),
    'control_console_2': (1360, 275),
    'store_upper_crates': (1480, 120), 'store_middle_crates': (1480, 190),
    'store_lower_crate': (1480, 268), 'store_barrel': (1555, 280),
    'corridor_weapon_rack_west': (334, 360), 'corridor_weapon_rack_east': (1060, 260),
    'yard_bench_north': (1208, 431), 'yard_bench_south': (1208, 530),
    'yard_stone_planter': (1354, 505),
    'utility_energy_device_pipes': (1536, 492), 'utility_crate': (1464, 544),
    'utility_small_stool': (1464, 590), 'utility_green_console': (1460, 670),
    'utility_box': (1550, 674),
    'workshop_crate_by_rack': (516, 614), 'workshop_small_barrel': (555, 642),
    'workshop_stool': (570, 798),
    'infirmary_bed_west': (867, 615), 'infirmary_bed_east': (1050, 615),
    'infirmary_nightstand_west': (922, 615), 'infirmary_nightstand_east': (1016, 615),
    'infirmary_privacy_screen': (963, 696), 'infirmary_plant_west': (835, 608),
    'infirmary_plant_east': (1090, 694), 'infirmary_terminal': (838, 768),
    'infirmary_stool': (884, 784), 'infirmary_candle_stool': (1020, 770),
    'infirmary_medical_cart': (1080, 766),
}


# Remove empty floor spans while preserving masonry thickness and facade height.
# Source-space landmarks remain useful for passages, NPC paths and gameplay tests.
X_SPANS = ((76, 231, 40), (262, 395, 28), (425, 792, 92),
           (824, 1131, 62), (1162, 1414, 47), (1446, 1597, 6))
Y_SPANS = ((99, 229, 20), (289, 337, 20), (429, 511, 30),
           (685, 714, 8), (762, 823, 20))


def compact_axis(value, spans):
    return value - sum(removed * max(0, min(1, (value - start) / (end - start)))
                       for start, end, removed in spans)


def compact_pixel(x, y):
    return compact_axis(x, X_SPANS), compact_axis(y, Y_SPANS)


def compact_world(position):
    x, y = compact_pixel(position[0] * 35 + 40, position[1] * 35 + 25)
    return [(x - 40) / 35, (y - 25) / 35]


def compact_architecture(scene):
    """Resize floor/wall bounds, retaining native furniture, actors and gate sizes.

    Map absolute edges rather than multiplying sizes so joined walls, cut openings,
    coping strips, location bounds and patrol restrictions keep the same topology.
    """
    for entity in scene['entities']:
        old_position = entity['transform']['position']
        new_position = compact_world(old_position)
        structural = (entity['name'].startswith(('Masonry ', 'Floor ', 'Stone passage '))
                      or entity['name'] == 'Corridor Runner')
        rotated = entity['transform'].get('rotation', 0) == 90
        for component in entity['components']:
            p = component['properties']
            if structural and component['type'] in ('SpriteRenderer', 'Collider'):
                w, h = p['size']
                if rotated:
                    w, h = h, w
                dx, dy = p.get('offset', [0, 0])
                if rotated:
                    dx, dy = -dy, dx
                left, top = compact_world([old_position[0] + dx - w / 2,
                                           old_position[1] + dy - h / 2])
                right, bottom = compact_world([old_position[0] + dx + w / 2,
                                               old_position[1] + dy + h / 2])
                offset = [(left + right) / 2 - new_position[0],
                          (top + bottom) / 2 - new_position[1]]
                p['size'] = [bottom - top, right - left] if rotated else [right - left, bottom - top]
                p['offset'] = [offset[1], -offset[0]] if rotated else offset
                if entity['name'] == 'Corridor Runner':
                    # Keep the complete top and bottom cloth pattern after compaction.
                    p['size'][1] = 45 / 35
                    p.update(drawMode='Sliced', sliceFill='Tile')
            if component['type'] == 'SpriteRenderer' and p.get('ySort'):
                depth = compact_world([old_position[0], old_position[1] + p.get('sortOffset', 0)])
                p['sortOffset'] = depth[1] - new_position[1]
            for key in ('boundsMin', 'boundsMax', 'restrictedMin', 'restrictedMax', 'returnPosition'):
                if key in p:
                    p[key] = compact_world(p[key])
        entity['transform']['position'] = new_position


def furnish_compact_rooms(scene):
    """Furniture top-lefts on the compact native-pixel plan, grouped by use.

    A two-pixel wall gap reads as contact without burying opaque pixels in stone.
    Bedside tables sit three pixels from the bed; seating directly faces its table.
    """
    def mapped(x, y):
        return tuple(round(v) for v in compact_pixel(x, y))

    west, north = mapped(76, 99)
    kitchen, _ = mapped(425, 99)
    laundry, _ = mapped(824, 99)
    control, _ = mapped(1162, 99)
    stores, _ = mapped(1446, 99)
    _, common = mapped(76, 289)
    _, south_cell = mapped(76, 685)
    _, south = mapped(425, 601)
    _, service_bay = mapped(1446, 660)
    _, yard = mapped(1162, 429)
    positions = {
        'Dorm Bed Nw': (west + 2, north + 3),
        'Dorm Chair Nw': (west + 55, north + 8),
        'Dorm Bed Ne': (mapped(395, 99)[0] - 40, north + 3),
        'Dorm Chair Ne Top': (mapped(395, 99)[0] - 70, north + 8),
        'South Dorm Bed West': (west + 2, south_cell + 3),
        'South Dorm Candle Stool': (west + 51, south_cell + 3),
        'South Dorm Bed East': (mapped(395, 685)[0] - 40, south_cell + 3),
        'South Dorm Chair': (mapped(395, 685)[0] - 70, south_cell + 8),
        'West Common Table': (west + 12, common + 104),
        'West Common Chair Left': (west + 18, common + 64),
        'West Common Chair Right': (west + 54, common + 64),
        'West Storage Chest': (mapped(395, 289)[0] - 40, common + 8),
        'West Plant': (west + 2, mapped(76, 628)[1] - 46),
        'Corridor Weapon Rack West': (west + 2, common + 185),
        'Kitchen Barrel North': (kitchen + 2, north + 3),
        'Kitchen Shelf Utensils': (kitchen + 35, north + 2),
        'Kitchen Small Cabinet': (kitchen + 94, north + 2),
        'Kitchen Shelf East': (kitchen + 127, north + 2),
        'Kitchen Fireplace': (kitchen + 218, north + 2),
        'Kitchen Preparation Table': (kitchen + 165, north + 76),
        'Canteen Dining Set West': (kitchen + 8, mapped(425, 337)[1] - 77),
        'Canteen Dining Set East': (kitchen + 155, mapped(425, 337)[1] - 77),
        'Laundry Linen Rail': (laundry + 86, north + 2),
        'Laundry Basin 1': (laundry + 2, north + 61),
        'Laundry Basin 2': (laundry + 2, north + 104),
        'Laundry Basin 3': (laundry + 196, north + 2),
        'Laundry Basket': (laundry + 48, north + 112),
        'Linen table': (laundry + 2, north + 2),
        'Security desk': (control + 20, north + 2),
        'Control Tower': (control + 119, north + 2),
        'Control Chair': (control + 54, north + 61),
        'Control Console 1': (control + 171, north + 65),
        'Control Console 2': (control + 171, north + 114),
        'Control Plant': (control + 2, north + 116),
        'Store Upper Crates': (stores + 2, north + 2),
        'Store Middle Crates': (stores + 2, north + 43),
        'Fuse cabinet': (stores + 2, north + 84),
        'Store Barrel': (stores + 110, north + 2),
        'Corridor Weapon Rack East': (laundry + 196, north + 64),
        'Corridor Plant': (laundry + 215, mapped(824, 511)[1] - 44),
        'Corridor Crate West': (stores + 80, north + 84),
        'Workshop Pipe West': (kitchen + 2, south + 2),
        'Tool bench': (kitchen + 17, south + 2),
        'Tool chest': (kitchen + 68, south + 19),
        'Workshop Tools Rack East': (kitchen + 220, south + 2),
        'Workshop Barrel West': (kitchen + 2, south + 68),
        'Workshop Crate Southwest': (kitchen + 2, mapped(425, 823)[1] - 39),
        'Workshop Chest Southeast': (kitchen + 183, south + 2),
        'Repair workbench': (kitchen + 80, mapped(425, 823)[1] - 56),
        'Workshop Candle Stool': (kitchen + 177, mapped(425, 823)[1] - 36),
        'Infirmary Bed West': (laundry + 12, south + 2),
        'Infirmary Nightstand West': (laundry + 51, south + 2),
        'Infirmary Bed East': (laundry + 194, south + 2),
        'Infirmary Nightstand East': (laundry + 167, south + 2),
        'Infirmary Privacy Screen': (laundry + 12, south + 78),
        'Infirmary Plant West': (laundry + 78, south + 2),
        'Infirmary Plant East': (laundry + 212, mapped(824, 823)[1] - 46),
        'Infirmary Terminal': (laundry + 2, mapped(824, 823)[1] - 44),
        'Infirmary Medical Cart': (laundry + 171, mapped(824, 823)[1] - 48),
        'Yard Bench North': (control + 4, yard + 64),
        'Yard Bench South': (control + 4, yard + 44),
        'Yard Stone Planter': (control + 155, yard + 64),
        'Yard Exercise Fence': (control + 4, yard + 124),
        'Arena Court Markings': (control + 18, yard + 144),
        'Utility Energy Device Pipes': (stores + 96, yard + 2),
        'Utility Crate': (stores + 2, yard + 90),
        'Power terminal': (stores + 2, service_bay + 2),
        'Emergency exit switch': (stores + 113, service_bay + 2),
    }
    for entity in scene['entities']:
        if entity['name'] not in positions:
            continue
        sprite = next(c['properties'] for c in entity['components'] if c['type'] == 'SpriteRenderer')
        x, y = positions[entity['name']]
        w, h = sprite['size']
        dx, dy = sprite.get('offset', [0, 0])
        entity['transform']['position'] = [(x - 40) / 35 + w / 2 - dx,
                                           (y - 25) / 35 + h / 2 - dy]
        sprite['sortOffset'] = 0

    # Short useful routines follow the newly authored aisles, rather than the
    # reference image's now-removed empty floor space.
    routes = {
        'Mara': [(west + 115, common + 90), (west + 115, common + 143),
                 (west + 115, common + 176), (west + 115, common + 143)],
        'Cook Bram': [(kitchen + 137, north + 90), (kitchen + 137, north + 130)],
        'Ada': [(laundry + 125, north + 145), (laundry + 180, north + 145),
                (laundry + 180, north + 165), (laundry + 125, north + 165)],
        'Tomas': [(kitchen + 110, south + 100), (kitchen + 180, south + 100)],
        'Nurse Iona': [(laundry + 100, south + 105), (laundry + 160, south + 105)],
        'Rowan': [(control + 117, yard + 68), (control + 117, yard + 107)],
        'Control guard': [(control + 95, north + 121), (control + 125, north + 121),
                          (control + 137, north + 155), (control + 137, yard + 24),
                          (control + 125, north + 155), (control + 95, north + 121)],
    }
    by_name = {e['name']: e for e in scene['entities']}
    by_id = {e['id']: e for e in scene['entities']}
    for name, route in routes.items():
        entity = by_name[name]
        entity['transform']['position'] = [(route[0][0] - 40) / 35, (route[0][1] - 25) / 35]
        path = next(c['properties'] for c in entity['components'] if c['type'] == 'NpcPath')
        # Keep the original waypoint count and IDs to avoid dangling references.
        for index, waypoint in enumerate(path['waypoints']):
            x, y = route[index % len(route)]
            by_id[waypoint]['transform']['position'] = [(x - 40) / 35, (y - 25) / 35]

    # Native-size brackets must still fit after a facade becomes shorter.
    faces = []
    for entity in scene['entities']:
        for component in entity['components']:
            p = component['properties']
            if component['type'] == 'SpriteRenderer' and p.get('texture', '').endswith('hq-wall-run.png'):
                x, y = entity['transform']['position']
                dx, dy = p.get('offset', [0, 0])
                w, h = p['size']
                faces.append((x + dx - w / 2, y + dy - h / 2,
                              x + dx + w / 2, y + dy + h / 2))
    for entity in scene['entities']:
        sprite = next((c['properties'] for c in entity['components'] if c['type'] == 'SpriteRenderer'), None)
        if not sprite or not sprite.get('texture', '').endswith('/control_banner.png'):
            continue
        x, y = entity['transform']['position']
        dx, dy = sprite.get('offset', [0, 0])
        w, h = sprite['size']
        candidates = [face for face in faces if face[2] - face[0] >= w + 4 / 35
                      and face[1] + 26 / 35 <= y + dy - h / 2 + .01
                      and y + dy + h / 2 <= face[3] + .01]
        left, top, right, bottom = min(candidates,
            key=lambda face: abs(max(face[0] + w / 2, min(face[2] - w / 2, x + dx)) - x - dx))
        entity['transform']['position'][0] = max(left + w / 2 + 2 / 35,
                                               min(right - w / 2 - 2 / 35, x + dx)) - dx
