/*
 * 文件功能：把 Telink SDK 32K 时钟提供给公共业务计时接口。
 * bms/platform/telink/bms_time.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_time.h"
#include "tl_common.h"
#include "drivers.h"

uint32_t bms_time_now_32k(void)
{
    return pm_get_32k_tick();
}
