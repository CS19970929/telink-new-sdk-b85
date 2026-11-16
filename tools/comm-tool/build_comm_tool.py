"""在用户临时区编译Comm Tool统一Keil工程；默认仅链接AXF，不生成BIN。"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
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
    for item in list(targets):
        if item is not selected:
            targets.remove(item)
    # 所有源文件仍指向唯一源码；只有工程副本和构建产物写到临时区。
    for node in selected.findall(".//FilePath"):
        node.text = str((PROJECT.parent / node.text.replace("\\", "/")).resolve())
        if not Path(node.text).is_file():
            raise FileNotFoundError(node.text)
    for node in selected.findall(".//IncludePath"):
        if node.text:
            node.text = ";".join(str((PROJECT.parent / part.replace("\\", "/")).resolve())
                                 for part in node.text.split(";") if part)
    common = selected.find("TargetOption/TargetCommonOption")
    common.find("OutputDirectory").text = str(output / "objects") + "\\"
    common.find("CreateHexFile").text = "1" if images else "0"
    for node in selected.findall(".//ListingPath"):
        node.text = str(output / "listings") + "\\"
    for section in ("BeforeMake", "AfterMake", "BeforeCompile"):
        for node in common.findall(section + "/*"):
            if node.tag.startswith("RunUserProg"):
                node.text = "0"
    for child in ("objects", "listings"):
        (output / child).mkdir(parents=True, exist_ok=True)
    path = output / PROJECT.name
    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)
    return path

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
        artifacts = list((output / "objects").glob("*.axf"))
        if completed.returncode > 1 or "0 Error(s)" not in text or not artifacts:
            print(text)
            return 1
        result["artifacts"] = [str(p) for p in artifacts]
        result["log"] = str(log)
    print(json.dumps(result, ensure_ascii=False))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
