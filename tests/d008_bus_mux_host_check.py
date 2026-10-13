"""Exercise the production D008 bus mux with GPIO/IRQ/UART port substitutes."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'bms/platform/telink/bus_mux.c').read_text(encoding='utf8')
source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
# 模型寄存器没有 W1C 语义，仅替换清标志操作。
source = source.replace('reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;',
                        'reg_irq_src &= ~FLD_IRQ_GPIO_RISC0_EN;')
header = (ROOT / 'bms/platform/telink/bus_mux.h').read_text(encoding='utf8')
state = re.search(r'typedef enum \{[\s\S]*?\} bus_state_t;', header)[0]
prefix = r'''
#include <stdint.h>
#include <assert.h>
#define _attribute_ram_code_
#define BMS_BOARD_OWC_RX_PIN 3
#define BMS_BOARD_OWC_TX_PIN 2
#define AS_GPIO 0
#define PM_PIN_PULLDOWN_100K 0
#define PM_PIN_UP_DOWN_FLOAT 0
#define POL_FALLING 1
#define FLD_IRQ_GPIO_RISC0_EN 8u
static uint32_t now=16u,reg_irq_src;
static uint8_t irq_enabled=1u;
static int rx_high,edge_enabled,tx_output,uart_enabled,uart_busy,rx_pending;
static unsigned uart_starts,uart_stops;
static uint32_t clock_time(void){return now;}
static int clock_time_exceed(uint32_t t,uint32_t us){return (uint32_t)(now-t)>us*16u;}
static uint8_t irq_disable(void){uint8_t s=irq_enabled;irq_enabled=0u;return s;}
static void irq_restore(uint8_t s){irq_enabled=s;}
static int gpio_read(int p){assert(p==3);return rx_high;}
static void gpio_set_func(int p,int f){(void)p;(void)f;}
static void gpio_set_input_en(int p,int e){(void)p;(void)e;}
static void gpio_set_output_en(int p,int e){if(p==2)tx_output=e;}
static void gpio_write(int p,int v){assert(p==2);(void)v;}
static void gpio_setup_up_down_resistor(int p,int r){(void)p;(void)r;}
static void gpio_en_interrupt_risc0(int p,int e){assert(p==3);edge_enabled=e;}
static void gpio_set_interrupt_risc0(int p,int polarity){
 assert(polarity==POL_FALLING);reg_irq_src&=~8u;gpio_en_interrupt_risc0(p,1);
}
static void modbus_uart_disable(void){
 assert(!irq_enabled);uart_enabled=0;rx_pending=0;uart_busy=0;++uart_stops;
}
static int modbus_uart_bus_idle(void){assert(!irq_enabled);return !uart_busy&&!rx_pending&&rx_high;}
static void modbus_uart_init(void){assert(!irq_enabled);uart_enabled=1;++uart_starts;}
'''
tail = r'''
static void fall(uint32_t after_us){
 now+=after_us*16u;
 if(edge_enabled){reg_irq_src|=8u;bus_mux_irq_handler();assert(!(reg_irq_src&8u));}
}
static void connect_owc(void){
 rx_high=1;bus_mux_task();now+=50001u*16u;bus_mux_task();
 assert(bus_mux_get_state()==BUS_STATE_OWC_TX&&tx_output&&edge_enabled);
}
static void uart_burst(void){fall(100u);fall(100u);fall(100u);}
int main(void){
 bus_mux_init();assert(bus_mux_get_state()==BUS_STATE_OWC_IDLE&&!uart_enabled&&!tx_output);
 assert(irq_enabled&&edge_enabled&&uart_stops==1u);
 // 上电直接接串口：首轮触发，主循环初始化，延迟请求不能撤销 UART。
 uart_burst();assert(!edge_enabled&&uart_starts==0u);bus_mux_task();
 assert(bus_mux_get_state()==BUS_STATE_UART_MODBUS&&uart_enabled&&!edge_enabled);
 bus_mux_return_to_owc_idle();assert(bus_mux_get_state()==BUS_STATE_UART_MODBUS);
 now+=4999999u*16u;bus_mux_task();assert(uart_enabled);
 bus_mux_on_uart_rx_byte();now+=4999999u*16u;bus_mux_task();assert(uart_enabled);
 // 静默到期也不能截断 TX、待处理帧或 RX 低电平。
 now+=2u*16u;uart_busy=1;rx_high=1;bus_mux_task();assert(uart_enabled);
 uart_busy=0;rx_pending=1;bus_mux_task();assert(uart_enabled);
 rx_pending=0;rx_high=0;bus_mux_task();assert(uart_enabled);
 rx_high=1;bus_mux_task();assert(!uart_enabled&&!tx_output&&edge_enabled);
 connect_owc();
 // 连续一线通回波：最快每 2 ms 一个下降沿，不得误切串口。
 for(unsigned i=0;i<200u;++i){fall(2000u);bus_mux_task();assert(!uart_enabled);}
 // 从活动的一线通接管；确认后锁存，主循环晚到也不丢失请求。
 fall(3000u);uart_burst();now+=20000u*16u;bus_mux_task();
 assert(uart_enabled&&bus_mux_get_state()==BUS_STATE_UART_MODBUS);
 now+=5000001u*16u;rx_high=1;bus_mux_task();assert(!uart_enabled);connect_owc();
 // 真正断开后返回监听；孤立噪声过期，仍可重新接一线通。
 bus_mux_return_to_owc_idle();assert(!tx_output&&edge_enabled);
 fall(10u);now+=2501u*16u;bus_mux_task();
 now+=50001u*16u;bus_mux_task();assert(bus_mux_get_state()==BUS_STATE_OWC_TX);
 // 窗口外的三个边沿不能凑数；窗口边界内允许确认。
 bus_mux_return_to_owc_idle();fall(10u);fall(1300u);fall(1300u);
 bus_mux_task();assert(!uart_enabled);
 fall(1250u);fall(1250u);bus_mux_task();assert(uart_enabled);
 // tick 回绕不改变检测/静默回退；嵌套调用保留关闭的中断状态。
 irq_enabled=0u;now=UINT32_MAX-100u;bus_mux_on_uart_rx_byte();
 now+=5000001u*16u;bus_mux_task();assert(!uart_enabled&&!irq_enabled);
 now=UINT32_MAX-100u;uart_burst();bus_mux_task();assert(uart_enabled&&!irq_enabled);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='d008-bus-mux-') as folder:
    c = Path(folder) / 'check.c'
    exe = Path(folder) / 'check.exe'
    c.write_text(prefix + state + source + tail, encoding='utf8')
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                   ['-std=c99', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS D008 bus mux detection, self-echo rejection, ownership, idle return and tick wrap')
