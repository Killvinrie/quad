#!/usr/bin/env python3
"""Build the flash image and check startup layout against the original vectors."""
from pathlib import Path
import re
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
subprocess.run(["make", "-C", "software", "-j4"], cwd=root, check=True)
elf = root / "software/build/quad.elf"
binary = (root / "software/build/quad.bin").read_bytes()
symbols = {}
for line in subprocess.check_output(["arm-none-eabi-nm", "-n", str(elf)], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0], 16)
original = (root / "software/EWARM/startup_stm32f411xe.s").read_text()
vectors = re.findall(r"^\s+DCD\s+(\S+)", original, re.M)
words = struct.unpack_from("<" + "I" * len(vectors), binary)
assert words[0] == 0x20020000
assert symbols["g_pfnVectors"] == 0x08000000
for index, name in enumerate(vectors[1:], 1):
    expected = 0 if name == "0" else symbols[name] | 1
    assert words[index] == expected, (index, name, hex(words[index]), hex(expected))
assert symbols["USART1_IRQHandler"] != symbols["Default_Handler"]
assert symbols["SysTick_Handler"] != symbols["Default_Handler"]
assert 0x08000000 <= symbols["_sidata"] < 0x08080000
assert 0x20000000 <= symbols["_sdata"] <= symbols["_edata"] <= symbols["_sbss"]
assert symbols["_ebss"] <= symbols["_heap_start"] < symbols["_heap_end"]
assert symbols["_heap_end"] <= symbols["_stack_limit"] < symbols["_estack"]
assert len(binary) <= 512 * 1024
undefined = subprocess.check_output(["arm-none-eabi-nm", "-u", str(elf)], text=True)
assert not undefined.strip(), undefined
print(f"PASS: {len(vectors)} vectors, reset/ISR entries, RAM/flash layout, no undefined symbols")
print(f"BIN size: {len(binary)} bytes")
