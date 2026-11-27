#ifdef D008_WIRED_HOST
#include "tests/wired_mock.h"
#else
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#endif
#include "registers.h"
#include "link.h"
#include "sif.h"

enum { RX=GPIO_PC3, TX=GPIO_PC2 };
typedef struct {uint32_t size; uint8_t data[268];} DmaPacket;
static DmaPacket rx __attribute__((aligned(4))), tx __attribute__((aligned(4)));
static RtuSession session;
static volatile bool uart_mode,rx_ready,transmitting;
static volatile unsigned edges;
static volatile uint32_t first_edge;
static bool high_seen,pad_armed;
static uint32_t last_uart_ms,last_frame_ms,last_cell_ms;
static volatile uint32_t frame_due;
enum { LISHEN_IDLE_TICKS=16000000 };
static unsigned startup,identity_count;
static uint8_t wave[64];
static volatile unsigned wave_size,position,phase_ticks;

static void stop_timer(void)
{
    reg_tmr_ctrl &= ~FLD_TMR0_EN; reg_irq_mask &= ~FLD_IRQ_TMR0_EN;
    reg_tmr_sta=FLD_TMR_STA_TMR0; transmitting=false;
}
void wired_stop(void)
{
    u8 irq=irq_disable();
    stop_timer(); gpio_en_interrupt_risc0(RX,0);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX|FLD_DMA_CHN_UART_TX,0);
    dma_chn_enable(FLD_DMA_CHN_UART_RX|FLD_DMA_CHN_UART_TX,0);
    uart_dma_enable(0,0); uart_irq_enable(0,0);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX|FLD_DMA_CHN_UART_TX);
    cpu_set_gpio_wakeup(RX,Level_High,0); pad_armed=false;
    gpio_set_func(TX,AS_GPIO); gpio_set_output_en(TX,0); gpio_write(TX,0);
    uart_mode=rx_ready=false; edges=0; irq_restore(irq);
}
void wired_init(void)
{
    wired_stop(); memset(&session,0,sizeof(session));
    gpio_set_func(RX,AS_GPIO); gpio_set_input_en(RX,1); gpio_set_output_en(RX,0);
    gpio_setup_up_down_resistor(RX,PM_PIN_PULLDOWN_100K);
    gpio_set_interrupt_risc0(RX,POL_FALLING); gpio_en_interrupt_risc0(RX,1);
    high_seen=false; startup=3; identity_count=0;
}
static void uart_enter(uint32_t ms)
{
    u8 irq=irq_disable(); stop_timer(); gpio_en_interrupt_risc0(RX,0);
    rx.size=0; uart_recbuff_init((u8 *)&rx,sizeof(rx));
    uart_gpio_set(TX,RX); uart_reset(); uart_init(9,13,PARITY_NONE,STOP_BIT_ONE);
    uart_dma_enable(1,1); uart_irq_enable(0,0);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX|FLD_DMA_CHN_UART_TX);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX|FLD_DMA_CHN_UART_TX,1);
    irq_set_mask(FLD_IRQ_DMA_EN); rx_ready=false; uart_mode=true; last_uart_ms=ms;
    irq_restore(irq);
}
/* 第 0 段为同步，剩余位置每次只处理一个位的一个半毫秒电平。 */
_attribute_ram_code_ static void wave_irq(void)
{
    unsigned duration,low;
    if (!position) {
        duration=D008_LISHEN?128:22; low=D008_LISHEN?124:20;
    } else if (position<=wave_size*8) {
        unsigned bit=position-1;
        unsigned set=(wave[bit/8]>>(D008_LISHEN?(7-bit%8):bit%8))&1;
        duration=D008_LISHEN?12:4; low=D008_LISHEN?(set?4:8):(set?1:3);
    } else {
        duration=D008_LISHEN?54:10; low=D008_LISHEN?50:10;
    }
    gpio_write(TX,phase_ticks>=low);
    if (++phase_ticks==duration) {
        phase_ticks=0;
        if (++position>wave_size*8+1) {
            stop_timer(); gpio_write(TX,1);
            if (D008_LISHEN) frame_due=clock_time()+LISHEN_IDLE_TICKS;
            else gpio_set_output_en(TX,0);
        }
    }
}
_attribute_ram_code_ void wired_irq(void)
{
    if (reg_irq_src&FLD_IRQ_GPIO_RISC0_EN) {
        reg_irq_src=FLD_IRQ_GPIO_RISC0_EN;
        uint32_t now=clock_time();
        if (!edges || now-first_edge>40000u) {edges=0; first_edge=now;}
        if (++edges>=3) gpio_en_interrupt_risc0(RX,0);
    }
    if (reg_tmr_sta&FLD_TMR_STA_TMR0) {
        reg_tmr_sta=FLD_TMR_STA_TMR0;
        if (transmitting) wave_irq();
    }
    if (uart_mode) {
        u8 flags=dma_chn_irq_status_get();
        if (flags&FLD_DMA_CHN_UART_RX) {
            dma_chn_enable(FLD_DMA_CHN_UART_RX,0); rx_ready=true;
        }
        dma_chn_irq_status_clr(flags&(FLD_DMA_CHN_UART_RX|FLD_DMA_CHN_UART_TX));
    }
}
static bool line_present(void)
{
    return gpio_read(RX)!=0;
}
bool wired_transmitting(void) {return transmitting;}
void wired_pause_frame(void)
{
    u8 irq=irq_disable();
    if (transmitting) {
        stop_timer(); gpio_write(TX,1); gpio_set_output_en(TX,0);
        high_seen=false;
    }
    irq_restore(irq);
}

