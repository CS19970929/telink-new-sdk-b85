"""快捷编译窗口的少量参数；共用校验、编译宏和镜像配置记录。"""
from __future__ import annotations

import json
import re
import secrets
import hashlib
from pathlib import Path
import subprocess

ENV_NAME = "BMS_BUILD_OPTIONS"
MACROS = {
    "afe_model": "BMS_BUILD_AFE_MODEL",
    "cell_count": "BMS_BUILD_CELL_COUNT",
    "chemistry": "BMS_PRODUCT_CHEMISTRY",
    "capacity_0p1ah": "BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH",
}
UPDATE_GROUPS = {"sw": 1, "afe": 2, "business": 4, "soc": 8, "soc_state": 64, "factory_reset": 512}
OPTION_KEYS = (*MACROS, "update_groups", "update_id")


def new_update_id() -> str:
    # 首 word=0 区分旧合法编号；80 位随机标识由工具生成，无需查询客户设备。
    return f"0000{(secrets.randbits(80) or 1):020x}"


def normalize(options: dict, product: str, profile: str | None) -> dict:
    if not isinstance(options, dict) or set(options) - set(OPTION_KEYS):
        raise ValueError("快捷编译配置包含不支持的字段。")
    result = {}
    model = options.get("afe_model", "dvc1124" if product == "d008" else "sh3673510")
    allowed = ("dvc1124",) if product == "d008" else ("sh3673510", "sh3673520")
    if model not in allowed:
        raise ValueError("AFE 型号与板型后端不一致。")
    for key, value in options.items():
        if key in ("update_groups", "update_id"):
            continue
        if key == "afe_model":
            result[key] = value
            continue
        if key == "chemistry":
            if value not in ("lfp", "nmc"):
                raise ValueError("电池类型必须为 lfp 或 nmc。")
            if product == "d008" and profile != "custom":
                expected = "nmc" if profile == "20s-nmc" else "lfp"
                if value != expected:
                    raise ValueError("D008 电池类型必须与所选装配一致。")
        else:
            if key == "cell_count":
                maximum = 24 if model == "dvc1124" else (20 if model == "sh3673520" else 10)
            else:
                maximum = 6553
            if type(value) is not int or not 1 <= value <= maximum:
                raise ValueError(f"{key} 必须为 1..{maximum} 的整数。")
        result[key] = value
    if product == "d008" and profile == "custom" and not {"cell_count", "chemistry"} <= result.keys():
        raise ValueError("D008 自定义配置须同时选择串数和电池类型。")
    if "cell_count" in result:
        if result["cell_count"] < 4 or "chemistry" not in result:
            raise ValueError("串数至少为 4，且须同时选择电池类型。")
        if product == "d008":
            if result["chemistry"] == "nmc" and result["cell_count"] * 4200 > 100000:
                raise ValueError("DVC1124-2 电池包限于 100V，4.20V 三元锂最多 23 串。")
            if profile != "custom" and result["cell_count"] != int((profile or "16s-lfp").split("s-")[0]):
                raise ValueError("串数与 D008 装配不一致；任意配置须选择 custom。")
    groups = options.get("update_groups", [])
    if not isinstance(groups, list) or any(type(group) is not str or group not in UPDATE_GROUPS for group in groups):
        raise ValueError("OTA 更新组必须为 sw、afe、business、soc、soc_state 或 factory_reset。")
    if len(groups) != len(set(groups)):
        raise ValueError("OTA 更新组不能重复。")
    if "factory_reset" in groups and len(groups) != 1:
        raise ValueError("恢复出厂必须单独选择，不能混合普通 OTA 更新组。")
    update_id = options.get("update_id")
    if groups:
        if not isinstance(update_id, str) or not re.fullmatch(r"0000[0-9a-f]{20}", update_id) or int(update_id, 16) == 0:
            raise ValueError("选择更新组时须提供工具生成的一次性更新标识。")
        result["update_groups"] = [group for group in UPDATE_GROUPS if group in groups]
        result["update_id"] = update_id
    elif update_id is not None:
        raise ValueError("保留参数时不能携带更新标识。")
    return dict(sorted(result.items()))


