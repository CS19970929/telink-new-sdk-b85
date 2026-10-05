/*
 * 文件功能：工厂老化模式与运行分钟累计；使用 32K 差值计时并持久化，
 * deep sleep 时间不计入运行时长。
 * bms/core/bms_factory_mode.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_FACTORY_MODE_H_
#define BMS_FACTORY_MODE_H_

#include "tl_common.h"

#define FACTORY_TIME_LIMIT_MIN (60 * 24 * 3) /* 三天 */

typedef enum
{
    MODE_FACTORY = 0,
    MODE_NORMAL
}bms_mode_t;

/* 加载工厂模式状态并初始化 32K 计时基准。 */
void Runtime_Init(void);
/* 消费当前运行时间差并推进老化计时。 */
void Runtime_Poll(void);
/* 深睡前重设运行计时基准，不把睡眠时长计入老化。 */
void Runtime_PrepareForDeepSleep(void);
/* 取消深睡准备并恢复运行计时基准。 */
void Runtime_CancelPendingDeepSleep(void);
/* 查询当前运行或工厂老化模式。 */
bms_mode_t Runtime_GetMode(void);
/* 恢复工厂模式默认状态和累计时间。 */
int Runtime_FactoryReset(void);
/* 重新进入工厂老化模式并重建计时状态。 */
int Runtime_ReenterFactoryMode(void);

#endif
