#!/usr/bin/env python3
"""Collect runnable apps, six portable downloads, and separate native installers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile


PRODUCTS = (
    ('YK Engine', 'YKEngine', 'yk_editor'),
    ('Invisible Castle', 'InvisibleCastle-Demo', 'InvisibleCastle'),
    ('Brothers Run', 'BrothersRun-Demo', 'BrothersRun'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('macos-arm64', 'windows-x64'), required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--invisible-castle', type=Path, required=True)
    parser.add_argument('--brothers-run', type=Path, required=True)
    parser.add_argument('--installers', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path('release'))
    parser.add_argument('--installer-out', type=Path,
                        help='Installer destination (default: installers/ beside --out)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    out = args.out.resolve()
    installer_root = (args.installer_out.resolve() if args.installer_out else
                      out.parent / 'installers')
    manifest_path = out / 'manifest.json'
    manifest = {'version': args.version, 'sourceCommit': commit, 'apps': []}
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        if (manifest['version'], manifest['sourceCommit']) != (args.version, commit):
            parser.error('Output contains a different release; use a new output directory')
    macos = args.platform.startswith('macos')
    sources = (args.engine, args.invisible_castle, args.brothers_run)
    # Validate all inputs before replacing any existing products.
    for (name, stem, executable), source in zip(PRODUCTS, sources):
        program = (Path('Contents/MacOS') / executable if macos else
                   Path('bin/yk_editor.exe') if name == 'YK Engine' else Path(executable + '.exe'))
        if not (source / program).is_file():
            parser.error(f'Missing runnable program: {source / program}')
        extensions = ('pkg', 'dmg') if macos else ('exe',)
        for extension in extensions:
            installer = args.installers / f'{stem}-{args.version}-{args.platform}.{extension}'
            if not installer.is_file():
                parser.error(f'Missing installer: {installer}')
    apps = out / 'apps' / args.platform
    installers = installer_root / args.platform
    downloads = out / 'downloads'
    for directory in (apps, installers, downloads):
        directory.mkdir(parents=True, exist_ok=True)
    manifest['apps'] = [entry for entry in manifest['apps'] if entry['platform'] != args.platform]
    for (name, stem, executable), source in zip(PRODUCTS, sources):
        app = apps / (name + '.app' if macos else name)
        if app.exists():
            shutil.rmtree(app)
        if macos:
            subprocess.run(['ditto', str(source), str(app)], check=True)
        else:
            shutil.copytree(source, app, symlinks=True)
        archive = downloads / f'{stem}-{args.version}-{args.platform}.zip'
        archive.unlink(missing_ok=True)
        if macos:
            subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
                            str(app), str(archive)], check=True)
        else:
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zipped:
                for path in sorted(app.rglob('*')):
                    if path.is_file():
                        zipped.write(path, path.relative_to(app.parent))
        for extension in (('pkg', 'dmg') if macos else ('exe',)):
            filename = f'{stem}-{args.version}-{args.platform}.{extension}'
            source_installer = args.installers / filename
            destination = installers / filename
            if source_installer.resolve() != destination.resolve():
                shutil.copy2(source_installer, destination)
        program = (app / 'Contents/MacOS' / executable if macos else
                   app / 'bin/yk_editor.exe' if name == 'YK Engine' else app / (executable + '.exe'))
        manifest['apps'].append({'name': name, 'platform': args.platform,
                                 'app': app.relative_to(out).as_posix(),
                                 'executable': program.relative_to(out).as_posix(),
                                 'download': archive.relative_to(out).as_posix()})
        print(f'Packaged {name}: {archive}', flush=True)
    manifest['apps'].sort(key=lambda entry: (entry['name'], entry['platform']))
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    files = sorted(path for directory in (out / 'downloads', installer_root)
                   for path in directory.rglob('*') if path.is_file())
    files = [path for path in files if path.suffix in ('.zip', '.exe', '.dmg', '.pkg')]
    with (out / 'SHA256SUMS.txt').open('w') as checksums, \
            (installer_root / 'SHA256SUMS.txt').open('w') as installer_checksums:
        for path in files:
            with path.open('rb') as stream:
                digest = hashlib.sha256()
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            value = digest.hexdigest()
            checksums.write(f'{value}  {Path(os.path.relpath(path, out)).as_posix()}\n')
            if path.is_relative_to(installer_root):
                installer_checksums.write(f'{value}  {path.relative_to(installer_root).as_posix()}\n')


if __name__ == '__main__':
    main()
