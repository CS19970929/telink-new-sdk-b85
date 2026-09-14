#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        if new in text:
            return text
        raise RuntimeError(f"{label}: source text not found")
    if text.count(old) != 1:
        raise RuntimeError(f"{label}: expected exactly one source block")
    return text.replace(old, new, 1)


def patch_driver() -> None:
    path = VENDOR / "sh3673520.c"
    text = read(path)

    # The old name hid what the helper actually does. It is a single full-duplex
    # byte exchange, not a complete SPI transaction.
    text = text.replace("sh3673520_xfer", "sh36735xx_xfer_byte")

    if "SH3673520_SetCellCount" not in text:
        anchor = "\nsh3673520_status_t SH3673520_Reset(void)\n"
        if anchor not in text:
            raise RuntimeError("generic helper insertion anchor not found")
        addition = r'''
sh3673520_status_t SH3673520_SetCellCount(uint8_t cell_count)
{
    uint8_t current;
    uint8_t target;
    uint8_t verify;
    sh3673520_status_t status;

    if ((cell_count < SH3673520_MIN_CELLS) ||
        (cell_count > SH3673520_MAX_CELLS)) {
        return SH3673520_ERR_RANGE;
    }
    if (s_ready == 0u) {
        return SH3673520_ERR_NOT_READY;
    }

    status = SH3673520_ReadReg(SH3673520_REG_SCONF4, &current);
    if (status != SH3673520_OK) return status;

    target = (uint8_t)((current & (uint8_t)~SH3673520_SCONF4_CELL_COUNT_MASK) |
                       (cell_count & SH3673520_SCONF4_CELL_COUNT_MASK));
    if (target != current) {
        status = SH3673520_WriteReg(SH3673520_REG_SCONF4, target);
        if (status != SH3673520_OK) return status;
    }

    status = SH3673520_ReadReg(SH3673520_REG_SCONF4, &verify);
    if (status != SH3673520_OK) return status;
    if ((verify & SH3673520_SCONF4_CELL_COUNT_MASK) != cell_count) {
        return SH3673520_ERR_VERIFY;
    }
    return SH3673520_OK;
}

sh3673520_status_t SH3673520_SetBalanceMask(uint32_t cell_mask,
                                            uint8_t cell_count)
{
    uint32_t valid_mask;
    uint8_t values[3];

    if ((cell_count < SH3673520_MIN_CELLS) ||
        (cell_count > SH3673520_MAX_CELLS)) {
        return SH3673520_ERR_RANGE;
    }
    if (s_ready == 0u) {
        return SH3673520_ERR_NOT_READY;
    }

    valid_mask = (1UL << cell_count) - 1UL;
    if ((cell_mask & ~valid_mask) != 0u) {
        return SH3673520_ERR_RANGE;
    }

    /* Datasheet mapping: H=CB20..17, M=CB16..9, L=CB8..1. */
    values[0] = (uint8_t)((cell_mask >> 16u) & 0x0Fu);
    values[1] = (uint8_t)((cell_mask >> 8u) & 0xFFu);
    values[2] = (uint8_t)(cell_mask & 0xFFu);
    return SH3673520_WriteRegs(SH3673520_REG_BALANCEH, values, 3u);
}

'''
        text = text.replace(anchor, "\n" + addition + "sh3673520_status_t SH3673520_Reset(void)\n", 1)

    write(path, text)


def patch_control() -> None:
    path = VENDOR / "sh3673510_control.c"
    text = read(path)

    old = """    ok &= sh3510_update_reg(SH3673520_REG_SCONF4,
                            SH3673520_SCONF4_CELL_COUNT_MASK,
                            SH3673510_D011_CELL_COUNT);
"""
    new = """    ok &= (SH3673520_SetCellCount(SH3673510_D011_CELL_COUNT) == SH3673520_OK);
"""
    text = replace_once(text, old, new, "use common cell-count helper")

    old = """    port_status = SH3673520_PortConfigure(SH3673510_D011_SPI_GROUP,
                                          SH3673510_D011_SPI_TARGET_HZ);
"""
    new = """    port_status = SH3673520_PortConfigure(SH3673510_D011_SPI_GROUP);
"""
    text = replace_once(text, old, new, "simplify SPI port configure call")

    old = """uint8_t sh3673510_control_set_balance(uint16_t cell_mask)
{
    uint8_t values[3];
    uint16_t valid = (uint16_t)(cell_mask & 0x03FFu);
    if (!s_control_ready) return 0u;

    /* 10S uses CB1..CB10 only. */
    values[0] = 0u;
    values[1] = (uint8_t)((valid >> 8) & 0x03u); /* CB10..CB9 */
    values[2] = (uint8_t)(valid & 0xFFu);        /* CB8..CB1 */
    if (SH3673520_WriteRegs(SH3673520_REG_BALANCEH, values, 3u) != SH3673520_OK)
        return 0u;
    return 1u;
}
"""
    new = """uint8_t sh3673510_control_set_balance(uint16_t cell_mask)
{
    if (!s_control_ready) return 0u;
    return (SH3673520_SetBalanceMask((uint32_t)(cell_mask & 0x03FFu),
                                     SH3673510_D011_CELL_COUNT) == SH3673520_OK) ? 1u : 0u;
}
"""
    text = replace_once(text, old, new, "use common 20S balance helper")
    write(path, text)


