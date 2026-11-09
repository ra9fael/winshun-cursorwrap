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
    tag = ''
    publish = name == 'push' or str(event.get('inputs', {}).get('publish', 'false')).lower() == 'true'
    if name == 'push':
        if not event.get('created') or event.get('deleted') or not event.get('ref', '').startswith('refs/tags/'):
            raise ValueError('Only newly created tags can publish automatically')
        tag = event['ref'].removeprefix('refs/tags/')
    else:
        tag = str(event.get('inputs', {}).get('ref', '') or '')
    if publish:
        if not tag:
            raise ValueError('Publishing requires a version tag')
        if tag != f'v{version}':
            raise ValueError(f'Tag {tag} does not match plugin.json version {version}')
        tags = git('tag', '--list', tag).splitlines()
        if tag not in tags:
            raise ValueError(f'Tag {tag} does not exist')
        if git('rev-parse', f'refs/tags/{tag}^{{commit}}') != git('rev-parse', 'HEAD'):
            raise ValueError(f'Tag {tag} does not point at the selected source')
    elif tag and tag != f'v{version}':
        raise ValueError(f'Ref {tag} does not match plugin.json version {version}')
    changelog = (ROOT / 'CHANGELOG.md').read_text(encoding='utf-8-sig')
    if f'## [{version}]' not in changelog:
        raise ValueError(f'CHANGELOG.md has no [{version}] section')
    if not (ROOT / 'LICENSE').is_file() or 'MIT' not in (ROOT / 'LICENSE').read_text(encoding='utf-8-sig'):
        raise ValueError('LICENSE (MIT) is required')
    print(f'::group::source ok v{version} tag={tag or "-"} publish={publish}')
    print('::endgroup::')
    return version, tag, publish


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
    for required in ('plugin.json', 'cursorwrap.exe', 'README.md', 'LICENSE'):
        if required not in names:
            raise ValueError(f'Package is missing {required}')
    for forbidden in ('.git', 'build/', 'dist/', 'plugins.log'):
        if any(n.startswith(forbidden) or n == forbidden for n in names):
            raise ValueError(f'Package contains {forbidden}')
    print(f'::group::package ok {package.name} {package.stat().st_size} bytes')
    print('::endgroup::')


def main():
    dist = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if 'GITHUB_EVENT_PATH' in os.environ:
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
        name = os.environ['GITHUB_EVENT_NAME']
        if name not in ('push', 'workflow_dispatch'):
            raise ValueError('Only a new version tag or manual dispatch is allowed')
        version, tag, publish = validate_source(event, name)
        sha = git('rev-parse', 'HEAD')
        outputs = [
            f'sha={sha}', f'tag={tag}', f'publish={str(publish).lower()}',
            f'version={version}', f'prerelease={str("-" in version).lower()}',
        ]
        for line in outputs:
            print(f'::set-output name={line.split("=", 1)[0]}::{line.split("=", 1)[1]}')
        if dist:
            validate_package(dist, version)
        return 0
    # Local: validate the packaged artifact for the manifest version.
    manifest = json.loads((ROOT / 'plugin.json').read_text(encoding='utf-8-sig'))
    validate_package(Path(dist or (ROOT / 'dist')), manifest['version'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
