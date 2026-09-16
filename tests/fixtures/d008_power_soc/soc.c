#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint32_t UINT32;
typedef uint32_t u32;
#define CapacityFactory 1000u
#define SOC_PARAM_DEFAULT_SOC 60u
#define SOC_KV_FLAG_CAPACITY_LEARNED 1u
typedef struct { uint32_t soc,dsg,cycle,learned_capacity_0p1ah,flags; } soc_kv_data_t;
static soc_kv_data_t soc_kv_store_get_default_data(void) {soc_kv_data_t d={60,0,0,0,0};return d;}
static int soc_kv_store_write_learning(u32 c,u32 f){return 1;}
typedef struct {uint32_t battery_chemistry,soc_profile_id,capacity_factory;} bms_cold_system_params_t;
static bms_cold_system_params_t stored_profile={1,1,1000};
static int bms_cold_kv_store_get_system(bms_cold_system_params_t*p){*p=stored_profile;return 1;}
static int bms_cold_kv_store_set_system(const bms_cold_system_params_t*p){stored_profile=*p;return 1;}
typedef enum {BMS_FAULT_SOC_HIGH_FIRST,BMS_FAULT_SOC_HIGH_SECOND,BMS_FAULT_SOC_HIGH_THIRD} bms_fault_code_t;
static void bms_fault_history_record(bms_fault_code_t c){}
typedef union MDLCHGFAULT_REG {struct {unsigned b1SocLow:1,b1CellOvp:1,b1CellUvp:1;}bits;uint16_t all;} MDLCHGFAULT_REG;
static struct {struct {uint16_t u16VcellOvp_Third,u16VcellUvp_Third,u16SocUp_Filter,u16SocUp_First,u16SocUp_Second,u16SocUp_Third,u16SocUp_Rcv;}protect;}g_tParam;
static struct {
 uint16_t u16VCellMax,u16VCellMin,u16VCellDelta,u16Ichg,u16IDischg;
 MDLCHGFAULT_REG unMdlFault_First,unMdlFault_Second,unMdlFault_Third;
 struct{uint16_t u16Soc,u16Soh,u16Cycle_times,u16CapacityNow,u16CapacityFull,u16CapacityFactory;}SocElement;
}g_stCellInfoReport;
/* CURRENT_FLOOR */
/* PRODUCTION_SOURCE */
static uint32_t tick;
static void setup(uint8_t chemistry,uint8_t soc,uint16_t voltage){
 memset(&g_stCellInfoReport,0,sizeof(g_stCellInfoReport));
 memset(&SOC_Calculate_Element,0,sizeof(SOC_Calculate_Element));
 stored_profile.battery_chemistry=chemistry;stored_profile.soc_profile_id=chemistry;
 g_tParam.protect.u16VcellOvp_Third=chemistry==1?3650:4250;
 g_tParam.protect.u16VcellUvp_Third=2500;
 g_stCellInfoReport.u16VCellMin=voltage;g_stCellInfoReport.u16VCellMax=voltage;
 soc_kv_data_t d={soc,0,0,0,0};soc_param_lib_init(&d);tick=0;
}
static void sample(int valid,int32_t ma,uint32_t delta){tick+=delta;APP_SOC_IntEnhance_Ctrl(valid,ma,tick);}
static uint32_t integrate(uint8_t chemistry,int32_t ma,uint32_t step,unsigned count){
 setup(chemistry,60,chemistry==1?3330:3800);sample(1,ma,1);
 uint32_t before=SOC_Calculate_Element.u32CapNow;
 for(unsigned i=0;i<count;i++)sample(1,ma,step);
 uint32_t after=SOC_Calculate_Element.u32CapNow;
 return ma>0?before-after:after-before;
}
int main(void){
 /* Host-only 64-bit oracle, including INT32_MIN and retained remainders. */
 setup(1,60,3330);
 uint32_t random_state=17,ref_remainder=0;
 g_soc_integral_dir=SOC_INTEGRAL_DIR_DSG;g_soc_integral_tick_remainder=0;
 for(int n=0;n<10000;n++){
  random_state=random_state*1664525u+1013904223u;
  g_soc_input_current_ma=n==0?INT32_MIN:(int32_t)random_state;
  g_soc_input_valid=1;g_soc_interval_32k=(random_state%12800u)+1u;
  uint32_t magnitude=g_soc_input_current_ma<0?0u-(uint32_t)g_soc_input_current_ma:(uint32_t)g_soc_input_current_ma;
  uint64_t reference=(uint64_t)magnitude*g_soc_interval_32k+ref_remainder;
  assert(soc_integral_delta_from_current(0,SOC_INTEGRAL_DIR_DSG)==reference/3200000u);
  ref_remainder=(uint32_t)(reference%3200000u);
  assert(g_soc_integral_tick_remainder==ref_remainder);
 }

 for(uint8_t chemistry=1;chemistry<=2;chemistry++){
  assert(integrate(chemistry,499,6400,100)==99);
  assert(integrate(chemistry,499,8000,80)==99);
  assert(integrate(chemistry,499,12800,50)==99);
  assert(integrate(chemistry,-499,6400,100)==99);
  assert(integrate(chemistry,199,6400,100)==0);
  assert(integrate(chemistry,200,6400,100)==0);
  assert(integrate(chemistry,201,6400,100)==40);
  assert(integrate(chemistry,-199,6400,100)==0);
  assert(integrate(chemistry,-200,6400,100)==0);
  assert(integrate(chemistry,-201,6400,100)==40);
  assert(integrate(chemistry,-500,6400,100)==100);
  assert(integrate(chemistry,501,6400,100)==100);
  assert(integrate(chemistry,-501,6400,100)==100);
  setup(chemistry,60,2900);sample(1,0,1);
  for(int i=0;i<100;i++)sample(0,0,6400);
  assert(get_soc_real()==60);assert(g_soc_runtime.idle_stable_ticks==0);
  setup(chemistry,60,chemistry==1?3330:3800);sample(1,500,1);
  uint32_t before=SOC_Calculate_Element.u32CapNow;
  for(int i=0;i<100;i++)sample(1,500,0);
  assert(SOC_Calculate_Element.u32CapNow==before);
  sample(1,500,12801);assert(SOC_Calculate_Element.u32CapNow==before);
  sample(1,500,6400);assert(SOC_Calculate_Element.u32CapNow==before-1);
  sample(0,0,6400);sample(1,500,6400);assert(SOC_Calculate_Element.u32CapNow==before-1);
  setup(chemistry,60,chemistry==1?3330:3800);tick=UINT32_MAX-3200;sample(1,500,0);
  before=SOC_Calculate_Element.u32CapNow;sample(1,500,6400);
  assert(SOC_Calculate_Element.u32CapNow==before-1);
  /* The unreliable floor is still an idle candidate (user policy), but
   * invalid frames cannot qualify and a reliable excursion resets rest. */
  setup(chemistry,60,chemistry==1?3330:3800);sample(1,200,1);
  for(int i=0;i<3010;i++)sample(1,200,6400);
  assert(g_soc_runtime.ocv_confidence==100);
  sample(0,200,6400);assert(g_soc_runtime.idle_stable_ticks==0);
  /* Fresh zero current enables rest, an observed excursion resets it. */
  setup(chemistry,80,chemistry==1?3300:3750);sample(1,0,1);
  for(int i=0;i<2995;i++)sample(1,0,6400);
  assert(g_soc_runtime.ocv_state!=BMS_SOC_OCV_READY);
  for(int i=0;i<10;i++)sample(1,0,6400);
  assert(g_soc_runtime.ocv_confidence==100);
  sample(1,300,3200);sample(1,0,3200);assert(g_soc_runtime.idle_stable_ticks==0);
  for(int i=0;i<12010;i++)sample(1,0,6400);
  assert(get_soc_real()==79); /* 10 min prepare + 30 min downward step */
  setup(chemistry,20,chemistry==1?3350:3900);sample(1,0,1);
  for(int i=0;i<12100;i++)sample(1,0,6400);
  assert(get_soc_real()==20); /* OCV never calibrates upward */
  setup(chemistry,80,chemistry==1?3500:4180);sample(1,0,1);
  for(int i=0;i<350;i++)sample(1,0,6400);
  assert(get_soc_real()==80); /* high voltage alone is not charging */
  for(int i=0;i<350;i++)sample(1,-500,6400);
  assert(get_soc_real()>80); /* valid charging full anchor still works */
 }
 puts("PASS SOC: both chemistries, real elapsed integration, deadband, duplicate/gap/invalid, wrap, rest, downward OCV/full anchor");
 return 0;
}
