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

BUILD_DIR = (REPO_ROOT / "build" / "bms").resolve()
OBJ_DIR = BUILD_DIR / "obj"
GEN_DIR = BUILD_DIR / "gen"
ELF = BUILD_DIR / "825x_ble_sample.elf"
BIN = BUILD_DIR / "825x_ble_sample.bin"
RAW_BIN = BUILD_DIR / "825x_ble_sample.raw.bin"
LST = GEN_DIR / "825x_ble_sample.lst"
MAP = GEN_DIR / "825x_ble_sample.map"
MANIFEST = BUILD_DIR / "fw_manifest.json"

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
# Subcommand: build / rebuild
# ----------------------------------------------------------------------------
def _gen_sources_mk(build_dir: Path = BUILD_DIR) -> None:
    """Generate build/bms/sources.mk with one per-object rule. Paths are
    written via the space-free junction so GNU Make parses them correctly
    even when the repo directory contains a literal space."""
    _ensure_junction()
    obj_dir = build_dir / "obj"
    gen_dir = build_dir / "gen"
    build_dir.mkdir(parents=True, exist_ok=True)
    obj_dir.mkdir(parents=True, exist_ok=True)

    sdk_j = _junc(SDK_DIR)
    obj_j = _junc(obj_dir)

    src_groups = [
        SDK_DIR / "vendor" / "common",
        SDK_DIR / "vendor" / "ble_sample",
        SDK_DIR / "drivers" / "B85",
        SDK_DIR / "drivers" / "B85" / "flash",
        SDK_DIR / "drivers" / "B85" / "driver_ext",
        SDK_DIR / "common",                 # contains div_mod.S (provides __udivsi3 etc)
        SDK_DIR / "boot" / "B85",           # contains cstartup_825x.S
        SDK_DIR / "application" / "usbstd",
        SDK_DIR / "application" / "print",
        SDK_DIR / "application" / "keyboard",
        SDK_DIR / "application" / "audio",
        SDK_DIR / "application" / "app",
    ]
    out_lines = ["# auto-generated by bms_tools/bms.py - do not edit",
                 f"# generated_at: {_now_iso()}",
                 f"# NOTE: paths use the space-free junction {JUNCTION} -> {REPO_ROOT}"]
    objs: list[str] = []
    seen_obj_targets: set[str] = set()  # dedup case-insensitive collisions
    subdirs_to_create: set[Path] = set()
    for d in src_groups:
        if not d.exists():
            continue
        # C sources compiled with the full CFLAGS.
        c_srcs = sorted(p for p in d.glob("*.c") if p.is_file())
        # Assembly sources (.S, preprocessed; case-insensitive match picks up
        # .S and would also pick up .s if present). Eclipse compiles these
        # with `tc32-elf-gcc -DMCU_STARTUP_8258 -c`, i.e. the assembler defines
        # (AFLAGS).
        asm_srcs = sorted(p for p in d.glob("*.S") if p.is_file())
        for src, is_asm in [(s, False) for s in c_srcs] + [(s, True) for s in asm_srcs]:
            try:
                rel = src.relative_to(SDK_DIR)
            except ValueError:
                rel = Path(src.name)
            obj_rel = rel.with_suffix(".o")
            obj = (obj_j / obj_rel).as_posix()
            if obj in seen_obj_targets:
                continue  # skip duplicate obj target (case collision guard)
            seen_obj_targets.add(obj)
            src_j_posix = (sdk_j / rel).as_posix()
            objs.append(obj)
            subdirs_to_create.add((obj_dir / obj_rel).parent)
            out_lines.append("")
            out_lines.append(f"{obj}: {src_j_posix}")
            # NOTE: no `@mkdir` recipe — Python pre-creates the obj subdirs.
            if is_asm:
                out_lines.append(f"\t@echo 'Assembling: {src.name}'")
                out_lines.append(f"\t$(CC) $(AFLAGS) -c -o\"$@\" \"$<\"")
            else:
                out_lines.append(f"\t@echo 'Building: {src.name}'")
                out_lines.append(f"\t$(CC) $(CFLAGS) -c -o\"$@\" \"$<\"")
    out_lines.insert(3, f"OBJS := {' '.join(objs)}")
    # Pre-create every obj subdir (and BUILD_DIR / GEN_DIR) so recipe shells
    # never need `mkdir -p` (Windows' built-in mkdir does not support -p).
    for sd in subdirs_to_create:
        sd.mkdir(parents=True, exist_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)
    gen_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "sources.mk").write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    _info(f"generated sources.mk: {build_dir / 'sources.mk'}  ({len(objs)} objects)")


def _invoke_make(targets: list[str], jobs: int = 1,
                 build_dir: Path = BUILD_DIR) -> None:
    managed_root = (REPO_ROOT / "build").resolve()
    resolved_build = build_dir.resolve()
    try:
        resolved_build.relative_to(managed_root)
    except ValueError:
        _die(f"refusing Make clean/build outside managed build root: {resolved_build}")
    if resolved_build == managed_root:
        _die(f"refusing to use broad build root as a target: {resolved_build}")
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


