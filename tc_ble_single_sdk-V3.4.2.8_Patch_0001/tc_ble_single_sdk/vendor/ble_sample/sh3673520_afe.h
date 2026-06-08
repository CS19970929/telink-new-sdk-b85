#pragma once

#include "conf.h"
#include "sci_upper.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH3673520_MODEL_WORD              0x3520u
#define SH3673520_MAX_CELL_COUNT          20u
#define SH3673520_PARAM_REG_BASE          0x2200u
#define SH3673520_PARAM_WORD_COUNT        31u

#define SH3673520_PARAM_MODEL             0u
#define SH3673520_PARAM_SERIES_COUNT      1u
#define SH3673520_PARAM_RSENSE_UOHM       2u
#define SH3673520_PARAM_LAST_STATUS       3u
#define SH3673520_PARAM_FLAG1             4u
#define SH3673520_PARAM_FLAG2             5u
#define SH3673520_PARAM_FLAG3             6u
#define SH3673520_PARAM_RESERVED          7u

#define SH3673520_CFG_REG_START           0x41u
#define SH3673520_CFG_REG_END             0x57u
#define SH3673520_CFG_REG_COUNT           (SH3673520_CFG_REG_END - SH3673520_CFG_REG_START + 1u)
#define SH3673520_PARAM_CFG_BASE          8u

typedef struct {
    u16 cell_mv[SH3673520_MAX_CELL_COUNT];
    u16 temp_01c_offset[4];
    u32 pack_mv;
    u16 cplus_mv;
    u16 current_raw;
    u16 charge_ma;
    u16 discharge_ma;
    u8 status1;
    u8 status2;
    u8 flag1;
    u8 flag2;
    u8 flag3;
} sh3673520_sample_t;

void sh3673520_bus_init(void);
void sh3673520_param_load(void);
u8 sh3673520_reset(void);
u8 sh3673520_is_ready(void);
u8 sh3673520_apply_params(void);
void sh3673520_sleep(void);
u8 sh3673520_read_sample(sh3673520_sample_t *sample);
void sh3673520_publish_to_cell_info(const sh3673520_sample_t *sample, struct stCell_Info *report);

u16 sh3673520_param_read_word(u16 index);
int sh3673520_param_write_word(u16 index, u16 value);
int sh3673520_param_commit(void);

#ifdef __cplusplus
}
#endif