def patch_docs() -> None:
    path = ROOT / "docs" / "D011_HARDWARE_REFERENCE.md"
    text = read(path)
    text = text.replace(
        "仓库沿用 `sh3673520.*` 文件承载共用代码是软件现状，不是两种 AFE 完全兼容的证明。",
        "SH36735XX CV1.0A 明确说明 SH3673510/3514/3517/3520 仅支持串数不同，其余功能相同；仓库继续由 `sh3673520.*` 承载该系列共用寄存器与 SPI 协议驱动。",
    )
    text = text.replace(
        "| 温度 | RN5/RN6 标为 10K-3435；RN3/RN4 标为 10M | 不能把所有通道当同一种 NTC |",
        "| 温度 | 实装 NTC 均为 10K；原理图 RN3/RN4 的 10M 为图纸标注错误（用户已确认） | 软件按 10K NTC 曲线处理 |",
    )
    text = text.replace(
        "CODE：B85 SPI 组 `SH3673520_SPI_GROUP_B6_B7_D2_D7`，目标 375000 Hz，SPI_MODE3，GPIO 控制 CS。\n**375 kHz、Mode 3、最大频率、CRC、ACK、RESET/ALARM 电气类型、复位/唤醒时序均不是原理图能证明的事实。**",
        "CODE：B85 SPI 组 `SH3673520_SPI_GROUP_B6_B7_D2_D7`，固定 500000 Hz，SPI_MODE3，硬件 SPI + 帧级 GPIO/CS 控制。\nSH36735XX CV1.0A 要求 SCK 高/低电平时间均至少 500 ns，因此总线最高 1 MHz；500 kHz 留有 2 倍周期裕量。Mode 3、CRC、ACK 与帧格式由系列手册/参考代码约束。",
    )
    old = """| 27 / TS3 | TS3-NC | RN3=10M，C39=1n-50V；备注 RN3 放在加热 MOS 旁边 | 视作未用，不参加保护 | 名称 NC 与放置备注并存，需确认 BOM 和是否需要加热 MOS 温度保护 |
| 28 / TS4 | TS4-MOS | RN4=10M，C38=1n-50V；备注 RN4 放在充放电 MOS 旁边 | 按 10k NTC 曲线解释 MOS 温度 | **10M 标注与 10k NTC 软件假设不一致，需确认原图/BOM修订** |

不可把 RN3/RN4 的 10M 自动改写成 10K，也不可因 TS3-NC 命名忽略图纸的加热 MOS 放置备注。
当前软件用电阻有效区间 500..300000 Ohm；若 RN4 实装 10M，温度将被判无效。此项影响温度保护与加热许可，是上板前的关键核对点。"""
    new = """| 27 / TS3 | TS3-NC / 加热 MOS 邻近 | **实际 10K NTC**；原理图 RN3=10M 为标注错误 | 当前仍不参与保护/加热控制 | 后续可作为加热 MOS 独立过温保护输入 |
| 28 / TS4 | TS4-MOS | **实际 10K NTC**；原理图 RN4=10M 为标注错误 | 10K NTC 曲线，作为充放电 MOS 温度 | 已由用户确认实际器件 |

D011 温度换算统一按 10K NTC。TS3 当前“未参与控制”是软件策略，不再是阻值不确定导致的禁用；若后续启用加热 MOS 独立温度保护，应给出独立阈值/恢复值并纳入加热状态机。"""
    text = replace_once(text, old, new, "update verified 10K NTC documentation")
    write(path, text)


def patch_tests() -> None:
    path = ROOT / "tests" / "sh3673510_d011_integration_check.py"
    text = read(path)
    text = text.replace(
        'port = text("sh3673520_port.c")\n',
        'port = text("sh3673520_port.c")\nport_h = text("sh3673520_port.h")\ndriver = text("sh3673520.c")\n',
        1,
    )
    text = text.replace(
        'assert literal(cfg, "SH3673510_D011_SPI_TARGET_HZ") == 375000\n',
        'assert literal(cfg, "SH3673510_D011_NTC_NOMINAL_OHM") == 10000\n',
        1,
    )
    anchor = 'require(port, "FLD_SPI_BUSY")\n'
    checks = '''require(port_h, "#define SH3673520_PORT_SPI_CLOCK_HZ              500000UL")\nrequire(port, "#define SH3673520_PORT_SPI_DIVIDER          15u")\nrequire(port, "spi_master_init(SH3673520_PORT_SPI_DIVIDER, SPI_MODE3)")\nif "requested_clock_hz" in port or "requested_clock_hz" in port_h:\n    raise AssertionError("D011 SPI speed must be fixed in the port layer, not runtime-configured")\n\nassert literal(reg, "SH3673520_MAX_CELLS") == 20\nassert literal(reg, "SH3673510_MAX_CELLS") == 10\nassert literal(reg, "SH3673520_REG_CELL20H") == 0x8F\nassert literal(reg, "SH3673520_REG_CELL20L") == 0x90\nrequire(driver, "uint8_t raw[SH3673520_MAX_CELLS * 2u]")\nrequire(driver, "SH3673520_SetCellCount")\nrequire(driver, "SH3673520_SetBalanceMask")\nrequire(driver, "values[0] = (uint8_t)((cell_mask >> 16u) & 0x0Fu)")\nrequire(driver, "values[1] = (uint8_t)((cell_mask >> 8u) & 0xFFu)")\nrequire(driver, "values[2] = (uint8_t)(cell_mask & 0xFFu)")\nrequire(driver, "sh36735xx_xfer_byte")\n'''
    if checks not in text:
        if anchor not in text:
            raise RuntimeError("test SPI anchor missing")
        text = text.replace(anchor, anchor + checks, 1)

    text = text.replace(
        '    "SH3673510_D011_CELL_COUNT",\n',
        '    "SH3673510_D011_CELL_COUNT",\n    "SH3673520_SetCellCount",\n    "SH3673520_SetBalanceMask",\n',
        1,
    )
    write(path, text)


def main() -> None:
    patch_driver()
    patch_control()
    patch_docs()
    patch_tests()
    print("SH36735xx common driver / fixed-500k SPI refactor applied")


if __name__ == "__main__":
    main()
