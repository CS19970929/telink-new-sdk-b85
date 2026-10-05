/*
 * 文件功能：SIF 单线输出及定时器中断状态；保持既有波形节拍，
 * 不在 ISR 中加入日志发送。
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

/* 处理 SIF 包发送状态；未启用产品保留空入口。 */
void sif_send_data_handle(void);
/* 初始化 SIF 波形使用的硬件定时器。 */
void sif_timer_init(void);
/* 按已准备数据推进单线位波形与结束阶段。 */
void sif_timer_irq_handler(void);

/* 主循环生成包；IRQ 不访问报告、不组装包。 */
/* 启用 SIF 时重建待发送包并发布 ready；关闭时为空入口。 */
void sif_prepare_task(uint32_t sample_tick);

#endif
