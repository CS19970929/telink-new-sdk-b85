/* 产品低功耗接口；SDK 回调与主循环调用仍在原时点执行。 */
#ifndef APP_POWER_H_
#define APP_POWER_H_

#include "common/types.h"
#include <stdint.h>

/* Modbus 锁存请求；D008 关机，SH 最长等待500ms应答后提交深睡。 */
extern bool deepsleep_en;

/* 已提交保护性深睡/ACC/关机时只执行保持动作并返回 1；正常运行返回 0。 */
uint8_t app_power_prepare_loop(void);
/* app.c 必须传入非空采样标志指针；本次调用只读，不缓存指针或修改标志。 */
void app_power_process(const volatile uint8_t *sample_due);
/* 保留原 SDK suspend-enter callback 的名字、签名与注册位置。 */
void task_sleep_enter(u8 e, u8 *p, int n);
/* 仅 SH 注册：在 SDK 真正休眠前复核 UART 活动，已提交深睡始终放行。 */
int app_power_before_suspend(void);

#endif
