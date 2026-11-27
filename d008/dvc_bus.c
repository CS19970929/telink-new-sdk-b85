#include "tl_common.h"
#include "drivers.h"
#include "dvc_bus.h"

enum { WRITE_ADDR = 0x40, READ_ADDR = 0x41, MAX_BURST = 16,
       AFE_ENABLE_SETTLE_US = 20000, SDA_WAKE_US = 1000, BUS_FREE_US = 5,
       WAKE_RETRY_INTERVAL_US = 5000, WAKE_RETRY_WINDOW_US = 120000 };
static uint8_t crc_byte(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 7) : (uint8_t)(crc << 1);
    return crc;
}

void dvc_bus_init(void)
{
    i2c_gpio_set(I2C_GPIO_GROUP_C0C1);
    i2c_master_init(WRITE_ADDR, CLOCK_SYS_CLOCK_HZ / (4u * 100000u));
}

/* PD7 已启用 AFE 接口。沿用旧 D008 的 SDA 唤醒脉冲，只复位 MCU I²C 外设。 */
static void wake_shutdown(void)
{
    sleep_us(AFE_ENABLE_SETTLE_US);
    reset_i2c_module();
    gpio_set_output_en(GPIO_PC1,0);
    gpio_set_func(GPIO_PC1,AS_GPIO);
    gpio_set_input_en(GPIO_PC1,1);
    gpio_setup_up_down_resistor(GPIO_PC1,PM_PIN_PULLUP_10K);

    gpio_set_output_en(GPIO_PC0,0);
    gpio_write(GPIO_PC0,0);
    gpio_set_func(GPIO_PC0,AS_GPIO);
    gpio_setup_up_down_resistor(GPIO_PC0,PM_PIN_PULLUP_10K);
    gpio_set_input_en(GPIO_PC0,1);
    gpio_set_output_en(GPIO_PC0,1);
    sleep_us(SDA_WAKE_US);
    gpio_set_output_en(GPIO_PC0,0);
    sleep_us(BUS_FREE_US);
    dvc_bus_init();
}

/* 以整笔事务为界，16 个数据 + 16 个 CRC 在 100 kHz 约 3.3 ms。 */
static bool command(uint8_t cmd, bool expect_ack, uint32_t start)
{
    reg_i2c_ctrl = cmd;
    while (reg_i2c_status & FLD_I2C_CMD_BUSY) {
        if (clock_time_exceed(start, 6000)) return false;
    }
    return !clock_time_exceed(start, 6000) && (!expect_ack || !(reg_i2c_status & FLD_I2C_NAK));
}

static bool transfer(uint8_t reg, const uint8_t *tx, uint8_t *rx, uint8_t n, bool *no_reply)
{
    if (!n || n > MAX_BURST || (unsigned)reg+n > 0x91u) return false;
    uint32_t start = clock_time();
    uint8_t crc = crc_byte(crc_byte(0, WRITE_ADDR), reg);
    reg_i2c_id = WRITE_ADDR;
    reg_i2c_adr = reg;
    bool ok = command(FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR, true, start);
    if (no_reply) *no_reply=!ok;
    if (ok && rx) {
        reg_i2c_id = READ_ADDR;
        ok = command(FLD_I2C_CMD_START | FLD_I2C_CMD_ID, true, start);
        crc = crc_byte(crc, READ_ADDR);
    }
    for (uint8_t i = 0; ok && i < n; ++i) {
        if (rx) {
            ok = command(FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID, false, start);
            if (!ok) break;
            rx[i] = reg_i2c_di;
            crc = crc_byte(crc, rx[i]);
            uint8_t last = i+1 == n ? FLD_I2C_CMD_ACK : 0;
            ok = command(FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID | last, false, start)
                && reg_i2c_di == crc;
        } else {
            reg_i2c_do = tx[i];
            ok = command(FLD_I2C_CMD_DO, true, start);
            if (!ok) break;
            reg_i2c_do = crc_byte(crc, tx[i]);
            ok = command(FLD_I2C_CMD_DO, true, start);
        }
        crc = 0;
    }
    /* 出错仍尝试 STOP。下一轮由上层决定是否继续或进入静默。 */
    bool stopped = command(FLD_I2C_CMD_STOP, false, clock_time());
    return ok && stopped;
}

bool dvc_read(uint8_t reg, uint8_t *data, uint8_t length)
{
    return data && transfer(reg, 0, data, length, 0);
}

bool dvc_write(uint8_t reg, const uint8_t *data, uint8_t length)
{
    return data && transfer(reg, data, 0, length, 0);
}

static bool matches(uint8_t reg, uint8_t value, uint8_t mask)
{
    uint8_t readback;
    return dvc_read(reg,&readback,1) && ((readback^value)&mask)==0;
}

bool dvc_set(uint8_t reg, uint8_t value, uint8_t mask)
{
    return dvc_write(reg,&value,1) && matches(reg,value,mask);
}

DvcStartupResult dvc_startup_off(DvcStartup *startup)
{
    const uint8_t off=0;
    if (startup->retrying) {
        if (clock_time_exceed(startup->wake_tick,WAKE_RETRY_WINDOW_US-1u)) return DVC_STARTUP_ERROR;
        if (!clock_time_exceed(startup->last_try_tick,WAKE_RETRY_INTERVAL_US-1u)) return DVC_STARTUP_WAIT;
    }

    bool no_reply=false;
    bool written=transfer(0x51,&off,0,1,&no_reply);
    if (!written && no_reply && !startup->retrying) {
        wake_shutdown();
        startup->retrying=true;
        startup->wake_tick=clock_time();
        written=transfer(0x51,&off,0,1,&no_reply);
    }
    if (written) return matches(0x51,off,0xff) ? DVC_STARTUP_OK : DVC_STARTUP_ERROR;
    /* 只等待启动寻址。数据或读回失败仍立即上报。 */
    if (!no_reply) return DVC_STARTUP_ERROR;
    startup->last_try_tick=clock_time();
    return DVC_STARTUP_WAIT;
}
