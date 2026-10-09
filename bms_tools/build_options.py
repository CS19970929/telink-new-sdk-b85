"""快捷编译窗口的少量参数；共用校验、编译宏和镜像配置记录。"""
from __future__ import annotations

import json
import re

ENV_NAME = "BMS_BUILD_OPTIONS"
MACROS = {
    "chemistry": "BMS_PRODUCT_CHEMISTRY",
    "capacity_0p1ah": "BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH",
    "parameters_revision": "BMS_BUILD_PARAMETERS_REVISION",
    "soc_state_revision": "BMS_BUILD_SOC_STATE_REVISION",
}


def normalize(options: dict, product: str, profile: str | None) -> dict:
    if not isinstance(options, dict) or set(options) - MACROS.keys():
        raise ValueError("快捷编译配置包含不支持的字段。")
    result = {}
    for key, value in options.items():
        if key == "chemistry":
            if value not in ("lfp", "nmc"):
                raise ValueError("电池类型必须为 lfp 或 nmc。")
            if product == "d008":
                expected = "nmc" if profile == "20s-nmc" else "lfp"
                if value != expected:
                    raise ValueError("D008 电池类型必须与所选装配一致。")
        else:
            maximum = 6553 if key == "capacity_0p1ah" else 65535
            if type(value) is not int or not 1 <= value <= maximum:
                raise ValueError(f"{key} 必须为 1..{maximum} 的整数。")
        result[key] = value
    return dict(sorted(result.items()))


def from_environment(environment: dict, product: str, profile: str | None) -> dict:
    return normalize(json.loads(environment.get(ENV_NAME, "{}")), product, profile)


def defines(options: dict) -> list[str]:
    return [f"-D{MACROS[key]}=" +
            (f"BMS_SOC_CHEMISTRY_{value.upper()}" if key == "chemistry" else f"{value}u")
            for key, value in options.items()]


def check_extra_defines(extra: str) -> None:
    # 窗口负责这些参数，拒绝残留环境宏偷偷覆盖窗口显示值。
    names = "|".join((*MACROS.values(), "BMS_UPDATE_SW_REVISION", "BMS_UPDATE_AFE_REVISION",
                      "BMS_UPDATE_BUSINESS_REVISION", "BMS_UPDATE_SOC_REVISION",
                      "BMS_UPDATE_SOC_STATE_REVISION"))
    if re.search(r"(?:-D|-U)\s*(?:" + names + r")\b", extra):
        raise ValueError("EXTRA_DEFINES 残留电池/更新编号宏，请清除后使用窗口配置。")
