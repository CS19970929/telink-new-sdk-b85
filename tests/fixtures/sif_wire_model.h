#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#define _attribute_ram_code_
#define BMS_PRODUCT_SIF_ENABLE 1
#define CLOCK_SYS_CLOCK_1US 16
#define FLD_IRQ_TMR0_EN 1
#define FLD_TMR_STA_TMR0 1
#define FLD_TMR0_EN 1
#define OWC_TX_PIN 1
#define OWC_RX_PIN 2
#define MOS_TEMP1 3
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 116u
#define __TODO__ 0Xaa
typedef enum {SIF_IDLE=0,SYNC_SIGNAL,SEND_PUBLIC,SEND_DATA,SEND_DATA_COMPLETE,STOP_SIGNAL} SIF_STATE_E;
typedef struct {
 uint8_t discharge_ocp,charge_utp,charge_otp,discharge_otp,cell_uvp,cell_ovp,charge_ocp,discharge_utp;
} bms_fault_bits_t;
static struct {
 uint16_t charge_current_a10,discharge_current_a10,temperature_max_x10,temperature_min_x10,temperature_x10[4],pack_voltage_10mv;
 uint16_t cell_voltage_mv[32],cell_max_mv,cell_min_mv,cell_max_index,cell_min_index;
 struct {uint16_t soc_percent,cycle_count;} soc;
 struct {bms_fault_bits_t bits;} fault_third,fault_second;
} g_bms_report;
static int reg_irq_mask,reg_tmr0_tick,reg_tmr0_capt,reg_tmr_sta,reg_tmr_ctrl;
static int bus=1;
#define BUS_STATE_OWC_TX 1
static void irq_enable(void){}
static uint8_t irq_disable(void){return 1u;}
static void irq_restore(uint8_t state){assert(state==1u);}
static void gpio_write(int p,int v){(void)p;(void)v;}
static int gpio_read(int p){(void)p;return 1;}
static int bus_mux_get_state(void){return bus;}
static void bus_mux_return_to_owc_idle(void){bus=0;}
void sif_send_data_handle(void);
static void wire_case(unsigned n){
 uint32_t rng=19u+n*387u;
 memset(&g_bms_report,0,sizeof(g_bms_report));
#define NEXT (rng=rng*1664525u+1013904223u,(uint16_t)(rng>>7))
 g_bms_report.charge_current_a10=n&1?NEXT:0;
 g_bms_report.discharge_current_a10=n&2?NEXT:0;
 g_bms_report.temperature_max_x10=NEXT;g_bms_report.temperature_min_x10=NEXT;
 g_bms_report.temperature_x10[3]=NEXT;g_bms_report.pack_voltage_10mv=NEXT;
 g_bms_report.soc.soc_percent=NEXT;g_bms_report.soc.cycle_count=NEXT;
 g_bms_report.cell_max_mv=NEXT;g_bms_report.cell_min_mv=NEXT;
 g_bms_report.cell_max_index=NEXT;g_bms_report.cell_min_index=NEXT;
 for(unsigned i=0;i<32;++i)g_bms_report.cell_voltage_mv[i]=i<BMS_PRODUCT_CELL_COUNT?NEXT:61001u;
 g_bms_report.fault_third.bits.charge_ocp=n&1;
 g_bms_report.fault_third.bits.discharge_ocp=n&2;
 g_bms_report.fault_third.bits.cell_ovp=n&4;
 g_bms_report.fault_third.bits.discharge_utp=n&2;
#undef NEXT
}
static void print_packet(const uint8_t*p,unsigned n){
 for(unsigned i=0;i<n;++i) { printf("%02x",p[i]); }
 puts("");
}
