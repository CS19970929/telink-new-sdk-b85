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
#   python bms_tools/bms.py compile [--jobs N]  # compile objects only; no BIN
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
from datetime import datetime, timezone
from pathlib import Path

# ----------------------------------------------------------------------------
# Path resolution (machine-portable)
# ----------------------------------------------------------------------------
PRODUCTS = ("d008", "d011", "d013", "d014")
_cli = sys.argv[1:]
_product_args = argparse.ArgumentParser(add_help=False)
_product_args.add_argument("--product", choices=PRODUCTS, default=os.environ.get("BMS_PRODUCT", "d014"))
_product_args.add_argument("--all-products", action="store_true")
_product_args.add_argument("--production", action="store_true", help="enforce production policy on every translation unit")
_product_args.add_argument("--d008-profile", choices=("16s-lfp", "20s-nmc", "24s-lfp"))
_selection, _cli = _product_args.parse_known_args(_cli)
PRODUCT = _selection.product
PRODUCTION = _selection.production
D008_PROFILE = _selection.d008_profile
BUILD_MODE = "production" if PRODUCTION else "development"
PROFILE_IDS = {"24s-lfp": 1, "20s-nmc": 2, "16s-lfp": 3}


def _selection_args(product=None):
    return (["--product", product or PRODUCT] + (["--production"] if PRODUCTION else []) +
            (["--d008-profile", D008_PROFILE] if D008_PROFILE else []))


def _build_configuration():
    return {"product": PRODUCT, "build_mode": BUILD_MODE, "production": PRODUCTION,
            "d008_profile": (D008_PROFILE or "16s-lfp") if PRODUCT == "d008" else None,
            "core_optimization": "-Os" if PRODUCTION else "-O2"}

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

# Per-checkout, per-product build outputs stay outside the source worktree.
BUILD_ROOT = Path(os.environ.get("BMS_BUILD_ROOT", str(Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "CodexTemp" / "bms-monorepo-build")))
BUILD_VARIANT = BUILD_MODE + ("-" + (D008_PROFILE or "16s-lfp") if PRODUCT == "d008" else "")
BUILD_DIR = (BUILD_ROOT / hashlib.sha256(str(REPO_ROOT).encode()).hexdigest()[:12] / BUILD_VARIANT / PRODUCT).resolve()
FIRMWARE_DIR = REPO_ROOT / "firmware" / BUILD_VARIANT / PRODUCT
OBJ_DIR = BUILD_DIR / "obj"
GEN_DIR = BUILD_DIR / "gen"
ELF = BUILD_DIR / "825x_ble_sample.elf"
BIN = FIRMWARE_DIR / "825x_ble_sample.bin"
RAW_BIN = BUILD_DIR / "825x_ble_sample.raw.bin"
LST = GEN_DIR / "825x_ble_sample.lst"
MAP = GEN_DIR / "825x_ble_sample.map"
MANIFEST = FIRMWARE_DIR / "fw_manifest.json"
SOURCE_ORDER_FILE = REPO_ROOT / "bms" / "products" / PRODUCT / "sources.txt"
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
# We transparently create a worktree-specific junction from a space-free path
# to the repo root, and give Make all paths via that junction. A fixed shared
# junction is unsafe: concurrent builds from different Git worktrees can
# silently switch each other's source roots. The on-disk build artifacts are
# identical (the junction resolves to the same directory); it is purely a
# Make-facing path rewrite.
# --------------------------------------------------------------------------
def _worktree_junction(repo_root: Path) -> Path:
    """Return a deterministic, space-free junction unique to one worktree."""
    identity = repo_root.resolve().as_posix().casefold().encode("utf-8")
    suffix = hashlib.sha256(identity).hexdigest()[:12]
    return Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "CodexTemp" / "bms-junctions" / f"repo_{suffix}"


JUNCTION = _worktree_junction(REPO_ROOT)
_junction_ok = False


def _ensure_junction() -> None:
    """Create/refresh JUNCTION -> REPO_ROOT. Idempotent. Outside the repo so
    it never gets committed; lives under the pre-approved opencode temp dir."""
    global _junction_ok
    if _junction_ok:
        return
    if os.name != "nt":
        # Linux paths need no Windows junction. _junc returns real paths below.
        _junction_ok = True
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
        _die(f"junction belongs to another checkout: {JUNCTION}")
    # Create the junction (mklink /J). Junctions do NOT need admin on Win10+.
    r = subprocess.run(["cmd", "/c", "mklink", "/J", str(JUNCTION), str(REPO_ROOT)],
                       capture_output=True, text=True)
    if r.returncode != 0 or not JUNCTION.exists():
        _die(f"failed to create space-free junction {JUNCTION} -> {REPO_ROOT}: {r.stderr}")
    _junction_ok = True


