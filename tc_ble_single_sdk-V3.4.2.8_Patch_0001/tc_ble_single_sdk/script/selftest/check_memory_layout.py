#!/usr/bin/env python3
"""Fail the build when linker symbols escape the verified TLSR8258 layout."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


def symbols(elf: Path, nm: Path) -> dict[str, int]:
    output = subprocess.run([str(nm), "-n", str(elf)], check=True, capture_output=True, text=True).stdout
    result: dict[str, int] = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\w\s+(\S+)$", line.strip())
        if match:
            result[match.group(2)] = int(match.group(1), 16)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    sym = symbols(args.elf, args.nm)
    required = (
        "g_bms_image_manifest", "_bms_manifest_start_", "_bms_manifest_end_",
        "_bms_stack_guard_start_", "_bms_stack_guard_end_", "_ram_use_end_",
    )
    missing = [name for name in required if name not in sym]
    if missing:
        raise ValueError(f"missing linker symbols: {missing}")
    checks = {
        "manifest_size_ok": sym["_bms_manifest_end_"] - sym["_bms_manifest_start_"] == 64,
        "manifest_symbol_ok": sym["g_bms_image_manifest"] == sym["_bms_manifest_start_"],
        "image_slot_ok": args.bin.stat().st_size <= 0x1F000,
        "ram_guard_order_ok": sym["_bms_stack_guard_start_"] < sym["_bms_stack_guard_end_"] <= sym["_ram_use_end_"],
        "ram_stack_reserve_ok": sym["_ram_use_end_"] < 0x850000 - 600,
    }
    result = {"symbols": {name: sym[name] for name in required}, "checks": checks, "ok": all(checks.values())}
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
