#!/usr/bin/env python3
"""Create a separate one-bit-corrupted certification-test image."""

from __future__ import annotations

import argparse
from pathlib import Path

from image_manifest import symbol_address, unpack_manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--offset", type=lambda x: int(x, 0))
    parser.add_argument("--bit", type=int, default=0, choices=range(8))
    args = parser.parse_args()

    source = bytearray(args.image.read_bytes())
    manifest_offset = symbol_address(args.elf, args.nm)
    manifest = unpack_manifest(source, manifest_offset)
    offset = args.offset if args.offset is not None else manifest["range0_start"] + 16
    ranges = (
        range(manifest["range0_start"], manifest["range0_start"] + manifest["range0_length"]),
        range(manifest["range1_start"], manifest["range1_start"] + manifest["range1_length"]),
    )
    if not any(offset in item for item in ranges):
        raise ValueError("corruption offset must be inside a CRC-covered range")
    source[offset] ^= 1 << args.bit
    args.output.write_bytes(source)
    print(f"corrupted_offset=0x{offset:x} bit={args.bit} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
