"""在用户临时区编译Comm Tool统一Keil工程；默认仅链接AXF，不生成BIN。"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "firmware/comm_tool_f103ret6/keil/COMM_TOOL_F103RET6.uvprojx"

def prepare(target: str, output: Path, images: bool = False) -> Path:
    output = output.resolve()
    if output == ROOT or ROOT in output.parents:
        raise ValueError("编译输出必须位于源码树外")
    tree = ET.parse(PROJECT)
    targets = tree.find("Targets")
    selected = next((t for t in targets if t.findtext("TargetName") == target), None)
    if selected is None:
        raise ValueError("未知target: " + target)
    # Preserve the complete multi-target project: removing the other target hid
    # uVision's group/file merging and duplicate RESET sections during IDE use.
    for item in targets:
        target_output = output if item is selected else output / item.findtext("TargetName")
        # Point both targets to the same canonical source files; only the project
        # copy, logs and build products live in the user temporary directory.
        for node in item.findall(".//FilePath"):
            node.text = str((PROJECT.parent / node.text.replace("\\", "/")).resolve())
            if not Path(node.text).is_file():
                raise FileNotFoundError(node.text)
        for node in item.findall(".//IncludePath"):
            if node.text:
                node.text = ";".join(str((PROJECT.parent / part.replace("\\", "/")).resolve())
                                     for part in node.text.split(";") if part)
        common = item.find("TargetOption/TargetCommonOption")
        common.find("OutputDirectory").text = str(target_output / "objects") + "\\"
        common.find("CreateHexFile").text = "1" if images else "0"
        for node in item.findall(".//ListingPath"):
            node.text = str(target_output / "listings") + "\\"
        for section in ("BeforeMake", "AfterMake", "BeforeCompile"):
            for node in common.findall(section + "/*"):
                if node.tag.startswith("RunUserProg"):
                    node.text = "0"
        for child in ("objects", "listings"):
            (target_output / child).mkdir(parents=True, exist_ok=True)
    path = output / PROJECT.name
    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)
    return path

def verify_layout(path: Path, target: str) -> dict:
    data = path.read_bytes()
    if data[:6] != bytes.fromhex("7f454c460101"):
        raise ValueError("不支持的AXF格式")
    base, limit = (0x08008000, 0x08018000) if target == "COMM_TOOL_Release" else (0x08000000, 0x08008000)
    entry = struct.unpack_from("<I", data, 24)[0]
    if not base <= (entry & ~1) < limit:
        raise ValueError("AXF入口越界")
    start = struct.unpack_from("<I", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 46)
    flash_end, ram_end = base, 0x20000000
    for i in range(count):
        _, _, flags, addr, _, length, _, _, _, _ = struct.unpack_from("<10I", data, start + i * size)
        if not flags & 2 or not length:
            continue
        end = addr + length
        if 0x08000000 <= addr < 0x08100000:
            if addr < base or end > limit:
                raise ValueError("AXF Flash段越界")
            flash_end = max(flash_end, end)
        elif 0x20000000 <= addr < 0x20010000:
            if end > 0x2000FFE0:
                raise ValueError("AXF占用SRAM mailbox")
            ram_end = max(ram_end, end)
    program_offset = struct.unpack_from("<I", data, 28)[0]
    program_size, program_count = struct.unpack_from("<HH", data, 42)
    for i in range(program_count):
        kind, _, _, physical, file_bytes, _, _, _ = struct.unpack_from("<8I", data, program_offset + i * program_size)
        if kind == 1 and file_bytes and 0x08000000 <= physical < 0x08100000:
            if physical < base or physical + file_bytes > limit:
                raise ValueError("AXF初始化数据装载区越界")
            flash_end = max(flash_end, physical + file_bytes)
    return {"entry": hex(entry), "flash_end": hex(flash_end), "ram_end": hex(ram_end)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", choices=["COMM_TOOL_Release", "COMM_TOOL_IAP"], required=True)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--uv4", type=Path)
    ap.add_argument("--prepare-only", action="store_true")
    ap.add_argument("--images", action="store_true", help="显式生成HEX，仍不生成BIN")
    args = ap.parse_args()
    parent = Path(os.environ["LOCALAPPDATA"]) / "CodexTemp/comm-tool/keil"
    parent.mkdir(parents=True, exist_ok=True)
    output = args.output or Path(tempfile.mkdtemp(prefix=args.target + "-", dir=parent))
    project = prepare(args.target, output, args.images)
    result = {"target": args.target, "project": str(project), "output": str(output.resolve())}
    if not args.prepare_only:
        uv4 = args.uv4 or shutil.which("UV4.exe") or Path("C:/Keil_v5/UV4/UV4.exe")
        if not Path(uv4).is_file():
            raise FileNotFoundError("找不到UV4.exe，请用--uv4指定")
        log = output / "build.log"
        completed = subprocess.run([str(uv4), "-b", str(project), "-t", args.target,
                                    "-o", str(log)], timeout=180)
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        # Keil日志可能是GBK；不以进程返回码代替产物检查。
        artifact = output / "objects" / (args.target + ".axf")
        artifacts = [artifact] if artifact.is_file() else []
        if completed.returncode > 1 or "0 Error(s)" not in text or not artifacts:
            print(text)
            return 1
        result["artifacts"] = [str(p) for p in artifacts]
        result["log"] = str(log)
        result["layout"] = verify_layout(artifacts[0], args.target)
    print(json.dumps(result, ensure_ascii=False))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
