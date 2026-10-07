/* 产品低功耗接口；SDK 回调与主循环调用仍在原时点执行。 */
#ifndef APP_POWER_H_
#define APP_POWER_H_

#include "common/types.h"
#include <stdint.h>

/* Modbus 锁存原有请求；D008 关机路径消费，失败保持请求。 */
extern bool deepsleep_en;

/* 已提交休眠/关机时执行保持动作并返回 1；正常运行返回 0。 */
uint8_t app_power_prepare_loop(void);
/* app.c 必须传入非空采样标志指针；本次调用只读，不缓存指针或修改标志。 */
void app_power_process(const volatile uint8_t *sample_due);
/* 保留原 SDK suspend-enter callback 的名字、签名与注册位置。 */
void task_sleep_enter(u8 e, u8 *p, int n);

#endif
