/* 文件功能：SIF 单线输出及定时器中断状态；保持既有波形节拍，不在 ISR 中加入日志发送。
 * bms/platform/telink/sif_send.h；实际编译归属见各产品 sources.txt。
 */
#ifndef __SIF_SEND_H__
#define __SIF_SEND_H__

#include "stdint.h"
#include "conf.h"

typedef enum
{
    SIF_IDLE = 0,
    SYNC_SIGNAL,
    SEND_PUBLIC,
    SEND_DATA,
    SEND_DATA_COMPLETE,
    STOP_SIGNAL,
} SIF_STATE_E;

void sif_send_data_handle(void);
void sif_timer_init(void);
void sif_timer_irq_handler(void);

/* Main-loop packet producer; no report access or packet assembly in IRQ. */
void sif_prepare_task(uint32_t sample_tick);

#endif
