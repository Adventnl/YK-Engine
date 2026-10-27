#!/usr/bin/env python3
"""Export and package the independent demo games using this platform's built engine tools."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def cmake_quote(value):
    return '[==[' + str(value).replace('\\', '/') + ']==]'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--config', default='Release')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    windows = platform.system() == 'Windows'
    if not windows and platform.system() != 'Darwin':
        parser.error('Installer packaging requires Windows or macOS')
    target = 'windows' if windows else 'macos'
    suffix = '.exe' if windows else ''
    build = args.build_dir.resolve()
    binaries = build / args.config if (build / args.config / ('yk' + suffix)).exists() else build
    tool = binaries / ('yk' + suffix)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               SDL_AUDIODRIVER='dummy', YK_LOG_DIR=str(out / 'smoke-logs'))
    for folder in ('invisible-castle', 'brothers-run'):
        project = root / 'games' / folder
        manifest = json.loads((project / 'project.ykproj').read_text())
        name = manifest['build']['productName']
        executable = manifest['build']['executable']
        workspace = out / 'work' / folder
        if workspace.exists():
            shutil.rmtree(workspace)
        workspace.mkdir(parents=True)
        run(tool, 'validate', project)
        command = [tool, 'export', project, '--target', target, '--out', workspace,
                   '--player', binaries / ('yk_player' + suffix)]
        if not windows:
            command += ['--dmg']
            if os.environ.get('YK_CODESIGN_IDENTITY'):
                command += ['--codesign', os.environ['YK_CODESIGN_IDENTITY']]
        run(*command)
        stem = f'{executable}-Demo-{args.version}-{target}-'
        if windows:
            game = workspace / (name.replace(' ', '-') + '-windows')
            run(game / (executable + suffix), '--frames', '10', '--fixed', '--no-audio', env=env)
            # A separate CPack project prevents any editor install rules entering a demo installer.
            config = workspace / 'installer-source'
            config.mkdir()
            settings = {
                'CPACK_PACKAGE_NAME': executable + 'Demo',
                'CPACK_PACKAGE_VENDOR': 'YK',
                'CPACK_PACKAGE_VERSION': args.version,
                'CPACK_PACKAGE_DESCRIPTION_SUMMARY': name + ' — Demo game made with YK Engine',
                'CPACK_PACKAGE_INSTALL_DIRECTORY': name + ' Demo',
                'CPACK_PACKAGE_FILE_NAME': stem + 'x64',
                'CPACK_GENERATOR': 'NSIS',
                'CPACK_NSIS_DISPLAY_NAME': name + ' (Demo game)',
                'CPACK_NSIS_PACKAGE_NAME': name + ' Demo',
                'CPACK_NSIS_EXECUTABLES_DIRECTORY': '.',
                'CPACK_PACKAGE_EXECUTABLES': executable + ';' + name + ' (Demo game)',
                'CPACK_NSIS_INSTALLED_ICON_NAME': executable + '.exe',
                'CPACK_NSIS_MUI_FINISHPAGE_RUN': executable + '.exe',
                'CPACK_NSIS_IGNORE_LICENSE_PAGE': 'ON',
                'CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL': 'ON',
            }
            text = 'cmake_minimum_required(VERSION 3.25)\nproject(DemoInstaller NONE)\n'
            text += f'install(DIRECTORY {cmake_quote(str(game) + "/")} DESTINATION .)\n'
            text += ''.join(f'set({key} {cmake_quote(value)})\n' for key, value in settings.items())
            text += 'include(CPack)\n'
            (config / 'CMakeLists.txt').write_text(text, encoding='utf-8')
            run('cmake', '-S', config, '-B', workspace / 'installer-build')
            run('cpack', '--config', workspace / 'installer-build/CPackConfig.cmake', '-B', out)
        else:
            app = workspace / (name + '.app')
            run(app / 'Contents/MacOS' / executable, '--frames', '10', '--fixed', '--no-audio', env=env)
            run('codesign', '--verify', '--deep', '--strict', app)
            arch = platform.machine()
            dmg = out / (stem + arch + '.dmg')
            shutil.move(workspace / (name + '.dmg'), dmg)
            stage = workspace / 'pkg-root'
            stage.mkdir()
            shutil.copytree(app, stage / app.name, symlinks=True)
            pkg = out / (stem + arch + '.pkg')
            command = ['pkgbuild', '--root', stage, '--install-location', '/Applications',
                       '--identifier', manifest['build']['identifier'], '--version', args.version]
            if os.environ.get('YK_INSTALLER_IDENTITY'):
                command += ['--sign', os.environ['YK_INSTALLER_IDENTITY']]
            run(*command, pkg)
            if os.environ.get('YK_NOTARY_PROFILE'):
                for artifact in (dmg, pkg):
                    run('xcrun', 'notarytool', 'submit', artifact, '--keychain-profile',
                        os.environ['YK_NOTARY_PROFILE'], '--wait')
                    run('xcrun', 'stapler', 'staple', artifact)
        print(f'Packaged {name} (Demo game)', flush=True)


if __name__ == '__main__':
    main()
