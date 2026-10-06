/*
 * 文件功能：持久历史事件、重复计数和 Flash checkpoint；与详细运行调试日志独立，
 * 遵循现有更新编号策略。
 * bms/core/bms_event_log.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

#include "tl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMS_EVENT_LOG_ENTRY_COUNT     100u
#define BMS_EVENT_LOG_REG_BASE        0xC008u
#define BMS_EVENT_LOG_REG_COUNT       BMS_EVENT_LOG_ENTRY_COUNT

typedef enum {
    BMS_EVENT_NULL1 = 0,
    BMS_START_UP,
    BMS_SLEEP,
    BALANCE_OPEN,
    HEAT_OPEN,
    COOL_OPEN,
    VCELL_OVP,
    VBUS_OVP,
    CHG_OCP,
    VCELL_UVP,
    VBUS_UVP,
    DSG_OCP,
    CHG_UTP,
    DSG_UTP,
    CHG_OTP,
    DSG_OTP,
    VDELTA_OP,
    CBC_ERR,
    AFE1_ERR,
    AFE2_ERR,
    EEPROM_ERR,
    EVENT_NUM
} bms_event_log_id_t;

typedef struct {
    u8 balance;
    u8 vcell_ovp, vbus_ovp, chg_ocp, vcell_uvp, vbus_uvp, dsg_ocp;
    u8 chg_utp, dsg_utp, chg_otp, dsg_otp, vdelta_op;
    u8 afe1_err, cbc_err;
} bms_event_log_sample_t;

/* 加载历史记录并初始化事件跟踪状态。 */
int bms_event_log_init(void);
/* 记录本次启动；原协议不含启动原因。 */
void bms_event_log_note_startup(void);
/* 追加休眠尝试并保存检查点；失败保留待保存记录。 */
int bms_event_log_note_sleep(void);
/* 按秒检测事件变化并按策略保存检查点。 */
void bms_event_log_poll_1s(const bms_event_log_sample_t *sample);
/* 读取历史事件的重复次数信息。 */
u16 bms_event_log_read_repeat(u16 reg);
/* 读取历史事件窗口中的一个协议寄存器。 */
u16 bms_event_log_read_reg(u16 reg);
/* PM/shutdown 转换中止后允许下一次尝试追加记录。 */
void bms_event_log_cancel_sleep(void);

#ifdef __cplusplus
}
#endif
