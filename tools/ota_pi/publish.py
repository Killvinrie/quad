#!/usr/bin/env python3
"""Validate an uploaded release, then publish the manifest last. No dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile


def publish(source, public):
    manifest = json.loads((source/'manifest.json').read_bytes())
    version = manifest.get('version')
    if manifest.get('target') != 'stm32f411ce' or type(version) is not int or not 1 <= version <= 0xffffffff:
        raise ValueError('invalid target/version')
    images = []
    for slot, base in [('A', 0x08040000), ('B', 0x08060000)]:
        bank = manifest['banks'][slot]
        name = bank['file']
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_-]+\.bin', name):
            raise ValueError('invalid filename')
        data = (source/name).read_bytes()
        if len(data) != bank['size'] or not 8 <= len(data) <= 131072:
            raise ValueError(f'{slot}: invalid size')
        if hashlib.sha256(data).hexdigest() != bank['sha256']:
            raise ValueError(f'{slot}: SHA256 mismatch')
        sp, pc = struct.unpack_from('<II', data)
        if not (0x20000000 < sp <= 0x20020000 and sp % 8 == 0 and pc & 1 and base <= pc < base+len(data)):
            raise ValueError(f'{slot}: invalid vector table')
        images.append((name, data))
    public.mkdir(parents=True, exist_ok=True)
    current = public/'manifest.json'
    if current.exists():
        previous = json.loads(current.read_bytes())
        if previous['version'] > version or (previous['version'] == version and previous != manifest):
            raise ValueError('version must increase when changing firmware')
    # Validate both banks and existing immutable filenames before changing public files.
    for name, data in images:
        dest = public/name
        if dest.exists() and dest.read_bytes() != data:
            raise ValueError(f'immutable filename conflict: {name}')
    def atomic_write(name, data):
        fd, tmp = tempfile.mkstemp(prefix='.upload-', dir=public)
        try:
            with os.fdopen(fd, 'wb') as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
            os.chmod(tmp, 0o644)
            os.replace(tmp, public/name)
        finally:
            Path(tmp).unlink(missing_ok=True)
    for name, data in images:
        atomic_write(name, data)
    atomic_write('manifest.json', (json.dumps(manifest, indent=2)+'\n').encode())
    print(f'Published version {version} to {public}')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--public', type=Path, default=Path.home()/'quad-ota/public')
    args = p.parse_args()
    try:
        publish(args.source, args.public)
    except (OSError, ValueError, KeyError, TypeError) as error:
        p.exit(1, f'Publish rejected: {error}\n')
