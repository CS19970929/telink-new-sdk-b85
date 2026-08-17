#!/usr/bin/env python3
# =============================================================================
# bms_tools/bms.py - Unified AI-callable orchestrator for the TLSR8251 BMS.
#
# This is the single entry point for build, size/map analysis, firmware
# integrity and static analysis. All operations are scripted and reproducible;
# no Telink IDE GUI or generated project files are required.
#
# Usage:
#   python bms_tools/bms.py env                 # environment check
#   python bms_tools/bms.py build [--jobs N]    # incremental build
#   python bms_tools/bms.py rebuild [--jobs N]  # clean + build
#   python bms_tools/bms.py objcopy             # generate .bin
#   python bms_tools/bms.py check-fw            # tl_check_fw2.exe check
#   python bms_tools/bms.py size                # size report (text/data/bss)
#   python bms_tools/bms.py map                 # MAP analysis (flash/ram/sections)
#   python bms_tools/bms.py manifest            # firmware integrity manifest
#   python bms_tools/bms.py verify              # verify bin against manifest
#   python bms_tools/bms.py static              # cppcheck static analysis
#   python bms_tools/bms.py sources --check     # validate locked link order
#   python bms_tools/bms.py baseline <ref_bin>  # compare new build to reference
#   python bms_tools/bms.py ci                   # complete host build pipeline
#
# All paths are resolved relative to the repo root (this file lives in
# bms_tools/), so the tooling is host- and machine-portable.
# =============================================================================
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

# ----------------------------------------------------------------------------
# Path resolution (machine-portable)
# ----------------------------------------------------------------------------
_HERE = Path(__file__).resolve().parent
REPO_ROOT = _HERE.parent
SDK_SUBDIR = "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk"
SDK_DIR = (REPO_ROOT / SDK_SUBDIR).resolve()
PROJ_DIR = (SDK_DIR / "project" / "tlsr_tc32" / "B85").resolve()
LINKER_FILE = PROJ_DIR / "boot.link"
PROJ_LIB_DIR = SDK_DIR / "proj_lib"
REQUIRED_VENDOR_LIBS = (
    PROJ_LIB_DIR / "liblt_825x.a",
    PROJ_LIB_DIR / "liblt_general_stack.a",
)
TL_CHECK_FW2 = (SDK_DIR / "script" / "tl_check_fw" / "tl_check_fw2.exe").resolve()

# Keep command-line artifacts inside the Telink B85 project tree, but in a
# dedicated sibling of the Eclipse/IDE output directory.  The two build
# systems must never share objects or generated firmware.
BUILD_DIR = (PROJ_DIR / "825x_ble_sample_cli").resolve()
OBJ_DIR = BUILD_DIR / "obj"
GEN_DIR = BUILD_DIR / "gen"
ELF = BUILD_DIR / "825x_ble_sample.elf"
BIN = BUILD_DIR / "825x_ble_sample.bin"
RAW_BIN = BUILD_DIR / "825x_ble_sample.raw.bin"
LST = GEN_DIR / "825x_ble_sample.lst"
MAP = GEN_DIR / "825x_ble_sample.map"
MANIFEST = BUILD_DIR / "fw_manifest.json"
SOURCE_ORDER_FILE = _HERE / "source_order.txt"
IDE_BUILD_DIR = PROJ_DIR / "825x_ble_sample"

# Source group order is inherited from the Telink IDE generated makefile.  The
# application directory is recursive so project-owned feature subdirectories
# can be added without hand-written Make rules. Vendor/SDK directories remain
# deliberately non-recursive to avoid silently compiling unrelated examples.
SOURCE_GROUPS = (
    (Path("vendor/common"), False),
    (Path("vendor/ble_sample"), True),
    (Path("drivers/B85"), False),
    (Path("drivers/B85/flash"), False),
    (Path("drivers/B85/driver_ext"), False),
    (Path("common"), False),
    (Path("boot/B85"), False),
    (Path("application/usbstd"), False),
    (Path("application/print"), False),
    (Path("application/keyboard"), False),
    (Path("application/audio"), False),
    (Path("application/app"), False),
)

# --------------------------------------------------------------------------
# Space-free junction for GNU Make.
# The repo directory contains a literal space ("..._Patch_0001 (1)") that
# breaks GNU Make's whitespace tokenizer on Windows regardless of escaping.
# We transparently create a junction from a space-free path to the repo root,
# and give Make all paths via that junction. The on-disk build artifacts are
# identical (the junction resolves to the same directory); it is purely a
# Make-facing path rewrite.
# --------------------------------------------------------------------------
JUNCTION = Path("C:/opencode/bms_repo")
_junction_ok = False


def _ensure_junction() -> None:
    """Create/refresh JUNCTION -> REPO_ROOT. Idempotent. Outside the repo so
    it never gets committed; lives under the pre-approved opencode temp dir."""
    global _junction_ok
    if _junction_ok:
        return
    parent = JUNCTION.parent
    parent.mkdir(parents=True, exist_ok=True)
    if os.path.lexists(str(JUNCTION)):
        try:
            tgt = (JUNCTION / ".git").resolve() if (JUNCTION / ".git").exists() else None
        except Exception:
            tgt = None
        if tgt is not None and tgt.parent == REPO_ROOT:
            _junction_ok = True
            return
        # Stale or wrong junction: remove it.
        try:
            subprocess.run(["cmd", "/c", "rmdir", str(JUNCTION)], check=False)
        except Exception:
            pass
    # Create the junction (mklink /J). Junctions do NOT need admin on Win10+.
    r = subprocess.run(["cmd", "/c", "mklink", "/J", str(JUNCTION), str(REPO_ROOT)],
                       capture_output=True, text=True)
    if r.returncode != 0 or not JUNCTION.exists():
        _die(f"failed to create space-free junction {JUNCTION} -> {REPO_ROOT}: {r.stderr}")
    _junction_ok = True


def _junc(p: Path) -> Path:
    """Return the space-free junction-based equivalent of a repo-rooted path."""
    _ensure_junction()
    try:
        rel = p.resolve().relative_to(REPO_ROOT)
    except ValueError:
        return p  # outside repo: leave as-is
    return JUNCTION / rel

# Canonical toolchain locations (Telink IoT Studio install on this PC).
DEFAULT_TC32_DIR = Path("C:/TelinkIoTStudio/opt/tc32/bin")
DEFAULT_BDT = Path("C:/TelinkIoTStudio/tools/libusbBDT/bin/bdt.exe")
DEFAULT_CPPCHECK = Path("C:/Program Files/cppcheck/cppcheck.exe")
DEFAULT_STATIC_REPORT_TEMPLATE = Path(
    "D:/c11认证文档/功能安全/13849模板/XXX-BMS 软件静态分析报告.xlsx"
)
SDK_BASELINE_COMMIT = "b9d4d0790cd7f163867872bf6ce7980a71dfee76"
PROJECT_SOURCE_PREFIX = "vendor/ble_sample/"
STATIC_SCOPE_POLICY = (
    "仅检查 vendor/ble_sample 应用层；官方 SDK/工具链文件仅为解析真实类型、宏和条件编译，"
    "其诊断按范围策略排除"
)
STATIC_ANALYSIS_DIR = _HERE / "static_analysis"
CPPCHECK_CONFIG = STATIC_ANALYSIS_DIR / "cppcheck.cfg"
CPPCHECK_PLATFORM = STATIC_ANALYSIS_DIR / "tc32-platform.xml"
STATIC_REPORT_BUILDER = STATIC_ANALYSIS_DIR / "fill_static_report.py"

# Flash layout (per docs/project_flash_map_8251_512k.md). Hard-coded so the
# firmware-integrity tooling knows the protected program-flash range without
# parsing the linker script.
FW_SLOT_A_BASE = 0x00000
FW_SLOT_A_END = 0x1EFFF   # inclusive
FW_SLOT_B_BASE = 0x20000
FW_SLOT_B_END = 0x3EFFF
OTA_META_A = (0x1F000, 0x1FFFF)
OTA_META_B = (0x3F000, 0x3FFFF)
SDK_RESERVED = (0x74000, 0x7FFFF)

# This mismatch is inherited from the verified IDE configuration and is only
# reported, never auto-corrected: changing it would move the stack and alter
# firmware behavior. Confirm the populated die/SRAM before changing it.
DECLARED_MCU = "TLSR8251"
STARTUP_PROFILE = "MCU_STARTUP_8258"
STARTUP_SRAM_END = 0x850000
TLSR8251_SRAM_END_IN_SDK = 0x848000
TARGET_CONFIGURATION_RISK = (
    "declared TLSR8251 target uses inherited MCU_STARTUP_8258 profile; "
    "verify populated die/SRAM and approved baseline before changing it"
)


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _die(msg: str, code: int = 2) -> None:
    sys.stderr.write(f"[bms] ERROR: {msg}\n")
    sys.exit(code)


def _info(msg: str) -> None:
    print(f"[bms] {msg}")


def _run(cmd: list[str], cwd: Path | None = None, env: dict | None = None,
         check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
    if env is None:
        env = dict(os.environ)
    if capture:
        return subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env,
                               check=check, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    return subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, check=check)


def _ensure_toolchain_env(env: dict) -> dict:
    """Prepend the pinned TC32 directory to the supplied environment.

    Windows environment keys are case-insensitive, but a plain ``dict`` is
    not.  ``dict(os.environ)`` normally contains ``Path`` on this host, while
    Python's executable lookup expects the exact key ``PATH``.  Canonicalise
    all case variants to one entry before launching a subprocess.
    """
    path_keys = [key for key in env if key.upper() == "PATH"]
    current_path = env.get(path_keys[0], "") if path_keys else ""
    for key in path_keys:
        env.pop(key, None)
    if (shutil.which("tc32-elf-gcc", path=current_path) is None
            and DEFAULT_TC32_DIR.exists()):
        current_path = str(DEFAULT_TC32_DIR) + os.pathsep + current_path
    env["PATH"] = current_path
    return env


def _tc32_tool(name: str) -> str:
    """Return an auditable absolute path to a pinned TC32 executable."""
    filename = name if name.lower().endswith(".exe") else f"{name}.exe"
    pinned = DEFAULT_TC32_DIR / filename
    if pinned.exists():
        return str(pinned)
    discovered = shutil.which(name)
    if discovered:
        return discovered
    _die(f"TC32 tool missing: {name} (expected {pinned})")
    raise AssertionError("unreachable")


def _need_make() -> str:
    make = shutil.which("make") or shutil.which("gmake")
    if make is None:
        # Fall back to qtools make on this PC.
        cand = Path("C:/qp/qtools/bin/make.exe")
        if cand.exists():
            return str(cand)
        _die("GNU make not found on PATH (and C:/qp/qtools/bin/make.exe missing).")
    return make


