#!/usr/bin/env python3
"""Write the three-product download guide for a release."""
import sys

version = sys.argv[1]
base = f'https://github.com/Adventnl/YK-Engine/releases/download/v{version}'
print(f'''YK Engine {version} offers three separate products. Download the one you want:

| Portable app (extract the ZIP, then launch) | Windows x64 | macOS Apple silicon |
| --- | --- | --- |
| **YK Engine** | [Download app]({base}/YKEngine-{version}-windows-x64.zip) | [Download app]({base}/YKEngine-{version}-macos-arm64.zip) |
| **Invisible Castle — Demo game** | [Download app]({base}/InvisibleCastle-Demo-{version}-windows-x64.zip) | [Download app]({base}/InvisibleCastle-Demo-{version}-macos-arm64.zip) |
| **Brothers Run — Demo game** | [Download app]({base}/BrothersRun-Demo-{version}-windows-x64.zip) | [Download app]({base}/BrothersRun-Demo-{version}-macos-arm64.zip) |

Windows: keep the extracted app folder intact. Launch `YK Engine/bin/yk_editor.exe`,
`Invisible Castle/InvisibleCastle.exe`, or `Brothers Run/BrothersRun.exe`.
macOS: open the extracted `.app`, or drag it into Applications.

Setup wizards are also available:

| Download | Windows x64 installer | macOS Apple silicon installer |
| --- | --- | --- |
| **YK Engine** — blank editor, player and project tools | [Install engine]({base}/YKEngine-{version}-windows-x64.exe) | [Install engine]({base}/YKEngine-{version}-macos-arm64.pkg) |
| **Invisible Castle — Demo game** | [Install demo]({base}/InvisibleCastle-Demo-{version}-windows-x64.exe) | [Install demo]({base}/InvisibleCastle-Demo-{version}-macos-arm64.pkg) |
| **Brothers Run — Demo game** | [Install demo]({base}/BrothersRun-Demo-{version}-windows-x64.exe) | [Install demo]({base}/BrothersRun-Demo-{version}-macos-arm64.pkg) |

The engine starts with no project open. Create a new project or open any `project.ykproj` folder.
The demo installers each contain one standalone game; you do not need to install the engine to play.
macOS `.dmg` downloads provide the same apps for drag-and-drop installation.

Invisible Castle is the renamed castle escape demo. Brothers Run follows Ember and Tide, two brothers racing through a workshop to reach the exit first.

To edit the demos, download or clone the source repository. The editor discovers project manifests under `games/` and opens them from **Games** or the welcome page. **Open Project** also accepts those folders, or the `data` folder of an installed demo. Press **F5** to play.

This release includes six directly runnable app downloads, keeps installers separate, and includes the latest engine and game content from the release commit.

Windows and macOS installers are unsigned/ad hoc signed unless release signing credentials are configured. Checksums are in `SHA256SUMS.txt`.
''')
