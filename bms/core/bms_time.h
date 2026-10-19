/*
 * 文件功能：公共业务毫秒时钟与兼容观测接口；诊断不得拥有安全计时源。
 * bms/core/bms_time.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_TIME_H_
#define BMS_TIME_H_

#include <stdint.h>

/* 时钟初始化后、业务启动前调用；普通 Suspend 不重新初始化。 */
void bms_time_init(void);
/* 仅主循环上下文调用，不用于 ISR；每轮至少消费一次，间隔须小于原始
 * 32K 的一次回绕（约 37 小时）。保留小数 tick，毫秒值自然回绕。
 * 用于硬件最短等待时使用 elapsed > delay_ms，避免量化提前满足。 */
uint32_t bms_time_ms(void);
/* 供休眠状态协议及既有诊断、日志、参数窗口保留原始单位。 */
uint32_t bms_time_now_32k(void);

#endif /* BMS_TIME_H_ */