def variant(mode: str, product: str, profile: str | None, options: dict) -> str:
    if "cell_count" in options:
        model = "-" + options["afe_model"] if "afe_model" in options else ""
        return f"{mode}{model}-{options['cell_count']}s-{options['chemistry']}"
    return mode + ("-" + (profile or "16s-lfp") if product == "d008" else "")


def check_extra_defines(extra: str) -> None:
    # 窗口负责这些参数，拒绝残留环境宏偷偷覆盖窗口显示值。
    names = "|".join((*MACROS.values(), "BMS_UPDATE_SW_REVISION", "BMS_UPDATE_AFE_REVISION",
                      "BMS_UPDATE_BUSINESS_REVISION", "BMS_UPDATE_SOC_REVISION",
                      "BMS_UPDATE_SOC_STATE_REVISION", "BMS_UPDATE_CALIBRATION_REVISION",
                      "BMS_UPDATE_IDENTITY_REVISION", "BMS_UPDATE_EVENTS_REVISION",
                      "BMS_BUILD_PARAMETERS_REVISION", "BMS_BUILD_SOC_STATE_REVISION",
                      r"BMS_OTA_UPDATE_\w+", "D008_CUSTOM_CELL_COUNT", "D008_CUSTOM_CHEMISTRY"))
    if re.search(r"(?:-D|-U)\s*(?:" + names + r")\b", extra):
        raise ValueError("EXTRA_DEFINES 残留电池/OTA 策略或旧编号宏，请清除后使用窗口配置。")


PRODUCTS = ("d008", "d011", "d013", "d014")
PROFILES = {"16s-lfp": (16, "lfp"), "20s-nmc": (20, "nmc"),
            "24s-lfp": (24, "lfp"), "23s-lfp": (23, "lfp")}
OTA_BEGIN = "/* OTA_SOURCE_POLICY_BEGIN"
OTA_END = "/* OTA_SOURCE_POLICY_END */"
# 保守包含默认值构造代码；这些输入改变时，已有更新包必须重新保存并固定标识。
DEFAULT_INPUTS = ("bms/products/bms_battery_defaults.h", "bms/core/bms_parameters.c",
                  "bms/core/bms_config_store.c", "bms/core/bms_afe_hw_profile.c",
                  "bms/core/bms_soc_defs.h", "bms/core/bms_soc_profile.h",
                  "bms/core/bms_soc.h", "bms/core/bms_state_store.c", "bms/core/bms_state.h",
                  "bms/app/bms_features.h", "bms/afe/sh3673510/sh3673510_quantize.h",
                  "bms/core/bms_update_policy.h", "bms/core/bms_afe_register_policy.h")
# SH3673510 三产品共用默认值；产品文件只保留容量与 OTA 标识，摘要必须覆盖共用输入。
SHARED_PARAMETERS = "bms/products/sh3673510_protection_config.h"


def _text(path: Path) -> str:
    return path.read_bytes().decode("utf-8-sig")


def _macro(text: str, name: str, pattern: str = r"\S+") -> str:
    found = re.findall(r"(?m)^[ \t]*#define[ \t]+" + re.escape(name) +
                       r"[ \t]+(" + pattern + r")[ \t]*(?=\r?$|/\*)", text)
    if len(found) != 1:
        raise ValueError("源码配置必须有且仅有一个可编辑定义：" + name)
    return found[0]


def _number(text: str, name: str) -> int:
    return int(_macro(text, name, r"(?:0x[0-9a-fA-F]+|\d+)[uU]?").rstrip("uU"), 0)


def source_profile(root: Path) -> str:
    text = _text(root / "bms/products/d008/d008_product_profile.h")
    name = _macro(text, "D008_PRODUCT_PROFILE", r"D008_PRODUCT_PROFILE_[A-Z0-9_]+")
    return name.removeprefix("D008_PRODUCT_PROFILE_").lower().replace("_", "-")


