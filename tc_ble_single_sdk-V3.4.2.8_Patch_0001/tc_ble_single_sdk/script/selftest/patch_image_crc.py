#!/usr/bin/env python3
"""Patch the linker-kept BMS manifest in a raw Telink image."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

from image_manifest import CRC_OFFSET, FORMAT, MAGIC, POLYNOMIAL, SIZE, VERSION, crc32, manifest_crc, symbol_address


def patch_image(elf: Path, image: Path, nm: Path, test_build: bool = False) -> dict[str, int]:
    data = bytearray(image.read_bytes())
    offset = symbol_address(elf, nm)
    if offset < 0x20 or offset + SIZE > len(data):
        raise ValueError(f"manifest offset 0x{offset:x} is outside raw image length 0x{len(data):x}")

    range0_start = 0x20
    range0_length = offset - range0_start
    range1_start = offset + SIZE
    range1_length = len(data) - range1_start
    state = crc32(data[range0_start:offset])
    state = crc32(data[range1_start:], state)
    build_id = crc32(elf.read_bytes())
    flags = 1 if test_build else 0

    values = (
        MAGIC, VERSION, SIZE, flags, 0, len(data),
        range0_start, range0_length, range1_start, range1_length,
        state, POLYNOMIAL, build_id, 0, 0, 0, 0,
    )
    manifest = bytearray(struct.pack(FORMAT, *values))
    struct.pack_into("<I", manifest, CRC_OFFSET, manifest_crc(manifest))
    data[offset:offset + SIZE] = manifest
    image.write_bytes(data)
    return {
        "manifest_offset": offset,
        "raw_image_size": len(data),
        "range0_start": range0_start,
        "range0_length": range0_length,
        "range1_start": range1_start,
        "range1_length": range1_length,
        "image_crc32": state,
        "build_id": build_id,
        "test_build": int(test_build),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--test-build", action="store_true")
    args = parser.parse_args()
    info = patch_image(args.elf, args.image, args.nm, args.test_build)
    for key, value in info.items():
        print(f"{key}={value:#x}" if isinstance(value, int) else f"{key}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
