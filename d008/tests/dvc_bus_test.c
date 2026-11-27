#include "../dvc_bus.h"
#include "drivers.h"
#include "tl_common.h"
#include <assert.h>
#include <stdio.h>

uint8_t reg_i2c_id, reg_i2c_adr, reg_i2c_do;
static uint8_t ctrl, readback;
static unsigned step, nak1, nak2, timeout_step;
static unsigned resets, inits, delay_calls;
static bool output[2];
static bool corrupt_crc;

uint8_t *test_i2c_command(void) {++step; return &ctrl;}

uint8_t test_i2c_status(void)
{
    assert(reg_i2c_adr==0x51); /* 唤醒不发复位或清告警命令。 */
    return step==nak1 || step==nak2 ? FLD_I2C_NAK : 0;
}

uint8_t test_i2c_data(void)
{
    if (!(ctrl & FLD_I2C_CMD_ACK)) return readback;
    uint8_t crc=0;
    const uint8_t bytes[]={0x40,0x51,0x41,readback};
    for (unsigned i=0;i<sizeof(bytes);++i) {
        crc^=bytes[i];
        for (unsigned b=0;b<8;++b) crc=(uint8_t)((crc<<1)^((crc&0x80)?7:0));
    }
    return corrupt_crc ? (uint8_t)(crc^1u) : crc;
}

uint32_t clock_time(void) {return 0;}
bool clock_time_exceed(uint32_t start,uint32_t us)
{
    assert(start==0 && us==6000);
    return step==timeout_step;
}

void reset_i2c_module(void) {++resets;}
void i2c_gpio_set(unsigned group) {assert(group==I2C_GPIO_GROUP_C0C1);}
void i2c_master_init(unsigned addr,unsigned divider)
{
    assert(addr==0x40 && divider==40 && !output[GPIO_PC0] && !output[GPIO_PC1]);
    ++inits;
}
void gpio_set_output_en(unsigned pin,unsigned on)
{
    assert(pin<=GPIO_PC1 && (pin!=GPIO_PC1 || !on));
    output[pin]=on!=0;
}
void gpio_set_input_en(unsigned pin,unsigned on) {assert(pin<=GPIO_PC1 && on==1);}
void gpio_set_func(unsigned pin,unsigned func) {assert(pin<=GPIO_PC1 && func==AS_GPIO && resets==1);}
void gpio_setup_up_down_resistor(unsigned pin,unsigned pull) {assert(pin<=GPIO_PC1 && pull==PM_PIN_PULLUP_10K);}
void gpio_write(unsigned pin,unsigned level) {assert(pin==GPIO_PC0 && level==0 && !output[pin]);}
void sleep_us(unsigned long us)
{
    static const unsigned times[]={20000,1000,5};
    assert(delay_calls<3 && us==times[delay_calls]);
    assert(output[GPIO_PC0]==(delay_calls==1) && !output[GPIO_PC1]);
    ++delay_calls;
}

static void scenario(void)
{
    ctrl=readback=0;
    step=nak1=nak2=timeout_step=resets=inits=delay_calls=0;
    corrupt_crc=false;
}

int main(void)
{
    scenario();
    assert(dvc_startup_set(0x51,0,0xff));
    assert(step==9 && !resets && !inits && !delay_calls);

    scenario();
    nak1=1;
    assert(dvc_startup_set(0x51,0,0xff));
    assert(step==11 && resets==1 && inits==1 && delay_calls==3);

    scenario();
    timeout_step=1;
    assert(dvc_startup_set(0x51,0,0xff));
    assert(step==11 && resets==1 && inits==1 && delay_calls==3);

    scenario();
    nak1=1; nak2=3;
    assert(!dvc_startup_set(0x51,0,0xff));
    assert(step==4 && resets==1 && inits==1 && delay_calls==3);

    scenario();
    nak1=2;
    assert(!dvc_startup_set(0x51,0,0xff) && !resets);
    scenario();
    corrupt_crc=true;
    assert(!dvc_startup_set(0x51,0,0xff) && !resets);
    scenario();
    readback=1;
    assert(!dvc_startup_set(0x51,0,0xff) && !resets);
    scenario();
    nak1=1;
    assert(!dvc_set(0x51,0,0xff) && !resets);
    puts("PASS DVC bus: fast path, first NAK/timeout wakes once, open-drain pins and no alarm or reset writes");
    return 0;
}