def read_source(root: Path, product: str, profile: str | None = None, *, for_edit=False) -> dict:
    if product not in PRODUCTS:
        raise ValueError("未知产品：" + product)
    folder = root / "bms/products" / product
    header = _text(folder / "bms_product.h")
    parameters = _text(folder / f"{product}_protection_config.h")
    if product == "d008":
        profile = profile or source_profile(root)
        if profile == "custom":
            assembly = _text(folder / "d008_product_profile.h")
            cells = _number(assembly, "D008_CUSTOM_CELL_COUNT")
            chemistry = _macro(assembly, "D008_CUSTOM_CHEMISTRY", r"BMS_SOC_CHEMISTRY_(?:LFP|NMC)")
            chemistry = chemistry.rsplit("_", 1)[1].lower()
        else:
            if profile not in PROFILES:
                raise ValueError("未知 D008 装配：" + profile)
            cells, chemistry = PROFILES[profile]
        model = "dvc1124"
    else:
        cells = _number(header, "SH3673510_BOARD_CELL_COUNT")
        chemistry = _macro(header, "BMS_PRODUCT_CHEMISTRY", r"BMS_SOC_CHEMISTRY_(?:LFP|NMC)")
        chemistry = chemistry.rsplit("_", 1)[1].lower()
        model = "sh367" + str(_number(header, "BMS_BUILD_AFE_MODEL"))
    groups = []
    for group in UPDATE_GROUPS:
        enabled = _number(parameters, "BMS_OTA_UPDATE_" + group.upper())
        if enabled not in (0, 1):
            raise ValueError("OTA 更新开关必须为 0 或 1：" + group)
        if enabled:
            groups.append(group)
    options = dict(afe_model=model, cell_count=cells, chemistry=chemistry,
                   capacity_0p1ah=_number(parameters, "BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH"),
                   update_groups=groups)
    words = [_number(parameters, f"BMS_OTA_UPDATE_ID_{index}") for index in range(6)]
    if any(word > 65535 for word in words) or words[0]:
        raise ValueError("源码中的 OTA 标识格式无效。")
    incomplete = bool(groups and not any(words) and for_edit)
    if groups:
        options["update_id"] = "".join(f"{word:04x}" for word in words)
        if incomplete:
            options["update_id"] = "000000000000000000000001"
    elif any(words) and not for_edit:
        raise ValueError("保留参数时 OTA 标识必须全零。")
    result = normalize(options, product, profile)
    if incomplete:
        result.pop("update_id")
    return result


