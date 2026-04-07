# STM32 驱动分层与接口模板

本文件用于在 `STM32 + BMS` 项目中快速产出稳定的驱动边界、接口和目录结构。

## 推荐目录

```text
bsp/
  bsp_board.h
  bsp_board.c
  bsp_clock.h
  bsp_clock.c
  bsp_gpio.h
  bsp_gpio.c

drivers/
  drv_adc.h
  drv_adc.c
  drv_i2c.h
  drv_i2c.c
  drv_spi.h
  drv_spi.c
  drv_uart.h
  drv_uart.c
  drv_can.h
  drv_can.c
  drv_rtc.h
  drv_rtc.c
  drv_wdg.h
  drv_wdg.c

devices/
  dev_afe.h
  dev_afe.c
  dev_eeprom.h
  dev_eeprom.c
  dev_temp_ntc.h
  dev_temp_ntc.c
```

## 分层边界

- `bsp` 负责板级引脚、时钟、供电控制、通道映射
- `drivers` 负责 MCU 外设抽象，不感知 BMS 业务
- `devices` 负责具体器件协议和寄存器，不处理整机状态机

## 驱动接口模板

### `drv_adc.h`

```c
typedef enum {
    DRV_ADC_CH_PACK_VOLT,
    DRV_ADC_CH_PACK_CURR,
    DRV_ADC_CH_NTC_1,
    DRV_ADC_CH_NTC_2,
    DRV_ADC_CH_MAX,
} drv_adc_channel_t;

typedef struct {
    uint16_t raw[DRV_ADC_CH_MAX];
    uint8_t sample_ready;
} drv_adc_frame_t;

int drv_adc_init(void);
int drv_adc_start_dma(void);
int drv_adc_stop(void);
int drv_adc_get_frame(drv_adc_frame_t *frame);
void drv_adc_dma_irq_handler(void);
```

### `dev_afe.h`

```c
typedef struct {
    uint16_t cell_mv[16];
    int16_t pack_current_ma;
    int16_t temp_ddegc[8];
} dev_afe_data_t;

int dev_afe_init(void);
int dev_afe_read_all(dev_afe_data_t *data);
int dev_afe_clear_fault(void);
int dev_afe_set_balance_mask(uint16_t mask);
```

## 推荐初始化顺序

```text
1. bsp_board_init()
2. bsp_clock_init()
3. bsp_gpio_init()
4. drv_rtc_init()
5. drv_adc_init()
6. drv_i2c_init() / drv_spi_init()
7. dev_afe_init()
8. param_mgr_init()
9. bms_core_init()
10. power_mgr_init()
```

## 设计约束

- 驱动不得直接调用 `bms_core`
- 驱动回调只上报事件，不切状态
- `ADC/DMA` 原始值和工程值必须分离
- 外设 `init` 允许重复调用，便于唤醒恢复
- 所有驱动需要统一错误码

## 建议的错误码风格

```c
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_PARAM = -1,
    ERR_TIMEOUT = -2,
    ERR_BUSY = -3,
    ERR_HW_FAIL = -4,
    ERR_NOT_READY = -5,
} err_t;
```
