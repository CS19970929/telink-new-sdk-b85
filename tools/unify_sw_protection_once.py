#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
BACKEND = (HERE / "bms_afe_backend.h").read_text(encoding="utf-8")
ACTIVE_DVC = "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124" in BACKEND
ACTIVE_SH = "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510" in BACKEND
if ACTIVE_DVC == ACTIVE_SH:
    raise SystemExit("cannot resolve exactly one active AFE backend")

HEADER = r'''#ifndef BMS_SW_PROTECTION_H_
#define BMS_SW_PROTECTION_H_

#include <stdint.h>

/*
 * AFE-independent software protection input.
 * Temperatures use the existing firmware encoding: (degC + 40) * 10.
 * Voltage/current values are read from g_stCellInfoReport in the legacy units
 * documented by bms_state.h.
 */
typedef struct
{
    uint8_t battery_temp_valid;
    uint8_t mos_temp_valid;
    uint16_t battery_temp_min;
    uint16_t battery_temp_max;
    uint16_t mos_temp;
} bms_sw_protection_inputs_t;

void bms_sw_protection_init(void);
void bms_sw_protection_clear(void);
void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs);
void bms_sw_protection_record_fault_edges(void);
uint8_t bms_sw_protection_charge_blocked(void);
uint8_t bms_sw_protection_discharge_blocked(void);

#endif /* BMS_SW_PROTECTION_H_ */
'''

