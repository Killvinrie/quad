#!/usr/bin/env python3
"""Publish paired F411 images and a factory HEX; standard library only."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

SLOTS = {'A': 0x08040000, 'B': 0x08060000}

def check_image(image, base):
    if not 8 <= len(image) <= 131072:
        raise ValueError('image must be 8..131072 bytes')
    sp, pc = struct.unpack_from('<II', image)
    if not (0x20000000 < sp <= 0x20020000 and sp % 8 == 0 and
            pc & 1 and base <= pc < base + len(image)):
        raise ValueError(f'image vectors do not match slot at {base:#x}')

def metadata(image, version):
    words = [0x4d415451, 1, 0, 0xffffffff, 0,
             len(image), 0, zlib.crc32(image), 0, version, 0]
    data = struct.pack('<11I', *words)
    return data + struct.pack('<I', zlib.crc32(data))

def hex_record(address, kind, data):
    raw = bytes([len(data)]) + struct.pack('>H', address) + bytes([kind]) + data
    return ':' + (raw + bytes([-sum(raw) & 255])).hex().upper() + '\n'

def factory_hex(chunks):
    lines = []
    for base, data in sorted(chunks):
        for offset in range(0, len(data), 16):
            address = base + offset
            lines.append(hex_record(0, 4, struct.pack('>H', address >> 16)))
            lines.append(hex_record(address & 65535, 0, data[offset:offset+16]))
    lines.append(hex_record(0, 1, b''))
    return ''.join(lines)

def publish(version, a, b, boot, output):
    if not 1 <= version <= 0xffffffff:
        raise ValueError('version must be 1..4294967295')
    images = {'A': a.read_bytes(), 'B': b.read_bytes()}
    output.mkdir(parents=True, exist_ok=True)
    banks = {}
    for slot, data in images.items():
        check_image(data, SLOTS[slot])
        sha = hashlib.sha256(data).hexdigest()
        name = f'quad-{version}-{slot}-{sha[:16]}.bin'
        (output/name).write_bytes(data)
        banks[slot] = {'file': name, 'size': len(data), 'sha256': sha}
    boot_data = boot.read_bytes()
    if not 8 <= len(boot_data) <= 32768:
        raise ValueError('bootloader exceeds reserved 32KB')
    meta = metadata(images['A'], version)
    factory = factory_hex([(0x08000000, boot_data), (0x08008000, meta),
                           (0x0800c000, meta), (SLOTS['A'], images['A'])])
    (output/'factory.hex').write_text(factory)
    manifest = {'target': 'stm32f411ce', 'version': version, 'banks': banks}
    tmp = output/'manifest.json.tmp'
    tmp.write_text(json.dumps(manifest, indent=2)+'\n')
    tmp.replace(output/'manifest.json')
    return manifest

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--version', type=int, required=True)
    p.add_argument('--a', type=Path, default=Path('software/build/A/quad.bin'))
    p.add_argument('--b', type=Path, default=Path('software/build/B/quad.bin'))
    p.add_argument('--boot', type=Path, default=Path('software/build/boot/boot.bin'))
    p.add_argument('--out', type=Path, default=Path('software/build/release'))
    args = p.parse_args()
    try:
        publish(args.version, args.a, args.b, args.boot, args.out)
    except (ValueError, OSError) as error:
        p.exit(1, str(error)+'\n')
    print(f'Published version {args.version}: {args.out}/manifest.json')

if __name__ == '__main__':
    main()
