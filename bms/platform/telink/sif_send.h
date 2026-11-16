/*
 * 文件功能：SIF 单线输出及定时器中断状态；保持既有波形节拍，
 * 不在 ISR 中加入日志发送。
 * bms/platform/telink/sif_send.h；实际编译归属见各产品 sources.txt。
 */
#ifndef __SIF_SEND_H__
#define __SIF_SEND_H__

#include "stdint.h"
#include "common/types.h"

/* D008 一线通编译期协议选择（无需修改 bms_product.h 或 OTA 参数包）：
 * 1=A1.3.3（原有默认）；2=锂神 V1.1.1。
 * 仅改此宏，重新编译时不会把通信协议选择计入电池/保护默认参数摘要。 */
#define BMS_SIF_PROTOCOL_A133 1u
#define BMS_SIF_PROTOCOL_LISHEN_V111 2u
#ifndef BMS_PRODUCT_SIF_PROTOCOL
#define BMS_PRODUCT_SIF_PROTOCOL BMS_SIF_PROTOCOL_A133
#endif
#if BMS_PRODUCT_SIF_PROTOCOL != BMS_SIF_PROTOCOL_A133 && \
    BMS_PRODUCT_SIF_PROTOCOL != BMS_SIF_PROTOCOL_LISHEN_V111
#error "SIF protocol must be A133 or LISHEN_V111"
#endif

/* 本项目锂神扩展：保留字节0=int8电流(1A/bit)，80=缺测；
 * 保留字节4=C0|A1.3.3故障码。0保留标准V1.1.1的两个00。 */
#ifndef BMS_PRODUCT_SIF_LISHEN_EXTENDED
#define BMS_PRODUCT_SIF_LISHEN_EXTENDED 1u
#endif
#if BMS_PRODUCT_SIF_LISHEN_EXTENDED > 1u
#error "BMS_PRODUCT_SIF_LISHEN_EXTENDED must be 0 or 1"
#endif

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
/* 初始化波形定时器但保持停止，由总线所有者在真实发送期间启用。 */
void sif_timer_init(void);
/* bus_mux在关闭中断的所有权切换中调用；停止时同时清除旧波形状态。 */
void sif_set_bus_active(uint8_t active);
/* 按已准备数据推进单线位波形与结束阶段。 */
void sif_timer_irq_handler(void);

/* 主循环生成包；IRQ 不访问报告、不组装包。 */
/* 启用 SIF 时重建待发送包并发布 ready；关闭时为空入口。 */
void sif_prepare_task(uint32_t sample_tick);

#endif