SOURCE = r'''#include "bms_sw_protection.h"

#include "bms_error.h"
#include "bms_state.h"
#include "param.h"
#include <string.h>

/*
 * Unified software-protection policy shared by D008 / D011 / D013.
 *
 * Scope:
 *   - software threshold/filter/recovery state only;
 *   - no AFE register access, GPIO access or hardware-latch clearing;
 *   - Level 1/2 are report/alarm levels;
 *   - Level 3 is the software MOS-blocking level;
 *   - AFE hardware protection remains an independent backup. Backends merge
 *     hardware flags into Third after this module evaluates the software state.
 *
 * u16SocUp_* / b1SocLow are intentionally excluded from v1 because the legacy
 * naming/semantics are inconsistent. They must be specified before being made
 * part of the common protection state machine.
 */
#define BMS_SW_PROTECTION_SAMPLE_MS       200u
#define BMS_SW_PROTECTION_LEVEL_COUNT     3u
#define BMS_SW_PROTECTION_FILTER_COUNT    12u

typedef struct
{
    uint16_t trip_count;
    uint16_t recover_count;
    uint8_t active;
} bms_sw_filter_t;

typedef enum
{
    BMS_SW_F_CELL_OV = 0,
    BMS_SW_F_CELL_UV,
    BMS_SW_F_PACK_OV,
    BMS_SW_F_PACK_UV,
    BMS_SW_F_CHG_OC,
    BMS_SW_F_DSG_OC,
    BMS_SW_F_CHG_OT,
    BMS_SW_F_CHG_UT,
    BMS_SW_F_DSG_OT,
    BMS_SW_F_DSG_UT,
    BMS_SW_F_MOS_OT,
    BMS_SW_F_VDELTA,
    BMS_SW_F_COUNT
} bms_sw_filter_id_t;

typedef enum
{
    BMS_SW_HIGH = 0,
    BMS_SW_LOW
} bms_sw_direction_t;

static bms_sw_filter_t s_filter[BMS_SW_PROTECTION_LEVEL_COUNT][BMS_SW_F_COUNT];
static bms_fault_reg_t s_prev_fault[BMS_SW_PROTECTION_LEVEL_COUNT];

static uint16_t bms_sw_level_value(uint8_t level,
                                   uint16_t first,
                                   uint16_t second,
                                   uint16_t third)
{
    return (level == 0u) ? first : ((level == 1u) ? second : third);
}

static uint16_t bms_sw_filter_samples(uint16_t filter_10ms)
{
    uint32_t delay_ms = (uint32_t)filter_10ms * 10u;
    uint32_t samples;

    if (delay_ms == 0u) return 1u;
    samples = (delay_ms + BMS_SW_PROTECTION_SAMPLE_MS - 1u) /
              BMS_SW_PROTECTION_SAMPLE_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

static void bms_sw_filter_reset(bms_sw_filter_t *state)
{
    if (state == 0) return;
    state->trip_count = 0u;
    state->recover_count = 0u;
    state->active = 0u;
}

static uint8_t bms_sw_filter_update(bms_sw_filter_t *state,
                                    uint16_t value,
                                    uint16_t trip,
                                    uint16_t recover,
                                    uint16_t filter_10ms,
                                    bms_sw_direction_t direction)
{
    uint16_t required;
    uint8_t violated;
    uint8_t recovered;

    if (state == 0) return 0u;
    if (trip == 0u)
    {
        bms_sw_filter_reset(state);
        return 0u;
    }

    required = bms_sw_filter_samples(filter_10ms);
    if (state->active)
    {
        recovered = (direction == BMS_SW_HIGH) ?
                    (value <= recover) : (value >= recover);
        if (recovered)
        {
            if (state->recover_count < required) ++state->recover_count;
            if (state->recover_count >= required)
                bms_sw_filter_reset(state);
        }
        else
        {
            state->recover_count = 0u;
        }
        return state->active;
    }

    violated = (direction == BMS_SW_HIGH) ?
               (value >= trip) : (value <= trip);
    if (violated)
    {
        if (state->trip_count < required) ++state->trip_count;
        if (state->trip_count >= required)
        {
            state->active = 1u;
            state->trip_count = 0u;
            state->recover_count = 0u;
        }
    }
    else if (state->trip_count != 0u)
    {
        /* Preserve the existing D011/D013 leaky debounce behavior instead of
         * resetting the trip accumulator on one clean sample. */
        --state->trip_count;
    }
    return state->active;
}

static bms_fault_reg_t *bms_sw_fault_reg(uint8_t level)
{
    if (level == 0u) return &g_stCellInfoReport.unMdlFault_First;
    if (level == 1u) return &g_stCellInfoReport.unMdlFault_Second;
    return &g_stCellInfoReport.unMdlFault_Third;
}

static void bms_sw_clear_managed_bits(bms_fault_reg_t *fault)
{
    if (fault == 0) return;
    fault->bits.b1CellOvp = 0u;
    fault->bits.b1CellUvp = 0u;
    fault->bits.b1BatOvp = 0u;
    fault->bits.b1BatUvp = 0u;
    fault->bits.b1IchgOcp = 0u;
    fault->bits.b1IdischgOcp = 0u;
    fault->bits.b1CellChgOtp = 0u;
    fault->bits.b1CellChgUtp = 0u;
    fault->bits.b1CellDischgOtp = 0u;
    fault->bits.b1CellDischgUtp = 0u;
    fault->bits.b1TmosOtp = 0u;
    fault->bits.b1VcellDeltaBig = 0u;
}

void bms_sw_protection_clear(void)
{
    uint8_t level;
    memset(s_filter, 0, sizeof(s_filter));
    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
        bms_sw_clear_managed_bits(bms_sw_fault_reg(level));
    bms_error_clear(BMS_ERROR_TEMP_BREAK);
}

void bms_sw_protection_init(void)
{
    memset(s_prev_fault, 0, sizeof(s_prev_fault));
    bms_sw_protection_clear();
}

void bms_sw_protection_update(const bms_sw_protection_inputs_t *inputs)
{
    const struct PRT_E2ROM_PARAS *p = &g_tParam.protect;
    uint8_t level;
    uint8_t temp_valid;

    if (inputs == 0) return;
    temp_valid = (inputs->battery_temp_valid && inputs->mos_temp_valid) ? 1u : 0u;
    if (temp_valid) bms_error_clear(BMS_ERROR_TEMP_BREAK);
    else bms_error_raise(BMS_ERROR_TEMP_BREAK);

    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
    {
        bms_fault_reg_t *f = bms_sw_fault_reg(level);
        uint16_t trip;

        trip = bms_sw_level_value(level, p->u16VcellOvp_First,
                                  p->u16VcellOvp_Second, p->u16VcellOvp_Third);
        f->bits.b1CellOvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CELL_OV],
            g_stCellInfoReport.u16VCellMax, trip, p->u16VcellOvp_Rcv,
            p->u16VcellOvp_Filter, BMS_SW_HIGH);

        trip = bms_sw_level_value(level, p->u16VcellUvp_First,
                                  p->u16VcellUvp_Second, p->u16VcellUvp_Third);
        f->bits.b1CellUvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CELL_UV],
            g_stCellInfoReport.u16VCellMin, trip, p->u16VcellUvp_Rcv,
            p->u16VcellUvp_Filter, BMS_SW_LOW);

        trip = bms_sw_level_value(level, p->u16VbusOvp_First,
                                  p->u16VbusOvp_Second, p->u16VbusOvp_Third);
        f->bits.b1BatOvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_PACK_OV],
            g_stCellInfoReport.u16VCellTotle, trip, p->u16VbusOvp_Rcv,
            p->u16VbusOvp_Filter, BMS_SW_HIGH);

        trip = bms_sw_level_value(level, p->u16VbusUvp_First,
                                  p->u16VbusUvp_Second, p->u16VbusUvp_Third);
        f->bits.b1BatUvp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_PACK_UV],
            g_stCellInfoReport.u16VCellTotle, trip, p->u16VbusUvp_Rcv,
            p->u16VbusUvp_Filter, BMS_SW_LOW);

        trip = bms_sw_level_value(level, p->u16IchgOcp_First,
                                  p->u16IchgOcp_Second, p->u16IchgOcp_Third);
        f->bits.b1IchgOcp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CHG_OC],
            g_stCellInfoReport.u16Ichg, trip, p->u16IchgOcp_Rcv,
            p->u16IchgOcp_Filter, BMS_SW_HIGH);

        trip = bms_sw_level_value(level, p->u16IdsgOcp_First,
                                  p->u16IdsgOcp_Second, p->u16IdsgOcp_Third);
        f->bits.b1IdischgOcp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_DSG_OC],
            g_stCellInfoReport.u16IDischg, trip, p->u16IdsgOcp_Rcv,
            p->u16IdsgOcp_Filter, BMS_SW_HIGH);

        if (temp_valid)
        {
            trip = bms_sw_level_value(level, p->u16TChgOTp_First,
                                      p->u16TChgOTp_Second, p->u16TChgOTp_Third);
            f->bits.b1CellChgOtp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CHG_OT],
                inputs->battery_temp_max, trip, p->u16TChgOTp_Rcv,
                p->u16TChgOTp_Filter, BMS_SW_HIGH);

            trip = bms_sw_level_value(level, p->u16TchgUTp_First,
                                      p->u16TchgUTp_Second, p->u16TchgUTp_Third);
            f->bits.b1CellChgUtp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_CHG_UT],
                inputs->battery_temp_min, trip, p->u16TchgUTp_Rcv,
                p->u16TchgUTp_Filter, BMS_SW_LOW);

            trip = bms_sw_level_value(level, p->u16TdischgOTp_First,
                                      p->u16TdischgOTp_Second, p->u16TdischgOTp_Third);
            f->bits.b1CellDischgOtp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_DSG_OT],
                inputs->battery_temp_max, trip, p->u16TdischgOTp_Rcv,
                p->u16TdischgOTp_Filter, BMS_SW_HIGH);

            trip = bms_sw_level_value(level, p->u16TdischgUTp_First,
                                      p->u16TdischgUTp_Second, p->u16TdischgUTp_Third);
            f->bits.b1CellDischgUtp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_DSG_UT],
                inputs->battery_temp_min, trip, p->u16TdischgUTp_Rcv,
                p->u16TdischgUTp_Filter, BMS_SW_LOW);

            trip = bms_sw_level_value(level, p->u16TmosOTp_First,
                                      p->u16TmosOTp_Second, p->u16TmosOTp_Third);
            f->bits.b1TmosOtp = bms_sw_filter_update(&s_filter[level][BMS_SW_F_MOS_OT],
                inputs->mos_temp, trip, p->u16TmosOTp_Rcv,
                p->u16TmosOTp_Filter, BMS_SW_HIGH);
        }
        else
        {
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_OT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_UT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_DSG_OT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_DSG_UT]);
            bms_sw_filter_reset(&s_filter[level][BMS_SW_F_MOS_OT]);
            f->bits.b1CellChgOtp = 0u;
            f->bits.b1CellChgUtp = 0u;
            f->bits.b1CellDischgOtp = 0u;
            f->bits.b1CellDischgUtp = 0u;
            f->bits.b1TmosOtp = 0u;
        }

        trip = bms_sw_level_value(level, p->u16VdeltaOvp_First,
                                  p->u16VdeltaOvp_Second, p->u16VdeltaOvp_Third);
        f->bits.b1VcellDeltaBig = bms_sw_filter_update(&s_filter[level][BMS_SW_F_VDELTA],
            g_stCellInfoReport.u16VCellDelta, trip, p->u16VdeltaOvp_Rcv,
            p->u16VdeltaOvp_Filter, BMS_SW_HIGH);
    }
}

void bms_sw_protection_record_fault_edges(void)
{
    uint8_t level;
    for (level = 0u; level < BMS_SW_PROTECTION_LEVEL_COUNT; ++level)
    {
        bms_fault_reg_t *now = bms_sw_fault_reg(level);
        bms_fault_reg_t *prev = &s_prev_fault[level];
        uint8_t base = (uint8_t)(1u + 13u * level);
#define BMS_SW_RISE(field, offset) \
        do { if (now->bits.field && !prev->bits.field) \
            bms_fault_history_record((bms_fault_code_t)(base + (offset))); } while (0)
        BMS_SW_RISE(b1CellOvp, 0u);
        BMS_SW_RISE(b1CellUvp, 1u);
        BMS_SW_RISE(b1BatOvp, 2u);
        BMS_SW_RISE(b1BatUvp, 3u);
        BMS_SW_RISE(b1IchgOcp, 4u);
        BMS_SW_RISE(b1IdischgOcp, 5u);
        BMS_SW_RISE(b1CellChgOtp, 6u);
        BMS_SW_RISE(b1CellChgUtp, 7u);
        BMS_SW_RISE(b1CellDischgOtp, 8u);
        BMS_SW_RISE(b1CellDischgUtp, 9u);
        BMS_SW_RISE(b1TmosOtp, 10u);
        BMS_SW_RISE(b1VcellDeltaBig, 11u);
#undef BMS_SW_RISE
        *prev = *now;
    }
}

uint8_t bms_sw_protection_charge_blocked(void)
{
    const bms_fault_bits_t *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

uint8_t bms_sw_protection_discharge_blocked(void)
{
    const bms_fault_bits_t *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}
'''

