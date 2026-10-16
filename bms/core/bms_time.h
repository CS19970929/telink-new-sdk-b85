/*
 * 文件功能：公共业务使用的 32K 系统时钟接口；诊断不得拥有安全计时源。
 * bms/core/bms_time.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_TIME_H_
#define BMS_TIME_H_

#include <stdint.h>

/* 返回平台 32000 Hz 业务时钟的 32 位自然回绕值。 */
uint32_t bms_time_now_32k(void);

#endif /* BMS_TIME_H_ */