# ----------------------------------------------------------------------------
# Subcommand: env
# ----------------------------------------------------------------------------
def cmd_env(args: argparse.Namespace) -> int:
    _info("Environment check")
    print("-" * 70)
    print(f"repo root            : {REPO_ROOT}")
    print(f"SDK dir               : {SDK_DIR}  (exists={SDK_DIR.exists()})")
    print(f"project B85 dir       : {PROJ_DIR}  (exists={PROJ_DIR.exists()})")
    print(f"linker script         : {LINKER_FILE}  (exists={LINKER_FILE.exists()})")
    print(f"proj_lib dir          : {PROJ_LIB_DIR}  (exists={PROJ_LIB_DIR.exists()})")
    for library in REQUIRED_VENDOR_LIBS:
        print(f"required vendor lib   : {library}  (exists={library.exists()})")
    print(f"tl_check_fw2.exe      : {TL_CHECK_FW2}  (exists={TL_CHECK_FW2.exists()})")
    print(f"build dir              : {BUILD_DIR}")
    try:
        source_order = _load_source_order_strict()
    except SourceOrderError as exc:
        _die(str(exc))
    print(f"source/link order     : {SOURCE_ORDER_FILE}  "
          f"({len(source_order)} entries, sha256={_source_order_sha256(source_order)})")
    print("-" * 70)

    studio_version = "unknown"
    studio_version_file = DEFAULT_TC32_DIR.parents[2] / "version.csv"
    if studio_version_file.exists():
        parts = studio_version_file.read_text(encoding="utf-8", errors="replace").strip().split(",")
        if len(parts) >= 2:
            studio_version = parts[1]
    print(f"host OS              : {os.name} / {sys.platform}")
    print(f"Telink IoT Studio    : {studio_version}  ({DEFAULT_TC32_DIR.parents[2]})")
    checks = [
        ("python", sys.executable, sys.version.split()[0]),
        ("make", _need_make(), _tool_version([_need_make(), "--version"], 0)),
    ]
    # tc32 toolchain
    tc_bin = shutil.which("tc32-elf-gcc") or (str(DEFAULT_TC32_DIR / "tc32-elf-gcc.exe")
                                              if DEFAULT_TC32_DIR.exists() else None)
    checks.append(("tc32-elf-gcc", tc_bin, _tool_version([tc_bin, "--version"], 0) if tc_bin else "MISSING"))
    # tools
    bdt_version = "unknown"
    bdt_cfg = DEFAULT_BDT.parent / "config.ini"
    if bdt_cfg.exists():
        match = re.search(r"\[bdt_version\]\s*([0-9.]+)",
                          bdt_cfg.read_text(encoding="utf-8", errors="replace"), re.I)
        if match:
            bdt_version = match.group(1)
    checks.append(("bdt.exe", str(DEFAULT_BDT) if DEFAULT_BDT.exists() else "MISSING",
                   f"Telink BDT CLI {bdt_version}" if DEFAULT_BDT.exists() else "install Telink IoT Studio"))
    checks.append(("cppcheck.exe", str(DEFAULT_CPPCHECK) if DEFAULT_CPPCHECK.exists() else "MISSING",
                   _tool_version([str(DEFAULT_CPPCHECK), "--version"], 1) if DEFAULT_CPPCHECK.exists() else "MISSING"))
    for n in ("git", "cmake", "ninja", "python"):
        p = shutil.which(n)
        checks.append((n, p or "MISSING", _tool_version([p, "--version"], 0) if p else "-"))
    for name, path, ver in checks:
        print(f"{name:<20} {ver:<28} {path}")
    print("-" * 70)
    print("Flash layout (8251 / 512K, per docs/project_flash_map_8251_512k.md):")
    print(f"  Firmware A (running) : 0x{FW_SLOT_A_BASE:05X} - 0x{FW_SLOT_A_END:05X}")
    print(f"  OTA reserved A       : 0x{OTA_META_A[0]:05X} - 0x{OTA_META_A[1]:05X}")
    print(f"  Firmware B (OTA)     : 0x{FW_SLOT_B_BASE:05X} - 0x{FW_SLOT_B_END:05X}")
    print(f"  OTA reserved B       : 0x{OTA_META_B[0]:05X} - 0x{OTA_META_B[1]:05X}")
    print(f"  SDK reserved         : 0x{SDK_RESERVED[0]:05X} - 0x{SDK_RESERVED[1]:05X}")
    print("-" * 70)
    print(f"declared MCU          : {DECLARED_MCU}")
    print(f"startup profile       : {STARTUP_PROFILE}")
    print(f"startup SRAM end      : 0x{STARTUP_SRAM_END:06X}")
    print(f"TLSR8251 SRAM end     : 0x{TLSR8251_SRAM_END_IN_SDK:06X}")
    print(f"target identity       : {TARGET_CONFIGURATION_RISK}")
    # Critical sanity: toolchain must exist.
    if not (DEFAULT_TC32_DIR.exists() and (DEFAULT_TC32_DIR / "tc32-elf-gcc.exe").exists()):
        _die("TC32 toolchain MISSING. Install Telink IoT Studio or copy C:/TelinkIoTStudio/opt/tc32/bin.")
    if not LINKER_FILE.exists():
        _die(f"Linker script missing: {LINKER_FILE}")
    if not PROJ_LIB_DIR.exists():
        _die(f"proj_lib missing: {PROJ_LIB_DIR}")
    missing_libraries = [str(path) for path in REQUIRED_VENDOR_LIBS if not path.exists()]
    if missing_libraries:
        _die("required official SDK libraries missing: " + ", ".join(missing_libraries))
    _info("environment tools OK; target identity risk is reported above")
    return 0


def _tool_version(cmd: list[str], line: int) -> str:
    try:
        out = subprocess.run(cmd, check=False, capture_output=True, text=True, timeout=10)
        lines = (out.stdout or out.stderr or "").splitlines()
        return lines[line] if len(lines) > line else (lines[0] if lines else "?")
    except Exception:
        return "?"


# ----------------------------------------------------------------------------
# Source/link order management
# ----------------------------------------------------------------------------
class SourceOrderError(RuntimeError):
    """The version-controlled source order is missing, stale or ambiguous."""


def _normalised_order_bytes(entries: list[str]) -> bytes:
    return ("\n".join(entries) + "\n").encode("utf-8")


def _source_order_sha256(entries: list[str]) -> str:
    return hashlib.sha256(_normalised_order_bytes(entries)).hexdigest()


def _discover_managed_sources() -> list[str]:
    """Return all managed .c/.S files in deterministic IDE-compatible order."""
    entries: list[str] = []
    seen: set[str] = set()
    for group_rel, recursive in SOURCE_GROUPS:
        group = SDK_DIR / group_rel
        if not group.exists():
            continue
        candidates = group.rglob("*") if recursive else group.iterdir()
        sources = sorted(
            (path for path in candidates
             if path.is_file() and path.suffix in (".c", ".S")),
            key=lambda path: path.relative_to(group).as_posix(),
        )
        for source in sources:
            rel = source.relative_to(SDK_DIR).as_posix()
            folded = rel.casefold()
            if folded in seen:
                raise SourceOrderError(f"duplicate/case-colliding source: {rel}")
            seen.add(folded)
            entries.append(rel)
    return entries


def _read_source_order(path: Path = SOURCE_ORDER_FILE) -> list[str]:
    if not path.exists():
        raise SourceOrderError(f"source order file missing: {path}")
    entries: list[str] = []
    seen: set[str] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        value = raw.strip()
        if not value or value.startswith("#"):
            continue
        normalised = Path(value.replace("\\", "/"))
        if normalised.is_absolute() or ".." in normalised.parts:
            raise SourceOrderError(f"unsafe source_order entry at line {number}: {value}")
        rel = normalised.as_posix()
        if normalised.suffix not in (".c", ".S"):
            raise SourceOrderError(f"unsupported source suffix at line {number}: {value}")
        folded = rel.casefold()
        if folded in seen:
            raise SourceOrderError(f"duplicate/case-colliding entry at line {number}: {value}")
        seen.add(folded)
        entries.append(rel)
    if not entries:
        raise SourceOrderError(f"source order file is empty: {path}")
    return entries


def _validate_source_order(entries: list[str], discovered: list[str] | None = None) -> None:
    if discovered is None:
        discovered = _discover_managed_sources()
    listed = set(entries)
    actual = set(discovered)
    missing = [entry for entry in entries if entry not in actual]
    unlisted = [entry for entry in discovered if entry not in listed]
    if missing or unlisted:
        details = []
        if missing:
            details.append("missing on disk: " + ", ".join(missing))
        if unlisted:
            details.append("unlisted new source: " + ", ".join(unlisted))
        raise SourceOrderError("; ".join(details) +
                               ". Run 'python bms_tools/bms.py sources --update' and review the Git diff.")


def _load_source_order_strict() -> list[str]:
    entries = _read_source_order()
    _validate_source_order(entries)
    return entries


def _write_source_order(entries: list[str]) -> None:
    header = (
        "# TLSR8251 BMS authoritative source/link order.\n"
        "# Generated explicitly by: python bms_tools/bms.py sources --update\n"
        "# Review every order change in Git; build/rebuild never edits this file.\n"
    )
    SOURCE_ORDER_FILE.write_text(
        header + _normalised_order_bytes(entries).decode("utf-8"), encoding="utf-8"
    )


def _parse_ide_source_order(ide_build_dir: Path = IDE_BUILD_DIR) -> list[str]:
    """Read the optional Eclipse CDT generated makefiles without depending on them."""
    makefile = ide_build_dir / "makefile"
    if not makefile.exists():
        raise SourceOrderError(f"IDE generated makefile missing: {makefile}")
    include_files: list[Path] = []
    include_re = re.compile(r"^-include\s+(.+subdir\.mk)\s*$")
    for line in makefile.read_text(encoding="utf-8", errors="replace").splitlines():
        match = include_re.match(line.strip())
        if match:
            include_files.append(ide_build_dir / match.group(1).replace("\\", "/"))
    if not include_files:
        raise SourceOrderError(f"no subdir.mk includes found in {makefile}")

    discovered = _discover_managed_sources()
    object_to_source: dict[str, str] = {}
    for source in discovered:
        obj = Path(source).with_suffix(".o").as_posix().casefold()
        if obj in object_to_source:
            raise SourceOrderError(f"ambiguous IDE object target: {obj}")
        object_to_source[obj] = source

    result: list[str] = []
    for subdir_mk in include_files:
        if not subdir_mk.exists():
            raise SourceOrderError(f"IDE included file missing: {subdir_mk}")
        in_objects = False
        for raw in subdir_mk.read_text(encoding="utf-8", errors="replace").splitlines():
            stripped = raw.strip()
            if stripped == "OBJS += \\":
                in_objects = True
                continue
            if not in_objects:
                continue
            value = stripped.rstrip("\\").strip().removeprefix("./").replace("\\", "/")
            if not value:
                in_objects = False
                continue
            if not value.endswith(".o"):
                continue
            source = object_to_source.get(value.casefold())
            if source is None:
                raise SourceOrderError(f"IDE object is outside managed source set: {value}")
            result.append(source)
    _validate_source_order(result, discovered)
    return result


def _print_order_diff(reference: list[str], candidate: list[str],
                      reference_name: str, candidate_name: str) -> None:
    mismatch_indexes = [index for index, pair in enumerate(zip(reference, candidate))
                        if pair[0] != pair[1]]
    if len(reference) != len(candidate):
        mismatch_indexes.extend(range(min(len(reference), len(candidate)),
                                      max(len(reference), len(candidate))))
    print(f"{reference_name}: {len(reference)} entries, sha256={_source_order_sha256(reference)}")
    print(f"{candidate_name}: {len(candidate)} entries, sha256={_source_order_sha256(candidate)}")
    for index in mismatch_indexes[:20]:
        left = reference[index] if index < len(reference) else "<missing>"
        right = candidate[index] if index < len(candidate) else "<missing>"
        print(f"  [{index:03d}] {reference_name}={left}")
        print(f"        {candidate_name}={right}")
    if len(mismatch_indexes) > 20:
        print(f"  ... {len(mismatch_indexes) - 20} more order differences")


