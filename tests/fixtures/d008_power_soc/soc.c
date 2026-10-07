#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 1000u
#define BMS_STATE_DEFAULT_SOC 60u
typedef struct {uint32_t soc,dsg,cycle;} bms_state_store_data_t;
static bms_state_store_data_t bms_state_store_get_default_data(void) {bms_state_store_data_t d={60,0,0};return d;}
static int openwire_active,openwire_suspected;
static int balance_active,heater_on,charge_session_active,temp_valid=1;
typedef enum {
    BMS_HEATER_IDLE = 0u,
    BMS_HEATER_ARMING = 1u,
    BMS_HEATER_ACTIVE = 2u
} bms_heater_state_t;
typedef struct {
    bms_heater_state_t heater_state;
    uint8_t heater_on;
    uint8_t heater_fuse_fired;
    uint8_t charge_session_active;
    uint8_t balance_active;
    uint8_t balance_voltage_trusted;
    uint8_t openwire_suspected;
    uint8_t openwire_active;
    /* 包含健康 COW 轮询清 active 后的最后诊断样本，供 DVC SOC 使用。 */
    uint8_t openwire_sample_active;
} bms_features_status_t;
static void bms_features_get_status(bms_features_status_t *s)
{
 memset(s,0,sizeof(*s));s->heater_on=(uint8_t)heater_on;
 s->charge_session_active=(uint8_t)charge_session_active;
 s->balance_active=(uint8_t)balance_active;
 s->openwire_active=(uint8_t)openwire_active;s->openwire_sample_active=(uint8_t)openwire_active;
 s->openwire_suspected=(uint8_t)openwire_suspected;
}
typedef struct {uint8_t valid,cell_count,battery_temp_valid,heater_temp_valid,mos_temp_valid;uint16_t battery_temp_min_x10,battery_temp_max_x10,heater_temp_x10,mos_temp_x10;} bms_afe_feature_snapshot_t;
static uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s){memset(s,0,sizeof(*s));s->valid=1;s->battery_temp_valid=(uint8_t)temp_valid;s->battery_temp_min_x10=650;s->battery_temp_max_x10=650;return 1;}
typedef struct {uint32_t battery_chemistry,soc_profile_id,capacity_factory;} bms_config_system_params_t;
static bms_config_system_params_t stored_profile={1,1,1000};
static int bms_config_store_get_system(bms_config_system_params_t*p){*p=stored_profile;return 1;}
static int bms_config_store_set_system(const bms_config_system_params_t*p){stored_profile=*p;return 1;}
typedef struct {int32_t current_offset_ma;uint32_t current_gain_ppm;} bms_user_params_t;
static bms_user_params_t stored_user={0,1000000};
static int bms_config_get_user(bms_user_params_t*p){*p=stored_user;return 1;}
static int bms_config_get_current_calibration(int32_t*o,uint32_t*g){*o=stored_user.current_offset_ma;*g=stored_user.current_gain_ppm;return 1;}
#define BMS_ERROR_AFE1 0
static int afe_error;
static uint8_t bms_error_get(int error){(void)error;return (uint8_t)afe_error;}
typedef enum {BMS_FAULT_SOC_LOW_FIRST,BMS_FAULT_SOC_LOW_SECOND,BMS_FAULT_SOC_LOW_THIRD} bms_fault_code_t;
static void bms_fault_history_record(bms_fault_code_t c){}
typedef union {struct {unsigned soc_low:1,cell_ovp:1,cell_uvp:1;}bits;uint16_t all;} bms_fault_reg_t;
static struct {uint16_t cell_ovp_third_mv,cell_uvp_third_mv,soc_low_filter_10ms,soc_low_first_percent,soc_low_second_percent,soc_low_third_percent,soc_low_recover_percent;}g_bms_protection_params;
static struct {
 uint16_t cell_max_mv,cell_min_mv,cell_delta_mv,pack_voltage_10mv,charge_current_a10,discharge_current_a10;
 bms_fault_reg_t fault_first,fault_second,fault_third;
 struct{uint16_t soc_percent,soh_percent,cycle_count,remaining_capacity_0p01ah,effective_capacity_0p01ah,nominal_capacity_0p01ah;}soc;
}g_bms_report;
static int config_store_write_ok=1;
/* CURRENT_FLOOR */
/* PRODUCTION_SOURCE */
static uint32_t tick;
static void setup(uint8_t chemistry,uint8_t soc,uint16_t voltage){
 memset(&g_bms_report,0,sizeof(g_bms_report));
 memset(&g_bms_soc,0,sizeof(g_bms_soc));
 stored_profile.battery_chemistry=chemistry;stored_profile.soc_profile_id=chemistry;
 g_bms_protection_params.cell_ovp_third_mv=chemistry==1?3650:4250;
 g_bms_protection_params.cell_uvp_third_mv=2500;
 g_bms_report.cell_min_mv=voltage;g_bms_report.cell_max_mv=voltage;
 openwire_active=0;openwire_suspected=0;balance_active=0;heater_on=0;charge_session_active=0;temp_valid=1;
 afe_error=0;stored_user.current_offset_ma=0;stored_user.current_gain_ppm=1000000;
 bms_state_store_data_t d={soc,0,0};soc_param_lib_init(&d);tick=0;
 memset(&g_soc_input,0,sizeof(g_soc_input));g_soc_input.sample_valid=1;g_soc_input.voltage_valid=1;
 g_soc_input.temperature_valid=1;g_soc_input.temperature_min_x10=650;g_soc_input.temperature_max_x10=650;
 g_soc_input.cell_min_mv=voltage;g_soc_input.cell_max_mv=voltage;
}
static void set_core_voltage(uint16_t min_mv,uint16_t max_mv,uint16_t delta){
 g_bms_report.cell_min_mv=min_mv;g_bms_report.cell_max_mv=max_mv;g_bms_report.cell_delta_mv=delta;
 g_soc_input.cell_min_mv=min_mv;g_soc_input.cell_max_mv=max_mv;g_soc_input.cell_delta_mv=delta;
}
static void sample(int valid,int32_t ma,uint32_t delta){tick+=delta;app_update_soc_from_sample(valid,ma,tick);}
static uint32_t integrate(uint8_t chemistry,int32_t ma,uint32_t step,unsigned count){
 setup(chemistry,60,chemistry==1?3330:3800);sample(1,ma,1);
 uint32_t before=g_bms_soc.remaining_capacity_as10;
 for(unsigned i=0;i<count;i++)sample(1,ma,step);
 uint32_t after=g_bms_soc.remaining_capacity_as10;
 return ma>0?before-after:after-before;
}
static uint16_t lfp_mv_from_soc(double soc){
 if(soc<=0)return 2800;
 if(soc>=100)return 3500;
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
 if(min_mv<2500)min_mv=2500;
 if(min_mv>3800)min_mv=3800;
 g_bms_report.cell_min_mv=(uint16_t)min_mv;
 g_bms_report.cell_max_mv=(uint16_t)(min_mv+delta);
 g_bms_report.cell_delta_mv=delta;
 g_bms_report.pack_voltage_10mv=(uint16_t)(((uint32_t)(min_mv+delta/2u)*16u)/10u);
}
static void simulated_reboot(void){
 bms_state_store_data_t d={g_bms_soc.soc_estimate_percent,g_bms_soc.discharge_fraction_percent,
  g_bms_soc.cycle_count};
 soc_param_lib_init(&d);
}
static void check_invariants(void){
 bms_soc_diag_t d;bms_soc_get_diag(&d);
 assert(d.soc_estimate<=100&&d.soc_display<=100);
 assert(d.remaining_capacity_0p1ah<=d.effective_capacity_0p1ah);
 assert(d.time_to_empty_min==BMS_SOC_ETA_MINUTES_INVALID||d.time_to_empty_min<65535u);
 assert(d.time_to_full_min==BMS_SOC_ETA_MINUTES_INVALID||d.time_to_full_min<65535u);
 if(d.eta_direction==BMS_SOC_ETA_DIR_NONE)assert(!d.eta_valid);
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
  true_ah-=ma*0.4/3600000.0;
  if(true_ah<0)true_ah=0;
  if(true_ah>100)true_ah=100;
  model_voltage(true_ah,ma,random_state+n);
  if(n&&n%(13u*steps_per_hour)==0u)sample(0,0,step_ticks);else sample(1,ma,step_ticks);
  if(n&&n%(7u*24u*steps_per_hour)==0u)simulated_reboot();
  if(n%150u==0u){
   double error=(double)get_soc_real()-true_ah;
   if(error<0.0)error=-error;
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
   double error=(double)get_soc_real()-true_ah;
   if(error<0.0)error=-error;
   assert(error<=8.0);check_invariants();
  }
 }
 check_invariants();
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
  true_ah-=ma*0.4/3600000.0;
  if(true_ah<0)true_ah=0;
  if(true_ah>100)true_ah=100;
  random_state=random_state*1664525u+1013904223u;
  model_voltage(true_ah,ma,random_state);
  int missing=(n&&n%(6u*steps_per_hour)==0u);int reset=(n&&n%(2u*24u*steps_per_hour)==0u);
  sample(missing?0:1,ma,step_ticks);
  if(reset)simulated_reboot();
  if(n%150u==0u){
   bms_soc_diag_t d;bms_soc_get_diag(&d);
   printf("%u,%d,%u,%u,%u,25,%u,0,%d,%d,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
    (unsigned)(n*2u/5u),ma,g_bms_report.cell_min_mv,g_bms_report.cell_max_mv,
    g_bms_report.pack_voltage_10mv*10u,(unsigned)d.eta_direction,reset,missing,true_ah,
    d.soc_estimate,d.soc_display,d.remaining_capacity_0p1ah,d.effective_capacity_0p1ah,
    d.ocv_state,d.ocv_center,d.ocv_low,d.ocv_high,d.ocv_confidence,d.endpoint_state,
    d.time_to_empty_min,d.time_to_full_min,d.eta_confidence,0u,
    0u,0u,0u,d.soh);
  }
 }
 return 0;
}
static int replay_csv(const char *input_path,const char *output_path){
 FILE *in=fopen(input_path,"r"),*out=fopen(output_path,"w");char line[1024];int initialized=0;
 if(!in||!out){if(in)fclose(in);if(out)fclose(out);return 2;}
 fprintf(out,"scenario,step,timestamp_32k,current_ma,cell_min_mv,cell_max_mv,pack_voltage_mv,true_soc,soc_est,soc_display,remaining_0p1ah,full_0p1ah,ocv_state,ocv_center,ocv_low,ocv_high,ocv_confidence,rest_seconds,endpoint_state,endpoint_flags,learning_state,learning_reject,eta_direction,eta_state,eta_confidence,tte_min,ttf_min,last_action,sample_state,integral_delta_as10\n");
 while(fgets(line,sizeof(line),in)){
  unsigned scenario,step,min_mv,max_mv,delta_mv,pack_mv,tmin,tmax;
  unsigned sample_valid,voltage_valid,balance,heat,owa,ows,afe,tf,cf,pf,ovp,uvp,ck,cp,lk,lp,event,seed_soc;
  unsigned long timestamp;int current_ma;double true_soc;bms_soc_sample_t s;bms_soc_diag_t d;
  if(line[0]<'0'||line[0]>'9')continue;
  if(sscanf(line,"%u,%u,%lu,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lf,%u",
   &scenario,&step,&timestamp,&current_ma,&min_mv,&max_mv,&delta_mv,&pack_mv,&tmin,&tmax,
   &sample_valid,&voltage_valid,&balance,&heat,&owa,&ows,&afe,&tf,&cf,&pf,&ovp,&uvp,
   &ck,&cp,&lk,&lp,&event,&true_soc,&seed_soc)!=29)continue;
  if(!initialized||event==2u){setup(1u,(uint8_t)seed_soc,min_mv);initialized=1;}
  else if(event==1u||event==3u)simulated_reboot();
  memset(&s,0,sizeof(s));s.timestamp_32k=(uint32_t)timestamp;s.current_ma=current_ma;
  s.cell_min_mv=(uint16_t)min_mv;s.cell_max_mv=(uint16_t)max_mv;s.cell_delta_mv=(uint16_t)delta_mv;
  s.pack_voltage_mv=(uint32_t)pack_mv;s.temperature_min_x10=(uint16_t)tmin;s.temperature_max_x10=(uint16_t)tmax;
  s.sample_valid=(uint8_t)sample_valid;s.voltage_valid=(uint8_t)voltage_valid;s.temperature_valid=(tmin||tmax)?1u:0u;
  s.balancing_active=(uint8_t)balance;s.heating_active=(uint8_t)heat;s.open_wire_active=(uint8_t)owa;
  s.open_wire_suspected=(uint8_t)ows;s.afe_fault=(uint8_t)afe;s.temperature_fault=(uint8_t)tf;
  s.current_fault=(uint8_t)cf;s.pack_fault=(uint8_t)pf;s.third_cell_ovp=(uint8_t)ovp;s.third_cell_uvp=(uint8_t)uvp;
  s.charger_state_known=(uint8_t)ck;s.charger_present=(uint8_t)cp;s.load_state_known=(uint8_t)lk;s.load_present=(uint8_t)lp;
  g_bms_report.cell_min_mv=s.cell_min_mv;g_bms_report.cell_max_mv=s.cell_max_mv;
  g_bms_report.cell_delta_mv=s.cell_delta_mv;g_bms_report.pack_voltage_10mv=(uint16_t)(pack_mv/10u);
  bms_soc_process_sample(&s);bms_soc_get_diag(&d);
  fprintf(out,"%u,%u,%lu,%d,%u,%u,%u,%.6f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu\n",
   scenario,step,timestamp,current_ma,min_mv,max_mv,pack_mv,true_soc,d.soc_estimate,d.soc_display,
   d.remaining_capacity_0p1ah,d.effective_capacity_0p1ah,d.ocv_state,d.ocv_center,d.ocv_low,d.ocv_high,
   d.ocv_confidence,d.rest_seconds,d.endpoint_state,d.endpoint_event_flags,0u,
   0u,d.eta_direction,d.eta_state,d.eta_confidence,d.time_to_empty_min,
   d.time_to_full_min,d.last_soc_action,d.last_sample_state,(unsigned long)d.last_integral_delta_as10);
 }
 fclose(in);fclose(out);return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--trajectory"))return write_trajectory();
 if(argc==4&&!strcmp(argv[1],"--replay"))return replay_csv(argv[2],argv[3]);
 setup(1,60,3330);bms_state_store_data_t restored={60,0,5};
 soc_param_lib_init(&restored);assert(g_bms_soc.soh==100);
 stored_profile.capacity_factory=1200;bms_soc_nominal_capacity_changed();
 assert(get_soc_real()==60 && g_bms_soc.cycle_count==5);
 soc_param_lib_init(&restored);assert(g_bms_soc.effective_capacity_as10==1200u*3600u);
 stored_profile.capacity_factory=1000;

 setup(1,100,3330);stored_profile.capacity_factory=BMS_SOC_CAPACITY_MAX_0P1AH;
 soc_recalc_full_capacity();soc_recalc_now_capacity();SOC_Result_Pass();
 assert(g_bms_report.soc.nominal_capacity_0p01ah==65530);
 assert(g_bms_soc.remaining_capacity_as10==6553u*3600u);
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
  assert(soc_integral_delta_from_current(SOC_INTEGRAL_DIR_DSG)==reference/3200000u);
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
  assert(g_soc_runtime.last_sample_state==BMS_SOC_SAMPLE_FIRST);
  for(int i=0;i<100;i++)sample(0,0,6400);
  assert(get_soc_real()==60);assert(g_soc_runtime.idle_stable_ticks==0);
  assert(g_soc_runtime.last_sample_state==BMS_SOC_SAMPLE_INVALID);
  setup(chemistry,60,chemistry==1?3330:3800);sample(1,500,1);
  uint32_t before=g_bms_soc.remaining_capacity_as10;
  for(int i=0;i<100;i++)sample(1,500,0);
  assert(g_bms_soc.remaining_capacity_as10==before);
  assert(g_soc_runtime.last_sample_state==BMS_SOC_SAMPLE_DUPLICATE);
  sample(1,500,12801);assert(g_bms_soc.remaining_capacity_as10==before);
  assert(g_soc_runtime.last_sample_state==BMS_SOC_SAMPLE_GAP&&g_soc_runtime.last_sample_elapsed_32k==12801u);
  sample(1,500,6400);assert(g_bms_soc.remaining_capacity_as10==before-1);
  assert(g_soc_runtime.last_sample_state==BMS_SOC_SAMPLE_ACCEPTED);
  assert(g_soc_runtime.last_integral_direction==SOC_INTEGRAL_DIR_DSG);
  assert(g_soc_runtime.last_soc_action==BMS_SOC_ACTION_INTEGRATE);
  assert(g_soc_runtime.last_integral_delta_as10==1u);
  sample(0,0,6400);sample(1,500,6400);assert(g_bms_soc.remaining_capacity_as10==before-1);
  setup(chemistry,60,chemistry==1?3330:3800);tick=UINT32_MAX-3200;sample(1,500,0);
  before=g_bms_soc.remaining_capacity_as10;sample(1,500,6400);
  assert(g_bms_soc.remaining_capacity_as10==before-1);
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
  setup(chemistry,80,chemistry==1?3300:3750);sample(1,0,1);balance_active=1;
  for(int i=0;i<3100;i++)sample(1,0,6400);
  assert(g_soc_runtime.idle_stable_ticks==0&&g_soc_runtime.ocv_state==BMS_SOC_OCV_WAIT_CURRENT);
  balance_active=0;heater_on=1;for(int i=0;i<100;i++)sample(1,0,6400);
  assert(g_soc_runtime.idle_stable_ticks==0);heater_on=0;temp_valid=0;
  for(int i=0;i<100;i++){
   sample(1,0,6400);
  }
  assert(g_soc_runtime.idle_stable_ticks==0);
  temp_valid=1;
  for(int i=0;i<100;i++){
   sample(1,0,6400);
  }
  assert(g_soc_runtime.idle_stable_ticks>0);
  charge_session_active=1;sample(1,0,6400);assert(g_soc_runtime.idle_stable_ticks==0);
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
  g_bms_report.fault_third.bits.cell_ovp=1;sample(1,-500,6400);
  assert(get_soc_real()==100&&get_soc_display()==80);
  g_bms_report.fault_third.bits.cell_ovp=0;sample(1,0,6400);
  for(int i=0;i<10;i++)sample(1,0,6400);
  assert(get_soc_display()==85);
  for(int i=0;i<30;i++)sample(1,0,6400);
  assert(get_soc_display()==100);
 }

 /* ETA uses internal capacity and a stable filtered current, never display SOC. */
 setup(1,50,3330);sample(1,10000,1);
 for(int i=0;i<170;i++)sample(1,10000,6400);
 assert(g_soc_runtime.eta.eta_valid && g_soc_runtime.eta.eta_direction==BMS_SOC_ETA_DIR_DISCHARGE);
 assert(g_soc_runtime.eta.time_to_empty_min>=298 && g_soc_runtime.eta.time_to_empty_min<=300);
 assert(g_soc_runtime.eta.time_to_full_min==BMS_SOC_ETA_MINUTES_INVALID);
 setup(1,50,3330);sample(1,20000,1);
 for(int i=0;i<170;i++)sample(1,20000,6400);
 assert(g_soc_runtime.eta.eta_valid&&g_soc_runtime.eta.time_to_empty_min>=148&&g_soc_runtime.eta.time_to_empty_min<=150);
 setup(1,50,3330);sample(1,5000,1);
 for(int i=0;i<170;i++)sample(1,5000,6400);
 assert(g_soc_runtime.eta.eta_valid&&g_soc_runtime.eta.time_to_empty_min>=598&&g_soc_runtime.eta.time_to_empty_min<=600);
 for(int i=0;i<200;i++)sample(1,(i&1)?5000:20000,6400);
 assert(!g_soc_runtime.eta.eta_valid && g_soc_runtime.eta.eta_state==BMS_SOC_ETA_LOW_CONFIDENCE);
 setup(1,50,3330);sample(1,-10000,1);
 for(int i=0;i<170;i++)sample(1,-10000,6400);
 assert(g_soc_runtime.eta.eta_valid && g_soc_runtime.eta.eta_direction==BMS_SOC_ETA_DIR_CHARGE);
 assert(g_soc_runtime.eta.time_to_full_min>=298 && g_soc_runtime.eta.time_to_full_min<=300);
 sample(1,100,6400);sample(1,100,6400);assert(!g_soc_runtime.eta.eta_valid);

 /* Maximum configured capacity must not overflow the 32-bit TC32 ETA path. */
 assert(bms_eta_minutes(BMS_SOC_CAPACITY_MAX_0P1AH*3600u,201u)==65534u);
 assert(bms_eta_minutes(BMS_SOC_CAPACITY_MAX_0P1AH*3600u,2000000000u)==0u);

 /* CV/taper and endpoint correction deliberately withdraw ETA confidence. */
 setup(1,90,3400);sample(1,-20000,1);
 for(int i=0;i<170;i++)sample(1,-20000,6400);
 for(int i=0;i<80;i++)sample(1,-2000,6400);
 assert(!g_soc_runtime.eta.eta_valid);

 /* Exact D008 deadband boundary samples, including the requested 50..250mA set. */
 assert(integrate(1,50,6400,100)==0);assert(integrate(1,100,6400,100)==0);
 assert(integrate(1,150,6400,100)==0);assert(integrate(1,200,6400,100)==0);
 assert(integrate(1,201,6400,100)==40);assert(integrate(1,250,6400,100)==50);

 /* Early UVP remains a final safety anchor but is explicitly diagnosable. */
 setup(1,15,2500);sample(1,1000,1);
 g_bms_report.cell_max_mv=2600;g_bms_report.cell_delta_mv=100;
 g_bms_report.fault_third.bits.cell_uvp=1;
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
 g_bms_report.cell_min_mv=g_bms_report.cell_max_mv=2600;
 for(int i=0;i<6000&&get_soc_real()>6;i++)sample(1,1000,6400);
 assert(get_soc_real()<=6&&get_soc_real()>0);
 g_bms_report.cell_min_mv=g_bms_report.cell_max_mv=2550;
 for(int i=0;i<5000&&get_soc_real()>3;i++)sample(1,1000,6400);
 assert(get_soc_real()<=3&&get_soc_real()>0);
 g_bms_report.cell_min_mv=g_bms_report.cell_max_mv=2520;
 for(int i=0;i<5000&&get_soc_real()>1;i++)sample(1,1000,6400);
 assert(get_soc_real()==1);
 g_bms_report.cell_min_mv=g_bms_report.cell_max_mv=2500;
 for(int i=0;i<12;i++)sample(1,1000,6400);
 assert(get_soc_real()==0);
 setup(1,50,2500);sample(1,10000,1);
 for(int i=0;i<100;i++)sample(1,10000,6400);
 assert(get_soc_real()>=49);
 g_bms_report.fault_third.bits.cell_uvp=1;sample(1,10000,6400);
 assert(get_soc_real()==0&&(g_soc_runtime.endpoint_event_flags&SOC_ENDPOINT_EVENT_LARGE_SAG)!=0);

 /* Cycle-only SOH: independent boundary expectations, monotonicity, saturation,
  * equivalent partial discharge and restore; former diagnostic slots are zero. */
 const uint16_t cycles[]={0,80,81,122,500,501,530,799,800,65535};
 const uint8_t expected_soh[]={100,100,100,99,90,90,89,81,80,80};
 for(unsigned i=0;i<sizeof(cycles)/sizeof(cycles[0]);++i){
  setup(1,60,3330);
  bms_state_store_data_t state={60,99,cycles[i]};soc_param_lib_init(&state);
  bms_soc_diag_t diag;bms_soc_get_diag(&diag);
  assert(diag.soh==expected_soh[i]&&diag.soh_source==1&&diag.soh_confidence==25);
  assert(g_bms_soc.effective_capacity_as10==1000u*3600u*expected_soh[i]/100u);
  assert(g_bms_report.soc.effective_capacity_0p01ah==10000u*expected_soh[i]/100u);
  simulated_reboot();assert(g_bms_soc.soh==expected_soh[i]);
 }
 for(uint32_t cycle=1;cycle<=65535u;++cycle)
  assert(bms_soh_from_cycle((uint16_t)cycle)<=bms_soh_from_cycle((uint16_t)(cycle-1)));
 setup(1,60,3330);g_bms_soc.cycle_count=499;g_bms_soc.discharge_fraction_percent=75;
 soc_note_discharge_soc_drop(60,35);
 assert(g_bms_soc.cycle_count==500&&g_bms_soc.discharge_fraction_percent==0);
 assert(g_bms_soc.soh==90);
 g_bms_soc.cycle_count=65535;g_bms_soc.discharge_fraction_percent=99;
 soc_note_discharge_soc_drop(60,59);assert(g_bms_soc.cycle_count==65535);
 stored_profile.capacity_factory=2000;bms_soc_nominal_capacity_changed();
 assert(g_bms_soc.effective_capacity_as10==2000u*3600u*80u/100u);
 assert(g_bms_soc.cycle_count==65535);
 stored_profile.capacity_factory=1000;

 run_long_duration_checks();

 puts("PASS SOC: production C integration/OCV/endpoints, ETA confidence/taper, cycle-only SOH, 30-day Monte Carlo and 90-day shallow cycling");
 return 0;
}