def parameter_inputs_hash(root: Path, product: str, options: dict, replacements=None) -> str:
    """标识绑定受控默认值输入；排除标识本身，避免循环摘要。"""
    replacements = replacements or {}
    paths = list(DEFAULT_INPUTS) + [f"bms/products/{product}/bms_product.h",
                                  f"bms/products/{product}/{product}_protection_config.h",
                                  f"bms/products/{product}/afe_register_config.h"]
    if product != "d008":
        paths.append(SHARED_PARAMETERS)
    paths.append("bms/products/d008/d008_product_profile.h" if product == "d008"
                 else "bms/products/sh3673510_defaults.h")
    if product == "d008":
        paths.append("bms/products/d008/dvc1124_product_defaults.h")
    inputs = {}
    for relative in paths:
        text = replacements.get(relative)
        if text is None:
            text = _text(root / relative)
        if OTA_BEGIN in text:
            text = text[:text.index(OTA_BEGIN)] + text[text.index(OTA_END) + len(OTA_END):]
        inputs[relative] = hashlib.sha256(text.replace("\r\n", "\n").encode("utf-8")).hexdigest()
    selected = {key: value for key, value in options.items() if key != "update_id"}
    return hashlib.sha256(json.dumps({"inputs": inputs, "configuration": selected},
                                    sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def verify_source(root: Path, product: str, profile: str | None = None) -> dict:
    # 固定标识绑定源码装配；工程 ELF 的 profile 覆盖不能改变该绑定。
    options = read_source(root, product)
    parameters = _text(root / f"bms/products/{product}/{product}_protection_config.h")
    saved = _macro(parameters, "BMS_OTA_UPDATE_CONTENT_SHA256", r'"[0-9a-f]{64}"').strip('"')
    if options.get("update_groups") and saved != parameter_inputs_hash(root, product, options):
        raise ValueError("参数更新内容与固定标识不匹配；请保存新参数更新包，不能直接重编。")
    if options.get("update_groups"):
        _verify_identifier_history(root, product, options["update_id"], saved)
    return read_source(root, product, profile) if profile is not None else options


def _verify_identifier_history(root: Path, product: str, identifier: str, digest: str) -> None:
    relative = f"bms/products/{product}/{product}_protection_config.h"
    result = subprocess.run(["git", "log", "--format=%H", "-G", "BMS_OTA_UPDATE_ID_[1-5]", "--", relative],
                            cwd=root, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError("无法核对 Git 中的更新标识历史，请先初始化并提交源码。")
    for commit in result.stdout.splitlines():
        result = subprocess.run(["git", "show", commit + ":" + relative], cwd=root,
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
        if result.returncode:
            continue
        try:
            previous = "".join(f"{_number(result.stdout, f'BMS_OTA_UPDATE_ID_{i}'):04x}" for i in range(6))
            previous_digest = _macro(result.stdout, "BMS_OTA_UPDATE_CONTENT_SHA256", r'"[0-9a-f]{64}"').strip('"')
        except ValueError:
            continue
        if previous == identifier and previous_digest != digest:
            raise ValueError("Git 历史中该更新标识已对应其他内容；请制作新参数更新包。")


def require_clean_source(root: Path) -> None:
    result = subprocess.run(["git", "status", "--porcelain", "--untracked-files=normal"],
                            cwd=root, capture_output=True, text=True, timeout=10)
    if result.returncode or result.stdout.strip():
        raise ValueError("当前操作要求干净 Git 工作树，但源码存在未提交修改或未跟踪文件。")
    result = subprocess.run(["git", "rev-parse", "--verify", "HEAD"], cwd=root,
                            capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise ValueError("缺少有效 Git 提交，不能构建交付镜像。")


def _replace(text: str, name: str, value: str, pattern: str = r"\S+") -> str:
    expression = r"(?m)(^[ \t]*#define[ \t]+" + re.escape(name) + r"[ \t]+)" + pattern
    changed, count = re.subn(expression, lambda found: found[1] + value, text)
    if count != 1:
        raise ValueError("无法唯一修改源码配置：" + name)
    return changed


def save_source(root: Path, product: str, changes: dict, *, new_package=False) -> dict:
    """只编辑明确的产品宏；失败回滚本次写入，绝不暂存或提交其他文件。"""
    old = read_source(root, product, for_edit=True)
    requested = dict(old)
    requested.update(changes)
    groups = requested.get("update_groups", [])
    if groups:
        requested["update_id"] = old.get("update_id") or "000000000000000000000001"
    else:
        requested.pop("update_id", None)
    options = normalize(requested, product, "custom" if product == "d008" else None)
    relative = f"bms/products/{product}/{product}_protection_config.h"
    parameters = _text(root / relative)
    edited = {relative: _replace(parameters, "BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH",
                                str(options["capacity_0p1ah"]) + "u", r"\d+[uU]?")}
    if product == "d008":
        assembly_path = "bms/products/d008/d008_product_profile.h"
        assembly = _text(root / assembly_path)
        profile = next((name for name, pair in PROFILES.items()
                        if pair == (options["cell_count"], options["chemistry"])), "custom")
        assembly = _replace(assembly, "D008_PRODUCT_PROFILE",
                            "D008_PRODUCT_PROFILE_" + profile.upper().replace("-", "_"),
                            r"D008_PRODUCT_PROFILE_[A-Z0-9_]+")
        assembly = _replace(assembly, "D008_CUSTOM_CELL_COUNT", str(options["cell_count"]) + "u")
        assembly = _replace(assembly, "D008_CUSTOM_CHEMISTRY",
                            "BMS_SOC_CHEMISTRY_" + options["chemistry"].upper())
        edited[assembly_path] = assembly
    else:
        board_path = f"bms/products/{product}/bms_product.h"
        board = _text(root / board_path)
        board = _replace(board, "SH3673510_BOARD_CELL_COUNT", str(options["cell_count"]) + "u", r"\d+[uU]?")
        board = _replace(board, "BMS_PRODUCT_CHEMISTRY", "BMS_SOC_CHEMISTRY_" + options["chemistry"].upper())
        board = _replace(board, "BMS_BUILD_AFE_MODEL", options["afe_model"].removeprefix("sh367") + "u")
        edited[board_path] = board
    digest = parameter_inputs_hash(root, product, options, edited) if groups else "0" * 64
    old_digest = _macro(parameters, "BMS_OTA_UPDATE_CONTENT_SHA256", r'"[0-9a-f]{64}"').strip('"')
    if groups and (new_package or digest != old_digest or not old.get("update_id")):
        options["update_id"] = new_update_id()
    identifier = options.get("update_id", "0" * 24)
    text = edited[relative]
    for group in UPDATE_GROUPS:
        text = _replace(text, "BMS_OTA_UPDATE_" + group.upper(), "1u" if group in groups else "0u")
    for index in range(6):
        text = _replace(text, f"BMS_OTA_UPDATE_ID_{index}", f"0x{identifier[index*4:index*4+4]}u")
    edited[relative] = _replace(text, "BMS_OTA_UPDATE_CONTENT_SHA256", '"' + digest + '"', r'"[0-9a-f]{64}"')
    originals = {path: (root / path).read_bytes() for path in edited}
    candidates = {path: (b"\xef\xbb\xbf" if originals[path].startswith(b"\xef\xbb\xbf") else b"") +
                  text.encode("utf-8") for path, text in edited.items()}
    changed = [path for path in edited if candidates[path] != originals[path]]
    written = []
    try:
        for path in changed:
            if (root / path).read_bytes() != originals[path]:
                raise ValueError("保存期间源码被其他程序修改：" + path)
            (root / path).write_bytes(candidates[path])
            written.append(path)
        verify_source(root, product)
    except Exception:
        for path in reversed(written):
            if (root / path).read_bytes() == candidates[path]:
                (root / path).write_bytes(originals[path])
        raise
    return {"product": product, "changed_files": changed, "configuration": read_source(root, product),
            "parameter_inputs_sha256": digest}


def main() -> int:
    import argparse
    import sys
    parser = argparse.ArgumentParser(description="编辑并核对 Git 受控的产品配置；不构建、不发送 OTA。")
    parser.add_argument("action", choices=("show", "save", "verify"))
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--product", choices=PRODUCTS, required=True)
    parser.add_argument("--afe-model", choices=("dvc1124", "sh3673510", "sh3673520"))
    parser.add_argument("--cell-count", type=int)
    parser.add_argument("--chemistry", choices=("lfp", "nmc"))
    parser.add_argument("--capacity-0p1ah", type=int)
    parser.add_argument("--update-groups", nargs="*", choices=tuple(UPDATE_GROUPS))
    parser.add_argument("--new-package", action="store_true")
    parser.add_argument("--require-clean", action="store_true")
    args = parser.parse_args()
    try:
        if args.action == "save":
            changes = {key: getattr(args, key) for key in (*MACROS, "update_groups")
                       if getattr(args, key) is not None}
            result = save_source(args.root, args.product, changes, new_package=args.new_package)
        else:
            result = (verify_source(args.root, args.product) if args.action == "verify"
                      else read_source(args.root, args.product, for_edit=True))
        if args.require_clean:
            require_clean_source(args.root)
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
