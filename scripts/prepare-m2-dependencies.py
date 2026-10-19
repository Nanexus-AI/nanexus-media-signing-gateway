#!/usr/bin/env python3
"""Prepare isolated pinned sources from local upstream clones; never download/reset.
Fresh destination required. --verify accepts only exact approved tracked diffs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'cmake/dependencies.lock.json').read_text())

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args])

def verify(path, name, patch=None):
    if git(path, 'rev-parse', 'HEAD').decode().strip() != LOCK[name]:
        raise SystemExit('revision mismatch: ' + name)
    if git(path, 'ls-files', '--others', '--exclude-standard').strip():
        raise SystemExit('untracked source files: ' + name)
    if git(path, 'diff', '--cached', '--ignore-submodules=all').strip():
        raise SystemExit('staged changes: ' + name)
    # Canonical complete diff comparison rejects edits outside approved hunks too.
    changed = git(path, 'diff', '--name-only', '--ignore-submodules=all').decode().splitlines()
    if patch:
        current = git(path, 'diff', '--binary', '--ignore-submodules=all')
        # Product patches are canonical git diff output, possibly with a preamble.
        canonical = patch.read_bytes().split(b'diff --git ', 1)[1]
        if current != b'diff --git ' + canonical:
            raise SystemExit('source diff differs from approved patch: ' + name)
    elif changed:
        raise SystemExit('unexpected modifications: ' + name)
    return {'revision': LOCK[name], 'patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest() if patch else None}

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--zlm-source', type=Path)
p.add_argument('--onvif-source', type=Path)
p.add_argument('--dest', type=Path, required=True)
p.add_argument('--verify', action='store_true')
a = p.parse_args()
dest = a.dest.resolve()
patches = {'zlmediakit': ROOT / 'patches/zlmediakit/signed-access-unit.patch',
           'onvif': ROOT / 'patches/onvif/reject-undefined-tag.patch'}
paths = {'zlmediakit': dest / 'ZLMediaKit', 'zltoolkit': dest / 'ZLMediaKit/3rdpart/ZLToolKit',
         'jsoncpp': dest / 'ZLMediaKit/3rdpart/jsoncpp', 'media-server': dest / 'ZLMediaKit/3rdpart/media-server',
         'onvif': dest / 'onvif'}
if not a.verify:
    if not a.zlm_source or not a.onvif_source or dest.exists():
        p.error('fresh destination and both local source clones required')
    dest.mkdir(parents=True)
    sources = {'zlmediakit': a.zlm_source, 'zltoolkit': a.zlm_source / '3rdpart/ZLToolKit',
               'jsoncpp': a.zlm_source / '3rdpart/jsoncpp', 'media-server': a.zlm_source / '3rdpart/media-server',
               'onvif': a.onvif_source}
    for name, path in paths.items():
        subprocess.run(['git', 'clone', '--no-hardlinks', '--no-checkout', str(sources[name].resolve()), str(path)], check=True)
        subprocess.run(['git', '-C', str(path), 'checkout', '--detach', LOCK[name]], check=True)
        if name in patches:
            subprocess.run(['git', '-C', str(path), 'apply', '--check', str(patches[name])], check=True)
            subprocess.run(['git', '-C', str(path), 'apply', str(patches[name])], check=True)
manifest = {name: verify(path, name, patches.get(name)) for name, path in paths.items()}
print(json.dumps(manifest, indent=2))
