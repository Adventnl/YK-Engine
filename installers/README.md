# Installers — release 0.3.31

Installers live in this tracked folder at the repository root.
No compiler or build tools are needed to install or play.

| Product | Windows x64 | macOS Apple silicon | macOS disk image |
| --- | --- | --- | --- |
| YK Engine | [EXE](windows-x64/YKEngine-0.3.31-windows-x64.exe) | [PKG](macos-arm64/YKEngine-0.3.31-macos-arm64.pkg) | [DMG](macos-arm64/YKEngine-0.3.31-macos-arm64.dmg) |
| Invisible Castle | [EXE](windows-x64/InvisibleCastle-Demo-0.3.31-windows-x64.exe) | [PKG](macos-arm64/InvisibleCastle-Demo-0.3.31-macos-arm64.pkg) | [DMG](macos-arm64/InvisibleCastle-Demo-0.3.31-macos-arm64.dmg) |
| Brothers Run | [EXE](windows-x64/BrothersRun-Demo-0.3.31-windows-x64.exe) | [PKG](macos-arm64/BrothersRun-Demo-0.3.31-macos-arm64.pkg) | [DMG](macos-arm64/BrothersRun-Demo-0.3.31-macos-arm64.dmg) |

Run an EXE on Windows or open a PKG on macOS to use the setup wizard.
Alternatively, open a DMG and drag its app into Applications.
YK Engine contains the editor, player and command line; each demo installs separately.
The macOS apps require macOS 14 or newer and are signed ad hoc; Windows apps are unsigned.

Verify the installers from this folder with `shasum -a 256 -c SHA256SUMS.txt`.
Portable ZIPs and unpacked apps remain under [`release/`](../release/README.md).
