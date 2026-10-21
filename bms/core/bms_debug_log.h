/*
 * 文件功能：运行调试日志 RAM 环形缓存与只读接口；编译关闭时裁剪记录点，
 * 不写 Flash、不主动发送。
 * bms_debug_log.h；源码编译归属见 bms/products/<product>/sources.txt，
 * 具体实现受产品宏约束。
 */
/*
 * 运行调试日志：主循环记录结构化事件，串口/BLE 按需只读；不写 Flash。
 * 等级与模块在编译时筛选，关闭时不计算实参。禁止从 ISR 调用。
 */
#ifndef BMS_DEBUG_LOG_H
#define BMS_DEBUG_LOG_H
#include <stdint.h>

#include "bms_debug_log_config.h"

enum { BMS_LOG_ERROR=1, BMS_LOG_WARN=2, BMS_LOG_INFO=3, BMS_LOG_DEBUG=4 };
enum { BMS_LOG_SYSTEM, BMS_LOG_AFE, BMS_LOG_PROTECT, BMS_LOG_SOC,
       BMS_LOG_COMM, BMS_LOG_POWER, BMS_LOG_STORAGE, BMS_LOG_FEATURE, BMS_LOG_OTA };
/* 1..13 沿用运行诊断事件编号；32 起为更详细的运行过程事件。 */
enum {
    BMS_LOG_SAMPLE=32, BMS_LOG_SOC_DECISION, BMS_LOG_SOC_TIME,
    BMS_LOG_UART_FRAME, BMS_LOG_BLE_FRAME, BMS_LOG_CRC_REJECT,
    BMS_LOG_RX_LENGTH, BMS_LOG_UART_RECOVERY, BMS_LOG_BLE_NOTIFY_FAIL,
    BMS_LOG_LINK_STATE, BMS_LOG_SLEEP_ATTEMPT, BMS_LOG_SLEEP_ABORT,
    BMS_LOG_SLEEP_RETURN, BMS_LOG_OTA_STATE, BMS_LOG_BALANCE_REQUEST,
    BMS_LOG_BALANCE_RESULT, BMS_LOG_PROTECTION_EDGE, BMS_LOG_MODBUS_EXCEPTION,
    BMS_LOG_CELL_RANGE, BMS_LOG_SOC_STATE, BMS_LOG_MOS_REASONS,
    BMS_LOG_AFE_STATE, BMS_LOG_PM_CYCLE, BMS_LOG_AFE_RETRY,
    BMS_LOG_AFE_FAILED, BMS_LOG_AFE_RECOVERED, BMS_LOG_TX_BUSY
};
#define BMS_DEBUG_LOG_BASE 0x3000u
#define BMS_DEBUG_LOG_HEADER_WORDS 16u
#define BMS_DEBUG_LOG_CAPACITY 64u
#define BMS_DEBUG_LOG_RECORD_WORDS 10u
#define BMS_DEBUG_LOG_END (BMS_DEBUG_LOG_BASE + BMS_DEBUG_LOG_HEADER_WORDS + \
                          BMS_DEBUG_LOG_CAPACITY * BMS_DEBUG_LOG_RECORD_WORDS)

/* 单一协作主循环拥有环形缓存；读请求不消费记录，多个客户端各自维护序号。 */
#if BMS_DEBUG_LOG_ENABLE
/* 初始化运行日志 RAM 环形缓存与能力信息。 */
void bms_debug_log_init(void);
/* 按等级和模块筛选后写入一条结构化运行日志。 */
void bms_debug_log_write(uint8_t level, uint8_t module, uint16_t event,
                         uint32_t arg0, uint32_t arg1);
#define BMS_LOG(level, module, event, arg0, arg1) do { \
    if ((level) <= BMS_DEBUG_LOG_LEVEL && \
        (BMS_DEBUG_LOG_MODULE_MASK & (1u << (module)))) \
        bms_debug_log_write((level), (module), (event), (uint32_t)(arg0), (uint32_t)(arg1)); \
} while (0)
#else
#define bms_debug_log_init() ((void)0)
#define BMS_LOG(level, module, event, arg0, arg1) ((void)0)
#endif
/* 判断寄存器范围是否与运行日志窗口重叠。 */
int bms_debug_log_overlaps(uint16_t start, uint16_t count);
/* 读取 RAM 日志窗口，校验范围并按大端协议编码。 */
int bms_debug_log_read(uint16_t start, uint16_t count, uint8_t *bytes);
/* 日志读取自身不生成通信日志，避免不断读取导致自身填满缓冲。 */
int bms_debug_log_is_read(const uint8_t *frame, uint32_t length);
#endif