def _junc(p: Path) -> Path:
    """Return the space-free junction-based equivalent of a repo-rooted path."""
    if os.name != "nt":
        if " " in str(p.resolve()):
            _die("GNU Make requires a space-free path on Linux")
        return p.resolve()
    _ensure_junction()
    try:
        rel = p.resolve().relative_to(REPO_ROOT)
    except ValueError:
        return p  # outside repo: leave as-is
    return JUNCTION / rel

# Canonical toolchain locations (Telink IoT Studio install on this PC).
DEFAULT_TC32_DIR = Path(os.environ.get("TC32_BIN", "C:/TelinkIoTStudio/opt/tc32/bin"))
DEFAULT_BDT = Path("C:/TelinkIoTStudio/tools/libusbBDT/bin/bdt.exe")
DEFAULT_CPPCHECK = Path("C:/Program Files/cppcheck/cppcheck.exe")
DEFAULT_STATIC_REPORT_TEMPLATE = Path(
    "D:/c11认证文档/功能安全/13849模板/XXX-BMS 软件静态分析报告.xlsx"
)
SDK_BASELINE_COMMIT = "b9d4d0790cd7f163867872bf6ce7980a71dfee76"
PROJECT_SOURCE_PREFIX = "bms/"
STATIC_SCOPE_POLICY = (
    "检查选定产品的 bms/ 应用层；官方 SDK/工具链文件仅为解析真实类型、宏和条件编译，"
    "其诊断按范围策略排除"
)
STATIC_ANALYSIS_DIR = _HERE / "static_analysis"
CPPCHECK_CONFIG = STATIC_ANALYSIS_DIR / "cppcheck.cfg"
CPPCHECK_PLATFORM = STATIC_ANALYSIS_DIR / "tc32-platform.xml"
STATIC_REPORT_BUILDER = STATIC_ANALYSIS_DIR / "fill_static_report.py"

# Flash layout (per docs/STORAGE.md). Hard-coded so the
# firmware-integrity tooling knows the protected program-flash range without
# parsing the linker script.
FW_SLOT_A_BASE = 0x00000
FW_SLOT_A_END = 0x1EFFF   # inclusive
FW_SLOT_B_BASE = 0x20000
FW_SLOT_B_END = 0x3EFFF
OTA_META_A = (0x1F000, 0x1FFFF)
OTA_META_B = (0x3F000, 0x3FFFF)
SDK_RESERVED = (0x74000, 0x7FFFF)

DECLARED_MCU = "TLSR8251"
STARTUP_PROFILE = "MCU_STARTUP_8251"
STARTUP_SRAM_END = 0x848000
TLSR8251_SRAM_END_IN_SDK = 0x848000
TARGET_CONFIGURATION_RISK = "TLSR8251 startup profile matches the 32 KiB SRAM target"
MAIN_STACK_RESERVE_BYTES = 3072


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
    if os.name != "nt" and (DEFAULT_TC32_DIR / name).is_file():
        pinned = DEFAULT_TC32_DIR / name
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
    print(f"final firmware dir     : {FIRMWARE_DIR}")
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
    tc_bin = _tc32_tool("tc32-elf-gcc")
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
    print("Flash layout (8251 / 512K, per docs/STORAGE.md):")
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
    _info("environment tools OK; TLSR8251 target identity is consistent")
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
    """Discover selected product sources; reject omitted shared compilation units."""
    entries = []
    for group_rel, recursive in SOURCE_GROUPS:
        if group_rel == Path("vendor/ble_sample"):
            continue
        group = SDK_DIR / group_rel
        if group.exists():
            entries += [x.relative_to(REPO_ROOT).as_posix() for x in sorted(group.iterdir())
                        if x.is_file() and x.suffix in (".c", ".S")]
    backend = "dvc1124" if PRODUCT == "d008" else "sh3673510"
    for relative in ("bms/core", "bms/app", "bms/platform/telink", "bms/afe/"+backend,
                     "bms/products/"+PRODUCT):
        entries += [x.relative_to(REPO_ROOT).as_posix() for x in sorted((REPO_ROOT/relative).rglob("*.c"))]
    return [x for x in entries if PRODUCT == "d008" or Path(x).name not in ("bus_mux.c", "sif_send.c")]


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

        current = _load_source_order_strict()
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
        src = REPO_ROOT / rel
        obj_rel = rel.with_suffix(".o")
        obj = (obj_j / obj_rel).as_posix()
        src_j_posix = (_junc(REPO_ROOT) / rel).as_posix()
        objs.append(obj)
        subdirs_to_create.add((obj_dir / obj_rel).parent)
        out_lines.append("")
        out_lines.append(f"{obj}: {src_j_posix} {(_junc(gen_dir) / 'compile-inputs.json').as_posix()}")
        if src.suffix == ".S":
            out_lines.append(f"\t@echo 'Assembling: {src.name}'")
            out_lines.append(f"\t$(Q)$(CC) $(AFLAGS) -c -o\"$@\" \"$<\"")
        else:
            out_lines.append(f"\t@echo 'Building: {src.name}'")
            core_flags = " $(CORE_OPT_FLAGS)" if rel_text.startswith("bms/core/") else ""
            out_lines.append(f"\t$(Q)$(CC) $(CFLAGS){core_flags} -c -o\"$@\" \"$<\"")
    out_lines.insert(4, f"OBJS := {' '.join(objs)}")
    for directory in subdirs_to_create:
        directory.mkdir(parents=True, exist_ok=True)
    gen_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "sources.mk").write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    _info(f"source order: {len(objs)} objects")


