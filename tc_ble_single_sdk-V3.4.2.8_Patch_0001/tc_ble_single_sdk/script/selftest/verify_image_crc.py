#!/usr/bin/env python3
"""Verify BMS manifest ranges, CRC-32 and the native Telink trailer."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from image_manifest import MAGIC, POLYNOMIAL, SIZE, VERSION, crc32, manifest_crc, symbol_address, unpack_manifest


def verify(elf: Path, image: Path, nm: Path) -> dict[str, int | bool | str]:
    data = image.read_bytes()
    offset = symbol_address(elf, nm)
    if offset + SIZE > len(data):
        raise ValueError("manifest is outside the image")
    manifest_bytes = data[offset:offset + SIZE]
    manifest = unpack_manifest(data, offset)

    checks = {
        "magic_ok": manifest["magic"] == MAGIC,
        "version_ok": manifest["version"] == VERSION,
        "header_size_ok": manifest["header_size"] == SIZE,
        "polynomial_ok": manifest["polynomial"] == POLYNOMIAL,
        "manifest_crc_ok": manifest_crc(manifest_bytes) == manifest["manifest_crc32"],
        "range0_ok": manifest["range0_start"] + manifest["range0_length"] <= offset,
        "range1_ok": manifest["range1_start"] >= offset + SIZE,
        "raw_size_ok": manifest["image_size"] <= len(data),
    }
    end0 = manifest["range0_start"] + manifest["range0_length"]
    end1 = manifest["range1_start"] + manifest["range1_length"]
    if end0 > len(data) or end1 > len(data):
        raise ValueError("CRC range exceeds image")
    actual = crc32(data[manifest["range0_start"]:end0])
    actual = crc32(data[manifest["range1_start"]:end1], actual)
    checks["image_crc_ok"] = actual == manifest["image_crc32"]

    telink_size = struct.unpack_from("<I", data, 0x18)[0]
    telink_tail = struct.unpack_from("<I", data, len(data) - 4)[0]
    telink_actual = (~crc32(data[:-4])) & 0xFFFFFFFF
    checks["telink_size_ok"] = telink_size == len(data)
    checks["telink_crc_ok"] = telink_tail == telink_actual

    result: dict[str, int | bool | str] = {
        "image": str(image),
        "image_size": len(data),
        "manifest_offset": offset,
        "expected_crc32": manifest["image_crc32"],
        "actual_crc32": actual,
        "manifest_crc32": manifest["manifest_crc32"],
        "telink_expected_crc": telink_tail,
        "telink_actual_crc": telink_actual,
        "test_build": bool(manifest["flags"] & 1),
        **checks,
    }
    result["ok"] = all(checks.values())
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    result = verify(args.elf, args.image, args.nm)
    rendered = json.dumps(result, ensure_ascii=False, indent=2)
    print(rendered)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
