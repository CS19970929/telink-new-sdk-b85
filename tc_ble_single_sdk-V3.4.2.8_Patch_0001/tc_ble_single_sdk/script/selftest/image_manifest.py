#!/usr/bin/env python3
"""Shared BMS image-manifest definitions and CRC helpers."""

from __future__ import annotations

import re
import struct
import subprocess
import zlib
from pathlib import Path

MAGIC = 0x464D5342
VERSION = 1
SIZE = 64
CRC_OFFSET = 48
POLYNOMIAL = 0xEDB88320
FORMAT = "<IHH11I3I"


def crc32(data: bytes, seed: int = 0) -> int:
    return zlib.crc32(data, seed) & 0xFFFFFFFF


def symbol_address(elf: Path, nm: Path, symbol: str = "g_bms_image_manifest") -> int:
    completed = subprocess.run(
        [str(nm), "-n", str(elf)], check=True, capture_output=True, text=True
    )
    pattern = re.compile(rf"^([0-9a-fA-F]+)\s+\w\s+{re.escape(symbol)}$")
    for line in completed.stdout.splitlines():
        match = pattern.match(line.strip())
        if match:
            return int(match.group(1), 16)
    raise ValueError(f"ELF symbol not found: {symbol}")


def unpack_manifest(blob: bytes, offset: int) -> dict[str, int]:
    values = struct.unpack_from(FORMAT, blob, offset)
    keys = (
        "magic", "version", "header_size", "flags", "image_start", "image_size",
        "range0_start", "range0_length", "range1_start", "range1_length",
        "image_crc32", "polynomial", "build_id", "manifest_crc32",
        "reserved0", "reserved1", "reserved2",
    )
    return dict(zip(keys, values))


def manifest_crc(manifest_bytes: bytes) -> int:
    if len(manifest_bytes) != SIZE:
        raise ValueError("manifest must be exactly 64 bytes")
    mutable = bytearray(manifest_bytes)
    mutable[CRC_OFFSET:CRC_OFFSET + 4] = b"\x00\x00\x00\x00"
    return crc32(mutable)
