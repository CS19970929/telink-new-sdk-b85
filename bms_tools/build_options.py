"""快捷编译窗口的少量参数；共用校验、编译宏和镜像配置记录。"""
from __future__ import annotations

import json
import re
import secrets

ENV_NAME = "BMS_BUILD_OPTIONS"
MACROS = {
    "afe_model": "BMS_BUILD_AFE_MODEL",
    "cell_count": "BMS_BUILD_CELL_COUNT",
    "chemistry": "BMS_PRODUCT_CHEMISTRY",
    "capacity_0p1ah": "BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH",
}
UPDATE_GROUPS = {"sw": 1, "afe": 2, "business": 4, "soc": 8, "soc_state": 64}
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
        raise ValueError("OTA 更新组必须为 sw、afe、business、soc、soc_state 的列表。")
    if len(groups) != len(set(groups)):
        raise ValueError("OTA 更新组不能重复。")
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


def from_environment(environment: dict, product: str, profile: str | None) -> dict:
    return normalize(json.loads(environment.get(ENV_NAME, "{}")), product, profile)


def defines(options: dict) -> list[str]:
    flags = []
    for key, value in options.items():
        if key not in MACROS:
            continue
        if key == "afe_model":
            value = {"dvc1124": 1124, "sh3673510": 3510, "sh3673520": 3520}[value]
        value = f"BMS_SOC_CHEMISTRY_{value.upper()}" if key == "chemistry" else f"{value}u"
        flags.append(f"-D{MACROS[key]}={value}")
    mask = sum(UPDATE_GROUPS[group] for group in options.get("update_groups", []))
    flags.append(f"-DBMS_OTA_UPDATE_MASK=0x{mask:04x}u")
    if mask:
        for index in range(6):
            word = options["update_id"][index * 4:index * 4 + 4]
            flags.append(f"-DBMS_OTA_UPDATE_ID_{index}=0x{word}u")
    return flags


def check_extra_defines(extra: str) -> None:
    # 窗口负责这些参数，拒绝残留环境宏偷偷覆盖窗口显示值。
    names = "|".join((*MACROS.values(), "BMS_UPDATE_SW_REVISION", "BMS_UPDATE_AFE_REVISION",
                      "BMS_UPDATE_BUSINESS_REVISION", "BMS_UPDATE_SOC_REVISION",
                      "BMS_UPDATE_SOC_STATE_REVISION", "BMS_UPDATE_CALIBRATION_REVISION",
                      "BMS_UPDATE_IDENTITY_REVISION", "BMS_UPDATE_EVENTS_REVISION",
                      "BMS_BUILD_PARAMETERS_REVISION", "BMS_BUILD_SOC_STATE_REVISION",
                      "BMS_OTA_UPDATE_MASK", r"BMS_OTA_UPDATE_ID_\d+"))
    if re.search(r"(?:-D|-U)\s*(?:" + names + r")\b", extra):
        raise ValueError("EXTRA_DEFINES 残留电池/OTA 策略或旧编号宏，请清除后使用窗口配置。")
