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
 * SH36735xx SPI supports up to 1 MHz. This port deliberately uses a fixed
 * 500 kHz clock: comfortably inside the AFE timing limit and exactly derived
 * from the B85 16 MHz system clock. Application code does not choose a
 * divider or requested SPI rate.
 */
#define SH3673520_PORT_SPI_CLOCK_HZ              500000UL

/* Telink B85 hardware-SPI pin groups defined by the vendor SDK. */
typedef enum {
    SH3673520_SPI_GROUP_A2_A3_A4_D6 = 0,
    SH3673520_SPI_GROUP_B6_B7_D2_D7 = 1
} sh3673520_spi_group_t;

/* Board integration selects only the physical SPI pin group. */
sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t group);

/* Driver-internal byte/full-transaction primitives. */
sh3673520_port_status_t sh3673520_port_init(void);
sh3673520_port_status_t sh3673520_port_begin(void);
sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx);
void sh3673520_port_end(void);
sh3673520_port_status_t sh3673520_port_recover(void);
void sh3673520_port_delay_us(uint32_t us);
void sh3673520_port_delay_ms(uint32_t ms);

#endif /* SH3673520_PORT_H */
