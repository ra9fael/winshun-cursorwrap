"""Validate the selected source, release tag and packaged artifact (stdlib only)."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True, encoding='utf-8').strip()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def validate_source(event, name):
    manifest = json.loads((ROOT / 'plugin.json').read_text(encoding='utf-8-sig'))
    version = manifest['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Version must be X.Y.Z')
    if manifest.get('id') != 'ra9fael.cursorwrap':
        raise ValueError('plugin.json id mismatch')
    sha = git('rev-parse', 'HEAD')
    inputs = event.get('inputs', {})
    tag = ''
    publish = name == 'push' or str(inputs.get('publish', False)).lower() == 'true'
    if name == 'push':
        ref = event.get('ref', '')
        if not event.get('created') or event.get('deleted') or not re.fullmatch(r'refs/tags/v[0-9]+\.[0-9]+\.[0-9]+', ref):
            raise ValueError('Only newly created version tags can publish automatically')
        tag = ref.removeprefix('refs/tags/')
    elif not inputs.get('source_ref'):
        raise ValueError('Manual verification requires a source_ref')
    elif publish:
        tag = inputs['source_ref'].removeprefix('refs/tags/')
    if publish:
        if tag != f'v{version}':
            raise ValueError(f'Tag {tag} does not match plugin.json version {version}')
        ref = f'refs/tags/{tag}'
        if git('rev-parse', f'{ref}^{{commit}}') != sha:
            raise ValueError(f'Tag {tag} does not point at the selected source')
        remote = dict(line.split()[::-1] for line in git('ls-remote', 'origin', ref, f'{ref}^{{}}').splitlines())
        if remote.get(f'{ref}^{{}}', remote.get(ref)) != sha:
            raise ValueError(f'Remote tag {tag} does not point at the selected source')
    changelog = (ROOT / 'CHANGELOG.md').read_text(encoding='utf-8-sig')
    if f'## [{version}]' not in changelog:
        raise ValueError(f'CHANGELOG.md has no [{version}] section')
    if not (ROOT / 'LICENSE').is_file() or 'MIT' not in (ROOT / 'LICENSE').read_text(encoding='utf-8-sig'):
        raise ValueError('LICENSE (MIT) is required')
    print(f'::group::source ok v{version} tag={tag or "-"} publish={publish}')
    print('::endgroup::')
    return sha, tag, publish


def validate_package(dist: Path, version: str):
    package = dist / f'ra9fael.cursorwrap-{version}.wsplugin'
    if not package.is_file():
        raise ValueError(f'Missing package {package.name}')
    checksum = dist / f'{package.name}.sha256'
    if not checksum.is_file():
        raise ValueError('Missing .sha256 sidecar')
    expected = checksum.read_text(encoding='ascii').split()[0]
    if sha256_file(package) != expected:
        raise ValueError('SHA256 mismatch')
    with zipfile.ZipFile(package) as archive:
        names = set(archive.namelist())
        manifest = json.loads(archive.read('plugin.json'))
    if manifest != json.loads((ROOT / 'plugin.json').read_text(encoding='utf-8-sig')):
        raise ValueError('Package manifest differs from the checked-out source')
    for required in ('plugin.json', 'cursorwrap.exe', 'CursorWrapApp.dll', 'README.md', 'LICENSE'):
        if required not in names:
            raise ValueError(f'Package is missing {required}')
    for forbidden in ('.git', 'build/', 'dist/', 'plugins.log'):
        if any(n.startswith(forbidden) or n == forbidden for n in names):
            raise ValueError(f'Package contains {forbidden}')
    if any(Path(n).name.lower().startswith('qt6') and n.lower().endswith('.dll') for n in names):
        raise ValueError('Package must use the host Qt DLLs')
    print(f'::group::package ok {package.name} {package.stat().st_size} bytes')
    print('::endgroup::')


def main():
    dist = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if dist is None and 'GITHUB_EVENT_PATH' in os.environ:
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
        name = os.environ['GITHUB_EVENT_NAME']
        if name not in ('push', 'workflow_dispatch'):
            raise ValueError('Only a new version tag or manual dispatch is allowed')
        sha, tag, publish = validate_source(event, name)
        with Path(os.environ['GITHUB_OUTPUT']).open('a', encoding='utf-8') as output:
            output.write(f'sha={sha}\ntag={tag}\npublish={str(publish).lower()}\n')
        return 0
    manifest = json.loads((ROOT / 'plugin.json').read_text(encoding='utf-8-sig'))
    validate_package(Path(dist or (ROOT / 'dist')), manifest['version'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
