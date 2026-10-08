"""VS Code 一键流程：明确选择装配，验证本次镜像，再调用既有 Android Sender。"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "d008-16s-lfp": ("d008", "16s-lfp"),
    "d008-20s-nmc": ("d008", "20s-nmc"),
    "d008-24s-lfp": ("d008", "24s-lfp"),
    "d011": ("d011", None),
    "d013": ("d013", None),
    "d014": ("d014", None),
}


def default_sender() -> Path:
    configured = os.environ.get("BMS_ANDROID_SENDER")
    if configured:
        return Path(configured).expanduser().resolve()
    # 沿用本机已安装的 Sender；不复制上位机源码或更改其维护分支。
    return (Path.home() / "Documents/CodexOutputs/telink-new-sdk-b85/"
            "android-direct-sender-v3/BmsTool.Android.Sender.exe")


def run_workflow(target: str, sender: Path, *, build_only: bool = False,
                 root: Path = ROOT, mode: str = "production",
                 rebuild: bool = False) -> int:
    if mode not in ("production", "development"):
        raise ValueError("不支持的构建模式：" + mode)
    product, profile = TARGETS[target]
    variant = mode + ("-" + profile if profile else "")
    firmware = root / "firmware" / variant / product / "825x_ble_sample.bin"
    manifest = firmware.with_name("fw_manifest.json")
    if not build_only and not sender.is_file():
        raise ValueError("Android Sender 不存在；请安装原 Sender 或设置 BMS_ANDROID_SENDER：" + str(sender))

    print(f"[OTA] 已选择 {target}；模式 {mode}", flush=True)
    command = [sys.executable, str(root / "bms_tools/bms.py"),
               "--product", product]
    if mode == "production":
        command.append("--production")
    if profile:
        command += ["--d008-profile", profile]
    env = dict(os.environ, PYTHONUTF8="1", PYTHONDONTWRITEBYTECODE="1")
    # build 为增量构建，内含批准、warning、资源、Telink checker/CRC 门禁。
    # 每一步检查退出码；旧 BIN 即使仍在磁盘也不能走到发送步骤。
    for arguments in (["rebuild" if rebuild else "build", "--jobs", "4"], ["manifest"], ["verify"]):
        quiet = arguments == ["manifest"]
        result = subprocess.run(command + arguments, cwd=root, env=env,
                                capture_output=quiet, text=True,
                                encoding="utf-8", errors="replace")
        if result.returncode:
            if quiet:
                print(result.stdout or "", end="")
                print(result.stderr or "", end="", file=sys.stderr)
            print(f"[OTA] {arguments[0]} 失败，已停止；没有发送固件。", file=sys.stderr)
            return result.returncode

    # 除 bms.py 的完整性/源码溯源检查外，再核对发送路径与用户选择。
    data = json.loads(manifest.read_text(encoding="utf-8"))
    configuration = data.get("configuration", {})
    if (data.get("product") != product
            or configuration.get("product") != product
            or configuration.get("production") is not (mode == "production")
            or configuration.get("build_mode") != mode
            or configuration.get("d008_profile") != profile
            or Path(data.get("bin", "")).resolve() != firmware.resolve()):
        raise ValueError("manifest 的产品、装配或镜像路径不匹配；没有发送固件。")
    payload = firmware.read_bytes()
    if (len(payload) != data.get("size_bytes")
            or hashlib.sha256(payload).hexdigest() != data.get("sha256")):
        raise ValueError("发送前 BIN 大小或 SHA-256 不匹配；没有发送固件。")
    print(f"[OTA] 校验通过：{firmware}", flush=True)
    if build_only:
        print("[OTA] 仅验证构建；没有发送固件或启动 OTA。", flush=True)
        return 0

    print("[OTA] 无线发送并请求自动 OTA；沿用 App 已连接的 BMS。", flush=True)
    result = subprocess.run([str(sender), "--firmware", str(firmware), "--auto-ota"],
                            cwd=sender.parent, env=env)
    if result.returncode:
        print("[OTA] Android Sender 失败；请查看上方手机连接/传输错误。", file=sys.stderr)
    else:
        print("[OTA] Sender 已完成发送并请求 OTA；升级结果请查看安卓 App。", flush=True)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=TARGETS)
    parser.add_argument("--sender", type=Path, default=default_sender())
    parser.add_argument("--build-only", action="store_true", help="验证实际构建，不连接手机")
    parser.add_argument("--mode", choices=("production", "development"), default="production")
    parser.add_argument("--rebuild", action="store_true", help="清理后全量重编译")
    args = parser.parse_args()
    try:
        return run_workflow(args.target, args.sender.resolve(), build_only=args.build_only,
                            mode=args.mode, rebuild=args.rebuild)
    except (OSError, ValueError, KeyError) as exc:
        print(f"[OTA] ERROR: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("[OTA] 流程已取消。", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
