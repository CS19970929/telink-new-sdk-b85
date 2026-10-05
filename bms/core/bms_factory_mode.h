/* 文件功能：工厂老化模式与运行分钟累计；使用 32K 差值计时并持久化，deep sleep 时间不计入运行时长。
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

void Runtime_Init(void);
void Runtime_Poll(void);
void Runtime_PrepareForDeepSleep(void);
void Runtime_CancelPendingDeepSleep(void);
bms_mode_t Runtime_GetMode(void);
int Runtime_FactoryReset(void);
int Runtime_ReenterFactoryMode(void);

#endif