bool wired_busy(void)
{
    return uart_mode || transmitting || edges ||
        (D008_LISHEN ? high_seen!=line_present() : gpio_read(RX)!=0);
}
uint32_t wired_wakeup(uint32_t sample_tick)
{
    if (D008_LISHEN && high_seen && !transmitting && !uart_mode &&
        (int32_t)(frame_due-sample_tick)<0) return frame_due;
    return sample_tick;
}
void wired_prepare_suspend(void)
{
    if (wired_busy()) return;
    cpu_set_gpio_wakeup(RX,gpio_read(RX)?Level_Low:Level_High,1); pad_armed=true;
    bls_pm_setWakeupSource(PM_WAKEUP_PAD);
}
void wired_poll(uint32_t ms)
{
    if (pad_armed) {cpu_set_gpio_wakeup(RX,Level_High,0); pad_armed=false;}
    if (uart_mode) {
        if (rx_ready && !uart_tx_is_busy()) {
            unsigned n=rx.size;
            tx.size=n<=256?rtu_channel(&session,rx.data,n,tx.data,ms):0;
            u8 irq=irq_disable();
            rx_ready=false; rx.size=0; uart_recbuff_init((u8 *)&rx,sizeof(rx));
            irq_restore(irq);
            if (tx.size) uart_send_dma((u8 *)&tx);
            last_uart_ms=ms;
        }
        if (ms-last_uart_ms>=10000 && !rx_ready && !uart_tx_is_busy()) wired_init();
        return;
    }
    u8 irq=irq_disable();
    unsigned count=edges;
    if (count && count<3 && clock_time()-first_edge>40000u) edges=0;
    irq_restore(irq);
    if (count>=3) {uart_enter(ms);return;}
    if (transmitting || update_active()) return;
    if (!line_present()) {
        if (high_seen) gpio_set_output_en(TX,0);
        high_seen=false; startup=3; identity_count=0; return;
    }
    if (!high_seen) {
        high_seen=true; last_frame_ms=ms;
        frame_due=clock_time()+LISHEN_IDLE_TICKS;
    }
    if (D008_LISHEN) {
        if ((int32_t)(clock_time()-frame_due)<0) return;
    } else if (ms-last_frame_ms<50) return;
    uint8_t type=0x3a;
    if (startup) type=1;
    else if (identity_count) type=0x3c;
    else if (ms-last_cell_ms>=60000 || !last_cell_ms) type=0x3b;
    unsigned size=sif_frame(&device,D008_LISHEN,type,wave);
    last_frame_ms=ms;
    if (!size) {
        if (D008_LISHEN) frame_due=clock_time()+LISHEN_IDLE_TICKS;
        return;
    }
    if (type==1 && !--startup) last_cell_ms=0;
    if (type==0x3c) --identity_count;
    if (type==0x3b) {identity_count=last_cell_ms?1:3; last_cell_ms=ms;}
    irq=irq_disable();
    wave_size=size; position=phase_ticks=0; transmitting=true;
    gpio_write(TX,1); gpio_set_output_en(TX,1);
    reg_tmr0_tick=0; reg_tmr0_capt=8000; reg_tmr_sta=FLD_TMR_STA_TMR0;
    reg_irq_mask|=FLD_IRQ_TMR0_EN; reg_tmr_ctrl|=FLD_TMR0_EN;
    irq_restore(irq);
}