def cmd_sources(args: argparse.Namespace) -> int:
    try:
        if args.source_action == "update":
            previous = _read_source_order() if SOURCE_ORDER_FILE.exists() else []
            updated = _discover_managed_sources()
            _write_source_order(updated)
            _print_order_diff(previous, updated, "previous", "updated")
            _info(f"source order updated: {SOURCE_ORDER_FILE}; review and commit the Git diff")
            return 0

        if args.source_action == "import-ide":
            previous = _read_source_order() if SOURCE_ORDER_FILE.exists() else []
            imported = _parse_ide_source_order()
            _write_source_order(imported)
            _print_order_diff(previous, imported, "previous", "IDE")
            _info("IDE order imported explicitly; review and commit the Git diff")
            return 0

        current = _load_source_order_strict()
        if args.source_action == "compare-ide":
            ide_order = _parse_ide_source_order()
            if current != ide_order:
                _print_order_diff(current, ide_order, "locked", "IDE")
                _info("IDE order MISMATCH; no file was modified")
                return 1
            _info(f"IDE order MATCH: {len(current)} entries, sha256={_source_order_sha256(current)}")
            return 0

        _info(f"source order OK: {len(current)} entries, sha256={_source_order_sha256(current)}")
        return 0
    except SourceOrderError as exc:
        _die(str(exc))
    return 2


# ----------------------------------------------------------------------------
# Subcommand: build / rebuild
# ----------------------------------------------------------------------------
def _gen_sources_mk(build_dir: Path = BUILD_DIR) -> None:
    """Generate compile rules in the exact version-controlled link order."""
    _ensure_junction()
    obj_dir = build_dir / "obj"
    gen_dir = build_dir / "gen"
    build_dir.mkdir(parents=True, exist_ok=True)
    obj_dir.mkdir(parents=True, exist_ok=True)
    try:
        source_order = _load_source_order_strict()
    except SourceOrderError as exc:
        _die(str(exc))

    sdk_j = _junc(SDK_DIR)
    obj_j = _junc(obj_dir)
    out_lines = [
        "# auto-generated by bms_tools/bms.py - do not edit",
        f"# generated_at: {_now_iso()}",
        f"# source_order_sha256: {_source_order_sha256(source_order)}",
        f"# NOTE: paths use the space-free junction {JUNCTION} -> {REPO_ROOT}",
    ]
    objs: list[str] = []
    subdirs_to_create: set[Path] = set()
    for rel_text in source_order:
        rel = Path(rel_text)
        src = SDK_DIR / rel
        obj_rel = rel.with_suffix(".o")
        obj = (obj_j / obj_rel).as_posix()
        src_j_posix = (sdk_j / rel).as_posix()
        objs.append(obj)
        subdirs_to_create.add((obj_dir / obj_rel).parent)
        out_lines.append("")
        out_lines.append(f"{obj}: {src_j_posix}")
        if src.suffix == ".S":
            out_lines.append(f"\t@echo 'Assembling: {src.name}'")
            out_lines.append(f"\t$(CC) $(AFLAGS) -c -o\"$@\" \"$<\"")
        else:
            out_lines.append(f"\t@echo 'Building: {src.name}'")
            out_lines.append(f"\t$(CC) $(CFLAGS) -c -o\"$@\" \"$<\"")
    out_lines.insert(4, f"OBJS := {' '.join(objs)}")
    for directory in subdirs_to_create:
        directory.mkdir(parents=True, exist_ok=True)
    gen_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "sources.mk").write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    _info(f"generated sources.mk: {build_dir / 'sources.mk'}  ({len(objs)} objects)")


def _invoke_make(targets: list[str], jobs: int = 1,
                 build_dir: Path = BUILD_DIR) -> None:
    resolved_build = build_dir.resolve()
    if resolved_build != BUILD_DIR:
        _die(f"refusing Make clean/build outside the dedicated CLI directory: {resolved_build}")
    env = _ensure_toolchain_env(dict(os.environ))
    make = _need_make()
    _gen_sources_mk(build_dir)
    # Pass all Make-facing paths via the junction (space-free).
    repo_j = _junc(REPO_ROOT).as_posix()
    sdk_j = _junc(SDK_DIR).as_posix()
    build_j = _junc(build_dir).as_posix()
    cmd = [make, "-f", str(_HERE / "build.mk"),
           "-j", str(jobs),
           f"REPO_ROOT={repo_j}",
           f"SDK_DIR={sdk_j}",
           f"BUILD_DIR={build_j}"]
    cmd += targets
    _info(f"make targets={targets} jobs={jobs} (via junction {JUNCTION})")
    r = _run(cmd, cwd=JUNCTION, env=env, check=False, capture=True)
    log_dir = build_dir / "gen"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "build.log"
    log_path.write_text(r.stdout or "", encoding="utf-8")
    warning_count = len(re.findall(r"\bwarning:", r.stdout or "", re.I))
    error_count = len(re.findall(r"\berror:", r.stdout or "", re.I))
    _info(f"compiler diagnostics: warnings={warning_count} errors={error_count} -> {log_path}")
    # Stream a trimmed tail so AI can see errors without 5000-line dumps.
    tail = r.stdout.splitlines()[-40:]
    print("\n".join(tail))
    if r.returncode != 0:
        raise subprocess.CalledProcessError(r.returncode, cmd, output=r.stdout)


def cmd_build(args: argparse.Namespace) -> int:
    _invoke_make(["all"], jobs=args.jobs)
    _finalize_firmware()
    _info("build complete (ELF/MAP/raw BIN/canonical BIN)")
    return 0


def cmd_rebuild(args: argparse.Namespace) -> int:
    _invoke_make(["clean"], jobs=1)
    _invoke_make(["all"], jobs=args.jobs)
    _finalize_firmware()
    _info("rebuild complete (ELF/MAP/raw BIN/canonical BIN)")
    return 0


# ----------------------------------------------------------------------------
# Subcommand: objcopy / check-fw
# ----------------------------------------------------------------------------
def cmd_objcopy(args: argparse.Namespace) -> int:
    if not ELF.exists():
        _die(f"ELF missing: {ELF}. Run 'build' first.")
    _finalize_firmware()
    _info(f"canonical BIN written: {BIN} ({BIN.stat().st_size} bytes)")
    return 0


def _objcopy(elf_path: Path, bin_path: Path) -> None:
    bin_path.parent.mkdir(parents=True, exist_ok=True)
    _run([_tc32_tool("tc32-elf-objcopy"), "-v", "-O", "binary",
          str(elf_path), str(bin_path)], env=dict(os.environ), check=True)


def _finalize_firmware() -> None:
    """Generate an auditable raw image and one canonical Telink-checked image."""
    if not ELF.exists():
        _die(f"ELF missing: {ELF}. Run 'build' first.")
    if not TL_CHECK_FW2.exists():
        _die(f"tl_check_fw2.exe missing: {TL_CHECK_FW2}")
    _objcopy(ELF, RAW_BIN)
    shutil.copy2(RAW_BIN, BIN)
    _run_tl_check_fw(BIN)
    if not _telink_crc_details(BIN.read_bytes()).get("valid"):
        _die("canonical BIN failed Telink trailer/residue validation")


def cmd_check_fw(args: argparse.Namespace) -> int:
    if not ELF.exists():
        _die(f"ELF missing: {ELF}. Run 'build' first.")
    if not TL_CHECK_FW2.exists():
        _die(f"tl_check_fw2.exe missing: {TL_CHECK_FW2}")
    _finalize_firmware()
    _info("tl_check_fw2 PASS; canonical BIN regenerated from ELF")
    return 0


def _run_tl_check_fw(bin_path: Path) -> None:
    # tl_check_fw2 expects to be run from its directory (it uses relative
    # paths to its companion assets).
    r = _run([str(TL_CHECK_FW2), str(bin_path)], cwd=TL_CHECK_FW2.parent,
             env=dict(os.environ), check=False, capture=True)
    print(r.stdout)
    if r.returncode != 0 or "done" not in (r.stdout or "").lower():
        _die("tl_check_fw2 reported failure (missing 'done' marker or nonzero exit).")


# ----------------------------------------------------------------------------
# Subcommand: size
# ----------------------------------------------------------------------------
def cmd_size(args: argparse.Namespace) -> int:
    if not ELF.exists():
        _die(f"ELF missing: {ELF}. Run 'build' first.")
    r = _run([_tc32_tool("tc32-elf-size"), "-t", str(ELF)],
             env=dict(os.environ), check=True, capture=True)
    print(r.stdout.strip())
    # Parse totals line for a structured summary
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0] not in ("text",) and parts[0].isalnum() and parts[0] != "filename":
            try:
                text, data, bss = int(parts[0]), int(parts[1]), int(parts[2])
                dec = text + data + bss
                flash_used = text + data          # in flash (XIP + .data initialisers)
                ram_used = data + bss              # in SRAM at runtime
                print("-" * 60)
                print(f"text={text}  data={data}  bss={bss}  dec={dec}")
                print(f"flash (text+data)         = {flash_used} bytes  "
                      f"of {FW_SLOT_A_END - FW_SLOT_A_BASE + 1} (slot A)")
                print(f"ram   (data+bss)          = {ram_used} bytes")
                return 0
            except ValueError:
                pass
    return 0


# ----------------------------------------------------------------------------
# Subcommand: map
# ----------------------------------------------------------------------------
def cmd_map(args: argparse.Namespace) -> int:
    if not MAP.exists():
        _die(f"MAP missing: {MAP}. Run 'build' first.")
    text = MAP.read_text(encoding="utf-8", errors="replace")
    # Extract top-level section sizes from the "Memory Configuration" / "Linker
    # script and memory map" anchor sections. tc32-elf-ld uses GNU ld-style MAP.
    sections = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r"^\.(vectors|cstartup_ram_funcs|ram_code|retention_data|text|rodata|data|bss|data_no_init|sdk_version)\s", line)
        if m:
            cur = m.group(1)
            sections.setdefault(cur, {"size": 0, "addr": None})
        # A line like "  0x0000008c                _xxx = ."
        m2 = re.match(r"^\s+0x([0-9a-fA-F]+)\s+.*=\s*\.", line)
        if m2 and cur and sections[cur]["addr"] is None:
            sections[cur]["addr"] = int(m2.group(1), 16)
    # Better approach: parse "Output section" headers with size info.
    print("MAP analysis:")
    print(f"  file: {MAP}")
    print(f"  size: {MAP.stat().st_size} bytes")
    # Pull _bin_size_ and _code_size_ provided symbols from the linker script.
    for sym in ("_bin_size_", "_code_size_", "_ram_use_end_", "_start_bss_",
                "_end_bss_", "_start_data_", "_end_data_"):
        m = re.search(rf"\b{re.escape(sym)}\b\s*=\s*0x([0-9a-fA-F]+)", text)
        if m:
            print(f"  {sym:<22} = 0x{int(m.group(1), 16):x}")
    # Section start addresses (search for ".<section> 0xADDR" patterns)
    found = re.findall(r"^\.(vectors|cstartup_ram_funcs|ram_code|retention_data|text|rodata|data|bss|data_no_init|sdk_version)\s+0x([0-9a-fA-F]+)", text, re.M)
    if found:
        print("  sections (start address):")
        for name, addr in found[:10]:
            print(f"    .{name:<22} @ 0x{int(addr, 16):08x}")
    # Provide RAM endpoint + stack margin if available.
    m = re.search(r"__SRAM_SIZE\s*=\s*(0x[0-9a-fA-F]+|\d+)", text)
    if m:
        print(f"  __SRAM_SIZE = {m.group(1)}")
    print("MAP analysis complete. (Use 'bms.py size' for_flash/ram byte totals.)")
    return 0


# ----------------------------------------------------------------------------
# Subcommand: manifest / verify  (firmware integrity)
# ----------------------------------------------------------------------------
# CRC-32/IEEE 802.3 (zlib/PNG).  tl_check_fw2 appends the one's complement of
# the payload CRC as a little-endian uint32, making the CRC of the complete
# processed image equal to the fixed residue 0xFFFFFFFF.
_CRC32_POLY = 0xEDB88320
_CRC32_TABLE = None
_TELINK_CRC_TRAILER_SIZE = 4
_TELINK_WHOLE_IMAGE_RESIDUE = 0xFFFFFFFF


