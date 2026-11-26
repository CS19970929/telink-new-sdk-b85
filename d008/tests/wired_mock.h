#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef uint8_t u8;
#define _attribute_ram_code_
enum { GPIO_PC2=2, GPIO_PC3=3, AS_GPIO, PM_PIN_PULLDOWN_100K, POL_FALLING,
       PARITY_NONE, STOP_BIT_ONE, Level_Low=0, Level_High=1, PM_WAKEUP_PAD=16 };
enum { FLD_TMR0_EN=1, FLD_IRQ_TMR0_EN=2, FLD_TMR_STA_TMR0=1,
       FLD_IRQ_GPIO_RISC0_EN=4, FLD_IRQ_DMA_EN=8, FLD_DMA_CHN_UART_RX=1, FLD_DMA_CHN_UART_TX=2 };
static unsigned reg_tmr_ctrl,reg_irq_mask,reg_tmr_sta,reg_irq_src,reg_tmr0_tick,reg_tmr0_capt;
static unsigned pin_rx,pin_tx,tx_enable,wake_pol,wake_enabled;
static uint32_t simulated_tick;
#define clock_time() simulated_tick
#define gpio_write(pin,level) ((void)(pin), (void)(pin_tx=(level)))
#define gpio_set_output_en(pin,on) ((void)((int)(pin)==GPIO_PC2 ? (tx_enable=(on)) : 0))
#define gpio_read(pin) ((void)(pin),pin_rx)
#define cpu_set_gpio_wakeup(pin,pol,on) ((void)(pin), (void)(wake_pol=(pol)),(void)(wake_enabled=(on)))
#define irq_disable() 0
#define irq_restore(token) ((void)(token))
#define gpio_en_interrupt_risc0(pin,on) ((void)0)
#define gpio_set_func(pin,func) ((void)0)
#define dma_chn_irq_enable(mask,on) ((void)0)
#define dma_chn_enable(mask,on) ((void)0)
#define uart_dma_enable(rx,tx) ((void)0)
#define uart_irq_enable(rx,tx) ((void)0)
#define dma_chn_irq_status_clr(mask) ((void)(mask))
#define gpio_set_input_en(pin,on) ((void)0)
#define gpio_setup_up_down_resistor(pin,resistor) ((void)0)
#define gpio_set_interrupt_risc0(pin,edge) ((void)0)
#define uart_recbuff_init(p,n) ((void)0)
#define uart_gpio_set(tx,rx) ((void)0)
#define uart_reset() ((void)0)
#define uart_init(a,b,c,d) ((void)0)
#define irq_set_mask(mask) ((void)0)
#define dma_chn_irq_status_get() 0
#define bls_pm_setWakeupSource(source) ((void)0)
#define uart_tx_is_busy() 0
#define uart_send_dma(packet) ((void)0)
