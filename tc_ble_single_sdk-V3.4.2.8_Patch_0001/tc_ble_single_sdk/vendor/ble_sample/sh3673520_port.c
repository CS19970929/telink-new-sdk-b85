#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"

#include "sh3673520_port.h"
#include "sh3673520_reg.h"

#define SH3673520_PORT_BYTE_TIMEOUT_US      1000UL
#define SH3673520_PORT_CS_SETUP_US          5UL
#define SH3673520_PORT_CS_HOLD_US           5UL
#define SH3673520_PORT_CS_HIGH_GAP_US       5UL
#define SH3673520_PORT_SPI_DIV_MAX          127UL

#if ((CLOCK_SYS_CLOCK_HZ % 1000000UL) != 0UL)
#error "SH3673520 port timeout needs an integer CLOCK_SYS_CLOCK_HZ ticks-per-us value"
#endif

#define SH3673520_PORT_TICKS_PER_US         (CLOCK_SYS_CLOCK_HZ / 1000000UL)

typedef struct {
    uint8_t configured;
    uint8_t initialized;
    uint8_t divider;
    sh3673520_spi_group_t group;
    GPIO_PinTypeDef cs_pin;
    uint32_t actual_clock_hz;
} sh3673520_port_context_t;

static sh3673520_port_context_t s_port;

static sh3673520_port_status_t sh3673520_port_select_group(
    sh3673520_spi_group_t group,
    SPI_GPIO_GroupTypeDef *sdk_group,
    GPIO_PinTypeDef *cs_pin)
{
    if ((sdk_group == 0) || (cs_pin == 0)) {
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }

    switch (group) {
    case SH3673520_SPI_GROUP_A2_A3_A4_D6:
        *sdk_group = SPI_GPIO_GROUP_A2A3A4D6;
        *cs_pin = GPIO_PD6;
        return SH3673520_PORT_OK;

    case SH3673520_SPI_GROUP_B6_B7_D2_D7:
        *sdk_group = SPI_GPIO_GROUP_B6B7D2D7;
        *cs_pin = GPIO_PD2;
        return SH3673520_PORT_OK;

    default:
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }
}

sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t group,
                                                uint32_t requested_clock_hz)
{
    SPI_GPIO_GroupTypeDef sdk_group;
    GPIO_PinTypeDef cs_pin;
    uint32_t denominator;
    uint32_t divider_plus_one;
    uint32_t actual_clock_hz;
    sh3673520_port_status_t status;

    status = sh3673520_port_select_group(group, &sdk_group, &cs_pin);
    if (status != SH3673520_PORT_OK) {
        return status;
    }

    (void)sdk_group;

    if ((requested_clock_hz == 0u) ||
        (requested_clock_hz > SH3673520_SPI_MAX_CLOCK_HZ)) {
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }

    denominator = requested_clock_hz * 2u;
    divider_plus_one = ((uint32_t)CLOCK_SYS_CLOCK_HZ + denominator - 1u) / denominator;

    if ((divider_plus_one == 0u) ||
        (divider_plus_one > (SH3673520_PORT_SPI_DIV_MAX + 1u))) {
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }

    actual_clock_hz = (uint32_t)CLOCK_SYS_CLOCK_HZ / (divider_plus_one * 2u);
    if ((actual_clock_hz == 0u) ||
        (actual_clock_hz > requested_clock_hz) ||
        (actual_clock_hz > SH3673520_SPI_MAX_CLOCK_HZ)) {
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }

    s_port.group = group;
    s_port.cs_pin = cs_pin;
    s_port.divider = (uint8_t)(divider_plus_one - 1u);
    s_port.actual_clock_hz = actual_clock_hz;
    s_port.configured = 1u;
    s_port.initialized = 0u;

    return SH3673520_PORT_OK;
}

uint32_t SH3673520_PortGetActualClockHz(void)
{
    return s_port.actual_clock_hz;
}

