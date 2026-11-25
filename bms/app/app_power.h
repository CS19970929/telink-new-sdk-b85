/* 产品低功耗接口；SDK 回调与主循环调用仍在原时点执行。 */
#ifndef APP_POWER_H_
#define APP_POWER_H_

#include <stdint.h>

/* Modbus 锁存请求；D008 关机，SH 最长等待500ms应答后提交深睡。 */
extern uint8_t deepsleep_en;

/* 已提交保护性深睡/ACC/关机时只执行保持动作并返回 1；正常运行返回 0。 */
uint8_t app_power_prepare_loop(void);
/* 只在实际采样轮次推进保护计时；到期查询统一由 app_sample_is_due 拥有。 */
void app_power_process(uint8_t sampled);
/* 保留原 SDK suspend-enter callback 的名字、签名与注册位置。 */
void task_sleep_enter(uint8_t e, uint8_t *p, int n);
/* 四产品临睡前复核采样期限和通信活动；已提交深睡始终放行。 */
int app_power_before_suspend(void);

#endif
