#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"

#include "sh3673520_port.h"
#include "sh3673520_reg.h"

#define SH3673520_PORT_BYTE_TIMEOUT_US      1000UL
#define SH3673520_PORT_CS_SETUP_US          5UL
#define SH3673520_PORT_CS_HOLD_US           5UL
#define SH3673520_PORT_CS_HIGH_GAP_US       5UL

/* Telink B85 official formula: SCK = SYS_CLK / ((DivClock + 1) * 2). */
#if (CLOCK_SYS_CLOCK_HZ != 16000000UL)
#error "SH36735xx fixed 500 kHz SPI divider assumes a 16 MHz B85 system clock"
#endif
#if (SH3673520_PORT_SPI_CLOCK_HZ > SH3673520_SPI_MAX_CLOCK_HZ)
#error "SH36735xx SPI clock exceeds the AFE 1 MHz maximum"
#endif
#define SH3673520_PORT_SPI_DIVIDER          15u
#define SH3673520_PORT_TICKS_PER_US         (CLOCK_SYS_CLOCK_HZ / 1000000UL)

#if ((CLOCK_SYS_CLOCK_HZ / ((SH3673520_PORT_SPI_DIVIDER + 1UL) * 2UL)) != SH3673520_PORT_SPI_CLOCK_HZ)
#error "SH36735xx SPI divider does not produce the configured 500 kHz clock"
#endif

typedef struct {
    uint8_t configured;
    uint8_t initialized;
    sh3673520_spi_group_t group;
    GPIO_PinTypeDef cs_pin;
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

sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t group)
{
    SPI_GPIO_GroupTypeDef sdk_group;
    GPIO_PinTypeDef cs_pin;
    sh3673520_port_status_t status;

    status = sh3673520_port_select_group(group, &sdk_group, &cs_pin);
    if (status != SH3673520_PORT_OK) {
        return status;
    }

    (void)sdk_group;
    s_port.group = group;
    s_port.cs_pin = cs_pin;
    s_port.configured = 1u;
    s_port.initialized = 0u;
    return SH3673520_PORT_OK;
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

    /* Official Telink B85 hardware-SPI setup. */
    spi_master_gpio_set(sdk_group);
    spi_master_init(SH3673520_PORT_SPI_DIVIDER, SPI_MODE3);

    /*
     * The SDK pin-group helper configures SCLK/MOSI/MISO and its associated CS.
     * SH36735xx keeps CS low for the whole protocol frame, so the AFE driver
     * controls CS explicitly while using the hardware SPI shift engine.
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

    /* Full-duplex master-write mode: every transmitted byte also receives one. */
    reg_spi_ctrl &= ~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl &= ~FLD_SPI_RD;
    gpio_write(s_port.cs_pin, 0);
    sh3673520_port_delay_us(SH3673520_PORT_CS_SETUP_US);
    return SH3673520_PORT_OK;
}

sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx)
{
    unsigned int start_tick;
    const unsigned int timeout_ticks =
        (unsigned int)(SH3673520_PORT_BYTE_TIMEOUT_US * SH3673520_PORT_TICKS_PER_US);

    if (rx == 0) {
        return SH3673520_PORT_ERR_INVALID_PARAM;
    }
    if (s_port.initialized == 0u) {
        return SH3673520_PORT_ERR_NOT_CONFIGURED;
    }

    /*
     * This is the byte primitive used by the SH36735xx wire protocol:
     * writing reg_spi_data starts eight clocks, BUSY marks completion, and the
     * same register then holds the simultaneously received byte. Do not use the
     * SDK spi_read() helper here; its RD mode generates extra clock cycles.
     */
    start_tick = clock_time();
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
