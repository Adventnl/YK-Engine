#!/usr/bin/env python3
"""Give GitHub downloads clear engine / demo labels."""
import json
import subprocess
import sys

tag = sys.argv[1]
release = json.loads(subprocess.check_output(['gh', 'api', f'repos/Adventnl/YK-Engine/releases/tags/{tag}']))
for asset in release['assets']:
    name = asset['name']
    if name.startswith('YKEngine-'):
        product = 'YK Engine'
    elif name.startswith('InvisibleCastle-Demo-'):
        product = 'Invisible Castle — Demo game'
    elif name.startswith('BrothersRun-Demo-'):
        product = 'Brothers Run — Demo game'
    else:
        continue
    system = 'Windows x64' if 'windows-x64' in name else 'macOS Apple silicon'
    kind = ('portable app' if name.endswith('.zip') else
            'disk image' if name.endswith('.dmg') else 'installer')
    subprocess.run(['gh', 'api', '--method', 'PATCH',
                    f"repos/Adventnl/YK-Engine/releases/assets/{asset['id']}",
                    '-f', f'label={product} · {system} {kind}'], check=True, stdout=subprocess.DEVNULL)
