#!/usr/bin/env python3
"""Capture the actual player without changing shipping scenes or requiring a desktop.

Use --audit [DIRECTORY] for fixed, display-resolution views of every room in both
chapters. Audit captures include a manifest of camera bounds and their source scenes.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from PIL import Image
from castle_layout import compact_world

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_AUDIT = ROOT.parent.parent / 'build/invisible_castle-visual-audit'

# Chapter one predates LocationArea components; these are its complete floor regions.
# Bounds follow the authored masonry and include every traversable area.
CHAPTER_ONE_ROOMS = (
    ('cells', (1, 1, 6, 7)),
    ('locked-stores', (7, 1, 12, 7)),
    ('guard-office', (13, 1, 20, 7)),
    ('common-room', (1, 8, 20, 11)),
    ('workshop', (1, 12, 10, 17)),
    ('laundry', (11, 12, 20, 17)),
    ('east-archive', (21, 1, 31, 7)),
    ('east-tower', (21, 8, 31, 15)),
    ('drainage', (21, 15, 31, 17)),
)


def full_resolution(scene):
    for entity in scene['entities']:
        for component in entity['components']:
            if component['type'] == 'Camera':
                component['properties']['pixelsPerUnit'] = 35
    return scene


def fixed_view(scene, bounds, aspect):
    """Keep the entire room and its wall returns visible, at display resolution."""
    scene = copy.deepcopy(scene)
    left, top, right, bottom = bounds
    center = [(left + right) / 2, (top + bottom) / 2]
    # The extra space exposes the complete wall face and coping around each floor.
    margin = 2.8
    height = max(11.6, bottom - top + 2 * margin, (right - left + 2 * margin) / aspect)
    for entity in scene['entities']:
        entity['components'] = [c for c in entity['components'] if c['type'] != 'InventoryHud']
        for component in entity['components']:
            if component['type'] == 'Camera':
                component['properties'].update(mode='Fixed', orthographicHeight=height,
                                               viewportInset=[0, 0], clampToBounds=False,
                                               pixelsPerUnit=35)
                entity['transform']['position'] = center
    return scene, dict(center=center, visibleHeight=height,
                       visibleWidth=height * aspect, roomBounds=list(bounds))


def chapter_two_rooms(scene):
    # Floor entities also cover the exercise arena, which has no LocationArea.
    for entity in scene['entities']:
        if not entity['name'].startswith('Floor '):
            continue
        for component in entity['components']:
            if component['type'] != 'SpriteRenderer':
                continue
            properties = component['properties']
            width, height = properties['size']
            x, y = entity['transform']['position']
            dx, dy = properties.get('offset', [0, 0])
            name = re.sub(r'[^a-z0-9]+', '-', entity['name'][6:].lower()).strip('-')
            yield name, (x + dx - width / 2, y + dy - height / 2,
                         x + dx + width / 2, y + dy + height / 2)


def size_argument(value):
    if not re.fullmatch(r'[1-9][0-9]*x[1-9][0-9]*', value):
        raise argparse.ArgumentTypeError('Use WIDTHxHEIGHT, for example 2560x1440')
    width, height = map(int, value.split('x'))
    if width > 8192 or height > 8192:
        raise argparse.ArgumentTypeError('Capture dimensions must be at most 8192')
    return width, height


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT.parent.parent / 'build/dev')
    parser.add_argument('--audit', nargs='?', type=Path, const=DEFAULT_AUDIT,
                        help='Also capture every room and both overviews (default: build/invisible_castle-visual-audit)')
    parser.add_argument('--audit-size', type=size_argument, default=(2560, 1440), metavar='WIDTHxHEIGHT',
                        help='Actual audit render dimensions; no image upscaling (default: 2560x1440)')
    parser.add_argument('--audit-frames', type=int, default=3,
                        help='Fixed simulation frames before each fixed-camera audit capture (default: 3)')
    options = parser.parse_args()
    if options.audit_frames < 1:
        parser.error('--audit-frames must be positive')
    player = options.build.resolve() / ('yk_player.exe' if os.name == 'nt' else 'yk_player')
    if not player.is_file():
        parser.error('Build yk_player first: cmake --build --preset dev')
    previews = options.build.resolve() / 'invisible-castle-previews'
    previews.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='castle-preview-', dir=options.build) as work:
        work = Path(work).resolve()
        project = work / 'project'
        project.mkdir()
        project_settings = json.loads((ROOT / 'project.ykproj').read_text())
        (project / 'project.ykproj').write_text(json.dumps(project_settings, indent=2))
        for directory in ('assets', 'data', 'scenes'):
            shutil.copytree(ROOT / directory, project / directory,
                            ignore=shutil.ignore_patterns('source', '__pycache__'))
        first = full_resolution(json.loads((ROOT / 'scenes/courtyard.ykscene').read_text()))
        second = full_resolution(json.loads((ROOT / 'scenes/level2.ykscene').read_text()))
        workshop = copy.deepcopy(second)
        for entity in workshop['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = compact_world([(560 - 40) / 35, (710 - 25) / 35])
        overview = copy.deepcopy(second)
        for entity in overview['entities']:
            entity['components'] = [c for c in entity['components'] if c['type'] != 'InventoryHud']
            for component in entity['components']:
                if component['type'] == 'Camera':
                    bounds = component['properties']['boundsMax']
                    component['properties'].update(mode='Fixed', orthographicHeight=bounds[1] + 1.4,
                                                   viewportInset=[0, 0], clampToBounds=False)
                    entity['transform']['position'] = [v / 2 for v in bounds]
        corridor = copy.deepcopy(second)
        for entity in corridor['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = compact_world([(700 - 40) / 35, (475 - 25) / 35])
        wall_contact = copy.deepcopy(second)
        for entity in wall_contact['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                # Stand beside the west corridor return, with the hero's body
                # overlapping the stone. The complete character must stay visible.
                entity['transform']['position'] = [275 / 35, 304 / 35]
        utility = copy.deepcopy(second)
        for entity in utility['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = compact_world([(1490 - 40) / 35, (590 - 25) / 35])
        wire_terminal = copy.deepcopy(second)
        terminal = next(e for e in second['entities'] if e['name'] == 'Power terminal')
        tx, ty = terminal['transform']['position']
        for entity in wire_terminal['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = [tx + 1.2, ty - .45]
        infirmary = copy.deepcopy(second)
        for entity in infirmary['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = compact_world([(970 - 40) / 35, (750 - 25) / 35])
        for name, scene in [('courtyard', first), ('level2', second), ('workshop', workshop),
                            ('overview', overview), ('corridor', corridor), ('wall-contact', wall_contact), ('utility', utility),
                            ('infirmary', infirmary), ('wire-terminal', wire_terminal)]:
            (project / 'scenes' / (name + '.ykscene')).write_text(json.dumps(scene, indent=2))
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                   SDL_AUDIODRIVER='dummy', YK_NO_DIALOGS='1', YK_LOG_DIR=str(work / 'logs'))

        def capture(scene, destination, frames=20):
            bitmap = work / (scene + '.bmp')
            subprocess.run([str(player), str(project), '--scene', 'scenes/' + scene + '.ykscene',
                            '--fixed', '--frames', str(frames), '--no-audio', '--capture', str(bitmap)],
                           env=env, check=True)
            with Image.open(bitmap) as image:
                image.save(destination)
            print('Captured', destination, flush=True)

        for scene, destination in [('courtyard', 'level1-preview.png'),
                                    ('workshop', 'level2-preview.png'),
                                    ('corridor', 'level2-corridor-preview.png'),
                                    ('wall-contact', 'level2-wall-contact-preview.png'),
                                    ('level2', 'level2-cells-preview.png'),
                                    ('utility', 'level2-utility-preview.png'),
                                    ('infirmary', 'level2-infirmary-preview.png'),
                                    ('wire-terminal', 'level2-wire-terminal-preview.png'),
                                    ('overview', 'level2-overview.png')]:
            capture(scene, previews / destination)
        if options.audit is not None:
            destination = options.audit.resolve()
            destination.mkdir(parents=True, exist_ok=True)
            width, height = options.audit_size
            project_settings['window'].update(width=width, height=height)
            (project / 'project.ykproj').write_text(json.dumps(project_settings, indent=2))
            manifest = dict(resolution=[width, height], worldPixelsPerUnit=35, frames=options.audit_frames, captures=[])
            chapters = [(1, 'courtyard', first, list(CHAPTER_ONE_ROOMS), (0, 0, 32, 18)),
                        (2, 'level2', second, list(chapter_two_rooms(second)),
                         (0, 0, *compact_world([1591 / 35, 883 / 35])))]
            for chapter, source, scene, rooms, bounds in chapters:
                for name, room_bounds in rooms + [('overview', bounds)]:
                    key = 'level' + str(chapter) + '-' + name
                    temporary, framing = fixed_view(scene, room_bounds, width / height)
                    (project / 'scenes' / (key + '.ykscene')).write_text(json.dumps(temporary, indent=2))
                    filename = key + '.png'
                    capture(key, destination / filename, frames=options.audit_frames)
                    manifest['captures'].append(dict(file=filename, source='scenes/' + source + '.ykscene',
                                                     room=name, **framing))
            (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
            print('Audit manifest:', destination / 'manifest.json')


if __name__ == '__main__':
    main()