def _firmware_git_build_id() -> str:
    """Return the first 32 bits of HEAD as an unsigned C literal."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--verify", "HEAD"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, check=False, timeout=10,
        )
        sha = result.stdout.strip().lower()
        if result.returncode == 0 and re.fullmatch(r"[0-9a-f]{40}", sha):
            return f"0x{sha[:8]}u"
    except Exception:
        pass
    return "0u"


def _firmware_git_dirty() -> int:
    """Return 1 when HEAD alone cannot reproduce the current worktree."""
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, check=False, timeout=10,
        )
        if result.returncode == 0:
            return 1 if result.stdout.strip() else 0
    except Exception:
        pass
    return 1  # unknown provenance is not a clean worktree


def _effective_extra_defines() -> str:
    extra = os.environ.get("EXTRA_DEFINES", "").strip()
    reserved = r"(?:-D|-U)\s*(BMS_PRODUCTION_BUILD|BMS_DIAG_BUILD_ID|BMS_DIAG_BUILD_DIRTY|D008_PRODUCT_PROFILE|BMS_D008_SCD_POLICY_APPROVED|BMS_D008_20S_NMC_PROTECTION_APPROVED|BMS_D013_HW_CONFIG_APPROVED)(?:\b)"
    if re.search(reserved, extra):
        _die("Build identity/mode/profile are owned by bms.py; use --production / --d008-profile")
    build_id, dirty = _firmware_git_build_id(), _firmware_git_dirty()
    if PRODUCTION:
        if dirty or build_id == "0u":
            _die("Production requires a clean committed worktree with a valid Git build ID")
        if PRODUCT == "d008" and D008_PROFILE is None:
            _die("D008 production requires --d008-profile (16s-lfp, 20s-nmc or 24s-lfp)")
    flags = [extra, f"-DBMS_PRODUCTION_BUILD={int(PRODUCTION)}",
             f"-DBMS_DIAG_BUILD_ID={build_id}", f"-DBMS_DIAG_BUILD_DIRTY={dirty}"]
    if PRODUCT == "d008" and D008_PROFILE:
        flags.append(f"-DD008_PRODUCT_PROFILE={PROFILE_IDS[D008_PROFILE]}")
    return " ".join(x for x in flags if x)


def _capture_compile_inputs(extra_defines: str) -> dict:
    """Conservative header closure: extra unused headers may rebuild, none go stale."""
    paths = {REPO_ROOT / rel for rel in _load_source_order_strict()}
    paths.update(SDK_DIR.rglob("*.h"))
    paths.update((REPO_ROOT / "bms").rglob("*.h"))
    paths.update(SDK_DIR.rglob("*.inc"))
    paths.update((SOURCE_ORDER_FILE, LINKER_FILE, _HERE / "build.mk", _HERE / "bms.py"))
    paths.update(REQUIRED_VENDOR_LIBS)
    files = {p.relative_to(REPO_ROOT).as_posix(): _sha256(p) for p in sorted(paths)}
    executables = [_tc32_tool(n) for n in ("tc32-elf-gcc", "tc32-elf-as", "tc32-elf-ld", "tc32-elf-objcopy", "tc32-elf-objdump")]
    gcc = Path(executables[0])
    executables += [str(p) for directory in ("libexec/gcc", "lib/gcc")
                    for p in (gcc.parent.parent / directory).rglob("cc1*") if p.is_file()]
    executables.append(str(TL_CHECK_FW2))
    return {"schema": "bms-compile-inputs/v1", "files": files,
            "tools": {str(Path(p).resolve()): _sha256(Path(p)) for p in executables},
            "configuration": _build_configuration(),
            "extra_defines": extra_defines,
            "git_build_id": _firmware_git_build_id(), "git_dirty": _firmware_git_dirty()}


def _write_compile_inputs(extra_defines: str) -> None:
    path = GEN_DIR / "compile-inputs.json"
    content = json.dumps(_capture_compile_inputs(extra_defines), sort_keys=True, indent=2)+"\n"
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def _mark_build_complete() -> None:
    receipt = {"inputs_sha256": _sha256(GEN_DIR / "compile-inputs.json"),
               "artifacts": {p.name: _sha256(p) for p in (ELF, MAP, LST, RAW_BIN)}}
    (GEN_DIR / "build-completed.json").write_text(json.dumps(receipt, sort_keys=True), encoding="utf-8")


def _read_compile_inputs(elf_only: bool = False) -> dict:
    path = GEN_DIR / "compile-inputs.json"
    if not path.exists():
        _die("Build input receipt missing; rebuild required")
    complete_path = GEN_DIR / ("link-completed.json" if elf_only else "build-completed.json")
    if not complete_path.exists():
        _die("No successful completed build receipt; rebuild required")
    complete = json.loads(complete_path.read_text(encoding="utf-8"))
    artifacts = (ELF, MAP, LST) if elf_only else (ELF, MAP, LST, RAW_BIN)
    if complete != {"inputs_sha256": _sha256(path), "artifacts": {p.name: _sha256(p) for p in artifacts}}:
        _die("Build receipt/artifacts differ from successful build; rebuild required")
    receipt = json.loads(path.read_text(encoding="utf-8"))
    if receipt != _capture_compile_inputs(receipt.get("extra_defines", "")):
        _die("Source/header/toolchain/build configuration changed since build; rebuild required")
    return receipt


def _make_diagnostics(output: str) -> list[str]:
    """Keep source locations clickable while hiding Make's follow-on noise."""
    sdk_prefix = _junc(SDK_DIR).as_posix().rstrip("/") + "/"
    lines = []
    for line in output.splitlines():
        match = re.match(r"^(.+?):(\d+):(\d+):\s+(fatal error|error|warning):\s+(.*)$", line)
        if match is None:
            continue
        source, line_no, column, severity, message = match.groups()
        if source.lower().startswith(sdk_prefix.lower()):
            source = source[len(sdk_prefix):]
        if severity == "fatal error":
            severity = "error"
        lines.append(f"{source}:{line_no}:{column}: {severity}: {message}")
    return lines


