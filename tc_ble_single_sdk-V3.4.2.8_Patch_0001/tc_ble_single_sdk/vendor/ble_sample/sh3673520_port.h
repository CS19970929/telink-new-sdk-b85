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
 * These are Telink B85 hardware-SPI pin groups defined by the vendor SDK.
 * Selecting a group is a board-level decision. No group is selected by default.
 */
typedef enum {
    SH3673520_SPI_GROUP_A2_A3_A4_D6 = 0,
    SH3673520_SPI_GROUP_B6_B7_D2_D7 = 1
} sh3673520_spi_group_t;

/*
 * Board integration must call this before SH3673520_Init().
 * Requested clock must be <= the SH3673520 1 MHz maximum.
 */
sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t group,
                                                uint32_t requested_clock_hz);

uint32_t SH3673520_PortGetActualClockHz(void);

/* Driver-internal port contract. Exposed for host mocks and static testing. */
sh3673520_port_status_t sh3673520_port_init(void);
sh3673520_port_status_t sh3673520_port_begin(void);
sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx);
void sh3673520_port_end(void);
sh3673520_port_status_t sh3673520_port_recover(void);
void sh3673520_port_delay_us(uint32_t us);
void sh3673520_port_delay_ms(uint32_t ms);

#endif /* SH3673520_PORT_H */
