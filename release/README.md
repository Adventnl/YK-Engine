# Desktop apps — release 0.3.31

Six runnable apps: YK Engine, Invisible Castle and Brothers Run, each for Windows x64
and macOS Apple silicon. These ZIPs contain the compiled apps and their required files.

| App | Windows x64 | macOS Apple silicon |
| --- | --- | --- |
| YK Engine | [Download ZIP](downloads/YKEngine-0.3.31-windows-x64.zip) | [Download ZIP](downloads/YKEngine-0.3.31-macos-arm64.zip) |
| Invisible Castle | [Download ZIP](downloads/InvisibleCastle-Demo-0.3.31-windows-x64.zip) | [Download ZIP](downloads/InvisibleCastle-Demo-0.3.31-macos-arm64.zip) |
| Brothers Run | [Download ZIP](downloads/BrothersRun-Demo-0.3.31-windows-x64.zip) | [Download ZIP](downloads/BrothersRun-Demo-0.3.31-macos-arm64.zip) |

The same downloads are available on [GitHub Releases](https://github.com/Adventnl/YK-Engine/releases/tag/v0.3.31).

## Run directly from this repository

The compiled apps are also unpacked under `apps/`:

- macOS: open `apps/macos-arm64/YK Engine.app`, `Invisible Castle.app` or `Brothers Run.app`.
- Windows: run `apps/windows-x64/YK Engine/bin/yk_editor.exe`,
  `apps/windows-x64/Invisible Castle/InvisibleCastle.exe` or
  `apps/windows-x64/Brothers Run/BrothersRun.exe`.

For a downloaded ZIP, extract the entire folder first. Keep each Windows app's folders
together; its executable uses the adjacent game data or engine resources. No compiler or
separate engine installation is needed to play either game. The engine opens to its blank
welcome screen; the game apps each contain only their own project.

## Installers

Setup wizards are kept in the repository root under [`installers/`](../installers/README.md):

| Product | Windows installer | macOS installer | macOS disk image |
| --- | --- | --- | --- |
| YK Engine | [EXE](../installers/windows-x64/YKEngine-0.3.31-windows-x64.exe) | [PKG](../installers/macos-arm64/YKEngine-0.3.31-macos-arm64.pkg) | [DMG](../installers/macos-arm64/YKEngine-0.3.31-macos-arm64.dmg) |
| Invisible Castle | [EXE](../installers/windows-x64/InvisibleCastle-Demo-0.3.31-windows-x64.exe) | [PKG](../installers/macos-arm64/InvisibleCastle-Demo-0.3.31-macos-arm64.pkg) | [DMG](../installers/macos-arm64/InvisibleCastle-Demo-0.3.31-macos-arm64.dmg) |
| Brothers Run | [EXE](../installers/windows-x64/BrothersRun-Demo-0.3.31-windows-x64.exe) | [PKG](../installers/macos-arm64/BrothersRun-Demo-0.3.31-macos-arm64.pkg) | [DMG](../installers/macos-arm64/BrothersRun-Demo-0.3.31-macos-arm64.dmg) |

The macOS apps are signed ad hoc, and the Windows apps are unsigned. macOS may require
approval in Privacy & Security after downloading. These macOS builds target Apple silicon;
these Windows builds target x64. The macOS apps require macOS 14 or newer.

`manifest.json` records the build version, source commit and all six executable paths.
`SHA256SUMS.txt` covers the six ZIPs and nine installers. Verify from this folder with
`shasum -a 256 -c SHA256SUMS.txt`.