def _invoke_make(targets: list[str], jobs: int = 1,
                 build_dir: Path = BUILD_DIR) -> None:
    resolved_build = build_dir.resolve()
    if resolved_build != BUILD_DIR:
        _die(f"refusing Make clean/build outside the dedicated CLI directory: {resolved_build}")
    env = _ensure_toolchain_env(dict(os.environ))
    env["PATH"] = str(Path(_tc32_tool("tc32-elf-gcc")).parent) + os.pathsep + env["PATH"]
    extra_defines = _effective_extra_defines()
    build_id = _firmware_git_build_id()
    dirty = _firmware_git_dirty()
    env["EXTRA_DEFINES"] = extra_defines
    if targets != ["clean"]:
        _info(f"firmware diagnostic build id: {build_id}; dirty={dirty}")
    make = _need_make()
    _gen_sources_mk(build_dir)
    if "all" in targets or "compile" in targets or "link" in targets:
        _write_compile_inputs(extra_defines)
        (GEN_DIR / "build-completed.json").unlink(missing_ok=True)
        (GEN_DIR / "link-completed.json").unlink(missing_ok=True)
        (GEN_DIR / "resources.json").unlink(missing_ok=True)
    # Pass all Make-facing paths via the junction (space-free).
    repo_j = _junc(REPO_ROOT).as_posix()
    sdk_j = _junc(SDK_DIR).as_posix()
    build_j = _junc(build_dir).as_posix()
    cmd = [make, "-f", str(_HERE / "build.mk"),
           "-j", str(jobs),
           f"REPO_ROOT={repo_j}",
           f"SDK_DIR={sdk_j}",
           f"BUILD_DIR={build_j}", f"PRODUCT={PRODUCT}", f"AFE_BACKEND={'dvc1124' if PRODUCT == 'd008' else 'sh3673510'}"]
    cmd += targets
    if targets != ["clean"]:
        _info(f"make: {', '.join(targets) if len(targets) < 3 else 'multiple targets'}; jobs={jobs}")
    r = _run(cmd, cwd=_junc(REPO_ROOT), env=env, check=False, capture=True)
    log_dir = build_dir / "gen"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "build.log"
    log_path.write_text(r.stdout or "", encoding="utf-8")
    warning_count = len(re.findall(r"\bwarning:", r.stdout or "", re.I))
    error_count = len(re.findall(r"\berror:", r.stdout or "", re.I))
    log_display = log_path.as_posix()
    if targets == ["clean"] and r.returncode == 0:
        _info("clean complete")
    else:
        diagnostic_lines = _make_diagnostics(r.stdout or "")
        if r.returncode != 0:
            _info(f"BUILD FAILED: {error_count} error(s), {warning_count} warning(s)")
        elif warning_count != 0:
            _info(f"warning gate failed: {warning_count} warning(s)")
        else:
            _info("make succeeded: 0 errors, 0 warnings")
        if diagnostic_lines:
            print("\n".join(diagnostic_lines))
        elif r.returncode != 0 or warning_count != 0:
            other_errors = [line for line in (r.stdout or "").splitlines()
                            if "undefined reference" in line or
                            re.search(r"\b(?:error|warning):", line, re.I) or
                            re.search(r"make(?:\.EXE)?: \*\*\*", line, re.I)]
            print("\n".join(other_errors or (r.stdout or "").splitlines()[-10:]))
        if r.returncode != 0 or warning_count != 0:
            _info(f"full log: {log_display}")
    if r.returncode != 0:
        raise subprocess.CalledProcessError(r.returncode, cmd, output=r.stdout)
    if warning_count != 0:
        _die(f"compiler warning gate failed: warnings={warning_count}; see {log_path}")
    if "all" in targets:
        _mark_build_complete()
    if "link" in targets or "all" in targets:
        receipt = {"inputs_sha256": _sha256(GEN_DIR / "compile-inputs.json"),
                   "artifacts": {p.name: _sha256(p) for p in (ELF, MAP, LST)}}
        (GEN_DIR / "link-completed.json").write_text(json.dumps(receipt, sort_keys=True), encoding="utf8")
        if PRODUCTION:
            cmd_map(argparse.Namespace(elf_only=True))


