#!/usr/bin/env python3
"""Host regression tests and ARM compile/link-symbol checks (no hardware needed)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]

def run(args):
    subprocess.run(args, cwd=ROOT, check=True)

with tempfile.TemporaryDirectory(prefix="quad-check-") as tmp:
    host = str(Path(tmp) / "protocol")
    run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
         "-Isoftware/Common", "-Isoftware/Core/Inc",
         "-Isoftware/esp32_oled/main",
         "test/sensors/test_protocol.c", "software/Core/Src/gps_nmea.c",
         "software/Core/Src/bmp388_math.c", "software/Core/Src/imu_calibration.c",
         "software/Core/Src/attitude_6dof.c",
         "software/esp32_oled/main/phone_control.c",
         "-lm", "-o", host])
    run([host])
    if not shutil.which("arm-none-eabi-gcc"):
        raise SystemExit("ARM compiler unavailable; host tests passed, ARM checks not run")
    tree = ET.parse(ROOT / "software/EWARM/software.ewp")
    sources = []
    for name in tree.findall(".//file/name"):
        if name.text.endswith(".c"):
            sources.append((ROOT / "software/EWARM" /
                            name.text.replace("$PROJ_DIR$/", "")).resolve())
    flags = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
             "-std=c99", "-Os", "-ffunction-sections", "-fdata-sections",
             "-Wall", "-Wextra", "-Werror", "-DUSE_HAL_DRIVER", "-DSTM32F411xE",
             "-Isoftware/Core/Inc", "-Isoftware/Common",
             "-Isoftware/Drivers/STM32F4xx_HAL_Driver/Inc",
             "-Isoftware/Drivers/CMSIS/Device/ST/STM32F4xx/Include",
             "-Isoftware/Drivers/CMSIS/Include"]
    objects = []
    for src in sources:
        obj = str(Path(tmp) / (src.stem + ".o"))
        # Vendor HAL uses unused Banks parameters for single-bank F411.
        extra = ["-Wno-unused-parameter"] if "Drivers" in src.parts else []
        run(["arm-none-eabi-gcc", *flags, *extra, "-c", str(src), "-o", obj])
        objects.append(obj)
    # Symbol resolution only, not a flashable image: the project uses IAR startup.
    run(["arm-none-eabi-gcc", "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16",
         "-mfloat-abi=hard", "-nostartfiles", "--specs=nosys.specs",
         "-Wl,-e,main", *objects, "-lm", "-o", str(Path(tmp) / "symbol-check.elf")])
    print(f"PASS: {len(sources)} ARM C files from IAR project compile and link")