TEST = r'''#!/usr/bin/env python3
"""Static contract for the AFE-independent D008/D011/D013 software protection core."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
source = (HERE / "bms_sw_protection.c").read_text(encoding="utf-8", errors="ignore")
header = (HERE / "bms_sw_protection.h").read_text(encoding="utf-8", errors="ignore")
backend = (HERE / "bms_afe_backend.h").read_text(encoding="utf-8", errors="ignore")

for token in (
    "BMS_SW_PROTECTION_LEVEL_COUNT     3u",
    "BMS_SW_PROTECTION_FILTER_COUNT    12u",
    "trip_count",
    "recover_count",
    "unMdlFault_First",
    "unMdlFault_Second",
    "unMdlFault_Third",
    "u16VcellOvp_First",
    "u16VcellOvp_Second",
    "u16VcellOvp_Third",
    "u16VcellUvp_First",
    "u16VbusOvp_First",
    "u16VbusUvp_First",
    "u16IchgOcp_First",
    "u16IdsgOcp_First",
    "u16TChgOTp_First",
    "u16TchgUTp_First",
    "u16TdischgOTp_First",
    "u16TdischgUTp_First",
    "u16TmosOTp_First",
    "u16VdeltaOvp_First",
    "BMS_ERROR_TEMP_BREAK",
    "bms_sw_protection_record_fault_edges",
):
    if token not in source and token not in header:
        raise AssertionError(f"missing common protection invariant: {token}")

for forbidden in ("DVC1124_", "SH3673510_", "SH3673520_", "gpio_", "ReadReg", "WriteReg"):
    if forbidden in source:
        raise AssertionError(f"common protection leaked backend detail: {forbidden}")

if "p->u16SocUp_" in source:
    raise AssertionError("legacy SOC protection must remain out until semantics are specified")

if "BMS_AFE_BACKEND_DVC1124" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124" in backend:
    dvc = (HERE / "dvc1124_bms.c").read_text(encoding="utf-8", errors="ignore")
    for token in ("bms_sw_protection_update(&sw);", "dvc_merge_hw_faults(alarm);", "bms_sw_protection_record_fault_edges();"):
        if token not in dvc:
            raise AssertionError(f"D008 integration missing: {token}")
    if "dvc_publish_faults(alarm, &cfg);" in dvc:
        raise AssertionError("D008 still executes legacy per-backend software protection")

if "BMS_AFE_BACKEND_SH3673510" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510" in backend:
    sh = (HERE / "sh3673510_bms.c").read_text(encoding="utf-8", errors="ignore")
    for token in ("bms_sw_protection_update(&sw);", "bms_sw_protection_clear();", "bms_sw_protection_record_fault_edges();"):
        if token not in sh:
            raise AssertionError(f"SH3673510 integration missing: {token}")
    if "update_faults();" in sh:
        raise AssertionError("SH3673510 still executes legacy per-backend software protection")
    if sh.index("bms_sw_protection_update(&sw);") > sh.index("merge_hw_protection_faults(&status);"):
        raise AssertionError("software protection must run before hardware flags are merged")
    if sh.index("merge_hw_protection_faults(&status);") > sh.index("bms_sw_protection_record_fault_edges();"):
        raise AssertionError("fault history must observe merged software + hardware Third state")

print("Unified software protection contract: PASS")
'''