def _require_release_approval() -> None:
    """正式生产镜像必须通过源码签核；link/resources 允许验证未签核工程配置。"""
    if not PRODUCTION:
        return
    command = [_tc32_tool("tc32-elf-gcc"), "-E", "-x", "c",
               "-I", str(REPO_ROOT / "bms/products" / PRODUCT),
               "-I", str(REPO_ROOT / "bms/products"),
               "-I", str(REPO_ROOT / "bms/core"),
               *shlex.split(_effective_extra_defines()), "-"]
    result = subprocess.run(command, input='#include "bms_release_approval.h"\n',
                            text=True, capture_output=True)
    if result.returncode:
        _die("生产镜像签核门未通过；link/resources 仅供工程验证。\n" + result.stderr.strip())


def cmd_build(args: argparse.Namespace) -> int:
    _require_release_approval()
    _invoke_make(["all"], jobs=args.jobs)
    _finalize_firmware()
    _info("build complete (ELF/MAP/raw BIN/canonical BIN)")
    return 0


def cmd_link(args: argparse.Namespace) -> int:
    _invoke_make(["link"], jobs=args.jobs)
    return 0


def cmd_compile(args: argparse.Namespace) -> int:
    _invoke_make(["compile"], jobs=args.jobs)
    _info("compile complete (object files only; no firmware image generated)")
    return 0


def cmd_rebuild(args: argparse.Namespace) -> int:
    _require_release_approval()
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
    _require_release_approval()
    if not ELF.exists():
        _die(f"ELF missing: {ELF}. Run 'build' first.")
    if not TL_CHECK_FW2.exists():
        _die(f"tl_check_fw2.exe missing: {TL_CHECK_FW2}")
    if PRODUCTION:
        cmd_map(argparse.Namespace(elf_only=True))
    _objcopy(ELF, RAW_BIN)
    processed_bin = BUILD_DIR / "825x_ble_sample.bin"
    shutil.copy2(RAW_BIN, processed_bin)
    _run_tl_check_fw(processed_bin)
    if not _telink_crc_details(processed_bin.read_bytes()).get("valid"):
        _die("canonical BIN failed Telink trailer/residue validation")
    BIN.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(processed_bin, BIN)
    MANIFEST.unlink(missing_ok=True)
    _info(f"final firmware: {BIN}")


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
def _map_symbol_value(text: str, symbol: str) -> int | None:
    patterns = (
        rf"\b{re.escape(symbol)}\b\s*=\s*0x([0-9a-fA-F]+)",
        rf"^\s*0x([0-9a-fA-F]+)\s+PROVIDE\s*\(\s*{re.escape(symbol)}\s*,",
        rf"^\s*0x([0-9a-fA-F]+)\s+{re.escape(symbol)}\b",
    )
    for pattern in patterns:
        match = re.search(pattern, text, re.M)
        if match:
            return int(match.group(1), 16)
    return None


def _listing_abs_symbol_value(text: str, symbol: str) -> int | None:
    match = re.search(
        rf"^\s*([0-9a-fA-F]+)\s+\S+\s+\*ABS\*\s+[0-9a-fA-F]+\s+{re.escape(symbol)}\s*$",
        text,
        re.M,
    )
    return int(match.group(1), 16) if match else None