def _crc32_table() -> list[int]:
    global _CRC32_TABLE
    if _CRC32_TABLE is not None:
        return _CRC32_TABLE
    tbl = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ _CRC32_POLY if (c & 1) else (c >> 1)
        tbl.append(c)
    _CRC32_TABLE = tbl
    return tbl


def _crc32(data: bytes, init: int = 0xFFFFFFFF, xor_out: int = 0xFFFFFFFF) -> int:
    tbl = _crc32_table()
    c = init ^ 0xFFFFFFFF if False else init  # init consumed as-is
    c = init
    for b in data:
        c = tbl[(c ^ b) & 0xFF] ^ (c >> 8)
    return c ^ xor_out


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _telink_crc_details(data: bytes) -> dict:
    if len(data) < _TELINK_CRC_TRAILER_SIZE:
        return {
            "valid": False,
            "reason": "image shorter than Telink CRC trailer",
        }
    payload = data[:-_TELINK_CRC_TRAILER_SIZE]
    trailer = int.from_bytes(data[-_TELINK_CRC_TRAILER_SIZE:], "little")
    payload_crc = _crc32(payload)
    expected_trailer = (~payload_crc) & 0xFFFFFFFF
    whole_residue = _crc32(data)
    return {
        "valid": (trailer == expected_trailer
                  and whole_residue == _TELINK_WHOLE_IMAGE_RESIDUE),
        "payload_size_bytes": len(payload),
        "payload_crc32": payload_crc,
        "trailer_size_bytes": _TELINK_CRC_TRAILER_SIZE,
        "trailer_value_le": trailer,
        "expected_trailer_value_le": expected_trailer,
        "whole_image_crc32_residue": whole_residue,
        "expected_whole_image_crc32_residue": _TELINK_WHOLE_IMAGE_RESIDUE,
    }


def _git_provenance() -> dict:
    def capture(argv: list[str]) -> str | None:
        try:
            result = subprocess.run(argv, cwd=str(REPO_ROOT), capture_output=True,
                                    text=True, check=False, timeout=10)
            value = result.stdout.strip()
            return value if result.returncode == 0 and value else None
        except Exception:
            return None

    status = capture(["git", "status", "--porcelain"])
    return {
        "commit": capture(["git", "rev-parse", "HEAD"]),
        "branch": capture(["git", "branch", "--show-current"]),
        "dirty": bool(status) if status is not None else None,
    }


def _build_input_provenance() -> dict:
    entries = _load_source_order_strict()
    objects = []
    object_order: list[str] = []
    for source in entries:
        object_path = BUILD_DIR / "obj" / Path(source).with_suffix(".o")
        object_rel = object_path.relative_to(REPO_ROOT).as_posix()
        if not object_path.exists():
            raise SourceOrderError(f"compiled object missing: {object_path}; run rebuild first")
        object_order.append(object_rel)
        objects.append({
            "source": source,
            "object": object_rel,
            "size_bytes": object_path.stat().st_size,
            "sha256": _sha256(object_path),
        })
    build_mk = _HERE / "build.mk"
    return {
        "source_order_file": str(SOURCE_ORDER_FILE.relative_to(REPO_ROOT)).replace("\\", "/"),
        "source_order_file_sha256": _sha256(SOURCE_ORDER_FILE),
        "source_order_sha256": _source_order_sha256(entries),
        "source_count": len(entries),
        "object_order_sha256": hashlib.sha256(
            _normalised_order_bytes(object_order)
        ).hexdigest(),
        "objects": objects,
        "build_driver": {
            "path": str(build_mk.relative_to(REPO_ROOT)).replace("\\", "/"),
            "sha256": _sha256(build_mk),
        },
        "linker_script": {
            "path": str(LINKER_FILE.relative_to(REPO_ROOT)).replace("\\", "/"),
            "sha256": _sha256(LINKER_FILE),
        },
    }


