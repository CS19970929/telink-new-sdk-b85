#pragma once
#include <stdint.h>
enum { GPIO_PC0, GPIO_PC1, AS_GPIO, PM_PIN_PULLUP_10K, I2C_GPIO_GROUP_C0C1 };
enum { FLD_I2C_CMD_START=1, FLD_I2C_CMD_ID=2, FLD_I2C_CMD_ADDR=4, FLD_I2C_CMD_STOP=8,
       FLD_I2C_CMD_DI=16, FLD_I2C_CMD_READ_ID=32, FLD_I2C_CMD_ACK=64, FLD_I2C_CMD_DO=128 };
enum { FLD_I2C_CMD_BUSY=1, FLD_I2C_NAK=2 };
extern uint8_t reg_i2c_id, reg_i2c_adr, reg_i2c_do;
uint8_t *test_i2c_command(void);
uint8_t test_i2c_status(void);
uint8_t test_i2c_data(void);
#define reg_i2c_ctrl (*test_i2c_command())
#define reg_i2c_status test_i2c_status()
#define reg_i2c_di test_i2c_data()
void i2c_gpio_set(unsigned group);
void i2c_master_init(unsigned addr,unsigned divider);
void reset_i2c_module(void);
void sleep_us(unsigned long us);
void gpio_set_output_en(unsigned pin,unsigned on);
void gpio_set_input_en(unsigned pin,unsigned on);
void gpio_set_func(unsigned pin,unsigned func);
void gpio_setup_up_down_resistor(unsigned pin,unsigned resistance);
void gpio_write(unsigned pin,unsigned level);
