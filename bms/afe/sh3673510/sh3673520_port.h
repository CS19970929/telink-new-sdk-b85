/*
 * 文件功能：SH36735xx 的 TLSR8251 SPI 端口；封装片选、字节交换、时序与总线恢复。
 * bms/afe/sh3673510/sh3673520_port.h；实际编译归属见各产品 sources.txt。
 */
#ifndef SH3673520_PORT_H
#define SH3673520_PORT_H

#include <stdint.h>

typedef enum {
    SH3673520_PORT_OK = 0,
    SH3673520_PORT_ERR_INVALID_PARAM = -1,
    SH3673520_PORT_ERR_NOT_CONFIGURED = -2,
    SH3673520_PORT_ERR_TIMEOUT = -3,
    SH3673520_PORT_ERR_SPI = -4
} sh3673520_port_status_t;

/*
 * SH36735xx SPI 上限 1 MHz；此端口固定 500 kHz，从 B85 16 MHz 系统时钟精确分频，
 * 满足时序裕量。应用不选择分频器或请求速率。
 */
#define SH3673520_PORT_SPI_CLOCK_HZ              500000UL

/* Telink B85 硬件 SPI 引脚组由 Vendor SDK 定义。 */
typedef enum {
    SH3673520_SPI_GROUP_A2_A3_A4_D6 = 0,
    SH3673520_SPI_GROUP_B6_B7_D2_D7 = 1
} sh3673520_spi_group_t;

/* 板级集成仅选择实际 SPI 引脚组。 */
sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t group);

/* 驱动内部字节与完整事务原语。 */
sh3673520_port_status_t sh3673520_port_init(void);
/* 拉低片选以开始 AFE SPI 事务。 */
sh3673520_port_status_t sh3673520_port_begin(void);
/* 交换一个 SPI 字节并返回端口状态。 */
sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx);
/* 释放片选以结束 SPI 事务。 */
void sh3673520_port_end(void);
/* 复位并重新初始化 SPI 端口。 */
sh3673520_port_status_t sh3673520_port_recover(void);
/* 提供 SPI 器件所需的微秒等待。 */
void sh3673520_port_delay_us(uint32_t us);
/* 提供 SPI 器件所需的毫秒等待。 */
void sh3673520_port_delay_ms(uint32_t ms);

#endif /* 头文件保护：SH3673520_PORT_H。 */