def cmd_manifest(args: argparse.Namespace) -> int:
    if not BIN.exists():
        _die(f"BIN missing: {BIN}. Run 'objcopy' first.")
    data = BIN.read_bytes()
    telink_crc = _telink_crc_details(data)
    if not telink_crc.get("valid"):
        _die("BIN is not a valid single-pass tl_check_fw2 image. Run 'check-fw' first.")
    try:
        build_inputs = _build_input_provenance()
    except SourceOrderError as exc:
        _die(str(exc))
    manifest = {
        "format": "bms-fw-manifest/v3",
        "generated_at": _now_iso(),
        "firmware_name": "825x_ble_sample",
        "chip": "TLSR8251 / TLSR825x (B85)",
        "elf": str(ELF.relative_to(REPO_ROOT)),
        "bin": str(BIN.relative_to(REPO_ROOT)),
        "size_bytes": len(data),
        "sha256": _sha256(BIN),
        "integrity": {
            "algorithm": "CRC-32/IEEE 802.3 (poly 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF)",
            "hashed_file_range": [0, len(data) - 1],
            "telink_postbuild": telink_crc,
        },
        "flash_layout": {
            "slot_a": [FW_SLOT_A_BASE, FW_SLOT_A_END],
            "slot_b": [FW_SLOT_B_BASE, FW_SLOT_B_END],
            "note": "Layout bounds only; this manifest hashes the emitted BIN bytes, not unused slot padding.",
        },
        "elf_size_bytes": ELF.stat().st_size if ELF.exists() else None,
        "tools": {
            "tc32": "tc32-elf-gcc 4.5.1-tc32-1.3",
            "sdk": "tc_ble_single_sdk V3.4.2.8_Patch_0001",
        },
        "target_configuration": {
            "declared_mcu": DECLARED_MCU,
            "startup_profile": STARTUP_PROFILE,
            "startup_sram_end": STARTUP_SRAM_END,
            "tlsr8251_sram_end_in_sdk": TLSR8251_SRAM_END_IN_SDK,
            "risk": TARGET_CONFIGURATION_RISK,
        },
        "build_inputs": build_inputs,
        "vendor_libraries": {
            str(path.relative_to(REPO_ROOT)).replace("\\", "/"): {
                "size_bytes": path.stat().st_size,
                "sha256": _sha256(path),
            }
            for path in REQUIRED_VENDOR_LIBS
        },
        "git": _git_provenance(),
    }
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    _info(f"manifest written: {MANIFEST}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    if not MANIFEST.exists():
        _die(f"manifest missing: {MANIFEST}. Run 'manifest' first.")
    if not BIN.exists():
        _die(f"BIN missing: {BIN}.")
    m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    data = BIN.read_bytes()
    ok = True
    sha = _sha256(BIN)
    telink_crc = _telink_crc_details(data)
    print(f"size   manifest={m['size_bytes']}  actual={len(data)}  "
          f"{'OK' if len(data) == m['size_bytes'] else 'MISMATCH'}")
    print(f"sha256 manifest={m['sha256']}  actual={sha}  "
          f"{'OK' if sha == m['sha256'] else 'MISMATCH'}")
    expected_crc = m.get("integrity", {}).get("telink_postbuild", {})
    payload_ok = (telink_crc.get("payload_crc32") == expected_crc.get("payload_crc32"))
    trailer_ok = bool(telink_crc.get("valid"))
    print(f"payload crc32 manifest={expected_crc.get('payload_crc32', 0):08x}  "
          f"actual={telink_crc.get('payload_crc32', 0):08x}  "
          f"{'OK' if payload_ok else 'MISMATCH'}")
    print(f"Telink trailer/residue {'OK' if trailer_ok else 'MISMATCH'}")
    ok = (ok and len(data) == m["size_bytes"] and sha == m["sha256"]
          and payload_ok and trailer_ok)

    build_inputs = m.get("build_inputs", {})
    try:
        entries = _load_source_order_strict()
        order_sha = _source_order_sha256(entries)
        order_ok = order_sha == build_inputs.get("source_order_sha256")
        order_file_ok = (_sha256(SOURCE_ORDER_FILE) ==
                         build_inputs.get("source_order_file_sha256"))
        build_driver = build_inputs.get("build_driver", {})
        linker_script = build_inputs.get("linker_script", {})
        build_driver_ok = (_sha256(_HERE / "build.mk") == build_driver.get("sha256"))
        linker_ok = (_sha256(LINKER_FILE) == linker_script.get("sha256"))
        object_records = build_inputs.get("objects", [])
        object_mismatches = []
        for record in object_records:
            object_path = REPO_ROOT / Path(record["object"])
            if (not object_path.exists() or
                    _sha256(object_path) != record.get("sha256") or
                    object_path.stat().st_size != record.get("size_bytes")):
                object_mismatches.append(record["object"])
        recorded_sources = [record.get("source") for record in object_records]
        recorded_object_order = [record.get("object") for record in object_records]
        recorded_object_order_sha = hashlib.sha256(
            _normalised_order_bytes(recorded_object_order)
        ).hexdigest() if all(isinstance(item, str) for item in recorded_object_order) else None
        objects_ok = (
            len(object_records) == len(entries)
            and build_inputs.get("source_count") == len(entries)
            and recorded_sources == entries
            and recorded_object_order_sha == build_inputs.get("object_order_sha256")
            and not object_mismatches
        )
        print(f"source order          {'OK' if order_ok and order_file_ok else 'MISMATCH'}")
        print(f"build.mk              {'OK' if build_driver_ok else 'MISMATCH'}")
        print(f"linker script         {'OK' if linker_ok else 'MISMATCH'}")
        print(f"compiled objects      {'OK' if objects_ok else 'MISMATCH'} "
              f"({len(object_records)} recorded, {len(object_mismatches)} mismatched)")
        ok = (ok and order_ok and order_file_ok and build_driver_ok and linker_ok and objects_ok)
    except (SourceOrderError, KeyError, OSError) as exc:
        print(f"build input verification MISMATCH: {exc}")
        ok = False

    expected_libraries = m.get("vendor_libraries", {})
    required_library_names = {
        str(path.relative_to(REPO_ROOT)).replace("\\", "/")
        for path in REQUIRED_VENDOR_LIBS
    }
    library_mismatches = []
    if set(expected_libraries) != required_library_names:
        library_mismatches.append("manifest library set")
    for rel, expected in expected_libraries.items():
        library = REPO_ROOT / Path(rel)
        if (not library.exists() or _sha256(library) != expected.get("sha256") or
                library.stat().st_size != expected.get("size_bytes")):
            library_mismatches.append(rel)
    print(f"vendor libraries      {'OK' if not library_mismatches else 'MISMATCH'}")
    ok = ok and not library_mismatches
    _info("verify PASS" if ok else "verify FAIL")
    return 0 if ok else 1


# ----------------------------------------------------------------------------
# Subcommand: baseline <reference_bin>
# ----------------------------------------------------------------------------
def cmd_baseline(args: argparse.Namespace) -> int:
    ref = Path(args.reference_bin).resolve()
    if not ref.exists():
        _die(f"reference bin missing: {ref}")
    if not BIN.exists():
        _die(f"new bin missing: {BIN}. Run 'build' + 'objcopy' first.")
    a = ref.read_bytes()
    b = BIN.read_bytes()
    print(f"reference : {ref}")
    print(f"  size={len(a)}  sha256={_sha256(ref)}")
    print(f"new build : {BIN}")
    print(f"  size={len(b)}  sha256={_sha256(BIN)}")
    if len(a) != len(b):
        print(f"SIZE DIFF: {len(b) - len(a):+d} bytes")
    # Byte-by-byte diff summary (first/last divergence)
    if a == b:
        _info("BASELINE MATCH: byte-identical to reference.")
        return 0
    n = min(len(a), len(b))
    diffs = [i for i in range(n) if a[i] != b[i]]
    print(f"BYTE DIFF: {len(diffs)} differing bytes out of {n} compared")
    if diffs:
        print(f"  first diff at offset 0x{diffs[0]:x}  ref=0x{a[diffs[0]]:02x}  new=0x{b[diffs[0]]:02x}")
        print(f"  last  diff at offset 0x{diffs[-1]:x}  ref=0x{a[diffs[-1]]:02x}  new=0x{b[diffs[-1]]:02x}")
    # Length-difference handling
    if len(a) != len(b):
        tail = abs(len(a) - len(b))
        print(f"  plus {tail} trailing bytes in the longer image (classification requires analysis)")
    if not diffs and len(a) != len(b):
        _info("BASELINE PREFIX MATCH: common bytes match; only trailing length differs.")
    else:
        _info("BASELINE CONTENT MISMATCH: compiled content differs and requires "
              "source/section/hardware impact analysis; this is not a padding-only difference.")
    return 0 if args.report_only else 1


# ----------------------------------------------------------------------------
# Subcommand: static   (cppcheck)
# ----------------------------------------------------------------------------
def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _read_cppcheck_config() -> list[str]:
    if not CPPCHECK_CONFIG.exists():
        _die(f"cppcheck config missing: {CPPCHECK_CONFIG}")
    args: list[str] = []
    for raw_line in CPPCHECK_CONFIG.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        args.extend(shlex.split(line, posix=False))
    return args


def _capture_real_compile_database(out_dir: Path) -> tuple[list[dict], list[str], str]:
    """Capture compile commands from the same Make driver used by build/rebuild.

    ``make -B -n`` expands build.mk and the generated per-source rules without
    compiling.  This keeps Cppcheck synchronized with the real source list,
    include paths, defines, language standard and TC32-specific compiler flags.
    """
    entries = _load_source_order_strict()
    _gen_sources_mk()
    env = _ensure_toolchain_env(dict(os.environ))
    make = _need_make()
    command = [
        make, "-B", "-n", "--no-print-directory", "-f", str(_HERE / "build.mk"),
        f"REPO_ROOT={_junc(REPO_ROOT).as_posix()}",
        f"SDK_DIR={_junc(SDK_DIR).as_posix()}",
        f"BUILD_DIR={_junc(BUILD_DIR).as_posix()}",
        "all",
    ]
    result = subprocess.run(command, cwd=str(JUNCTION), env=env, check=False,
                            capture_output=True, text=True)
    dry_run = result.stdout or ""
    (out_dir / "make_dry_run.log").write_text(dry_run, encoding="utf-8")
    if result.returncode != 0:
        _die(f"failed to extract real compile commands (rc={result.returncode}); "
             f"see {out_dir / 'make_dry_run.log'}")
    compile_lines = [line.strip() for line in dry_run.splitlines()
                     if re.match(r"^(?:.*[/\\])?tc32-elf-gcc(?:\.exe)?\s", line.strip(), re.I)
                     and " -c " in f" {line.strip()} "]
    if len(compile_lines) != len(entries):
        _die("real compile command count does not match source_order.txt: "
             f"commands={len(compile_lines)} sources={len(entries)}")

    database: list[dict] = []
    for rel, compile_line in zip(entries, compile_lines):
        source = _junc(SDK_DIR / rel).as_posix()
        output = _junc(OBJ_DIR / Path(rel).with_suffix(".o")).as_posix()
        if source.casefold() not in compile_line.replace("\\", "/").casefold():
            _die(f"dry-run command/source order mismatch for {rel}")
        database.append({
            "directory": JUNCTION.as_posix(),
            "command": compile_line,
            "file": source,
            "output": output,
        })
    _write_json(out_dir / "compile_commands_build.json", database)
    return database, entries, dry_run


def _git_changed_sdk_paths() -> tuple[set[str] | None, str | None]:
    """Return SDK paths changed from the documented product baseline.

    ``git diff <baseline> --`` includes committed changes and working-tree
    changes.  If provenance cannot be established, callers conservatively scan
    every compiled C translation unit instead of risking a missed modified SDK
    file.
    """
    check = subprocess.run(["git", "cat-file", "-e", f"{SDK_BASELINE_COMMIT}^{{commit}}"],
                           cwd=str(REPO_ROOT), capture_output=True, text=True, check=False)
    if check.returncode != 0:
        return None, f"baseline commit unavailable: {SDK_BASELINE_COMMIT}"
    result = subprocess.run(
        ["git", "diff", "--name-only", SDK_BASELINE_COMMIT, "--", SDK_SUBDIR],
        cwd=str(REPO_ROOT), capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        return None, (result.stderr or "git diff failed").strip()
    prefix = SDK_SUBDIR.rstrip("/") + "/"
    changed = {
        line.strip().replace("\\", "/")[len(prefix):]
        for line in result.stdout.splitlines()
        if line.strip().replace("\\", "/").startswith(prefix)
    }
    return changed, None


def _canonical_repo_path(value: str) -> tuple[str, Path | None]:
    normal = value.replace("\\", "/")
    junction = JUNCTION.as_posix().rstrip("/") + "/"
    if normal.casefold().startswith(junction.casefold()):
        normal = REPO_ROOT.as_posix().rstrip("/") + "/" + normal[len(junction):]
    candidate = Path(normal)
    try:
        resolved = candidate.resolve()
        return resolved.relative_to(REPO_ROOT).as_posix(), resolved
    except (OSError, ValueError):
        return normal, candidate if candidate.exists() else None


def _sdk_relative_path(value: str) -> str | None:
    """Return an SDK-relative path for repo, junction or absolute inputs."""
    normal = value.replace("\\", "/")
    repo_prefix = SDK_SUBDIR.rstrip("/") + "/"
    if normal.casefold().startswith(repo_prefix.casefold()):
        return normal[len(repo_prefix):]
    junction_prefix = _junc(SDK_DIR).as_posix().rstrip("/") + "/"
    if normal.casefold().startswith(junction_prefix.casefold()):
        return normal[len(junction_prefix):]
    try:
        return Path(normal).resolve().relative_to(SDK_DIR).as_posix()
    except (OSError, ValueError):
        return None


def _is_application_scope_path(value: str) -> bool:
    sdk_relative = _sdk_relative_path(value)
    return sdk_relative is not None and sdk_relative.startswith(PROJECT_SOURCE_PREFIX)


def _cppcheck_scope_path(value: str) -> str:
    candidate = Path(value)
    if not candidate.is_absolute():
        candidate = REPO_ROOT / candidate
    return _junc(candidate).as_posix()


def _write_cppcheck_scope_exclusions(dependencies: set[str], out_dir: Path) -> tuple[Path, list[str]]:
    """Exclude dependency diagnostics outside the user-owned application.

    Cppcheck must still parse SDK headers so application types, macros and
    conditional compilation remain faithful to the real TC32 build.  This
    generated list changes only the diagnostic boundary; no application
    finding is suppressed.
    """
    excluded = sorted(path for path in dependencies if not _is_application_scope_path(path))
    suppression_path = out_dir / "cppcheck-sdk-scope-exclusions.txt"
    suppression_path.write_text(
        "\n".join(f"*:{_cppcheck_scope_path(path)}" for path in excluded) + "\n",
        encoding="utf-8",
    )
    evidence = []
    for value in excluded:
        path = Path(value)
        if not path.is_absolute():
            path = REPO_ROOT / path
        evidence.append({
            "file": value,
            "kind": "头文件" if path.suffix.lower() == ".h" else "依赖文件",
            "reason": "不属于 vendor/ble_sample 应用层检查范围；仅为真实编译依赖解析",
            "sha256": _sha256(path) if path.exists() and path.is_file() else "",
        })
    _write_json(out_dir / "sdk_scope_exclusions.json", {
        "policy": STATIC_SCOPE_POLICY,
        "application_prefix": PROJECT_SOURCE_PREFIX,
        "excluded_dependency_count": len(excluded),
        "excluded_dependencies": evidence,
        "note": "范围排除不阻止 Cppcheck 解析 SDK 头文件；应用层诊断未使用 suppression。",
    })
    return suppression_path, excluded


def _assert_application_diagnostics(rows: list[dict], evidence_path: Path) -> None:
    outside = []
    for row in rows:
        locations = row.get("locations", [])
        if locations and not _is_application_scope_path(locations[0].get("file", "")):
            outside.append(f"{row.get('id')}:{locations[0].get('file')}:{locations[0].get('line')}")
    if outside:
        _die("Cppcheck emitted diagnostics outside vendor/ble_sample scope: "
             + ", ".join(outside[:5]) + f"; see {evidence_path}")


def _analyse_dependency_graph(analysis_database: list[dict], out_dir: Path) -> set[str]:
    """Use the real TC32 commands in dependency-only mode to audit headers."""
    dependencies: set[str] = set()
    logs: list[str] = []
    env = _ensure_toolchain_env(dict(os.environ))
    for entry in analysis_database:
        tokens = shlex.split(entry["command"], posix=True)
        filtered: list[str] = [str(_tc32_tool("tc32-elf-gcc"))]
        skip_next = False
        for token in tokens[1:]:
            if skip_next:
                skip_next = False
                continue
            if token == "-c":
                continue
            if token == "-o":
                skip_next = True
                continue
            if token.startswith("-o"):
                continue
            if token.lower().endswith((".c", ".s")):
                continue
            filtered.append(token)
        filtered.extend(["-MM", entry["file"]])
        result = subprocess.run(filtered, cwd=entry["directory"], env=env,
                                capture_output=True, text=True, check=False)
        logs.append(f"# {entry['file']} rc={result.returncode}\n{result.stdout}{result.stderr}")
        if result.returncode != 0:
            _die(f"TC32 dependency audit failed for {entry['file']}; "
                 f"see {out_dir / 'dependencies.log'}")
        flattened = re.sub(r"\\\s*\r?\n", " ", result.stdout or "")
        payload = flattened.split(":", 1)[1] if ":" in flattened else ""
        for token in payload.split():
            path = token.strip().replace("\\ ", " ")
            rel, resolved = _canonical_repo_path(path)
            if resolved is not None and resolved.exists():
                dependencies.add(rel)
    (out_dir / "dependencies.log").write_text("\n".join(logs), encoding="utf-8")
    return dependencies


def _compiler_predefines(dependency_paths: set[str], out_dir: Path) -> tuple[dict[str, str], dict[str, str]]:
    compiler = _tc32_tool("tc32-elf-gcc")
    result = subprocess.run([compiler, "-dM", "-E", "-x", "c", "-"], input="",
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        _die("unable to read TC32 compiler predefined macros")
    (out_dir / "compiler_predefines.txt").write_text(result.stdout, encoding="utf-8")
    all_defs: dict[str, str] = {}
    for line in result.stdout.splitlines():
        match = re.match(r"#define\s+([A-Za-z_]\w*)(?:\s+(.*))?$", line)
        if match:
            all_defs[match.group(1)] = (match.group(2) or "1").strip()

    conditional_names: set[str] = set()
    for rel in dependency_paths:
        path = REPO_ROOT / rel
        if path.suffix.lower() not in (".c", ".h") or not path.exists():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.match(r"\s*#\s*(?:if|elif|ifdef|ifndef)\b(.*)$", line)
            if match:
                conditional_names.update(re.findall(r"\b[A-Za-z_]\w*\b", match.group(1)))
    applied = {
        name: value for name, value in all_defs.items()
        if name in conditional_names and len(value) <= 80 and "\"" not in value
    }
    _write_json(out_dir / "compiler_predefines_applied.json", applied)
    return all_defs, applied


def _extract_real_compile_settings(command: str) -> dict:
    tokens = shlex.split(command, posix=True)
    includes = [token[2:] for token in tokens if token.startswith("-I")]
    defines = [token[2:] for token in tokens if token.startswith("-D")]
    standard = next((token.split("=", 1)[1] for token in tokens
                     if token.startswith("-std=")), None)
    compile_flags = [token for token in tokens[1:]
                     if not token.startswith(("-I", "-D", "-o"))
                     and token != "-c" and not token.lower().endswith((".c", ".s"))]
    return {
        "compiler": str(_tc32_tool("tc32-elf-gcc")),
        "compiler_version": _tool_version([str(_tc32_tool("tc32-elf-gcc")), "--version"], 0),
        "include_paths": includes,
        "defines": defines,
        "c_standard": standard,
        "compile_flags": compile_flags,
    }


def _parse_cppcheck_xml(path: Path, engine: str) -> tuple[str, list[dict]]:
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as exc:
        _die(f"invalid Cppcheck XML {path}: {exc}")
    version_node = root.find("cppcheck")
    version = version_node.get("version", "unknown") if version_node is not None else "unknown"
    rows: list[dict] = []
    for error in root.findall(".//error"):
        locations = []
        for location in error.findall("location"):
            rel, _ = _canonical_repo_path(location.get("file", ""))
            locations.append({
                "file": rel,
                "line": int(location.get("line", "0") or 0),
                "column": int(location.get("column", "0") or 0),
                "info": location.get("info", ""),
            })
        analysis_unit, _ = _canonical_repo_path(error.get("file0", ""))
        eid = error.get("id", "unknown")
        misra_match = re.search(r"misra(?:-c)?(?:2012)?[-:]?(\d+\.\d+)", eid, re.I)
        rows.append({
            "engine": engine,
            "id": eid,
            "severity": error.get("severity", "unknown"),
            "message": error.get("msg", ""),
            "verbose": error.get("verbose", ""),
            "cwe": error.get("cwe", ""),
            "inconclusive": error.get("inconclusive", "false").lower() == "true",
            "symbol": (error.findtext("symbol") or ""),
            "analysis_unit": analysis_unit,
            "misra_rule": misra_match.group(1) if misra_match else "",
            "locations": locations,
        })
    return version, rows


def _function_at_line(path: Path | None, target_line: int) -> str:
    """Best-effort C function locator; global/header diagnostics remain explicit."""
    if path is None or not path.exists() or target_line <= 0:
        return "<全局/未定位>"
    text = path.read_text(encoding="utf-8", errors="replace")
    cleaned: list[str] = []
    in_block = False
    for raw in text.splitlines():
        line: list[str] = []
        i = 0
        in_string: str | None = None
        while i < len(raw):
            pair = raw[i:i + 2]
            if in_block:
                if pair == "*/":
                    in_block = False
                    i += 2
                else:
                    line.append(" ")
                    i += 1
                continue
            if in_string:
                if raw[i] == "\\":
                    line.extend("  ")
                    i += 2
                elif raw[i] == in_string:
                    line.append(" ")
                    in_string = None
                    i += 1
                else:
                    line.append(" ")
                    i += 1
                continue
            if pair == "/*":
                in_block = True
                line.extend("  ")
                i += 2
            elif pair == "//":
                line.extend(" " * (len(raw) - i))
                break
            elif raw[i] in ('"', "'"):
                in_string = raw[i]
                line.append(" ")
                i += 1
            else:
                line.append(raw[i])
                i += 1
        cleaned.append("".join(line))

    depth = 0
    current = "<全局/未定位>"
    function_depth: int | None = None
    signature = ""
    controls = {"if", "for", "while", "switch", "sizeof", "return"}
    for number, line in enumerate(cleaned, 1):
        signature = (signature + " " + line.strip())[-1200:]
        if "{" in line:
            before = signature[:signature.rfind("{")]
            candidates = re.findall(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$", before)
            if candidates and candidates[-1] not in controls and not re.search(r"\btypedef\b", before):
                current = candidates[-1]
                function_depth = depth + 1
            signature = signature[signature.rfind("{") + 1:]
        depth += line.count("{") - line.count("}")
        if function_depth is not None and depth < function_depth:
            current = "<全局/未定位>"
            function_depth = None
        if number >= target_line:
            return current
        if ";" in line and depth == 0:
            signature = signature[signature.rfind(";") + 1:]
    return current


def _finding_classification(row: dict, modified_sdk: set[str]) -> tuple[str, str, str]:
    location = row["locations"][0] if row["locations"] else {"file": "", "line": 0}
    file = location["file"]
    sdk_rel = file[len(SDK_SUBDIR.rstrip("/") + "/"):] if file.startswith(SDK_SUBDIR.rstrip("/") + "/") else file
    config_ids = {"missingInclude", "missingIncludeSystem", "syntaxError", "preprocessorErrorDirective",
                  "toomanyconfigs", "internalAstError", "internalError", "unhandledCharLiteral"}
    if row["misra_rule"]:
        return "MISRA违规（待评审）", "待评审", "按规则逐项评审；需要保留时走正式 Deviation 审批"
    if row["id"] in config_ids or not file:
        return "配置导致的问题", "待修正配置", "先修正分析配置并重跑，不能作为代码问题关闭"
    is_project = sdk_rel.startswith(PROJECT_SOURCE_PREFIX) or sdk_rel in modified_sdk
    if not is_project:
        deviation_ids = {"badBitmaskCheck", "clarifyCalculation", "invalidPointerCast",
                         "reinterpretCast", "integerOverflow"}
        if row["id"] in deviation_ids or row["severity"] in ("error", "warning", "portability"):
            return "SDK问题", "Deviation候选（未批准）", "不要直接修改官方 SDK；评估影响并提交人工 Deviation 审批"
        return "SDK问题", "待评审（SDK）", "核实是否为头文件/内联函数上下文告警；不要直接修改官方 SDK"
    if row["severity"] in ("error", "warning"):
        return "真实代码问题（待确认）", "待整改", "建议优先整改并执行固件与硬件回归"
    return "代码质量建议", "待评审", "评审可维护性与风险；不修改时记录技术依据"


def _deduplicate_findings(raw_rows: list[dict], modified_sdk: set[str]) -> list[dict]:
    merged: dict[tuple, dict] = {}
    for row in raw_rows:
        location = row["locations"][0] if row["locations"] else {"file": "", "line": 0, "column": 0}
        key = (row["engine"], row["id"], location["file"], location["line"], row["message"], row["misra_rule"])
        if key not in merged:
            resolved = REPO_ROOT / location["file"] if location["file"] else None
            classification, status, recommendation = _finding_classification(row, modified_sdk)
            merged[key] = {
                "engine": row["engine"],
                "file": location["file"],
                "function": _function_at_line(resolved, location["line"]),
                "line": location["line"],
                "column": location.get("column", 0),
                "id": row["id"],
                "severity": row["severity"],
                "message": row["message"],
                "verbose": row["verbose"],
                "cwe": row["cwe"],
                "misra_rule": row["misra_rule"],
                "inconclusive": row["inconclusive"],
                "classification": classification,
                "status": status,
                "recommendation": recommendation,
                "analysis_units": set(),
                "occurrence_count": 0,
            }
        merged[key]["occurrence_count"] += 1
        if row["analysis_unit"]:
            merged[key]["analysis_units"].add(row["analysis_unit"])
    findings = []
    for value in merged.values():
        value["analysis_units"] = sorted(value["analysis_units"])
        findings.append(value)
    order = {"error": 0, "warning": 1, "performance": 2, "portability": 3,
             "style": 4, "information": 5, "debug": 6}
    findings.sort(key=lambda item: (order.get(item["severity"], 99), item["file"], item["line"], item["id"]))
    return findings


def _write_findings_csv(path: Path, findings: list[dict]) -> None:
    fields = ["file", "function", "line", "column", "id", "severity", "message", "cwe",
              "misra_rule", "classification", "status", "recommendation", "inconclusive",
              "occurrence_count", "analysis_units"]
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for item in findings:
            row = {key: item.get(key, "") for key in fields}
            row["analysis_units"] = "; ".join(item["analysis_units"])
            writer.writerow(row)


def _find_misra_addon() -> Path | None:
    candidates = [
        DEFAULT_CPPCHECK.parent / "addons" / "misra.py",
        DEFAULT_CPPCHECK.parent / "misra.py",
    ]
    return next((path for path in candidates if path.exists()), None)


def _run_cppcheck(cmd: list[str], xml_path: Path, log_path: Path) -> int:
    full = [*cmd, f"--output-file={_junc(xml_path)}", "--xml-version=2"]
    result = subprocess.run(full, cwd=str(REPO_ROOT), capture_output=True, text=True, check=False)
    log_path.write_text(
        "COMMAND:\n" + subprocess.list2cmdline(full) + "\n\nSTDOUT:\n" + (result.stdout or "")
        + "\nSTDERR:\n" + (result.stderr or ""), encoding="utf-8",
    )
    if result.returncode != 0:
        _die(f"Cppcheck failed rc={result.returncode}; see {log_path}")
    return result.returncode


def _resolve_artifact_runtime() -> Path:
    python_candidates: list[Path] = []
    if os.environ.get("BMS_ARTIFACT_PYTHON"):
        python_candidates.append(Path(os.environ["BMS_ARTIFACT_PYTHON"]))
    profile = os.environ.get("USERPROFILE")
    if profile:
        bundled = (Path(profile) / ".cache" / "codex-runtimes" / "codex-primary-runtime"
                   / "dependencies" / "python" / "python.exe")
        python_candidates.append(bundled)
    python_candidates.append(Path(sys.executable))
    probe = "import artifact_tool_v2"
    for python in python_candidates:
        if not python.exists():
            continue
        result = subprocess.run([str(python), "-c", probe], capture_output=True,
                                text=True, check=False)
        if result.returncode == 0:
            return python.resolve()
    _die("Excel report runtime unavailable. Set BMS_ARTIFACT_PYTHON to a Python "
         "runtime containing artifact_tool_v2.")
    raise AssertionError("unreachable")


def _run_static_report_builder(template: Path, data_path: Path, output_path: Path,
                               preview_dir: Path) -> None:
    if not template.exists():
        _die(f"static analysis report template missing: {template}")
    if not STATIC_REPORT_BUILDER.exists():
        _die(f"static report builder missing: {STATIC_REPORT_BUILDER}")
    python = _resolve_artifact_runtime()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    # The desktop artifact runtime owns a lifecycle for its interactive output
    # directory. Keep that lifecycle isolated from Cppcheck evidence, then copy
    # only completed files into the immutable run directory.
    with tempfile.TemporaryDirectory(prefix="bms_static_report_") as temp_name:
        temp_root = Path(temp_name)
        temp_data = temp_root / data_path.name
        temp_output = temp_root / output_path.name
        temp_preview = temp_root / "report_verification"
        shutil.copy2(data_path, temp_data)
        result = subprocess.run(
            [str(python), str(STATIC_REPORT_BUILDER), str(template), str(temp_data),
             str(temp_output), str(temp_preview)],
            cwd=str(temp_root), capture_output=True, text=True,
            encoding="utf-8", errors="replace", check=False,
        )
        if result.returncode == 0 and temp_output.exists():
            shutil.copy2(temp_output, output_path)
            verification = temp_preview / "verification.json"
            if verification.exists():
                preview_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(verification, preview_dir / verification.name)
    (data_path.parent / "report_builder.log").write_text(
        (result.stdout or "") + "\n" + (result.stderr or ""), encoding="utf-8")
    if result.returncode != 0 or not output_path.exists():
        _die(f"Excel report generation failed; see {data_path.parent / 'report_builder.log'}")


def cmd_static(args: argparse.Namespace) -> int:
    if not DEFAULT_CPPCHECK.exists():
        _die(f"cppcheck not found: {DEFAULT_CPPCHECK}")
    if not CPPCHECK_PLATFORM.exists():
        _die(f"TC32 Cppcheck platform missing: {CPPCHECK_PLATFORM}")

    started_at = _now_iso()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    static_root = BUILD_DIR / "static"
    run_dir = static_root / f"run_{stamp}"
    run_dir.mkdir(parents=True, exist_ok=False)

    full_database, source_order, _ = _capture_real_compile_database(run_dir)
    changed_sdk, baseline_error = _git_changed_sdk_paths()
    compiled_c = {rel for rel in source_order if rel.endswith(".c")}
    modified_sdk: set[str] = set() if changed_sdk is None else {
        path for path in changed_sdk if not path.startswith(PROJECT_SOURCE_PREFIX)
    }
    selected_sources = {
        rel for rel in compiled_c if rel.startswith(PROJECT_SOURCE_PREFIX)
    }
    selection_note = STATIC_SCOPE_POLICY

    rel_to_entry = dict(zip(source_order, full_database))
    analysis_database = [rel_to_entry[rel] for rel in source_order if rel in selected_sources]
    _write_json(run_dir / "compile_commands_analysis.json", analysis_database)
    dependencies = _analyse_dependency_graph(analysis_database, run_dir)
    dependencies.update(
        (SDK_DIR / rel).relative_to(REPO_ROOT).as_posix() for rel in selected_sources
    )
    scope_suppression_path, excluded_dependencies = _write_cppcheck_scope_exclusions(
        dependencies, run_dir)
    _, applied_predefs = _compiler_predefines(dependencies, run_dir)

    cppcheck_args = _read_cppcheck_config()
    project_arg = f"--project={_junc(run_dir / 'compile_commands_analysis.json')}"
    platform_arg = f"--platform={_junc(CPPCHECK_PLATFORM)}"
    predef_args = [f"-D{name}={value}" for name, value in sorted(applied_predefs.items())]
    scope_args = [
        f"--suppressions-list={_junc(scope_suppression_path)}",
        "--suppress=unmatchedSuppression",
    ]
    config_cmd = [str(DEFAULT_CPPCHECK), "--check-config", project_arg, platform_arg,
                  *scope_args, *predef_args]
    config_check = subprocess.run(config_cmd, cwd=str(REPO_ROOT), capture_output=True,
                                  text=True, check=False)
    (run_dir / "configuration_check.log").write_text(
        "COMMAND:\n" + subprocess.list2cmdline(config_cmd) + "\n\nSTDOUT:\n"
        + (config_check.stdout or "") + "\nSTDERR:\n" + (config_check.stderr or ""),
        encoding="utf-8",
    )
    if config_check.returncode != 0:
        _die(f"Cppcheck configuration audit failed; see {run_dir / 'configuration_check.log'}")

    native_xml = run_dir / "cppcheck-native.xml"
    native_cache = run_dir / "cppcheck-cache-native"
    native_cache.mkdir(parents=True, exist_ok=True)
    native_cmd = [
        str(DEFAULT_CPPCHECK), *cppcheck_args, project_arg, platform_arg,
        f"--cppcheck-build-dir={_junc(native_cache)}",
        f"--checkers-report={_junc(run_dir / 'cppcheck-checkers-report.txt')}",
        "-j", str(max(1, args.jobs)), *scope_args, *predef_args,
    ]
    _info(f"cppcheck native: {len(analysis_database)} C translation units from real compile database")
    _run_cppcheck(native_cmd, native_xml, run_dir / "cppcheck-native.log")
    cppcheck_version, raw_native_all = _parse_cppcheck_xml(native_xml, "Cppcheck原生")
    tool_metadata = [row for row in raw_native_all if row["id"] in {"checkersReport"}]
    raw_native = [row for row in raw_native_all if row not in tool_metadata]
    _assert_application_diagnostics(raw_native, run_dir / "sdk_scope_exclusions.json")

    addon = _find_misra_addon()
    raw_misra: list[dict] = []
    misra_status: dict = {
        "executed": False,
        "addon": str(addon) if addon else None,
        "coverage": "未执行：本机 Cppcheck 安装不包含 MISRA addon；原生告警不计为 MISRA 违规",
    }
    if addon is not None:
        misra_xml = run_dir / "cppcheck-misra.xml"
        misra_cache = run_dir / "cppcheck-cache-misra"
        misra_cache.mkdir(parents=True, exist_ok=True)
        misra_cmd = [
            str(DEFAULT_CPPCHECK), "--enable=warning", "--std=c99", "--language=c",
            project_arg, platform_arg, f"--addon={addon}",
            f"--cppcheck-build-dir={_junc(misra_cache)}",
            "-j", str(max(1, args.jobs)), *scope_args, *predef_args,
        ]
        _run_cppcheck(misra_cmd, misra_xml, run_dir / "cppcheck-misra.log")
        _, addon_rows = _parse_cppcheck_xml(misra_xml, "Cppcheck MISRA addon")
        raw_misra = [row for row in addon_rows if row["misra_rule"]]
        _assert_application_diagnostics(raw_misra, run_dir / "sdk_scope_exclusions.json")
        misra_status = {
            "executed": True,
            "addon": str(addon),
            "coverage": "Cppcheck MISRA addon 仅覆盖其实现的可自动检查规则；不代表完整 MISRA C 合规",
        }
    _write_json(run_dir / "misra_capability.json", misra_status)

    findings = _deduplicate_findings(raw_native + raw_misra, set())
    _write_json(run_dir / "findings.json", findings)
    _write_findings_csv(run_dir / "findings.csv", findings)

    project_headers = sorted(
        path.relative_to(SDK_DIR).as_posix()
        for path in (SDK_DIR / "vendor" / "ble_sample").rglob("*.h")
    )
    dependency_sdk_rel = {
        rel[len(SDK_SUBDIR.rstrip("/") + "/"):]
        for rel in dependencies if rel.startswith(SDK_SUBDIR.rstrip("/") + "/")
    }
    dependency_headers = {rel for rel in dependency_sdk_rel if rel.endswith(".h")}
    project_dependency_headers = {
        rel for rel in dependency_headers if rel.startswith(PROJECT_SOURCE_PREFIX)
    }
    sdk_dependency_headers = dependency_headers - project_dependency_headers
    scope_rows = []
    for rel in source_order:
        analysed = rel in selected_sources
        reason = ("直接作为 C 翻译单元分析" if analysed else
                  "汇编源文件，Cppcheck 不支持" if rel.endswith(".S") else
                  "官方 SDK 不属于用户确认的静态检查范围")
        scope_rows.append({
            "file": rel,
            "kind": "C翻译单元" if rel.endswith(".c") else "汇编源文件",
            "owner": "BMS应用层" if rel.startswith(PROJECT_SOURCE_PREFIX) else "Telink官方SDK（范围排除）",
            "modified_from_baseline": changed_sdk is not None and rel in changed_sdk,
            "participates_in_build": True,
            "cppcheck_mode": "直接分析" if analysed else "不检查",
            "exclusion_reason": "" if analysed else reason,
            "sha256": _sha256(SDK_DIR / rel),
        })
    for rel in project_headers:
        covered = rel in dependency_sdk_rel
        scope_rows.append({
            "file": rel,
            "kind": "头文件",
            "owner": "BMS应用层",
            "modified_from_baseline": changed_sdk is not None and rel in changed_sdk,
            "participates_in_build": covered,
            "cppcheck_mode": "随翻译单元解析" if covered else "未被真实构建依赖引用",
            "exclusion_reason": "" if covered else "真实编译配置未引用该头文件",
            "sha256": _sha256(SDK_DIR / rel),
        })
    for rel in sorted(sdk_dependency_headers):
        scope_rows.append({
            "file": rel,
            "kind": "SDK依赖头文件",
            "owner": "Telink官方SDK（范围排除）",
            "modified_from_baseline": changed_sdk is not None and rel in changed_sdk,
            "participates_in_build": True,
            "cppcheck_mode": "仅解析（诊断排除）",
            "exclusion_reason": "应用层真实编译依赖；按用户确认的范围策略不检查SDK问题",
            "sha256": _sha256(SDK_DIR / rel),
        })
    _write_json(run_dir / "scope_audit.json", scope_rows)
    with (run_dir / "scope_audit.csv").open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(scope_rows[0]))
        writer.writeheader()
        writer.writerows(scope_rows)

    severity_counts: dict[str, int] = {}
    classification_counts: dict[str, int] = {}
    id_counts: dict[str, int] = {}
    for finding in findings:
        severity_counts[finding["severity"]] = severity_counts.get(finding["severity"], 0) + 1
        classification_counts[finding["classification"]] = classification_counts.get(finding["classification"], 0) + 1
        id_counts[finding["id"]] = id_counts.get(finding["id"], 0) + 1
    deviation_candidates = [finding for finding in findings if finding["status"].startswith("Deviation候选")]
    uncovered_project_headers = [rel for rel in project_headers if rel not in dependency_sdk_rel]
    coverage_gaps = uncovered_project_headers
    compile_settings = _extract_real_compile_settings(full_database[0]["command"])
    compile_settings.update({
        "target_mcu": DECLARED_MCU,
        "startup_profile": STARTUP_PROFILE,
        "target_configuration_risk": TARGET_CONFIGURATION_RISK,
        "platform_model": str(CPPCHECK_PLATFORM.relative_to(REPO_ROOT)).replace("\\", "/"),
        "compiler_predefines_applied": applied_predefs,
        "configuration_source": "build.mk 经 make -B -n 展开 + source_order.txt",
    })
    summary = {
        "format": "tlsr8251-bms-static-analysis/v2",
        "run_id": stamp,
        "started_at": started_at,
        "finished_at": _now_iso(),
        "git": _git_provenance(),
        "sdk_baseline_commit": SDK_BASELINE_COMMIT,
        "baseline_audit_error": baseline_error,
        "selection_policy": selection_note,
        "scope_policy": STATIC_SCOPE_POLICY,
        "cppcheck_version": cppcheck_version,
        "cppcheck_config": str(CPPCHECK_CONFIG.relative_to(REPO_ROOT)).replace("\\", "/"),
        "actual_build_source_count": len(source_order),
        "actual_build_c_count": len(compiled_c),
        "actual_build_assembly_count": len(source_order) - len(compiled_c),
        "analysis_translation_unit_count": len(analysis_database),
        "analysis_header_dependency_count": len(dependency_headers),
        "application_header_dependency_count": len(project_dependency_headers),
        "sdk_parse_only_header_count": len(sdk_dependency_headers),
        "sdk_diagnostic_exclusion_count": len(excluded_dependencies),
        "project_c_translation_unit_count": sum(rel.startswith(PROJECT_SOURCE_PREFIX) for rel in compiled_c),
        "modified_sdk_translation_unit_count": 0,
        "modified_sdk_excluded_translation_unit_count": sum(
            rel in modified_sdk for rel in compiled_c if not rel.startswith(PROJECT_SOURCE_PREFIX)
        ),
        "excluded_official_sdk_c_count": sum(rel.endswith(".c") and rel not in selected_sources for rel in source_order),
        "excluded_assembly_count": sum(rel.endswith(".S") for rel in source_order),
        "coverage_gap_count": len(coverage_gaps),
        "coverage_gaps": coverage_gaps,
        "raw_native_occurrence_count": len(raw_native),
        "tool_metadata": tool_metadata,
        "distinct_finding_count": len(findings),
        "native_distinct_finding_count": sum(not item["misra_rule"] for item in findings),
        "misra_distinct_finding_count": sum(bool(item["misra_rule"]) for item in findings),
        "sdk_distinct_finding_count": 0,
        "misra": misra_status,
        "severity_counts": severity_counts,
        "classification_counts": classification_counts,
        "id_counts": dict(sorted(id_counts.items(), key=lambda item: (-item[1], item[0]))),
        "deviation_candidate_count": len(deviation_candidates),
        "approved_deviation_count": 0,
        "configuration": compile_settings,
        "scope": scope_rows,
        "findings": findings,
        "deviation_candidates": deviation_candidates,
        "report": None,
    }
    data_path = run_dir / "static_analysis_report_data.json"
    _write_json(data_path, summary)

    report_path: Path | None = None
    if not args.no_report:
        template = Path(args.report_template).resolve() if args.report_template else DEFAULT_STATIC_REPORT_TEMPLATE
        report_path = run_dir / "XXX-BMS_软件静态分析报告_已填写.xlsx"
        _run_static_report_builder(template, data_path, report_path, run_dir / "report_previews")
        summary["report"] = report_path.relative_to(REPO_ROOT).as_posix()
        _write_json(data_path, summary)

    latest = {
        "run_id": stamp,
        "run_dir": run_dir.relative_to(REPO_ROOT).as_posix(),
        "summary": data_path.relative_to(REPO_ROOT).as_posix(),
        "report": report_path.relative_to(REPO_ROOT).as_posix() if report_path else None,
    }
    _write_json(static_root / "latest.json", latest)
    _info(f"actual build sources: {len(source_order)} ({len(compiled_c)} C + "
          f"{len(source_order) - len(compiled_c)} assembly)")
    _info(f"cppcheck analysed translation units: {len(analysis_database)}; "
          f"application headers: {len(project_dependency_headers)}; "
          f"SDK headers parse-only: {len(sdk_dependency_headers)}; coverage gaps: {len(coverage_gaps)}")
    _info(f"SDK diagnostics excluded by scope policy: {len(excluded_dependencies)} dependency files")
    _info(f"total issues: {len(findings)} distinct ({len(raw_native)} raw native occurrences); "
          f"severity={severity_counts}")
    _info(f"MISRA: {'executed' if misra_status['executed'] else 'not executed'}; "
          f"findings={summary['misra_distinct_finding_count']}")
    _info(f"static analysis complete -> {run_dir}")
    if report_path:
        _info(f"filled Excel report -> {report_path}")
    return 1 if args.strict and findings else 0


# ----------------------------------------------------------------------------
# Subcommand: flash-help  (semi-automated burning instructions)
# ----------------------------------------------------------------------------
def cmd_flash_help(args: argparse.Namespace) -> int:
    print("=" * 70)
    print("Telink BDT burning (semi-automated; GUI tool, hardware-assisted step)")
    print("=" * 70)
    print(f"1. Use the official Telink BDT GUI tool:")
    print(f"     {DEFAULT_BDT.parent / 'bdt_gui.exe'}")
    print(f"   OR the command-line: bdt.exe")
    print(f"     {DEFAULT_BDT}")
    print("   Official guide:")
    print("     https://doc.telink-semi.cn/doc/en/software/res/tools/bdt_wins/bdt_wins_en/")
    print("2. Firmware file to flash (slot A, 0x00000):")
    print(f"     {BIN}")
    print(f"   Do not flash the intermediate raw image: {RAW_BIN.name}")
    print(f"   Ensure firmware integrity first:")
    print(f"     python bms_tools/bms.py verify   (checks {MANIFEST.name})")
    print("3. After burning:")
    print("   - Power-cycle the board.")
    print("   - Execute the product's existing board smoke test and communication check.")
    print("   - Record the firmware SHA-256 from firmware_manifest.json.")
    print("= NOTE: chip erase / sector erase of the SDK reserved area")
    print("  (0x74000-0x7FFFF) is NOT recommended — it holds SMP/MAC/calibration.")
    print("=" * 70)
    return 0


# ----------------------------------------------------------------------------
# Subcommand: ci  (repeatable host-side toolchain pipeline)
# ----------------------------------------------------------------------------
def cmd_ci(args: argparse.Namespace) -> int:
    report_dir = BUILD_DIR / "ci"
    report_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_path = report_dir / f"ci_{stamp}.json"
    latest_path = report_dir / "latest.json"

    script = str(Path(__file__).resolve())
    steps: list[tuple[str, list[str]]] = [
        ("tooling_unit_tests", [sys.executable, "-m", "unittest",
                                "tests.test_bms_tools", "-v"]),
        ("source_order", [sys.executable, script, "sources", "--check"]),
        ("environment", [sys.executable, script, "env"]),
        ("rebuild", [sys.executable, script, "rebuild", "--jobs", str(args.jobs)]),
        ("telink_postbuild", [sys.executable, script, "check-fw"]),
        ("size", [sys.executable, script, "size"]),
        ("map", [sys.executable, script, "map"]),
        ("manifest", [sys.executable, script, "manifest"]),
        ("verify", [sys.executable, script, "verify"]),
        ("static", [sys.executable, script, "static"]
                   + (["--strict"] if args.strict_static else [])),
    ]
    if args.baseline:
        steps.append(("baseline", [sys.executable, script, "baseline", args.baseline]))

    report = {
        "format": "bms-host-build-pipeline/v1",
        "started_at": _now_iso(),
        "git": _git_provenance(),
        "steps": [],
        "findings": [],
        "hardware_execution": {
            "performed": False,
            "pending": ["BDT flash/readback", "TLSR8251 board smoke test"],
        },
    }
    overall_rc = 0
    for name, command in steps:
        _info(f"ci step={name}")
        started = time.monotonic()
        result = subprocess.run(command, cwd=str(REPO_ROOT),
                                stdout=subprocess.PIPE, text=True,
                                stderr=subprocess.STDOUT, check=False)
        elapsed = round(time.monotonic() - started, 3)
        output = result.stdout or ""
        print(output, end="" if output.endswith("\n") else "\n")
        if name == "rebuild":
            warning_counts = [int(value) for value in
                              re.findall(r"compiler diagnostics: warnings=(\d+)", output)]
            compiler_warnings = max(warning_counts, default=0)
            if compiler_warnings:
                report["findings"].append({
                    "type": "compiler_warnings",
                    "count": compiler_warnings,
                    "evidence": (GEN_DIR / "build.log").relative_to(REPO_ROOT).as_posix(),
                })
        if name == "static":
            issue_counts = [int(value) for value in
                            re.findall(r"total issues: (\d+)", output)]
            static_issues = sum(issue_counts)
            if static_issues:
                report["findings"].append({
                    "type": "cppcheck_findings",
                    "count": static_issues,
                    "evidence": (BUILD_DIR / "static").relative_to(REPO_ROOT).as_posix()
                                + "/*.xml and *.txt",
                })
        report["steps"].append({
            "name": name,
            "command": command,
            "returncode": result.returncode,
            "elapsed_seconds": elapsed,
            "status": "PASS" if result.returncode == 0 else "FAIL",
            "output": output,
        })
        if result.returncode != 0:
            overall_rc = result.returncode
            break

    report["finished_at"] = _now_iso()
    report["status"] = ("FAIL" if overall_rc else
                        ("PASS_WITH_FINDINGS" if report["findings"] else "PASS"))
    report["acceptance"] = {
        "host_pipeline_completed": overall_rc == 0,
        "compiler_and_static_findings_closed": not bool(report["findings"]),
        "hardware_smoke_test_completed": False,
    }
    encoded = json.dumps(report, indent=2, ensure_ascii=False)
    # The rebuild step intentionally removes the complete CLI output tree,
    # including the report directory created at CI startup. Recreate only the
    # dedicated report directory after all steps have finished.
    report_dir.mkdir(parents=True, exist_ok=True)
    report_path.write_text(encoded, encoding="utf-8")
    latest_path.write_text(encoded, encoding="utf-8")
    _info(f"ci {report['status']} -> {report_path}")
    return overall_rc


# ----------------------------------------------------------------------------
# Argparse
# ----------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="bms.py",
        description="TLSR8251 BMS command-line build and analysis runner.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("env", help="check local toolchain / paths").set_defaults(func=cmd_env)

    psrc = sub.add_parser("sources", help="validate/update locked source and link order")
    source_mode = psrc.add_mutually_exclusive_group()
    source_mode.add_argument("--check", dest="source_action", action="store_const",
                             const="check", help="validate the locked order (default)")
    source_mode.add_argument("--update", dest="source_action", action="store_const",
                             const="update", help="explicitly regenerate deterministic order")
    source_mode.add_argument("--compare-ide", dest="source_action", action="store_const",
                             const="compare-ide", help="compare with optional IDE subdir.mk files")
    source_mode.add_argument("--import-ide", dest="source_action", action="store_const",
                             const="import-ide", help="explicitly replace locked order from IDE files")
    psrc.set_defaults(func=cmd_sources, source_action="check")

    pb = sub.add_parser("build", help="incremental build")
    pb.add_argument("-j", "--jobs", type=int, default=4)
    pb.set_defaults(func=cmd_build)

    pr = sub.add_parser("rebuild", help="clean + build")
    pr.add_argument("-j", "--jobs", type=int, default=4)
    pr.set_defaults(func=cmd_rebuild)

    sub.add_parser("objcopy", help="generate .bin from .elf").set_defaults(func=cmd_objcopy)
    sub.add_parser("check-fw", help="run tl_check_fw2.exe on the .bin").set_defaults(func=cmd_check_fw)
    sub.add_parser("size", help="text/data/bss size report").set_defaults(func=cmd_size)
    sub.add_parser("map", help="MAP file analysis").set_defaults(func=cmd_map)
    sub.add_parser("manifest", help="write firmware integrity manifest").set_defaults(func=cmd_manifest)
    sub.add_parser("verify", help="verify .bin against manifest").set_defaults(func=cmd_verify)

    pbl = sub.add_parser("baseline", help="compare new build to a reference .bin")
    pbl.add_argument("reference_bin")
    pbl.add_argument("--report-only", action="store_true",
                     help="report a mismatch but return success")
    pbl.set_defaults(func=cmd_baseline)

    ps = sub.add_parser("static", help="cppcheck static analysis")
    ps.add_argument("--strict", action="store_true", help="non-zero exit if any issue found")
    ps.add_argument("-j", "--jobs", type=int, default=4,
                    help="parallel Cppcheck jobs (default: 4)")
    ps.add_argument("--report-template",
                    help="Excel template path; defaults to the certified-document template on this PC")
    ps.add_argument("--no-report", action="store_true",
                    help="generate machine-readable analysis artifacts without the Excel report")
    ps.set_defaults(func=cmd_static)

    sub.add_parser("flash-help", help="human instructions for BDT burning").set_defaults(func=cmd_flash_help)

    pci = sub.add_parser("ci", help="run the repeatable host build pipeline and write a JSON report")
    pci.add_argument("-j", "--jobs", type=int, default=4)
    pci.add_argument("--strict-static", action="store_true",
                     help="fail the pipeline when cppcheck reports any issue")
    pci.add_argument("--baseline", help="optional reference BIN; content mismatch fails")
    pci.set_defaults(func=cmd_ci)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except subprocess.CalledProcessError as e:
        sys.stderr.write(f"[bms] command failed (rc={e.returncode}): {e}\n")
        if e.stdout:
            sys.stderr.write(e.stdout[-4000:] if isinstance(e.stdout, str) else "")
        return e.returncode


if __name__ == "__main__":
    sys.exit(main())
