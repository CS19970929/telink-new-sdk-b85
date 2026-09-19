#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint32_t UINT32;
typedef uint32_t u32;
#define CapacityFactory 1000u
#define SOC_PARAM_DEFAULT_SOC 60u
#define SOC_KV_FLAG_CAPACITY_LEARNED 1u
#define SOC_KV_FLAG_LEARNING_META 2u
#define SOC_KV_FLAG_LEARNING_ACTIVE 4u
#define SOC_KV_FLAG_NOMINAL_SHIFT 16u
typedef struct {
 uint32_t soc,dsg,cycle,learned_capacity_0p1ah,flags;
 uint32_t candidate_capacity_0p1ah,valid_learning_count,rejected_learning_count;
 uint32_t last_learning_reject_reason,candidate_match_count;
} soc_kv_data_t;
static soc_kv_data_t soc_kv_store_get_default_data(void) {soc_kv_data_t d={60,0,0,0,0,0,0,0,0,0};return d;}
static int soc_kv_store_write_learning(u32 c,u32 f){return 1;}
static int soc_kv_store_write_learning_meta(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 g){return 1;}
static int openwire_active,openwire_suspected;
static uint8_t bms_features_openwire_active(void){return (uint8_t)openwire_active;}
static uint8_t bms_features_openwire_suspected(void){return (uint8_t)openwire_suspected;}
typedef struct {uint32_t battery_chemistry,soc_profile_id,capacity_factory;} bms_cold_system_params_t;
static bms_cold_system_params_t stored_profile={1,1,1000};
static int bms_cold_kv_store_get_system(bms_cold_system_params_t*p){*p=stored_profile;return 1;}
static int bms_cold_kv_store_set_system(const bms_cold_system_params_t*p){stored_profile=*p;return 1;}
typedef struct {int32_t current_offset_ma;uint32_t current_gain_ppm;} bms_user_params_t;
static bms_user_params_t stored_user={0,1000000};
static int bms_config_get_user(bms_user_params_t*p){*p=stored_user;return 1;}
#define BMS_ERROR_AFE1 0
static int afe_error;
static uint8_t bms_error_get(int error){(void)error;return (uint8_t)afe_error;}
typedef enum {BMS_FAULT_SOC_HIGH_FIRST,BMS_FAULT_SOC_HIGH_SECOND,BMS_FAULT_SOC_HIGH_THIRD} bms_fault_code_t;
static void bms_fault_history_record(bms_fault_code_t c){}
typedef union MDLCHGFAULT_REG {struct {unsigned b1SocLow:1,b1CellOvp:1,b1CellUvp:1;}bits;uint16_t all;} MDLCHGFAULT_REG;
static struct {struct {uint16_t u16VcellOvp_Third,u16VcellUvp_Third,u16SocUp_Filter,u16SocUp_First,u16SocUp_Second,u16SocUp_Third,u16SocUp_Rcv;}protect;}g_tParam;
static struct {
 uint16_t u16VCellMax,u16VCellMin,u16VCellDelta,u16VCellTotle,u16Ichg,u16IDischg;
 MDLCHGFAULT_REG unMdlFault_First,unMdlFault_Second,unMdlFault_Third;
 struct{uint16_t u16Soc,u16Soh,u16Cycle_times,u16CapacityNow,u16CapacityFull,u16CapacityFactory;}SocElement;
}g_stCellInfoReport;
static int config_store_write_ok=1;
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
 openwire_active=0;openwire_suspected=0;
 afe_error=0;stored_user.current_offset_ma=0;stored_user.current_gain_ppm=1000000;
 soc_kv_data_t d={soc,0,0,0,0,0,0,0,0,0};soc_param_lib_init(&d);tick=0;
}
static void sample(int valid,int32_t ma,uint32_t delta){tick+=delta;APP_SOC_IntEnhance_Ctrl(valid,ma,tick);}
static uint32_t integrate(uint8_t chemistry,int32_t ma,uint32_t step,unsigned count){
 setup(chemistry,60,chemistry==1?3330:3800);sample(1,ma,1);
 uint32_t before=SOC_Calculate_Element.u32CapNow;
 for(unsigned i=0;i<count;i++)sample(1,ma,step);
 uint32_t after=SOC_Calculate_Element.u32CapNow;
 return ma>0?before-after:after-before;
}
static uint16_t lfp_mv_from_soc(double soc){
 if(soc<=0)return 2800;if(soc>=100)return 3500;
 for(unsigned i=1;i<sizeof(g_soc_ocv_lfp)/sizeof(g_soc_ocv_lfp[0]);i++)if(soc<=g_soc_ocv_lfp[i].soc){
  const soc_ocv_point_t *a=&g_soc_ocv_lfp[i-1],*b=&g_soc_ocv_lfp[i];
  double f=(soc-a->soc)/(double)(b->soc-a->soc);
  return (uint16_t)(a->mv+f*(b->mv-a->mv)+0.5);
 }
 return 3500;
}
static void model_voltage(double true_soc,int32_t ma,uint32_t noise){
 int32_t center=lfp_mv_from_soc(true_soc);
 int32_t sag=ma>0?ma/100:ma/200;
 int32_t min_mv=center-sag+(int32_t)(noise%5u)-2;
 uint16_t delta=(uint16_t)(5u+(noise%16u));
 if(min_mv<2500)min_mv=2500;if(min_mv>3800)min_mv=3800;
 g_stCellInfoReport.u16VCellMin=(uint16_t)min_mv;
 g_stCellInfoReport.u16VCellMax=(uint16_t)(min_mv+delta);
 g_stCellInfoReport.u16VCellDelta=delta;
 g_stCellInfoReport.u16VCellTotle=(uint16_t)(((uint32_t)(min_mv+delta/2u)*16u)/10u);
}
static void simulated_reboot(void){
 soc_kv_data_t d={SOC_Calculate_Element.u8SOC_Now,SOC_Calculate_Element.u8DSG_SOC_Int,
  SOC_Calculate_Element.u32Cycle_times,g_soc_runtime.learned_capacity_0p1ah,
  SOC_KV_FLAG_LEARNING_META|(1000u<<SOC_KV_FLAG_NOMINAL_SHIFT),
  g_soc_runtime.candidate_capacity_0p1ah,g_soc_runtime.valid_learning_count,
  g_soc_runtime.rejected_learning_count,g_soc_runtime.last_learning_reject_reason,
  g_soc_runtime.candidate_match_count};
 if(g_soc_runtime.capacity_learned)d.flags|=SOC_KV_FLAG_CAPACITY_LEARNED;
 if(g_soc_runtime.learning_state!=BMS_SOC_LEARNING_NONE)d.flags|=SOC_KV_FLAG_LEARNING_ACTIVE;
 soc_param_lib_init(&d);
}
static void check_invariants(void){
 bms_soc_diag_t d;bms_soc_get_diag(&d);
 assert(d.soc_estimate<=100&&d.soc_display<=100);
 assert(d.remaining_capacity_0p1ah<=d.effective_capacity_0p1ah);
 assert(d.time_to_empty_min==BMS_SOC_ETA_MINUTES_INVALID||d.time_to_empty_min<65535u);
 assert(d.time_to_full_min==BMS_SOC_ETA_MINUTES_INVALID||d.time_to_full_min<65535u);
 if(d.eta_direction==BMS_SOC_ETA_DIR_NONE)assert(!d.eta_valid);
 assert(!d.capacity_learning_enable&&d.learning_state==BMS_SOC_LEARNING_NONE);
}
static void run_long_duration_checks(void){
 const uint32_t step_ticks=12800u;
 const uint32_t steps_per_hour=9000u;
 double true_ah=50.0;
 uint32_t random_state=1234567u;
 setup(1,50,3330);sample(1,0,1);
 /* 30-day Monte Carlo: random load, rest, direction changes, missing frames,
  * noise and weekly reboot, all through the production 400ms entry point. */
 for(uint32_t n=0;n<30u*24u*steps_per_hour;n++){
  if(n%750u==0u)random_state=random_state*1664525u+1013904223u;
  int32_t choices[]={-10000,-5000,0,150,5000,10000,20000};
  int32_t ma=choices[random_state%7u];
  if((true_ah<=0.0&&ma>0)||(true_ah>=100.0&&ma<0))ma=0;
  true_ah-=ma*0.4/3600000.0;if(true_ah<0)true_ah=0;if(true_ah>100)true_ah=100;
  model_voltage(true_ah,ma,random_state+n);
  if(n&&n%(13u*steps_per_hour)==0u)sample(0,0,step_ticks);else sample(1,ma,step_ticks);
  if(n&&n%(7u*24u*steps_per_hour)==0u)simulated_reboot();
  if(n%150u==0u){
   double error=(double)get_soc_real()-true_ah;if(error<0.0)error=-error;
   assert(error<=15.0);check_invariants();
  }
 }
 /* 90-day storage shallow cycling. It never reaches a learning endpoint and
  * must remain usable without capacity learning. */
 true_ah=50.0;setup(1,50,3330);sample(1,0,1);
 for(uint32_t n=0;n<90u*24u*steps_per_hour;n++){
  uint32_t hour=n/steps_per_hour;
  int32_t ma;
  if(hour<6u)ma=5000;
  else ma=(((hour-6u)/12u)&1u)?5000:-5000;
  if((true_ah<=20.0&&ma>0)||(true_ah>=80.0&&ma<0))ma=0;
  true_ah-=ma*0.4/3600000.0;
  model_voltage(true_ah,ma,n);
  sample(1,ma,step_ticks);
  if(n&&n%(15u*24u*steps_per_hour)==0u)simulated_reboot();
  if(n%150u==0u){
   double error=(double)get_soc_real()-true_ah;if(error<0.0)error=-error;
   assert(error<=8.0);check_invariants();
  }
 }
 assert(g_soc_runtime.candidate_capacity_0p1ah==0u&&!g_soc_runtime.capacity_learned);
}
static int write_trajectory(void){
 const uint32_t step_ticks=12800u,steps_per_hour=9000u;
 double true_ah=50.0;uint32_t random_state=77u;
 setup(1,50,3330);sample(1,0,1);
 puts("time_s,current_ma,cell_min_mv,cell_max_mv,pack_mv,temperature_c,direction,protection,reset,missing,true_soc,soc_est,soc_display,remaining_0p1ah,effective_0p1ah,ocv_state,ocv_center,ocv_low,ocv_high,ocv_confidence,endpoint_state,tte_min,ttf_min,eta_confidence,learning_state,candidate_0p1ah,accepted_0p1ah,learning_confidence,soh");
 for(uint32_t n=0;n<7u*24u*steps_per_hour;n++){
  uint32_t hour=(n/steps_per_hour)%24u;int32_t ma;
  if(hour<4u||hour>=18u)ma=150;
  else if(hour<10u)ma=5000;
  else if(hour<12u)ma=0;
  else ma=-5000;
  if((true_ah<=5.0&&ma>0)||(true_ah>=95.0&&ma<0))ma=0;
  true_ah-=ma*0.4/3600000.0;if(true_ah<0)true_ah=0;if(true_ah>100)true_ah=100;
  random_state=random_state*1664525u+1013904223u;
  model_voltage(true_ah,ma,random_state);
  int missing=(n&&n%(6u*steps_per_hour)==0u);int reset=(n&&n%(2u*24u*steps_per_hour)==0u);
  sample(missing?0:1,ma,step_ticks);if(reset)simulated_reboot();
  if(n%150u==0u){
   bms_soc_diag_t d;bms_soc_get_diag(&d);
   printf("%u,%d,%u,%u,%u,25,%u,0,%d,%d,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
    (unsigned)(n*2u/5u),ma,g_stCellInfoReport.u16VCellMin,g_stCellInfoReport.u16VCellMax,
    g_stCellInfoReport.u16VCellTotle*10u,(unsigned)d.eta_direction,reset,missing,true_ah,
    d.soc_estimate,d.soc_display,d.remaining_capacity_0p1ah,d.effective_capacity_0p1ah,
    d.ocv_state,d.ocv_center,d.ocv_low,d.ocv_high,d.ocv_confidence,d.endpoint_state,
    d.time_to_empty_min,d.time_to_full_min,d.eta_confidence,d.learning_state,
    d.candidate_capacity_0p1ah,d.learned_capacity_0p1ah,d.capacity_learning_confidence,d.soh);
  }
 }
 return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--trajectory"))return write_trajectory();
 setup(1,60,3330);soc_kv_data_t learned={60,0,5,900,1u|(1000u<<16),0,0,0,0,0};
 soc_param_lib_init(&learned);assert(g_soc_runtime.capacity_learned);
 stored_profile.capacity_factory=1200;bms_soc_nominal_capacity_changed();
 assert(!g_soc_runtime.capacity_learned && get_soc_real()==60 && SOC_Calculate_Element.u32Cycle_times==5);
 soc_param_lib_init(&learned);assert(!g_soc_runtime.capacity_learned);
 stored_profile.capacity_factory=1000;

 setup(1,100,3330);stored_profile.capacity_factory=BMS_SOC_CAPACITY_MAX_0P1AH;
 soc_recalc_full_capacity();soc_recalc_now_capacity();SOC_Result_Pass();
 assert(g_stCellInfoReport.SocElement.u16CapacityFactory==65530);
 assert(SOC_Calculate_Element.u32CapNow==6553u*3600u);
 stored_profile.capacity_factory=1000;
 bms_soc_config_t configured=g_soc_config;configured.ocv_rest_prepare_s=900;
 config_store_write_ok=0;assert(!bms_soc_configure(&configured));assert(g_soc_config.ocv_rest_prepare_s==600);
 config_store_write_ok=1;assert(bms_soc_configure(&configured));assert(g_soc_config.ocv_rest_prepare_s==900);


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
  for(int i=0;i<200;i++)sample(1,-500,6400);
  assert(get_soc_real()==100&&get_soc_display()==100);

  /* A confirmed protection anchor may move estimate immediately for safety,
   * while display completes a bounded soft landing even if charging stops. */
  setup(chemistry,80,chemistry==1?3500:4180);sample(1,-500,1);
  g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp=1;sample(1,-500,6400);
  assert(get_soc_real()==100&&get_soc_display()==80);
  g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp=0;sample(1,0,6400);
  for(int i=0;i<10;i++)sample(1,0,6400);
  assert(get_soc_display()==85);
  for(int i=0;i<30;i++)sample(1,0,6400);
  assert(get_soc_display()==100);
 }

 /* ETA uses internal capacity and a stable filtered current, never display SOC. */
 setup(1,50,3330);sample(1,10000,1);
 for(int i=0;i<170;i++)sample(1,10000,6400);
 assert(g_soc_runtime.eta_valid && g_soc_runtime.eta_direction==BMS_SOC_ETA_DIR_DISCHARGE);
 assert(g_soc_runtime.time_to_empty_min>=298 && g_soc_runtime.time_to_empty_min<=300);
 assert(g_soc_runtime.time_to_full_min==BMS_SOC_ETA_MINUTES_INVALID);
 setup(1,50,3330);sample(1,20000,1);
 for(int i=0;i<170;i++)sample(1,20000,6400);
 assert(g_soc_runtime.eta_valid&&g_soc_runtime.time_to_empty_min>=148&&g_soc_runtime.time_to_empty_min<=150);
 setup(1,50,3330);sample(1,5000,1);
 for(int i=0;i<170;i++)sample(1,5000,6400);
 assert(g_soc_runtime.eta_valid&&g_soc_runtime.time_to_empty_min>=598&&g_soc_runtime.time_to_empty_min<=600);
 for(int i=0;i<200;i++)sample(1,(i&1)?5000:20000,6400);
 assert(!g_soc_runtime.eta_valid && g_soc_runtime.eta_state==BMS_SOC_ETA_LOW_CONFIDENCE);
 setup(1,50,3330);sample(1,-10000,1);
 for(int i=0;i<170;i++)sample(1,-10000,6400);
 assert(g_soc_runtime.eta_valid && g_soc_runtime.eta_direction==BMS_SOC_ETA_DIR_CHARGE);
 assert(g_soc_runtime.time_to_full_min>=298 && g_soc_runtime.time_to_full_min<=300);
 sample(1,100,6400);sample(1,100,6400);assert(!g_soc_runtime.eta_valid);

 /* Maximum configured capacity must not overflow the 32-bit TC32 ETA path. */
 assert(soc_eta_minutes(BMS_SOC_CAPACITY_MAX_0P1AH*3600u,201u)==65534u);
 assert(soc_eta_minutes(BMS_SOC_CAPACITY_MAX_0P1AH*3600u,2000000000u)==0u);

 /* CV/taper and endpoint correction deliberately withdraw ETA confidence. */
 setup(1,90,3400);sample(1,-20000,1);
 for(int i=0;i<170;i++)sample(1,-20000,6400);
 for(int i=0;i<80;i++)sample(1,-2000,6400);
 assert(!g_soc_runtime.eta_valid);

 /* Exact D008 deadband boundary samples, including the requested 50..250mA set. */
 assert(integrate(1,50,6400,100)==0);assert(integrate(1,100,6400,100)==0);
 assert(integrate(1,150,6400,100)==0);assert(integrate(1,200,6400,100)==0);
 assert(integrate(1,201,6400,100)==40);assert(integrate(1,250,6400,100)==50);

 /* Early UVP remains a final safety anchor but is explicitly diagnosable. */
 setup(1,15,2500);sample(1,1000,1);
 g_stCellInfoReport.u16VCellMax=2600;g_stCellInfoReport.u16VCellDelta=100;
 g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp=1;
 sample(1,1000,6400);
 assert(get_soc_real()==0 && get_soc_display()==0);
 assert((g_soc_runtime.endpoint_event_flags&SOC_ENDPOINT_EVENT_EARLY_UVP)!=0);
 assert((g_soc_runtime.endpoint_event_flags&SOC_ENDPOINT_EVENT_CAPACITY_MISMATCH)!=0);
 assert((g_soc_runtime.endpoint_event_flags&SOC_ENDPOINT_EVENT_IMBALANCE)!=0);

 /* Normal low-end tracking approaches the product cutoff in stages without
  * requiring a protection fault.  A high-current sag at the same voltage is
  * held and cannot pull a healthy mid-SOC estimate toward zero. */
 setup(1,15,2650);sample(1,1000,1);
 for(int i=0;i<5000&&get_soc_real()>12;i++)sample(1,1000,6400);
 assert(get_soc_real()<=12&&get_soc_real()>0);
 g_stCellInfoReport.u16VCellMin=g_stCellInfoReport.u16VCellMax=2600;
 for(int i=0;i<6000&&get_soc_real()>6;i++)sample(1,1000,6400);
 assert(get_soc_real()<=6&&get_soc_real()>0);
 g_stCellInfoReport.u16VCellMin=g_stCellInfoReport.u16VCellMax=2550;
 for(int i=0;i<5000&&get_soc_real()>3;i++)sample(1,1000,6400);
 assert(get_soc_real()<=3&&get_soc_real()>0);
 g_stCellInfoReport.u16VCellMin=g_stCellInfoReport.u16VCellMax=2520;
 for(int i=0;i<5000&&get_soc_real()>1;i++)sample(1,1000,6400);
 assert(get_soc_real()==1);
 g_stCellInfoReport.u16VCellMin=g_stCellInfoReport.u16VCellMax=2500;
 for(int i=0;i<12;i++)sample(1,1000,6400);
 assert(get_soc_real()==0);
 setup(1,50,2500);sample(1,10000,1);
 for(int i=0;i<100;i++)sample(1,10000,6400);
 assert(get_soc_real()>=49);
 g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp=1;sample(1,10000,6400);
 assert(get_soc_real()==0&&(g_soc_runtime.endpoint_event_flags&SOC_ENDPOINT_EVENT_LARGE_SAG)!=0);

 /* Capacity learning: qualified cycles create candidates; two consistent
  * independent candidates update effective capacity by at most 5%. */
 setup(1,100,3500);g_soc_config.capacity_learning_enable=1;
 g_soc_input_valid=1;g_soc_input_current_ma=-1000;
 soc_learning_on_full_anchor();
 assert(g_soc_runtime.learning_state==BMS_SOC_LEARNING_FULL_TO_EMPTY);
 g_soc_runtime.learning_capacity_as10=900u*3600u;
 g_soc_input_current_ma=1000;g_stCellInfoReport.u16VCellMin=2500;g_stCellInfoReport.u16VCellMax=2500;
 soc_learning_on_empty_anchor();
 assert(g_soc_runtime.candidate_capacity_0p1ah==900 && g_soc_runtime.candidate_match_count==1);
 assert(!g_soc_runtime.capacity_learned && g_soc_runtime.valid_learning_count==1);
 g_soc_runtime.learning_capacity_as10=910u*3600u;
 g_soc_input_current_ma=-1000;g_stCellInfoReport.u16VCellMin=3500;g_stCellInfoReport.u16VCellMax=3500;
 soc_learning_on_full_anchor();
 assert(g_soc_runtime.capacity_learned && g_soc_runtime.learned_capacity_0p1ah==950);
 assert(g_soc_runtime.valid_learning_count==2 && g_soc_runtime.learning_confidence==100);

 /* A real 100% -> 95% -> 90% -> 85% decline is intentionally tracked in
  * bounded steps.  Each accepted pair may move effective capacity by no more
  * 5%, so neither SOC nor ETA can jump to a single low-cycle observation. */
 {
  const uint16_t targets[]={900u,850u,850u};
  uint16_t previous=g_soc_runtime.learned_capacity_0p1ah;
  for(unsigned cycle=0;cycle<sizeof(targets)/sizeof(targets[0]);cycle++){
   for(unsigned confirmation=0;confirmation<2u;confirmation++){
    g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;
    g_soc_runtime.learning_capacity_as10=(uint32_t)targets[cycle]*3600u;
    assert(soc_learning_accept_candidate());
   }
   assert(previous-g_soc_runtime.learned_capacity_0p1ah<=(previous*5u)/100u);
   previous=g_soc_runtime.learned_capacity_0p1ah;
  }
  assert(g_soc_runtime.learned_capacity_0p1ah>=850u &&
         g_soc_runtime.learned_capacity_0p1ah<=860u);
 }

 /* A high-current/sag UVP can anchor SOC safety but cannot qualify capacity. */
 uint16_t learned_before=g_soc_runtime.learned_capacity_0p1ah;
 uint16_t rejected_before=g_soc_runtime.rejected_learning_count;
 g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;
 g_soc_runtime.learning_capacity_as10=700u*3600u;
 g_soc_input_current_ma=10000;g_stCellInfoReport.u16VCellMin=2500;g_stCellInfoReport.u16VCellMax=2500;
 soc_learning_on_empty_anchor();
 assert(g_soc_runtime.learned_capacity_0p1ah==learned_before);
 assert(g_soc_runtime.rejected_learning_count==rejected_before+1);
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_LOW_QUALITY_EMPTY);

 /* Candidate disagreement is retained as evidence and never immediately
  * overwrites the accepted effective capacity. */
 g_soc_runtime.capacity_learned=0;g_soc_runtime.learned_capacity_0p1ah=0;
 g_soc_runtime.candidate_capacity_0p1ah=1000;g_soc_runtime.candidate_match_count=1;
 g_soc_runtime.learning_state=BMS_SOC_LEARNING_EMPTY_TO_FULL;
 g_soc_runtime.learning_capacity_as10=800u*3600u;
 g_soc_input_current_ma=-1000;g_stCellInfoReport.u16VCellMin=3500;g_stCellInfoReport.u16VCellMax=3500;
 soc_learning_on_full_anchor();
 assert(!g_soc_runtime.capacity_learned && g_soc_runtime.candidate_capacity_0p1ah==800);
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_CANDIDATE_INCONSISTENT);

 /* Charge interruption/reversal and an early weak-cell full endpoint abort
  * learning without changing the accepted effective-capacity evidence. */
 learned_before=g_soc_runtime.learned_capacity_0p1ah;
 g_soc_runtime.learning_state=BMS_SOC_LEARNING_EMPTY_TO_FULL;
 soc_learning_on_delta(SOC_INTEGRAL_DIR_DSG,1u);
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_DIRECTION_REVERSE);
 g_soc_runtime.learning_state=BMS_SOC_LEARNING_EMPTY_TO_FULL;
 g_soc_runtime.learning_capacity_as10=850u*3600u;g_soc_input_valid=1;
 g_soc_input_current_ma=-1000;g_stCellInfoReport.u16VCellMin=3400;
 g_stCellInfoReport.u16VCellMax=3600;g_stCellInfoReport.u16VCellDelta=200;
 soc_learning_on_full_anchor();
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_LOW_QUALITY_FULL);
 assert(g_soc_runtime.learned_capacity_0p1ah==learned_before);

 /* Independent quality gates reject open-wire, imbalance, temperature and
  * missing samples without changing the accepted effective capacity. */
 learned_before=g_soc_runtime.learned_capacity_0p1ah;
 g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;openwire_suspected=1;
 soc_learning_monitor_quality();assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_OPEN_WIRE);
 openwire_suspected=0;g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;
 g_stCellInfoReport.u16VCellDelta=51;soc_learning_monitor_quality();
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_CELL_IMBALANCE);
 g_stCellInfoReport.u16VCellDelta=0;g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;
 g_stCellInfoReport.unMdlFault_Third.all=0x40;soc_learning_monitor_quality();
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_TEMPERATURE);
 g_stCellInfoReport.unMdlFault_Third.all=0;g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;
 sample(0,0,6400);assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_INVALID_SAMPLE);
 assert(g_soc_runtime.learned_capacity_0p1ah==learned_before);

 g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;afe_error=1;
 soc_learning_monitor_quality();assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_AFE_COMMUNICATION);
 afe_error=0;g_soc_runtime.learning_state=BMS_SOC_LEARNING_FULL_TO_EMPTY;
 g_soc_runtime.learning_current_offset_ma=0;g_soc_runtime.learning_current_gain_ppm=1000000;
 stored_user.current_offset_ma=1;soc_learning_monitor_quality();
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_CALIBRATION_CHANGED);
 stored_user.current_offset_ma=0;

 /* A persisted active-session marker makes reboot invalidation observable. */
 soc_kv_data_t interrupted={60,0,0,0,
  SOC_KV_FLAG_LEARNING_META|SOC_KV_FLAG_LEARNING_ACTIVE|(1000u<<SOC_KV_FLAG_NOMINAL_SHIFT),
  900,3,4,0,1};
 soc_param_lib_init(&interrupted);
 assert(g_soc_runtime.learning_state==BMS_SOC_LEARNING_NONE);
 assert(g_soc_runtime.rejected_learning_count==5);
 assert(g_soc_runtime.last_learning_reject_reason==BMS_SOC_LEARNING_REJECT_REBOOT);

 run_long_duration_checks();

 puts("PASS SOC: production C integration/OCV/endpoints, ETA confidence/taper, qualified learning, 30-day Monte Carlo and 90-day shallow cycling");
 return 0;
}
