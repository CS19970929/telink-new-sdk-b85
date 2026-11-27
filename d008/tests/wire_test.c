#define D008_LISHEN 1
#define D008_WIRED_HOST 1
#include "../wired.c"
#include <assert.h>
#include <stdio.h>

Device device;
static bool updating;
static unsigned mock_rtu_size, rtu_calls;
void rtu_reset(RtuSession *s) {memset(s,0,sizeof(*s));}
bool update_active(void) {return updating;}
unsigned rtu_channel(RtuSession *s,const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms)
{
    (void)s; (void)p; (void)n; (void)out; (void)ms;
    ++rtu_calls;
    return mock_rtu_size;
}
static unsigned ticks;
static void half_ms(unsigned level)
{
    simulated_tick+=8000; ++ticks;
    reg_irq_src=0; reg_tmr_sta=FLD_TMR_STA_TMR0;
    wired_irq(); reg_tmr_sta=0;
    assert(pin_tx==level && tx_enable);
}
static void expected_frame(const uint8_t data[6])
{
    assert(transmitting && wired_busy() && reg_tmr0_capt==8000);
    for (unsigned i=0;i<128;++i) half_ms(i>=124);
    for (unsigned i=0;i<48;++i) {
        unsigned bit=(data[i/8]>>(7-i%8))&1;
        for (unsigned j=0;j<12;++j) half_ms(j>=(bit?4:8));
        device.soc.state.percent=90;
    }
    for (unsigned i=0;i<54;++i) half_ms(i>=50);
    assert(!transmitting && !wired_busy() && !(reg_tmr_ctrl&FLD_TMR0_EN));
}
int main(void)
{
    device.valid=device.soc.initialized=true; device.soc.state.percent=50;
    device.sample.sum_mv=48000; device.sample.temp_x10[NTC_BAT1]=300;
    simulated_tick=UINT32_MAX-15000000; wired_init(); pin_rx=0;
    wired_poll(0); wired_prepare_suspend(); assert(wake_enabled && wake_pol==Level_High);
    pin_rx=1; wired_poll(0); assert(!wired_busy() && !wake_enabled);
    wired_prepare_suspend(); assert(wake_enabled && wake_pol==Level_Low);
    assert(wired_wakeup(simulated_tick+3200000)==simulated_tick+3200000);
    simulated_tick+=15992000;
    assert(wired_wakeup(simulated_tick+3200000)==simulated_tick+8000);
    wired_poll(999); assert(!transmitting);
    simulated_tick+=8000; wired_poll(1000);
    uint32_t start=simulated_tick;
    const uint8_t first[]={0,50,48,246,0,0x58};
    expected_frame(first);
    simulated_tick+=15992000; wired_poll(2378); assert(!transmitting);
    simulated_tick+=8000; wired_poll(2379);
    assert(simulated_tick-start==1379u*16000);
    const uint8_t second[]={0,90,48,246,0,0x80};
    expected_frame(second);
    device.valid=false; simulated_tick+=16000000;
    wired_poll(3758); assert(!transmitting && !wired_busy());
    pin_rx=0; wired_poll(3759); assert(!tx_enable && !wired_busy());
    for (unsigned i=0;i<3;++i) {
        reg_irq_src=FLD_IRQ_GPIO_RISC0_EN; reg_tmr_sta=0; simulated_tick+=900;
        wired_irq(); wired_poll(3760);
    }
    assert(uart_mode && wired_busy());
    rx.size=8; rx_ready=true; mock_rtu_size=RTU_PENDING;
    wired_poll(3760);
    assert(rx_ready && rx.size==8 && rtu_calls==1);
    mock_rtu_size=8; wired_poll(3760);
    assert(!rx_ready && rx.size==0 && rtu_calls==2 && tx.size==8);
    wired_poll(13760); assert(!uart_mode && !wired_busy());
    pin_rx=1; device.valid=true;
    wired_poll(14000); simulated_tick+=16000000;
    wired_poll(15000); assert(wired_transmitting());
    updating=true; wired_pause_frame();
    assert(!wired_transmitting() && !tx_enable);
    simulated_tick+=32000000; wired_poll(17000); assert(!wired_transmitting());
    updating=false; wired_poll(17001); simulated_tick+=16000000;
    wired_poll(18001); assert(wired_transmitting());
    assert(ticks==1516);
    puts("Lishen: 1516 output levels, 1379 ms spacing, wrap, Suspend pad polarity, UART handover OK");
}
