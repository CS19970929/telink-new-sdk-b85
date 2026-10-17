#pragma once
#include <stdint.h>
#include "bms_diag_config.h"

#if BMS_SERVICE_DIAG_ENABLE
/* 取得 FET 请求、控制和保护阻断诊断。 */
uint8_t sh3673510_bms_afe_get_fet_diagnostics(uint8_t *, uint8_t *, uint8_t *, uint8_t *);
typedef struct {
    uint8_t flag1, flag2, bstatus2;
    uint16_t backend_state, sensor_state, mos_ntc_raw, mos_temp_x10;
    uint32_t mos_ntc_ohm;
    uint32_t charge_block_reasons, discharge_block_reasons;
} sh3673510_fet_diag_detail_t;
/* 取得 FET 仲裁的详细诊断字段。 */
uint8_t sh3673510_bms_afe_get_fet_diag_detail(sh3673510_fet_diag_detail_t *detail);
#endif
