#!/usr/bin/env python3
"""Capture the actual player without changing either shipping scene or requiring a desktop."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT.parent / 'build/dev')
    options = parser.parse_args()
    player = options.build.resolve() / ('yk_player.exe' if os.name == 'nt' else 'yk_player')
    if not player.is_file():
        parser.error('Build yk_player first: cmake --build --preset dev')
    with tempfile.TemporaryDirectory(prefix='castle-preview-', dir=options.build) as work:
        work = Path(work).resolve()
        project = work / 'project'
        project.mkdir()
        shutil.copyfile(ROOT / 'project.ykproj', project / 'project.ykproj')
        for directory in ('assets', 'data', 'scenes'):
            shutil.copytree(ROOT / directory, project / directory,
                            ignore=shutil.ignore_patterns('source', '__pycache__'))
        second = json.loads((ROOT / 'scenes/level2.ykscene').read_text())
        workshop = copy.deepcopy(second)
        for entity in workshop['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = [(560 - 40) / 35, (710 - 25) / 35]
        overview = copy.deepcopy(second)
        for entity in overview['entities']:
            entity['components'] = [c for c in entity['components'] if c['type'] != 'EscapePresentation']
            for component in entity['components']:
                if component['type'] == 'Camera':
                    component['properties'].update(mode='Fixed', orthographicHeight=26.4,
                                                   viewportInset=[0, 0], clampToBounds=False)
                    entity['transform']['position'] = [1591 / 70, 883 / 70]
        corridor = copy.deepcopy(second)
        for entity in corridor['entities']:
            if entity['name'] in ('Explorer', 'Player follow camera'):
                entity['transform']['position'] = [(700 - 40) / 35, (475 - 25) / 35]
        for name, scene in [('workshop', workshop), ('overview', overview), ('corridor', corridor)]:
            (project / 'scenes' / (name + '.ykscene')).write_text(json.dumps(scene, indent=2))
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                   SDL_AUDIODRIVER='dummy', YK_NO_DIALOGS='1', YK_LOG_DIR=str(work / 'logs'))
        for scene, destination in [('courtyard', 'level1-preview.png'),
                                    ('workshop', 'level2-preview.png'),
                                    ('corridor', 'level2-corridor-preview.png'),
                                    ('overview', 'level2-overview.png')]:
            capture = work / (scene + '.bmp')
            subprocess.run([str(player), str(project), '--scene', 'scenes/' + scene + '.ykscene',
                            '--fixed', '--frames', '20', '--no-audio', '--capture', str(capture)],
                           env=env, check=True)
            with Image.open(capture) as image:
                image.save(ROOT / destination)
            print('Captured', ROOT / destination)


if __name__ == '__main__':
    main()
