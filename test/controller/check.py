#!/usr/bin/env python3
"""Build and check the standalone F103 controller image without hardware."""
from pathlib import Path
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
project = root / "controller/stm32f103"
subprocess.run(["make", "-C", str(project), "-j4"], check=True)
image = (project / "build/controller.bin").read_bytes()
vectors = struct.unpack_from("<16I", image)
assert vectors[0] == 0x20005000, "F103C8 stack top must be 20 KiB RAM"
assert 0x08000001 <= vectors[1] < 0x08010000, "invalid reset entry"
assert 0x08000001 <= vectors[15] < 0x08010000, "invalid SysTick entry"
assert len(image) <= 64 * 1024, "image exceeds F103C8 Flash"
symbols = subprocess.check_output(
    ["arm-none-eabi-nm", "-u", str(project / "build/controller.elf")], text=True
)
assert not symbols.strip(), f"unresolved symbols: {symbols}"
print(f"PASS: F103 image, reset/SysTick vectors, no unresolved symbols ({len(image)} bytes)")
