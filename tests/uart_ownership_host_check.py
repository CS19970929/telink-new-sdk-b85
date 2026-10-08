"""Run the actual UART port with a controlled DMA/IRQ model; no board timing claim."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile
from project_paths import selected_source

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8; typedef uint32_t u32;
#define _attribute_data_retention_
#define BMS_LOG(...) do {} while(0)
#define FLD_DMA_CHN_UART_RX 1
#define FLD_DMA_CHN_UART_TX 2
#define FLD_IRQ_DMA_EN 1
#define AS_GPIO 1
#define PARITY_NONE 0
#define STOP_BIT_ONE 1
#define MODBUS_RTU_FRAME_CAPACITY 268
static u32 now; static int busy,de,channels,irqsrc,resets,sends;
static u32 clock_time(void){return now;}
static int clock_time_exceed(u32 t,u32 us){return (u32)(now-t)>us*16u;}
static void gpio_write(int p,int v){(void)p;de=v;}
static void gpio_set_func(int p,int v){(void)p;(void)v;}
static void gpio_set_input_en(int p,int v){(void)p;(void)v;}
static void gpio_set_output_en(int p,int v){(void)p;(void)v;}
static void uart_gpio_set(int a,int b){(void)a;(void)b;}
static int uart_tx_is_busy(void){return busy;}
static void uart_reset(void){busy=0;++resets;}
static void uart_ndma_clear_tx_index(void){}
static void uart_ndma_clear_rx_index(void){}
static void uart_init(int a,int b,int c,int d){assert(a==TEST_UART_DIVIDER&&b==TEST_UART_BWPC&&c==0&&d==1);}
static void uart_dma_enable(int a,int b){(void)a;(void)b;}
static void uart_irq_enable(int a,int b){assert(!a&&!b);}
static void uart_recbuff_init(u8*p,unsigned n){assert(p&&n==272);channels|=1;}
static void uart_send_dma(u8*p){assert(p);++sends;busy=1;}
static void irq_set_mask(int m){(void)m;}
static void irq_enable(void){}
static u8 irq_disable(void){return 1;}
static void irq_restore(u8 s){assert(s==1);}
static int dma_chn_irq_status_get(void){return irqsrc;}
static void dma_chn_irq_status_clr(int m){irqsrc&=~m;}
static void dma_chn_irq_enable(int m,int e){(void)m;(void)e;}
static void dma_chn_enable(int m,int e){if(e)channels|=m;else channels&=~m;}
static int uart_is_parity_error(void){return 0;}
static void uart_clear_parity_error(void){}
static void bus_mux_on_uart_rx_byte(void){}
static int modbus_on_frame(const u8*p,u32 n,u8*r,u32*l){(void)p;(void)n;(void)r;*l=0;return 0;}
u8 modbus_uart_send(const u8*,u32);
'''
TAIL = r'''
static void receive(u8 value,u32 length){
 if(channels&1){memset(s_rx_pkt.data,value,sizeof(s_rx_pkt.data));s_rx_pkt.dma_len=length;irqsrc|=1;modbus_uart_irq_proc();}
}
int main(void){
 u8*p,*saved;u32 n;u8 data[268];memset(data,0x55,sizeof(data));
 modbus_uart_init();receive(0x11,8);assert(!(channels&1));
 assert(modbus_uart_poll(&p,&n)&&n==8);saved=p;
 receive(0x22,8);assert(saved[0]==0x11); // frozen consumer buffer
 modbus_uart_rx_reset();receive(0x33,268);assert(modbus_uart_poll(&p,&n)&&n==268&&p[267]==0x33);
 modbus_uart_rx_reset();receive(0x44,269);assert(!modbus_uart_poll(&p,&n)&&(channels&1));
 receive(0x44,0);assert(!modbus_uart_poll(&p,&n)&&(channels&1));
 assert(!modbus_uart_send(0,8)&&!modbus_uart_send(data,0)&&!modbus_uart_send(data,269));
 assert(sends==0);assert(modbus_uart_send(data,268));
 memset(data,0x66,sizeof(data));assert(!modbus_uart_send(data,8));
 assert(sends==1&&s_tx_pkt.dma_len==268&&s_tx_pkt.data[0]==0x55);
#if BMS_PRODUCT_RS485_ENABLE
 assert(de);irqsrc|=2;modbus_uart_irq_proc();assert(de);
 busy=0;now+=1000u*16u;main_loop_modbus();assert(de); // minimum full-frame hold
#if TEST_UART_BAUD_RATE != 115200
 now+=50000u*16u;main_loop_modbus();assert(de&&g_bms_rs485_tx_diag.tx_timeout_count==0);
 now+=(TEST_RS485_FRAME_END_US-51000u)*16u;
#else
 now+=(TEST_RS485_FRAME_END_US-1000u)*16u;
#endif
 main_loop_modbus();assert(!de&&!modbus_uart_tx_active());
 assert(g_bms_rs485_tx_diag.tx_complete_count==1);
 now=UINT32_MAX-100;assert(modbus_uart_send(data,8));int previous=resets;
 now+=(TEST_RS485_TIMEOUT_US+1u)*16u;main_loop_modbus(); // lost DMA IRQ and forever-busy UART
 assert(!de&&!modbus_uart_tx_active()&&!busy&&resets==previous+1);
 assert(g_bms_rs485_tx_diag.tx_timeout_count==1&&g_bms_rs485_tx_diag.tx_complete_count==1);
 assert(channels&1);assert(modbus_uart_send(data,8));
#endif
 return 0;
}
'''

products = (os.environ['BMS_PRODUCT'],) if 'BMS_PRODUCT' in os.environ else ('d008', 'd011', 'd013', 'd014')
for product in products:
    source = selected_source(ROOT / 'bms/platform/telink/modbus_uart.c', product)
    source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
    header = (ROOT / 'bms/platform/telink/modbus_uart.h').read_text(encoding='utf8')
    diag = re.search(r'typedef struct \{[\s\S]*?\} bms_rs485_tx_diag_t;', header)[0]
    defines = ('#define BMS_PRODUCT_RS485_ENABLE %d\n#define BMS_RS485_TX_DIAG_ENABLE 0\n'
               '#define BMS_DEBUG_LOG_ENABLE 0\n#define CLOCK_SYS_CLOCK_HZ 16000000u\n'
               '#define BMS_BOARD_OWC_TX_PIN 1\n#define BMS_BOARD_OWC_RX_PIN 2\n'
               '#define BMS_BOARD_RS485_EN_PIN 3\n'
               '#define BMS_BOARD_SCI1_TX_PIN 1\n#define BMS_BOARD_SCI1_RX_PIN 2\n') % (product in ('d011', 'd014'))
    with tempfile.TemporaryDirectory(prefix='uart-ownership-') as folder:
        c = Path(folder) / 'check.c'; exe = Path(folder) / 'check.exe'
        product_header = (ROOT / 'bms/products' / product / 'bms_product.h').read_text(encoding='utf8')
        baud_pattern = r'^#define\s+BMS_PRODUCT_UART_BAUD_RATE\s+(\d+)u?\s*$'
        baud_match = re.search(baud_pattern, product_header, re.M)
        if baud_match is None:
            baud_match = re.search(baud_pattern,
                (ROOT / 'bms/platform/telink/modbus_uart.c').read_text(encoding='utf8'), re.M)
        baud_rate = int(baud_match[1])
        # SDK 16 MHz 配置表与独立的最长帧时间验收点。
        divider, bwpc, timeout_us, frame_end_us = {
            9600: (118, 13, 350000, 281000),
            19200: (118, 6, 200000, 141000),
            115200: (9, 13, 50000, 25000),
        }[baud_rate]
        expected = ('#define TEST_UART_BAUD_RATE %du\n#define TEST_UART_DIVIDER %du\n'
                    '#define TEST_UART_BWPC %du\n#define TEST_RS485_TIMEOUT_US %du\n'
                    '#define TEST_RS485_FRAME_END_US %du\n') % (
                        baud_rate, divider, bwpc, timeout_us, frame_end_us)
        c.write_text(expected + PREFIX + defines + diag + source + TAIL, encoding='utf8')
        subprocess.run(shlex.split(os.environ.get('CC','cc')) + ['-std=c99','-Wall','-Wextra','-Werror',
                        '-Wno-unused-function',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS '+product+' UART RX ownership, bounds, busy rejection and bounded RS485 abort')