DOC = r'''# D008 / D011 / D013 统一软件保护框架

更新日期：2026-09-14。

## 目标

三个产品共享同一份 `bms_sw_protection.c/.h`。AFE backend 只负责采样、寄存器量化、硬件保护状态和硬件锁存恢复；软件阈值判断、三级滤波、恢复滤波和软件故障历史不再按 AFE 各写一套。

## 三级语义

- **First**：一级告警/报告，不直接关闭 MOS。
- **Second**：二级告警/报告，预留降额/策略升级，不直接关闭 MOS。
- **Third**：软件保护级，参与 CHG/DSG MOS 禁止。
- **AFE Hardware Protection**：独立安全后备，可以因芯片量化和硬件时序早于软件 Third 动作；backend 将有效硬件保护状态 OR 到 Third，再统一记录故障边沿。

软件三级与硬件保护不是“谁覆盖谁”，而是并行保护通道。软件层不能读写 AFE 寄存器，硬件层不能重新定义 First/Second/Third 的软件语义。

## 当前统一的软件保护项

1. 单体过压 / 欠压；
2. 总压过压 / 欠压；
3. 充电过流 / 放电过流；
4. 充电高温 / 低温；
5. 放电高温 / 低温；
6. MOS 高温；
7. 单体压差过大。

参数仍来自 `g_tParam.protect`，采样节拍按 200 ms。触发和恢复都使用同一参数滤波时间转换为采样次数；恢复必须连续满足恢复条件，不再出现 D008 旧逻辑“单次达到恢复值立即清故障”的差异。

## 温度断线

backend 向公共层提供 Battery min/max、MOS 温度以及有效性。任一必需温度无效时置 `BMS_ERROR_TEMP_BREAK`，软件温度阈值状态清零，MOS 由既有 fail-safe 门控禁止；恢复有效后再重新经过正常温度保护滤波。

## 硬件保护仍保持产品/AFE差异

本轮不统一以下内容：

- DVC1124 与 SH3673510 的寄存器、量化步进和硬件延时；
- SCD/SC 的硬件阈值与恢复条件；
- AFE WDT；
- Load Detect / LOADOFF；
- Body Diode；
- OpenWire / Balance；
- 产品 GPIO、Rsense、NTC 和 FET 拓扑。

这些属于 backend / Product Profile，不应为了“代码看起来一样”强行采用同一寄存器配置。

## 暂不纳入的 SOC 保护

历史结构同时存在 `u16SocUp_*`、`b1SocLow` 和 `BMS_FAULT_SOC_HIGH_*` 三套互相矛盾的命名。当前代码没有足够证据确定其产品语义，因此 v1 明确不把 SOC 阈值加入公共保护状态机。待协议/产品语义确认后再单独迁移，避免静默改变已出货行为。

## 后续

统一软件保护以后，下一阶段应统一 `bms_output_arbiter` 和硬件故障恢复语义，使最终 MOS 决策固定为：

`product request × output enable × communication health × software Third × hardware protection × hardware lockout/recovery`
'''


