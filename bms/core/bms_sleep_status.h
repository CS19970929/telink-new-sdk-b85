#pragma once
#include <stdint.h>
#include "bms_sleep_config.h"

/* 四产品常驻只读接口；沿用原休眠窗口和低 word 在前的 u32 格式。 */
#define BMS_SLEEP_STATUS_BASE 0x3300u
#define BMS_SLEEP_STATUS_WORDS 20u
#define BMS_SLEEP_STATUS_END (BMS_SLEEP_STATUS_BASE + BMS_SLEEP_STATUS_WORDS)
/* 旧上位机从 0x2A02 检测此位；仅保留发现入口，不要求服务诊断开启。 */
#define BMS_SLEEP_STATUS_CAPABILITY 0x0040u

enum { BMS_SLEEP_STATE_INIT=0, BMS_SLEEP_STATE_NONE=1, BMS_SLEEP_STATE_COUNTING=2,
    BMS_SLEEP_STATE_BLOCKED=3, BMS_SLEEP_STATE_READY=4, BMS_SLEEP_STATE_RETRY=5,
    BMS_SLEEP_STATE_COMMITTED=6 };
enum { BMS_SLEEP_BLOCK_OTA=1u, BMS_SLEEP_BLOCK_FLASH=2u,
    BMS_SLEEP_BLOCK_BUS=4u, BMS_SLEEP_BLOCK_BLE=8u,
    BMS_SLEEP_BLOCK_FIXED_UART=16u, BMS_SLEEP_BLOCK_UART_TX=32u,
    BMS_SLEEP_BLOCK_WAKE_PAD=64u, BMS_SLEEP_BLOCK_SAMPLE=128u,
    BMS_SLEEP_BLOCK_STORAGE=256u, BMS_SLEEP_BLOCK_AFE=512u,
    BMS_SLEEP_BLOCK_BLE_CONTROL=1024u };

/* 仅主循环的 PM 路径发布已有决策；不推进策略，不访问 AFE/Flash。 */
void bms_sleep_status_publish(uint8_t reason, uint32_t block_mask, uint32_t elapsed_ms,
    uint32_t delay_ms, uint32_t retry_ms, uint8_t suspend_allowed);
/* 即将深睡/断电，表示软件已提交，不是物理入睡反馈。 */
void bms_sleep_status_committed(void);
int bms_sleep_status_overlaps(uint16_t start, uint16_t count);
/* 一次读取使用同一个当前时间；不推进倒计时或修改快照。 */
int bms_sleep_status_read(uint16_t start, uint16_t count, uint8_t *bytes);