def cmd_map(args: argparse.Namespace) -> int:
    if getattr(args, "elf_only", False): _read_compile_inputs(elf_only=True)
    if not MAP.exists():
        _die(f"MAP missing: {MAP}. Run 'build' first.")
    if not LST.exists():
        _die(f"LST missing: {LST}. Run 'build' first.")
    text = MAP.read_text(encoding="utf-8", errors="replace")
    listing = LST.read_text(encoding="utf-8", errors="replace")
    print("MAP analysis:")
    print(f"  file: {MAP}")
    print(f"  size: {MAP.stat().st_size} bytes")

    symbols = {}
    for sym in ("_bin_size_", "_code_size_", "_ram_use_end_", "_start_bss_",
                "_end_bss_", "_start_data_", "_end_data_", "_retention_size_"):
        value = _map_symbol_value(text, sym)
        if value is not None:
            symbols[sym] = value
            print(f"  {sym:<22} = 0x{value:x}")

    found = re.findall(
        r"^\.(vectors|cstartup_ram_funcs|ram_code|retention_data|text|rodata|data|bss|data_no_init|sdk_version)"
        r"\s+0x([0-9a-fA-F]+)",
        text,
        re.M,
    )
    if found:
        print("  sections (start address):")
        for name, addr in found[:10]:
            print(f"    .{name:<22} @ 0x{int(addr, 16):08x}")

    sram_size = _listing_abs_symbol_value(listing, "__SRAM_SIZE")
    if sram_size is None:
        _die("LST missing __SRAM_SIZE absolute symbol; cannot validate TLSR8251 startup")
    print(f"  __SRAM_SIZE           = 0x{sram_size:06x}")
    if sram_size != STARTUP_SRAM_END:
        _die(
            f"startup SRAM mismatch: ELF/LST=0x{sram_size:06X}, "
            f"expected TLSR8251=0x{STARTUP_SRAM_END:06X}"
        )

    ram_end = symbols.get("_ram_use_end_")
    if ram_end is None:
        _die("MAP missing _ram_use_end_; cannot validate SRAM headroom")

    ram_base = 0x840000
    ram_limit = STARTUP_SRAM_END - MAIN_STACK_RESERVE_BYTES
    ram_used_span = ram_end - ram_base
    ram_total = STARTUP_SRAM_END - ram_base
    stack_safe_headroom = ram_limit - ram_end
    print(f"  TLSR8251 SRAM span    = {ram_total} bytes")
    print(f"  RAM address span used = {ram_used_span} bytes")
    print(f"  stack reserve         = {MAIN_STACK_RESERVE_BYTES} bytes")
    print(f"  stack-safe headroom   = {stack_safe_headroom} bytes")
    if ram_end < ram_base or ram_end >= ram_limit:
        _die(
            f"TLSR8251 SRAM overflow risk: _ram_use_end_=0x{ram_end:06X}, "
            f"limit=0x{ram_limit:06X}"
        )

    bin_size = symbols.get("_bin_size_")
    slot_size = FW_SLOT_A_END - FW_SLOT_A_BASE + 1
    if bin_size is None or bin_size <= 0:
        _die("MAP missing valid _bin_size_; cannot validate image")
    elf_only = getattr(args, "elf_only", False)
    if not elf_only and not BIN.exists():
        _die("Canonical BIN missing; run check-fw before map")
    # SDK checker pads to a 16-byte payload boundary then appends a CRC word.
    expected_size = ((bin_size + 15) // 16) * 16 + 4
    canonical_size = expected_size if elf_only else BIN.stat().st_size
    if not elf_only and canonical_size != expected_size:
        _die(f"MAP/BIN size mismatch: aligned {bin_size} + CRC != {canonical_size}")
    if canonical_size > slot_size:
        _die(f"firmware image exceeds slot A: {canonical_size} > {slot_size}")
    flash_headroom = slot_size - canonical_size
    if PRODUCTION and flash_headroom < 8192:
        _die(f"Production Flash reserve below 8 KiB: {flash_headroom} bytes; reduce code before release")
    report = {
        "schema": "bms-resources/v1", "git": _git_provenance(),
        "elf_sha256": _sha256(ELF) if ELF.exists() else None,
        "product": PRODUCT, "configuration": _build_configuration(), "image_generated": not elf_only,
        "bin_sha256": None if elf_only else _sha256(BIN), "map_sha256": _sha256(MAP),
        "flash_bytes": canonical_size, "flash_limit_bytes": slot_size,
        "flash_free_bytes": flash_headroom, "ram_span_bytes": ram_used_span,
        "ram_total_bytes": ram_total, "main_stack_gap_bytes": ram_total-ram_used_span,
        "main_stack_reserve_bytes": MAIN_STACK_RESERVE_BYTES,
        "ram_growth_headroom_bytes": stack_safe_headroom,
        "note": "Address span includes RAM code/cache/IRQ stack; main reserve is a budget, not measured high water.",
        "sections": {m.group(1): {"address": int(m.group(2),16), "bytes": int(m.group(3),16)}
            for m in re.finditer(r"^\.(vectors|ram_code|retention_data|text|cstartup_ram_funcs|rodata|data|bss|data_no_init|sdk_version)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", text, re.M)},
        "warnings": [],
    }
    if flash_headroom < 8192:
        report["warnings"].append("Flash free below 8 KiB: review feature budget")
    if stack_safe_headroom < 2048:
        report["warnings"].append("RAM growth headroom below 2 KiB after main stack reserve")
    baseline = getattr(args, "baseline", None)
    if baseline:
        old = json.loads(Path(baseline).read_text(encoding="utf-8"))
        if old.get("schema") != report["schema"]:
            _die("Unsupported resource baseline schema")
        report["delta"] = {k: report[k]-old[k] for k in ("flash_bytes", "ram_span_bytes")}
        if report["delta"]["flash_bytes"] > 1024 or report["delta"]["ram_span_bytes"] > 256:
            report["warnings"].append("Change exceeds +1 KiB Flash or +256 B RAM: review resource delta")
    output = Path(getattr(args, "output", None) or (GEN_DIR / "resources.json"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
    print(f"  {'projected image' if elf_only else 'canonical BIN'} = {canonical_size} bytes; free {flash_headroom}")
    for warning in report["warnings"]:
        print("  WARNING: " + warning)
    print(f"Resource report: {output}")
    print("MAP analysis PASS: TLSR8251 startup/RAM/slot limits are valid.")
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
    def capture(argv: list[str], allow_empty: bool = False) -> str | None:
        try:
            result = subprocess.run(argv, cwd=str(REPO_ROOT), capture_output=True,
                                    text=True, check=False, timeout=10)
            value = result.stdout.strip()
            return value if result.returncode == 0 and (value or allow_empty) else None
        except Exception:
            return None

    status = capture(["git", "status", "--porcelain"], allow_empty=True)
    return {
        "commit": capture(["git", "rev-parse", "HEAD"]),
        "branch": capture(["git", "branch", "--show-current"]),
        "dirty": bool(status) if status is not None else None,
    }


def _build_input_provenance() -> dict:
    entries = _load_source_order_strict()
    receipt = _read_compile_inputs()
    objects = []
    object_order: list[str] = []
    for source in entries:
        object_path = BUILD_DIR / "obj" / Path(source).with_suffix(".o")
        object_rel = object_path.relative_to(BUILD_DIR).as_posix()
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
        "compile_inputs": receipt,
        "artifacts": {p.name: _sha256(p) for p in (ELF, MAP, LST, RAW_BIN)},
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
        "format": "bms-fw-manifest/v4",
        "product": PRODUCT,
        "configuration": _build_configuration(),
        "build_directory": str(BUILD_DIR),
        "generated_at": _now_iso(),
        "firmware_name": "825x_ble_sample",
        "chip": "TLSR8251 / TLSR825x (B85)",
        "elf": str(ELF),
        "bin": str(BIN),
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
            "tc32": _tool_version([_tc32_tool("tc32-elf-gcc"), "--version"], 0),
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
    cmd_map(argparse.Namespace())
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
    receipt = _read_compile_inputs()
    provenance_ok = receipt == build_inputs.get("compile_inputs")
    artifacts_ok = build_inputs.get("artifacts") == {p.name: _sha256(p) for p in (ELF, MAP, LST, RAW_BIN)}
    print(f"source/header/tools   {'OK' if provenance_ok else 'MISMATCH'}")
    print(f"ELF/MAP/LST/raw BIN    {'OK' if artifacts_ok else 'MISMATCH'}")
    ok = ok and provenance_ok and artifacts_ok
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
            object_path = BUILD_DIR / Path(record["object"])
            if not object_path.resolve().is_relative_to(BUILD_DIR):
                raise SourceOrderError("Manifest object escapes product build directory")
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
def cmd_static(args: argparse.Namespace) -> int:
    if __package__:
        from .static_analysis import StaticAnalysis
    else:
        from static_analysis import StaticAnalysis
    return StaticAnalysis(sys.modules[__name__]).cmd_static(args)


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
def cmd_test(args: argparse.Namespace) -> int:
    command = [sys.executable, str(REPO_ROOT / "tests/run_host_regression.py")]
    if not _selection.all_products:
        command += ["--product", PRODUCT]
    if args.output:
        command += ["--output", args.output]
    return subprocess.call(command, cwd=REPO_ROOT)


def cmd_release(args: argparse.Namespace) -> int:
    if not PRODUCTION:
        _die("release requires --production and product approval")
    cmd_rebuild(args)
    cmd_manifest(args)
    return cmd_verify(args)


def cmd_ci(args: argparse.Namespace) -> int:
    report_dir = BUILD_DIR / "ci"
    report_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_path = report_dir / f"ci_{stamp}.json"
    latest_path = report_dir / "latest.json"

    script = str(Path(__file__).resolve())
    steps: list[tuple[str, list[str]]] = [
        ("host_regression", [sys.executable, "tests/run_host_regression.py"]),
        ("source_order", [sys.executable, script, *_selection_args(), "sources", "--check"]),
        ("environment", [sys.executable, script, *_selection_args(), "env"]),
        ("rebuild", [sys.executable, script, *_selection_args(), "rebuild", "--jobs", str(args.jobs)]),
        ("telink_postbuild", [sys.executable, script, *_selection_args(), "check-fw"]),
        ("size", [sys.executable, script, *_selection_args(), "size"]),
        ("map", [sys.executable, script, *_selection_args(), "map"]),
        ("manifest", [sys.executable, script, *_selection_args(), "manifest"]),
        ("verify", [sys.executable, script, *_selection_args(), "verify"]),
        ("static", [sys.executable, script, *_selection_args(), "static"]
                   + (["--strict"] if args.strict_static else [])),
    ]
    if args.baseline:
        steps.append(("baseline", [sys.executable, script, *_selection_args(), "baseline", args.baseline]))

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
                    "evidence": (GEN_DIR / "build.log").as_posix(),
                })
        if name == "static":
            issue_counts = [int(value) for value in
                            re.findall(r"total issues: (\d+)", output)]
            static_issues = sum(issue_counts)
            if static_issues:
                report["findings"].append({
                    "type": "cppcheck_findings",
                    "count": static_issues,
                    "evidence": (BUILD_DIR / "static").as_posix()
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
        description="TLSR8251 BMS runner. Global options: --product d008/d011/d013/d014 or --all-products; --production; --d008-profile 16s-lfp/20s-nmc/24s-lfp.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    test_parser = sub.add_parser("test", help="所选产品 host 回归；不生成 BIN")
    test_parser.add_argument("--output", help="源码树外的空报告目录")
    test_parser.set_defaults(func=cmd_test)
    release_parser = sub.add_parser("release", help="已签核生产镜像、manifest 和完整性验证；生成 BIN")
    release_parser.add_argument("-j", "--jobs", type=int, default=4)
    release_parser.set_defaults(func=cmd_release)

    sub.add_parser("env", help="check local toolchain / paths").set_defaults(func=cmd_env)

    psrc = sub.add_parser("sources", help="validate/update locked source and link order")
    source_mode = psrc.add_mutually_exclusive_group()
    source_mode.add_argument("--check", dest="source_action", action="store_const",
                             const="check", help="validate the locked order (default)")
    source_mode.add_argument("--update", dest="source_action", action="store_const",
                             const="update", help="explicitly regenerate deterministic order")
    psrc.set_defaults(func=cmd_sources, source_action="check")

    pb = sub.add_parser("build", help="incremental build")
    pb.add_argument("-j", "--jobs", type=int, default=4)
    pb.set_defaults(func=cmd_build)

    pc = sub.add_parser("compile", help="compile objects only, without generating firmware BIN")
    pc.add_argument("-j", "--jobs", type=int, default=4)
    pc.set_defaults(func=cmd_compile)

    pr = sub.add_parser("rebuild", help="clean + build")
    pr.add_argument("-j", "--jobs", type=int, default=4)
    pr.set_defaults(func=cmd_rebuild)

    sub.add_parser("objcopy", help="generate .bin from .elf").set_defaults(func=cmd_objcopy)
    sub.add_parser("check-fw", help="run tl_check_fw2.exe on the .bin").set_defaults(func=cmd_check_fw)
    sub.add_parser("size", help="text/data/bss size report").set_defaults(func=cmd_size)
    map_parser = sub.add_parser("map", help="Validate SRAM/image budgets and emit resources.json")
    map_parser.add_argument("--baseline", help="Previous resources.json for byte deltas")
    map_parser.add_argument("--output", help="Resource report destination")
    map_parser.set_defaults(func=cmd_map)
    resources_parser = sub.add_parser("resources", help="ELF/MAP resource gates; no firmware image required")
    resources_parser.add_argument("--output")
    resources_parser.add_argument("--baseline")
    resources_parser.set_defaults(func=cmd_map, elf_only=True)
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
    pl = sub.add_parser("link", help="compile and link ELF/MAP only; does not generate BIN")
    pl.add_argument("-j", "--jobs", type=int, default=4)
    pl.set_defaults(func=cmd_link)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(_cli if argv is None else argv)
    if _selection.all_products and argv is None and args.cmd != "test":
        results = [subprocess.call([sys.executable, str(Path(__file__).resolve()), *_selection_args(product), *_cli]) for product in PRODUCTS]
        return 1 if any(results) else 0
    try:
        return args.func(args)
    except subprocess.CalledProcessError as e:
        if e.output is None:
            sys.stdout.flush()
            sys.stderr.write(f"[bms] command failed (rc={e.returncode}).\n")
        return e.returncode


if __name__ == "__main__":
    sys.exit(main())