sh3673520_port_status_t sh3673520_port_init(void)
{
    SPI_GPIO_GroupTypeDef sdk_group;
    GPIO_PinTypeDef cs_pin;
    sh3673520_port_status_t status;

    if (s_port.configured == 0u) {
        return SH3673520_PORT_ERR_NOT_CONFIGURED;
    }

    status = sh3673520_port_select_group(s_port.group, &sdk_group, &cs_pin);
    if (status != SH3673520_PORT_OK) {
        return status;
    }

    spi_master_gpio_set(sdk_group);
    spi_master_init(s_port.divider, SPI_MODE3);

    /*
     * Use hardware SPI for SCLK/MOSI/MISO and software-controlled CS for a
     * complete SH3673520 transaction. The vendor GPIO group supplies the
     * official B85 CS pin associated with that selected group.
     */
    s_port.cs_pin = cs_pin;
    gpio_write(s_port.cs_pin, 1);
    reg_spi_ctrl &= ~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl &= ~FLD_SPI_RD;

    s_port.initialized = 1u;
    sh3673520_port_delay_us(SH3673520_PORT_CS_HIGH_GAP_US);

    return SH3673520_PORT_OK;
}

sh3673520_port_status_t sh3673520_port_begin(void)
{
    if (s_port.initialized == 0u) {
        return SH3673520_PORT_ERR_NOT_CONFIGURED;
    }

    reg_spi_ctrl &= ~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl &= ~FLD_SPI_RD;
    gpio_write(s_port.cs_pin, 0);
    sh3673520_port_delay_us(SH3673520_PORT_CS_SETUP_US);

    return SH3673520_PORT_OK;
}

sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx)
{
    unsigned int start_tick;
    unsigned int timeout_ticks;

    if (rx == 0) {
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }
    if (s_port.initialized == 0u) {
        return SH3673520_PORT_ERR_NOT_CONFIGURED;
    }

    /*
     * B85 standard SPI is full duplex in master write mode. Writing
     * reg_spi_data starts an 8-clock transfer; after BUSY clears the same
     * register contains the simultaneously received byte. Keep FLD_SPI_RD
     * clear here: in the vendor driver's read mode, reading reg_spi_data
     * itself launches another 8 clocks, which would break SH3673520 framing.
     */
    reg_spi_ctrl &= ~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl &= ~FLD_SPI_RD;

    start_tick = clock_time();
    timeout_ticks = (unsigned int)(SH3673520_PORT_BYTE_TIMEOUT_US *
                                   SH3673520_PORT_TICKS_PER_US);

    reg_spi_data = tx;
    while ((reg_spi_ctrl & FLD_SPI_BUSY) != 0u) {
        if ((unsigned int)(clock_time() - start_tick) >= timeout_ticks) {
            return SH3673520_PORT_ERR_TIMEOUT;
        }
    }

    *rx = reg_spi_data;
    return SH3673520_PORT_OK;
}

void sh3673520_port_end(void)
{
    if (s_port.initialized != 0u) {
        sh3673520_port_delay_us(SH3673520_PORT_CS_HOLD_US);
        gpio_write(s_port.cs_pin, 1);
        sh3673520_port_delay_us(SH3673520_PORT_CS_HIGH_GAP_US);
    }
}

sh3673520_port_status_t sh3673520_port_recover(void)
{
    if (s_port.configured == 0u) {
        return SH3673520_PORT_ERR_NOT_CONFIGURED;
    }

    s_port.initialized = 0u;
    reset_spi_module();
    return sh3673520_port_init();
}

void sh3673520_port_delay_us(uint32_t us)
{
    while (us != 0u) {
        uint32_t chunk = (us > 60000u) ? 60000u : us;
        sleep_us((unsigned int)chunk);
        us -= chunk;
    }
}

void sh3673520_port_delay_ms(uint32_t ms)
{
    while (ms != 0u) {
        uint32_t chunk = (ms > 60u) ? 60u : ms;
        sleep_us((unsigned int)(chunk * 1000u));
        ms -= chunk;
    }
}
