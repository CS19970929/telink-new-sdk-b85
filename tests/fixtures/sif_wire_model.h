#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#define _attribute_ram_code_
#define _FUNC_SIF_ 1
#define CLOCK_SYS_CLOCK_1US 16
#define FLD_IRQ_TMR0_EN 1
#define FLD_TMR_STA_TMR0 1
#define FLD_TMR0_EN 1
#define OWC_TX_PIN 1
#define OWC_RX_PIN 2
#define MOS_TEMP1 3
#define CapacityFactory 116u
#define __TODO__ 0Xaa
typedef enum {SIF_IDLE=0,SYNC_SIGNAL,SEND_PUBLIC,SEND_DATA,SEND_DATA_COMPLETE,STOP_SIGNAL} SIF_STATE_E;
typedef struct {
 uint8_t b1IdischgOcp,b1CellChgUtp,b1CellChgOtp,b1CellDischgOtp,b1CellUvp,b1CellOvp,b1IchgOcp,b1CellDischgUtp;
} bms_fault_bits_t;
static struct {
 uint16_t u16Ichg,u16IDischg,u16TempMax,u16TempMin,u16Temperature[4],u16VCellTotle;
 uint16_t u16VCell[32],u16VCellMax,u16VCellMin,u16VCellMaxPosition,u16VCellMinPosition;
 struct {uint16_t u16Soc,u16Cycle_times;} SocElement;
 struct {bms_fault_bits_t bits;} unMdlFault_Third,unMdlFault_Second;
} g_stCellInfoReport;
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
 memset(&g_stCellInfoReport,0,sizeof(g_stCellInfoReport));
#define NEXT (rng=rng*1664525u+1013904223u,(uint16_t)(rng>>7))
 g_stCellInfoReport.u16Ichg=n&1?NEXT:0;
 g_stCellInfoReport.u16IDischg=n&2?NEXT:0;
 g_stCellInfoReport.u16TempMax=NEXT;g_stCellInfoReport.u16TempMin=NEXT;
 g_stCellInfoReport.u16Temperature[3]=NEXT;g_stCellInfoReport.u16VCellTotle=NEXT;
 g_stCellInfoReport.SocElement.u16Soc=NEXT;g_stCellInfoReport.SocElement.u16Cycle_times=NEXT;
 g_stCellInfoReport.u16VCellMax=NEXT;g_stCellInfoReport.u16VCellMin=NEXT;
 g_stCellInfoReport.u16VCellMaxPosition=NEXT;g_stCellInfoReport.u16VCellMinPosition=NEXT;
 for(unsigned i=0;i<32;++i)g_stCellInfoReport.u16VCell[i]=i<SeriesNum?NEXT:61001u;
 g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp=n&1;
 g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp=n&2;
 g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp=n&4;
 g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp=n&2;
#undef NEXT
}
static void print_packet(const uint8_t*p,unsigned n){
 for(unsigned i=0;i<n;++i) { printf("%02x",p[i]); }
 puts("");
}