def cmd_manifest(args: argparse.Namespace) -> int:
    if not BIN.exists():
        _die(f"BIN missing: {BIN}. Run 'objcopy' first.")
    data = BIN.read_bytes()
    telink_crc = _telink_crc_details(data)
    if not telink_crc.get("valid"):
        _die("BIN is not a valid single-pass tl_check_fw2 image. Run 'check-fw' first.")
    manifest = {
        "format": "bms-fw-manifest/v2",
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
        "vendor_libraries": {
            str(path.relative_to(REPO_ROOT)): {
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
    if not DEFAULT_CPPCHECK.exists():
        _die(f"cppcheck not found: {DEFAULT_CPPCHECK}")
    out_dir = BUILD_DIR.parent / "static"
    out_dir.mkdir(parents=True, exist_ok=True)
    cfg_path = _HERE / "static_analysis" / "cppcheck.cfg"
    if not cfg_path.exists():
        _die(f"cppcheck config missing: {cfg_path}")
    cfg_args: list[str] = []
    for raw_line in cfg_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        cfg_args.extend(shlex.split(line, posix=False))
    # Four scopes; project code and official SDK code are reported separately.
    scopes = {
        "bms_app": [SDK_DIR / "vendor" / "ble_sample"],     # project-owned code
        "vendor_common": [SDK_DIR / "vendor" / "common"],   # SDK vendor sample (trusted)
        "drivers_b85": [SDK_DIR / "drivers" / "B85"],       # SDK B85 driver (trusted)
        "common": [SDK_DIR / "common"],                     # SDK common (trusted)
    }
    includes = [str(PROJ_DIR), str(SDK_DIR),
                 str(SDK_DIR / "vendor" / "common"),
                 str(SDK_DIR / "vendor" / "ble_sample"),
                 str(SDK_DIR / "common"),
                str(SDK_DIR / "drivers" / "B85")]
    defs = ["__PROJECT_8258_BLE_SAMPLE__=1", "CHIP_TYPE=CHIP_TYPE_825x"]
    overall_rc = 0
    for name, roots in scopes.items():
        out_xml = out_dir / f"{name}.xml"
        out_txt = out_dir / f"{name}.txt"
        # cppcheck 2.x writes the XML report to stderr by design. We capture
        # stderr into .xml and write a small human-readable text summary into
        # .txt (built from the XML so the two always agree).
        cmd = [str(DEFAULT_CPPCHECK), *cfg_args, "--xml-version=2"]
        for r in roots:
            cmd.append(str(r))
        for inc in includes:
            cmd += ["-I", inc]
        for d in defs:
            cmd += ["-D", d]
        _info(f"cppcheck scope={name} roots={[str(r) for r in roots]}")
        r = subprocess.run(cmd, capture_output=True, text=True)
        xml = r.stderr if r.stderr is not None else ""
        out_xml.write_text(xml, encoding="utf-8")
        # Count + summarise and include actionable file/line details.
        parsed_entries: list[dict] = []
        try:
            root = ET.fromstring(xml)
            for error in root.findall(".//error"):
                location = error.find("location")
                parsed_entries.append({
                    "id": error.get("id", "unknown"),
                    "severity": error.get("severity", "unknown"),
                    "message": error.get("msg", ""),
                    "file": location.get("file", "") if location is not None else "",
                    "line": location.get("line", "") if location is not None else "",
                })
        except ET.ParseError as exc:
            _die(f"cppcheck emitted invalid XML for scope {name}: {exc}")
        entries = [(entry["id"], entry["severity"]) for entry in parsed_entries]
        txt_lines = [f"# cppcheck {name} scope summary",
                     f"# generated_at: {_now_iso()}",
                     f"# total issues: {len(entries)}",
                     ""]
        sev_counts: dict[str, int] = {}
        id_counts: dict[str, int] = {}
        for eid, sev in entries:
            sev_counts[sev] = sev_counts.get(sev, 0) + 1
            id_counts[eid] = id_counts.get(eid, 0) + 1
        txt_lines.append("# by severity:")
        for s, c in sorted(sev_counts.items()):
            txt_lines.append(f"  {s:<14} {c}")
        txt_lines.append("# by id (top 15):")
        for eid, c in sorted(id_counts.items(), key=lambda kv: -kv[1])[:15]:
            txt_lines.append(f"  {c:>4}  {eid}")
        txt_lines.append("")
        txt_lines.append("# details:")
        for entry in parsed_entries:
            txt_lines.append(
                f"  {entry['severity']:<11} {entry['id']:<28} "
                f"{entry['file']}:{entry['line']} {entry['message']}"
            )
        out_txt.write_text("\n".join(txt_lines) + "\n", encoding="utf-8")
        _info(f"  total issues: {len(entries)}  ({dict(sev_counts)})  -> {out_xml.name} / {out_txt.name}")
        if len(entries) > 0 and args.strict:
            overall_rc = 1
    # Always print the project-app summary regardless of strictness.
    _info(f"static analysis complete. Reports under {out_dir}")
    return overall_rc


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
    report_dir = REPO_ROOT / "build" / "ci"
    report_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_path = report_dir / f"ci_{stamp}.json"
    latest_path = report_dir / "latest.json"

    script = str(Path(__file__).resolve())
    steps: list[tuple[str, list[str]]] = [
        ("tooling_unit_tests", [sys.executable, "-m", "unittest",
                                "tests.test_bms_tools", "-v"]),
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
                    "evidence": "build/bms/gen/build.log",
                })
        if name == "static":
            issue_counts = [int(value) for value in
                            re.findall(r"total issues: (\d+)", output)]
            static_issues = sum(issue_counts)
            if static_issues:
                report["findings"].append({
                    "type": "cppcheck_findings",
                    "count": static_issues,
                    "evidence": "build/static/*.xml and *.txt",
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
