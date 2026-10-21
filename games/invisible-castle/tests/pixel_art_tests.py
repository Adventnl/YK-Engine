"""Check shipping scene data against the actual exported pixels, not exporter helpers."""
import json
import math
import unittest
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PPU = 35


def properties(entity, kind):
    return [c['properties'] for c in entity['components'] if c['type'] == kind]


def scenery(scene):
    for entity in scene['entities']:
        sprites = properties(entity, 'SpriteRenderer')
        if sprites and sprites[0]['texture'].startswith('assets/castle/pixel/'):
            yield entity, sprites[0]


class CastlePixelArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scenes = {chapter: json.loads((ROOT / f'scenes/{chapter}.ykscene').read_text())
                      for chapter in ('courtyard', 'level2')}

    def test_native_density_and_repeated_sizes(self):
        banners = set()
        for chapter, scene in self.scenes.items():
            for entity, sprite in scenery(scene):
                with self.subTest(chapter=chapter, entity=entity['name']):
                    path = ROOT / sprite['texture']
                    meta = json.loads(Path(str(path) + '.ykmeta').read_text())
                    self.assertEqual(meta['pixelsPerUnit'], PPU)
                    self.assertEqual(meta['filter'], 'nearest')
                    if sprite.get('drawMode', 'Simple') != 'Simple':
                        continue
                    with Image.open(path) as image:
                        self.assertAlmostEqual(sprite['size'][0] * PPU, image.width)
                        self.assertAlmostEqual(sprite['size'][1] * PPU, image.height)
                    if path.stem == 'control_banner':
                        banners.add(tuple(sprite['size']))
            for entity in scene['entities']:
                for camera in properties(entity, 'Camera'):
                    self.assertEqual(camera['pixelsPerUnit'], PPU)
        self.assertEqual(len(banners), 1, 'Banners must never shrink to fit short walls')

    def test_compact_rooms_and_bedside_contact(self):
        scene = self.scenes['level2']
        entities = {e['name']: e for e in scene['entities']}

        def edges(name):
            entity = entities[name]
            sprite = properties(entity, 'SpriteRenderer')[0]
            x, y = entity['transform']['position']
            dx, dy = sprite.get('offset', [0, 0])
            w, h = sprite['size']
            return (x + dx - w / 2, y + dy - h / 2, x + dx + w / 2, y + dy + h / 2)

        # Measure exposed floor, excluding the material continued under walls
        # so doorway thresholds can share their room's texture.
        for name, original_area in [('Floor Infirmary', 308 * 225),
                                    ('Floor Canteen', 367 * 241),
                                    ('Floor Laundry', 308 * 241)]:
            left, top, right, bottom = edges(name)
            visible = Image.new('L', (round((right - left) * PPU), round((bottom - top) * PPU)), 255)
            draw = ImageDraw.Draw(visible)
            for wall in scene['entities']:
                if 'wall_solid' not in wall.get('tags', []):
                    continue
                x, y = wall['transform']['position']
                for shape in properties(wall, 'Collider'):
                    w, h = shape['size']
                    a, b, c, d = [round(v * PPU) for v in
                        (x - w / 2 - left, y - h / 2 - top, x + w / 2 - left, y + h / 2 - top)]
                    draw.rectangle((a, b, c - 1, d - 1), fill=0)
            self.assertLess(visible.histogram()[255], original_area * .80)
        west_bed, east_bed = edges('Infirmary Bed West'), edges('Infirmary Bed East')
        west_table, east_table = edges('Infirmary Nightstand West'), edges('Infirmary Nightstand East')
        self.assertLessEqual((west_table[0] - west_bed[2]) * PPU, 4)
        self.assertLessEqual((east_bed[0] - east_table[2]) * PPU, 4)
        self.assertAlmostEqual(west_bed[1], west_table[1])
        self.assertAlmostEqual(east_bed[1], east_table[1])
        self.assertNotIn('West Vertical Bed Upper', entities)
        self.assertNotIn('West Vertical Bed Lower', entities)

        # A closed frame must reach the base of the exit facade, covering its cut.
        gate_bottom = edges('Green exit gate')[3]
        facade_bottom = max(e['transform']['position'][1] + p.get('offset', [0, 0])[1] + p['size'][1] / 2
                            for e in scene['entities'] if 'wall_solid' in e.get('tags', [])
                            for p in properties(e, 'Collider'))
        self.assertLessEqual(abs(gate_bottom - facade_bottom) * PPU, 1)

    def test_room_floors_meet_at_wall_fronts(self):
        scene = self.scenes['level2']
        entities = {e['name']: e for e in scene['entities']}

        def bounds(name):
            entity = entities[name]
            sprite = properties(entity, 'SpriteRenderer')[0]
            x, y = entity['transform']['position']
            dx, dy = sprite.get('offset', [0, 0])
            w, h = sprite['size']
            return x + dx - w / 2, y + dy - h / 2, x + dx + w / 2, y + dy + h / 2

        front = bounds('Floor Main Corridor')[1]
        for name in ('Floor Canteen', 'Floor Laundry', 'Floor Control Room', 'Floor Store Room'):
            self.assertAlmostEqual(bounds(name)[3], front, places=5)
        for name in ('Floor Exercise Yard', 'Floor Utility Room'):
            self.assertAlmostEqual(bounds(name)[1], front, places=5)
        for index in range(4):
            passage = entities[f'Stone passage {index}']
            self.assertAlmostEqual(bounds(passage['name'])[1], front, places=5,
                                   msg='Side openings must not notch the wall face above the corridor')
        for index in range(8):
            self.assertLess(properties(entities[f'Stone passage {index}'], 'SpriteRenderer')[0]['layer'], -30,
                            'Passage patches must never repaint an adjoining room')

    def test_collision_covers_exactly_the_exported_silhouette(self):
        tested = 0
        for chapter, scene in self.scenes.items():
            for entity, sprite in scenery(scene):
                shapes = properties(entity, 'Collider')
                if not shapes:
                    continue
                with self.subTest(chapter=chapter, entity=entity['name']):
                    with Image.open(ROOT / sprite['texture']) as image:
                        expected = image.getchannel('A').point(lambda a: 255 if a >= 128 else 0)
                    actual = Image.new('L', expected.size)
                    draw = ImageDraw.Draw(actual)
                    dx, dy = sprite.get('offset', [0, 0])
                    for shape in shapes:
                        cx, cy = shape.get('offset', [0, 0])
                        w, h = shape['size']
                        edges = [(cx - dx - w / 2) * PPU + expected.width / 2,
                                 (cy - dy - h / 2) * PPU + expected.height / 2,
                                 (cx - dx + w / 2) * PPU + expected.width / 2,
                                 (cy - dy + h / 2) * PPU + expected.height / 2]
                        for edge in edges:
                            self.assertAlmostEqual(edge, round(edge), places=5)
                        left, top, right, bottom = map(round, edges)
                        self.assertGreater(right, left)
                        self.assertGreater(bottom, top)
                        draw.rectangle((left, top, right - 1, bottom - 1), fill=255)
                    self.assertIsNone(ImageChops.difference(expected, actual).getbbox(),
                                      'Solid pixels must match alpha, including gaps and padding')
                    tested += 1
        self.assertGreater(tested, 80)

    def test_wall_blocks_join_without_overlaps_and_match_the_solid_plan(self):
        for chapter, scene in self.scenes.items():
            actor = next(e for e in scene['entities'] if e['name'] == 'Explorer')
            actor_layer = properties(actor, 'SpriteRenderer')[0]['layer']
            # One native pixel per cell, including the projected north perimeter.
            masks = {kind: Image.new('L', (1800, 1200)) for kind in ('top', 'face', 'solid')}
            for entity in scene['entities']:
                tags = entity.get('tags', [])
                if 'masonry' not in tags:
                    continue
                x, y = entity['transform']['position']
                if 'wall_solid' in tags:
                    draw = ImageDraw.Draw(masks['solid'])
                    for shape in properties(entity, 'Collider'):
                        w, h = shape['size']
                        left, top, right, bottom = [round(v * PPU + 100) for v in
                            (x - w / 2, y - h / 2, x + w / 2, y + h / 2)]
                        draw.rectangle((left, top, right - 1, bottom - 1), fill=255)
                    continue
                sprite = properties(entity, 'SpriteRenderer')[0]
                self.assertLess(sprite['layer'], actor_layer,
                                'Characters must remain visible over every wall block')
                self.assertEqual(sprite['size'], [1, 1])
                self.assertEqual(entity['transform'].get('rotation', 0), 0)
                self.assertEqual(sprite['drawMode'], 'Tiled', 'Joined edges must snap at fractional zoom')
                with Image.open(ROOT / sprite['texture']) as image:
                    self.assertEqual(image.size, (35 * sprite['columns'], 35 * sprite['rows']))
                    frame = sprite['frame']
                    u, v = frame % sprite['columns'] * 35, frame // sprite['columns'] * 35
                    alpha = image.getchannel('A').crop((u, v, u + 35, v + 35))
                dx, dy = sprite.get('offset', [0, 0])
                left, top = [round(v * PPU + 100) for v in (x + dx - .5, y + dy - .5)]
                kind = 'top' if 'wall_top' in tags else 'face'
                before = masks[kind].crop((left, top, left + 35, top + 35))
                self.assertIsNone(ImageChops.multiply(before, alpha).getbbox(),
                                  f'{chapter}: overlapping {kind} at {entity["name"]}')
                masks[kind].paste(ImageChops.lighter(before, alpha), (left, top))
            # Cross-sections through unbroken perimeter coping: its top view
            # must have the same width on north and west walls.
            def first_run(values):
                start = next(i for i, value in enumerate(values) if value)
                return next((i - start for i in range(start, len(values)) if not values[i]),
                            len(values) - start)
            top_pixels = masks['top'].load()
            self.assertEqual(first_run([top_pixels[x, 450] for x in range(1800)]), 35)
            self.assertEqual(first_run([top_pixels[580, y] for y in range(1200)]), 35)
            self.assertIsNone(ImageChops.multiply(masks['top'], masks['face']).getbbox(),
                              'Caps must join the face at an edge, never cover another wall')
            visible = ImageChops.lighter(masks['top'], masks['face'])
            self.assertIsNone(ImageChops.difference(masks['solid'], visible).getbbox(),
                              'Every visible wall pixel must block the player, including its face')
            expected = Image.new('L', visible.size)
            for shift in range(29):
                moved = Image.new('L', visible.size)
                moved.paste(masks['top'], (0, shift))
                expected = ImageChops.lighter(expected, moved)
            self.assertIsNone(ImageChops.subtract(visible, expected).getbbox(),
                              'Exposed brick faces may extend at most 28 native pixels below the top')
            for entity, sprite in scenery(scene):
                if not sprite['texture'].endswith(('/control_banner.png', '/laundry_switch.png')):
                    continue
                self.assertLess(sprite['layer'], actor_layer,
                                'Wall fixtures must not conceal characters')
                with Image.open(ROOT / sprite['texture']) as image:
                    alpha = image.getchannel('A')
                x, y = entity['transform']['position']
                dx, dy = sprite.get('offset', [0, 0])
                w, h = sprite['size']
                left, top = [round(v * PPU + 100) for v in (x + dx - w / 2, y + dy - h / 2)]
                support = masks['face'].crop((left, top, left + alpha.width, top + alpha.height))
                self.assertIsNone(ImageChops.subtract(alpha, support).getbbox(),
                                  f'{chapter}: fixture lacks a supporting face: {entity["name"]}')

    def test_runners_keep_the_complete_vertical_pattern(self):
        for chapter, scene in self.scenes.items():
            for entity in scene['entities']:
                for sprite in properties(entity, 'SpriteRenderer'):
                    if not sprite['texture'].endswith('/corridor_runner.png'):
                        continue
                    with Image.open(ROOT / sprite['texture']) as image:
                        self.assertAlmostEqual(sprite['size'][1] * PPU, image.height,
                            msg=f'{chapter}: {entity["name"]} must show both halves of the carpet')

    def test_level_one_furniture_has_purpose_and_clear_aisles(self):
        scene = self.scenes['courtyard']
        names = {e['name'] for e in scene['entities']}
        self.assertNotIn('Terracotta fern', names)
        self.assertNotIn('Common room armor', names)
        self.assertNotIn('Common room table', names)
        basins = [e['transform']['position'] for e in scene['entities'] if e['name'] == 'Folded linen']
        self.assertEqual(len(basins), 3)
        self.assertEqual(len({p[1] for p in basins}), 1)
        for entity, sprite in scenery(scene):
            # The circulation strip from stores to the east gate stays usable.
            if not properties(entity, 'Collider'):
                continue
            x, y = entity['transform']['position']
            for shape in properties(entity, 'Collider'):
                dx, dy = shape.get('offset', [0, 0])
                w, h = shape['size']
                overlap = min(x + dx + w / 2, 20) - max(x + dx - w / 2, 7)
                depth = min(y + dy + h / 2, 10.8) - max(y + dy - h / 2, 8.1)
                self.assertFalse(overlap > 0 and depth > 0, entity['name'])

    def test_freestanding_furniture_does_not_overlap(self):
        for chapter, scene in self.scenes.items():
            props = []
            for entity, sprite in scenery(scene):
                if not properties(entity, 'Collider') or properties(entity, 'StateGate'):
                    continue
                with Image.open(ROOT / sprite['texture']) as image:
                    # Half-pixel precision handles source placements with odd canvas widths.
                    mask = image.getchannel('A').resize((image.width * 2, image.height * 2),
                                                        Image.Resampling.NEAREST)
                x, y = entity['transform']['position']
                dx, dy = sprite.get('offset', [0, 0])
                left = round((x + dx - sprite['size'][0] / 2) * PPU * 2)
                top = round((y + dy - sprite['size'][1] / 2) * PPU * 2)
                props.append((entity['name'], left, top, mask))
            for i, (name, x, y, mask) in enumerate(props):
                for other, ox, oy, omask in props[i + 1:]:
                    left, top = max(x, ox), max(y, oy)
                    right, bottom = min(x + mask.width, ox + omask.width), min(y + mask.height, oy + omask.height)
                    if right <= left or bottom <= top:
                        continue
                    a = mask.crop((left - x, top - y, right - x, bottom - y))
                    b = omask.crop((left - ox, top - oy, right - ox, bottom - oy))
                    with self.subTest(chapter=chapter, first=name, second=other):
                        self.assertIsNone(ImageChops.multiply(a, b).getbbox(),
                                          f'Furniture overlaps: {name} / {other}')

    def test_npc_routines_clear_scenery(self):
        for chapter, scene in self.scenes.items():
            entities = {e['id']: e for e in scene['entities']}
            obstacles = []
            for entity in scene['entities']:
                sprites = properties(entity, 'SpriteRenderer')
                if not sprites or not sprites[0]['texture'].startswith('assets/castle/'):
                    continue
                x, y = entity['transform']['position']
                for shape in properties(entity, 'Collider'):
                    if shape.get('isTrigger'):
                        continue
                    dx, dy = shape.get('offset', [0, 0])
                    w, h = shape['size']
                    obstacles.append((entity['name'], x + dx - w / 2, y + dy - h / 2,
                                      x + dx + w / 2, y + dy + h / 2))
            for entity in scene['entities']:
                paths = properties(entity, 'NpcPath')
                if not paths:
                    continue
                collider = properties(entity, 'Collider')[0]
                rx, ry = [size / 2 for size in collider['size']]
                dx, dy = collider.get('offset', [0, 0])
                points = [entities[eid]['transform']['position'] for eid in paths[0]['waypoints']]
                points = [entity['transform']['position']] + points + points[:1]
                for start, end in zip(points, points[1:]):
                    steps = max(1, math.ceil(max(abs(end[i] - start[i]) for i in (0, 1)) * PPU))
                    for step in range(steps + 1):
                        x = start[0] + (end[0] - start[0]) * step / steps + dx
                        y = start[1] + (end[1] - start[1]) * step / steps + dy
                        for name, left, top, right, bottom in obstacles:
                            overlaps = (min(x + rx, right) - max(x - rx, left) > .002 and
                                        min(y + ry, bottom) - max(y - ry, top) > .002)
                            if overlaps:
                                self.fail(f'{chapter}: {entity["name"]} routine enters {name} at {x:.2f}, {y:.2f}')


if __name__ == '__main__':
    unittest.main()
