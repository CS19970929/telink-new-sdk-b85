#include "tl_common.h"
#include "drivers.h"
#include "dvc_bus.h"

enum { WRITE_ADDR = 0x40, READ_ADDR = 0x41, MAX_BURST = 16 };
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

/* 以整笔事务为界，16 个数据 + 16 个 CRC 在 100 kHz 约 3.3 ms。 */
static bool command(uint8_t cmd, bool expect_ack, uint32_t start)
{
    reg_i2c_ctrl = cmd;
    while (reg_i2c_status & FLD_I2C_CMD_BUSY) {
        if (clock_time_exceed(start, 6000)) return false;
    }
    return !clock_time_exceed(start, 6000) && (!expect_ack || !(reg_i2c_status & FLD_I2C_NAK));
}

static bool transfer(uint8_t reg, const uint8_t *tx, uint8_t *rx, uint8_t n)
{
    if (!n || n > MAX_BURST || (unsigned)reg+n > 0x91u) return false;
    uint32_t start = clock_time();
    uint8_t crc = crc_byte(crc_byte(0, WRITE_ADDR), reg);
    reg_i2c_id = WRITE_ADDR;
    reg_i2c_adr = reg;
    bool ok = command(FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR, true, start);
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
    return data && transfer(reg, 0, data, length);
}

bool dvc_write(uint8_t reg, const uint8_t *data, uint8_t length)
{
    return data && transfer(reg, data, 0, length);
}

bool dvc_set(uint8_t reg, uint8_t value, uint8_t mask)
{
    uint8_t readback;
    return dvc_write(reg, &value, 1) && dvc_read(reg, &readback, 1)
        && ((readback ^ value) & mask) == 0;
}