def write(path, content):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")

write(HERE.relative_to(ROOT) / "bms_sw_protection.h", HEADER)
write(HERE.relative_to(ROOT) / "bms_sw_protection.c", SOURCE)
write(Path("tests") / "sw_protection_contract_check.py", TEST)
write(Path("docs") / "SOFTWARE_PROTECTION.md", DOC)


def sub_once(text, pattern, repl, label):
    out, count = re.subn(pattern, repl, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one replacement, got {count}")
    return out

if ACTIVE_DVC:
    path = HERE / "dvc1124_bms.c"
    text = path.read_text(encoding="utf-8")
    text = text.replace('#include "bms_state.h"\n', '#include "bms_state.h"\n#include "bms_sw_protection.h"\n', 1)

    replacement = r'''static void dvc_merge_hw_faults(uint8_t alarm)
{
    bms_fault_reg_t *f = &g_stCellInfoReport.unMdlFault_Third;

    if (alarm & DVC1124_ALARM_COV_MASK) f->bits.b1CellOvp = 1u;
    if (alarm & DVC1124_ALARM_CUV_MASK) f->bits.b1CellUvp = 1u;
    if (alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK))
        f->bits.b1IdischgOcp = 1u;
    if (alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK))
        f->bits.b1IchgOcp = 1u;

    /* SCD remains a hardware/backend lockout. Do not fold its release policy
     * into the generic software-threshold state machine. */
    if (alarm & DVC1124_ALARM_SCD_MASK)
    {
        if (!bms_error_get(BMS_ERROR_CBC_DSG))
            bms_error_raise(BMS_ERROR_CBC_DSG);
    }
    else if (bms_error_get(BMS_ERROR_CBC_DSG))
    {
        bms_error_clear(BMS_ERROR_CBC_DSG);
    }
}

'''
    text = sub_once(
        text,
        r'static union MDLCHGFAULT_REG dvc_make_managed_fault_snapshot\(void\).*?(?=static uint8_t dvc_charge_blocked\(void\))',
        replacement,
        "replace D008 legacy protection block",
    )
    old = '''void DVC1124_BmsApp_AFEGet(void)\n{\n    dvc1124_snapshot_t snapshot;\n    dvc1124_config_t cfg;\n    uint8_t alarm;\n\n    DVC1124_App_AFEGet();\n    DVC1124_GetSnapshot(&snapshot);\n    if (!snapshot.valid) return;\n\n    DVC1124_GetConfig(&cfg);\n    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm);\n    dvc_publish_faults(alarm, &cfg);\n'''
    new = '''void DVC1124_BmsApp_AFEGet(void)\n{\n    dvc1124_snapshot_t snapshot;\n    dvc1124_config_t cfg;\n    bms_sw_protection_inputs_t sw;\n    uint16_t battery_temp;\n    uint16_t mos_temp;\n    uint8_t alarm;\n\n    DVC1124_App_AFEGet();\n    DVC1124_GetSnapshot(&snapshot);\n    if (!snapshot.valid) return;\n\n    DVC1124_GetConfig(&cfg);\n    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm);\n\n    memset(&sw, 0, sizeof(sw));\n    battery_temp = dvc_get_configured_temperature(cfg.battery_ntc_gp);\n    mos_temp = dvc_get_configured_temperature(cfg.mos_ntc_gp);\n    sw.battery_temp_valid = battery_temp ? 1u : 0u;\n    sw.mos_temp_valid = mos_temp ? 1u : 0u;\n    sw.battery_temp_min = battery_temp;\n    sw.battery_temp_max = battery_temp;\n    sw.mos_temp = mos_temp;\n    bms_sw_protection_update(&sw);\n    dvc_merge_hw_faults(alarm);\n    bms_sw_protection_record_fault_edges();\n'''
    if old not in text:
        raise SystemExit("D008 BmsApp anchor not found")
    text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8")

elif ACTIVE_SH:
    path = HERE / "sh3673510_bms.c"
    text = path.read_text(encoding="utf-8")
    text = text.replace('#include "bms_state.h"\n', '#include "bms_state.h"\n#include "bms_sw_protection.h"\n', 1)
    text = text.replace('    sh3673510_control_status_t status;\n', '    sh3673510_control_status_t status;\n    bms_sw_protection_inputs_t sw;\n', 1)
    text = text.replace('    memset(s_filter, 0, sizeof(s_filter));\n    memset(s_prev_fault, 0, sizeof(s_prev_fault));\n', '    bms_sw_protection_init();\n', 1)
    old = '''    publish_hw_status(&status);\n    if (!s_afe_reconfigure_required) {\n        update_faults();\n#if SH3673510_HW_PROTECT_ENABLE\n        merge_hw_protection_faults(&status);\n        service_short_recovery(&status);\n        service_hw_flag_recovery(&status);\n#endif\n    }\n'''
    new = '''    publish_hw_status(&status);\n    if (!s_afe_reconfigure_required) {\n        memset(&sw, 0, sizeof(sw));\n        sw.battery_temp_valid = battery_temperature_snapshot(&sw.battery_temp_min,\n                                                              &sw.battery_temp_max);\n        sw.mos_temp_valid = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ? 1u : 0u;\n        if (sw.mos_temp_valid)\n            sw.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];\n#if SH3673510_SW_PROTECT_ENABLE\n        bms_sw_protection_update(&sw);\n#else\n        bms_sw_protection_clear();\n#endif\n#if SH3673510_HW_PROTECT_ENABLE\n        merge_hw_protection_faults(&status);\n        service_short_recovery(&status);\n        service_hw_flag_recovery(&status);\n#endif\n        bms_sw_protection_record_fault_edges();\n    }\n'''
    if old not in text:
        raise SystemExit("SH publish/protection anchor not found")
    text = text.replace(old, new, 1)

    # Remove the old software-only implementation so there is exactly one
    # threshold/filter state machine in the product build.
    text = sub_once(text,
        r'typedef struct \{\s*uint16_t trip_count;\s*uint16_t recover_count;\s*uint8_t active;\s*\} sh3510_filter_t;\s*\n\ntypedef enum \{\s*F_CELL_OV.*?\} sh3510_filter_id_t;\s*\n\n',
        '', "remove SH software filter types")
    text = text.replace('static sh3510_filter_t s_filter[SH3510_LEVEL_COUNT][F_COUNT];\n', '', 1)
    text = text.replace('static union MDLCHGFAULT_REG s_prev_fault[SH3510_LEVEL_COUNT];\n', '', 1)
    text = text.replace('#define SH3510_LEVEL_COUNT            3u\n', '', 1)
    text = sub_once(text,
        r'static uint16_t level_value\(.*?(?=static uint16_t filter_samples\()',
        '', "remove SH level helper")
    text = sub_once(text,
        r'static uint8_t filter_update\(.*?(?=static uint16_t ntc_temp\()',
        '', "remove SH software filter function")
    text = sub_once(text,
        r'static union MDLCHGFAULT_REG \*fault_reg\(.*?(?=static uint8_t battery_temperature_snapshot\()',
        '', "remove SH fault/history helpers")
    text = sub_once(text,
        r'static void update_faults\(void\).*?(?=static uint8_t charge_blocked\(void\))',
        '', "remove SH software update")
    path.write_text(text, encoding="utf-8")

# CI: run the common protection contract in both Host and TC32 jobs.
wf = ROOT / ".github" / "workflows" / "bms-ci.yml"
workflow = wf.read_text(encoding="utf-8")
if "Run unified software protection contract check" not in workflow:
    anchor = "      - name: Run SOC profile/algorithm contract check\n"
    count = workflow.count(anchor)
    if count < 1:
        raise SystemExit("CI SOC anchor not found")
    step = "      - name: Run unified software protection contract check\n        run: python tests/sw_protection_contract_check.py\n\n"
    workflow = workflow.replace(anchor, step + anchor)
    wf.write_text(workflow, encoding="utf-8")

print("software protection unification patch applied")
