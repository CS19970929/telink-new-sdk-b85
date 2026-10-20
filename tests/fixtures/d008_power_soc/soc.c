#define BMS_SERVICE_DIAG_ENABLE 1
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 1000u
#define BMS_STATE_DEFAULT_SOC 60u
/* 该算法夹具统一使用16串100Ah，不替代各产品物理接线测试。 */
#define BMS_PRODUCT_CELL_COUNT 16u
static unsigned diagnostic_phase;
static int diagnostic_healthy=1;
static void bms_afe_request_voltage_check(void){if(!diagnostic_phase)diagnostic_phase=1u;}
typedef struct {uint32_t soc,dsg,cycle,full_confirmed,full_basis;} bms_state_store_data_t;
static bms_state_store_data_t bms_state_store_get_default_data(void) {bms_state_store_data_t d={60,0,0,0,0};return d;}
static int temp_valid=1;
static struct {struct {uint8_t heater_status;} bits;} g_bms_system_status;
typedef struct {uint8_t cell_count,battery_temp_valid,heater_temp_valid,mos_temp_valid;uint16_t battery_temp_min_x10,battery_temp_max_x10,heater_temp_x10,mos_temp_x10;} bms_afe_feature_snapshot_t;
static uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s){memset(s,0,sizeof(*s));s->battery_temp_valid=(uint8_t)temp_valid;s->battery_temp_min_x10=650;s->battery_temp_max_x10=650;return 1;}

typedef struct {uint32_t battery_chemistry,soc_profile_id,capacity_factory;} bms_config_system_params_t;
static bms_config_system_params_t stored_profile={1,1,1000};
static int bms_config_store_get_system(bms_config_system_params_t*p){*p=stored_profile;return 1;}
static int bms_config_store_set_system(const bms_config_system_params_t*p){stored_profile=*p;return 1;}
typedef struct {int32_t current_offset_ma;uint32_t current_gain_ppm;} bms_user_params_t;
static bms_user_params_t stored_user={0,1000000};
static int bms_config_get_user(bms_user_params_t*p){*p=stored_user;return 1;}
static int bms_config_get_current_calibration(int32_t*o,uint32_t*g){*o=stored_user.current_offset_ma;*g=stored_user.current_gain_ppm;return 1;}
#define BMS_ERROR_AFE1 0
#define BMS_ERROR_BALANCE 1
static int afe_error;
static uint8_t bms_error_get(int error){(void)error;return (uint8_t)afe_error;}
typedef enum {BMS_FAULT_SOC_LOW_FIRST,BMS_FAULT_SOC_LOW_SECOND,BMS_FAULT_SOC_LOW_THIRD} bms_fault_code_t;
static void bms_fault_history_record(bms_fault_code_t c){}
typedef union {struct {unsigned soc_low:1,cell_ovp:1,cell_uvp:1,pack_uvp:1;}bits;uint16_t all;} bms_fault_reg_t;
static struct {uint16_t cell_ovp_third_mv,cell_uvp_third_mv,pack_uvp_third_10mv,soc_low_filter_10ms,soc_low_first_percent,soc_low_second_percent,soc_low_third_percent,soc_low_recover_percent;}g_bms_protection_params;
static struct {
 uint16_t cell_max_mv,cell_min_mv,cell_delta_mv,pack_voltage_10mv,charge_current_a10,discharge_current_a10;
 uint16_t balance_bits_low,balance_bits_high;
 uint16_t cell_voltage_mv[32];
 bms_fault_reg_t fault_first,fault_second,fault_third;
 struct{uint16_t soc_percent,soh_percent,cycle_count,remaining_capacity_0p01ah,effective_capacity_0p01ah,nominal_capacity_0p01ah;}soc;
}g_bms_report;
static int config_store_read_ok=1;
/* CURRENT_FLOOR */
static uint32_t tick;
static uint32_t pm_get_32k_tick(void) {return tick;}
static uint32_t test_last_raw,test_ms,test_fraction;
static uint32_t bms_time_ms(void){uint32_t delta=tick-test_last_raw;test_last_raw=tick;test_fraction+=delta%32u;test_ms+=delta/32u+test_fraction/32u;test_fraction%=32u;return test_ms;}
/* PRODUCTION_SOURCE */
/* 私有策略测试的输入由夹具拥有；生产不保存本帧副本。 */
static bms_soc_sample_t g_soc_input;
static uint8_t display_soc(void) { bms_soc_diag_t diag; bms_soc_get_diag(&diag); return diag.soc_display; }

static void setup(uint8_t chemistry,uint8_t soc,uint16_t voltage){
 memset(&g_bms_report,0,sizeof(g_bms_report));
 memset(&g_bms_soc,0,sizeof(g_bms_soc));
 stored_profile.capacity_factory=1000u;diagnostic_phase=0u;diagnostic_healthy=1;
 stored_profile.battery_chemistry=chemistry;stored_profile.soc_profile_id=chemistry;
 bms_soc_profile_defaults(chemistry,&committed_profile);++committed_generation;config_store_read_ok=1;
 g_bms_protection_params.cell_ovp_third_mv=chemistry==1?3650:4250;
 g_bms_protection_params.cell_uvp_third_mv=2500;
 g_bms_protection_params.pack_uvp_third_10mv=4000u;
 g_bms_report.cell_min_mv=voltage;g_bms_report.cell_max_mv=voltage;
 g_bms_system_status.bits.heater_status=0;temp_valid=1;
 afe_error=0;stored_user.current_offset_ma=0;stored_user.current_gain_ppm=1000000;
 bms_state_store_data_t d={soc,0,0,0,0};soc_param_lib_init(&d);tick=0;test_last_raw=test_ms=test_fraction=0;
 memset(&g_soc_input,0,sizeof(g_soc_input));g_soc_input.sample_valid=1;g_soc_input.voltage_valid=1;
 g_soc_input.temperature_valid=1;g_soc_input.temperature_min_x10=650;g_soc_input.temperature_max_x10=650;
 g_soc_input.cell_min_mv=voltage;g_soc_input.cell_max_mv=voltage;
}
static void set_core_voltage(uint16_t min_mv,uint16_t max_mv,uint16_t delta){
 g_bms_report.cell_min_mv=min_mv;g_bms_report.cell_max_mv=max_mv;g_bms_report.cell_delta_mv=delta;
 g_soc_input.cell_min_mv=min_mv;g_soc_input.cell_max_mv=max_mv;g_soc_input.cell_delta_mv=delta;
}
static void fill_cells(bms_soc_sample_t *s){
 s->cell_count=BMS_PRODUCT_CELL_COUNT;
 for(unsigned i=0;i<s->cell_count;i++)s->cell_mv[i]=i+1u==s->cell_count?s->cell_max_mv:s->cell_min_mv;
}
static void direct_sample(bms_soc_sample_t *s){
 unsigned phase=diagnostic_phase;
 s->voltage_check_active=phase==1u;s->voltage_calibration_valid=phase==2u&&diagnostic_healthy;
 bms_soc_process_sample(s);
 if(phase==1u)diagnostic_phase=2u;else if(phase==2u)diagnostic_phase=0u;
 if(bms_soc_voltage_check_requested())bms_afe_request_voltage_check();
}
static void sample(int valid,int32_t ma,uint32_t delta){
 unsigned phase=diagnostic_phase;
 for(unsigned i=0;i<BMS_PRODUCT_CELL_COUNT;i++)g_bms_report.cell_voltage_mv[i]=i+1u==BMS_PRODUCT_CELL_COUNT?g_bms_report.cell_max_mv:g_bms_report.cell_min_mv;
 tick+=delta;app_update_soc_from_sample(valid,ma,phase==2u&&diagnostic_healthy,phase==1u);
 if(phase==1u)diagnostic_phase=2u;else if(phase==2u)diagnostic_phase=0u;
}
static void run_ms(int32_t ma,uint32_t milliseconds){
 for(uint32_t t=0u;t<milliseconds;t+=200u)sample(1,ma,6400u);
}
static uint32_t integrate(uint8_t chemistry,int32_t ma,uint32_t step,unsigned count){
 setup(chemistry,60,chemistry==1?3330:3800);sample(1,ma,1);
 uint32_t before=g_bms_soc.remaining_capacity_as10;
 for(unsigned i=0;i<count;i++)sample(1,ma,step);
 uint32_t after=g_bms_soc.remaining_capacity_as10;
 return ma>0?before-after:after-before;
}
static uint16_t lfp_mv_from_soc(double soc){
 if(soc<=0)return 2844;
 if(soc>=100)return 3445;
 for(unsigned i=1;i<g_soc_profile_lfp.point_count;i++)if(soc<=g_soc_profile_lfp.points[i].soc_percent){
  const bms_soc_ocv_config_point_t *a=&g_soc_profile_lfp.points[i-1],*b=&g_soc_profile_lfp.points[i];
  double f=(soc-a->soc_percent)/(double)(b->soc_percent-a->soc_percent);
  return (uint16_t)(a->mv+f*(b->mv-a->mv)+0.5);
 }
 return 3445;
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
 bms_state_store_data_t d=bms_soc_state_snapshot();
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
  /* 软件计量对照使用100%充电电荷效率，不代表实测电芯效率。 */
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
  /* 软件计量对照使用100%充电电荷效率，不代表实测电芯效率。 */
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
  /* 软件计量对照使用100%充电电荷效率，不代表实测电芯效率。 */
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
  unsigned sample_valid,voltage_valid,balance,heat,afe,tf,cf,pf,ovp,uvp,ck,cp,lk,lp,event,seed_soc;
  unsigned long timestamp;int current_ma;double true_soc;bms_soc_sample_t s;bms_soc_diag_t d;
  if(line[0]<'0'||line[0]>'9')continue;
  if(sscanf(line,"%u,%u,%lu,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lf,%u",
   &scenario,&step,&timestamp,&current_ma,&min_mv,&max_mv,&delta_mv,&pack_mv,&tmin,&tmax,
   &sample_valid,&voltage_valid,&balance,&heat,&afe,&tf,&cf,&pf,&ovp,&uvp,
   &ck,&cp,&lk,&lp,&event,&true_soc,&seed_soc)!=27)continue;
  if(!initialized||event==2u){setup(1u,(uint8_t)seed_soc,min_mv);initialized=1;}
  else if(event==1u||event==3u)simulated_reboot();
  memset(&s,0,sizeof(s));tick=(uint32_t)timestamp;s.timestamp_ms=bms_time_ms();s.timestamp_32k=(uint32_t)timestamp;s.current_ma=current_ma;
  s.cell_min_mv=(uint16_t)min_mv;s.cell_max_mv=(uint16_t)max_mv;s.cell_delta_mv=(uint16_t)delta_mv;
  s.temperature_min_x10=(uint16_t)tmin;s.temperature_max_x10=(uint16_t)tmax;
  s.sample_valid=(uint8_t)sample_valid;s.voltage_valid=(uint8_t)voltage_valid;s.temperature_valid=(tmin||tmax)?1u:0u;
  /* 均衡阻止普通 OCV；加热和插拔仍不作为 SOC 校准证据。 */
  s.balance_active=balance?1u:0u;
  s.afe_fault=(uint8_t)afe;s.temperature_fault=(uint8_t)tf;
  s.current_fault=(uint8_t)cf;s.pack_fault=(uint8_t)pf;s.third_cell_ovp=(uint8_t)ovp;s.third_cell_uvp=(uint8_t)uvp;
  /* 保留 CSV 环境记录列；SOC 只使用测量量，不以插拔标记重置资格。 */
  g_bms_report.cell_min_mv=s.cell_min_mv;g_bms_report.cell_max_mv=s.cell_max_mv;
  g_bms_report.cell_delta_mv=s.cell_delta_mv;g_bms_report.pack_voltage_10mv=(uint16_t)(pack_mv/10u);
  s.pack_voltage_10mv=(uint16_t)(pack_mv/10u);fill_cells(&s);direct_sample(&s);bms_soc_get_diag(&d);
  fprintf(out,"%u,%u,%lu,%d,%u,%u,%u,%.6f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu\n",
   scenario,step,timestamp,current_ma,min_mv,max_mv,pack_mv,true_soc,d.soc_estimate,d.soc_display,
   d.remaining_capacity_0p1ah,d.effective_capacity_0p1ah,d.ocv_state,d.ocv_center,d.ocv_low,d.ocv_high,
   d.ocv_confidence,d.rest_seconds,d.endpoint_state,d.endpoint_event_flags,0u,
   0u,d.eta_direction,d.eta_state,d.eta_confidence,d.time_to_empty_min,
   d.time_to_full_min,d.last_soc_action,d.last_sample_state,(unsigned long)d.last_integral_delta_as10);
 }
 fclose(in);fclose(out);return 0;
}
/* 用 host 的64位计算独立核对目标端32位运算，不在固件中引入运行库。 */
static void check_integer_arithmetic(void){
 const int32_t currents[]={INT32_MIN,INT32_MIN+1,-100001,-100000,-99999,-1,0,1,99999,100000,100001,INT32_MAX};
 const int32_t remainders[]={-99999,-1,0,1,99999};
 const uint32_t intervals[]={1,199,200,399,400};
 const uint32_t full=BMS_SOC_CAPACITY_MAX_0P1AH*3600u;
 setup(1,50,3330);
 assert(soc_scale_remaining_capacity(0u,full,3600u)==0u);
 assert(soc_scale_remaining_capacity(full,full,3600u)==3600u);
 assert(soc_scale_remaining_capacity(3599u,3600u,full)==(uint64_t)3599u*full/3600u);
 for(unsigned i=0;i<sizeof(currents)/sizeof(currents[0]);i++)
 for(unsigned j=0;j<sizeof(remainders)/sizeof(remainders[0]);j++)
 for(unsigned k=0;k<sizeof(intervals)/sizeof(intervals[0]);k++){
  int64_t sum=(int64_t)currents[i]*intervals[k]+remainders[j];
  int32_t delta=(int32_t)(sum/100000),remainder=(int32_t)(sum%100000);
  g_bms_soc.effective_capacity_as10=full;g_bms_soc.remaining_capacity_as10=full/2u;
  g_bms_soc.soc_estimate_percent=50;g_bms_soc.discharge_fraction_percent=0;
  g_soc_input_current_ma=currents[i];g_soc_integral_remainder=remainders[j];
  soc_integrate(intervals[k]);
  assert(g_bms_soc.remaining_capacity_as10==(uint32_t)((int64_t)(full/2u)-delta));
  assert(g_soc_integral_remainder==remainder);
 }
 uint32_t random=1234567u;
 for(unsigned i=0;i<10000u;i++){
  random=random*1664525u+1013904223u;uint32_t old=(random%6553u+1u)*3600u;
  random=random*1664525u+1013904223u;uint32_t remaining=random%(old+1u);
  random=random*1664525u+1013904223u;stored_profile.capacity_factory=(uint16_t)(random%6553u+1u);
  uint32_t new_full=(uint32_t)stored_profile.capacity_factory*3600u;
  g_bms_soc.cycle_count=0;g_bms_soc.effective_capacity_as10=old;g_bms_soc.remaining_capacity_as10=remaining;
  bms_soc_nominal_capacity_changed();
  assert(g_bms_soc.remaining_capacity_as10==(uint32_t)((uint64_t)remaining*new_full/old));
  assert(get_soc_real()==(uint64_t)g_bms_soc.remaining_capacity_as10*100u/new_full);
 }
 g_bms_soc.effective_capacity_as10=full;g_bms_soc.remaining_capacity_as10=full;
 g_bms_soc.full_confirmed=1;soc_update_percent();assert(get_soc_real()==100&&g_bms_soc.full_confirmed);
 g_bms_soc.remaining_capacity_as10=full*99u/100u;soc_update_percent();assert(g_bms_soc.full_confirmed);
 --g_bms_soc.remaining_capacity_as10;soc_update_percent();assert(!g_bms_soc.full_confirmed);
 puts("PASS SOC integer arithmetic: signed extrema, fractional direction changes, 10000 capacity rescalings, 99% boundary");
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--arithmetic")){check_integer_arithmetic();return 0;}
 if(argc==2&&!strcmp(argv[1],"--trajectory"))return write_trajectory();
 if(argc==4&&!strcmp(argv[1],"--replay"))return replay_csv(argv[2],argv[3]);
 check_integer_arithmetic();
 /* 小于200mA的实测电流也完整积分，充电不再折减。 */
 assert(integrate(1u,199,6400u,18000u)==7164u);
 assert(integrate(1u,-199,6400u,18000u)==7164u);
 setup(1,60,3330);sample(1,0,1);uint32_t q=g_bms_soc.remaining_capacity_as10;
 for(unsigned i=0;i<1000;i++){sample(1,1,6400);sample(1,-1,6400);}
 assert(g_bms_soc.remaining_capacity_as10==q&&g_soc_integral_remainder==0);
 /* 缺帧/重复和400ms边界，不把盲区补进电量。 */
 sample(1,1000,12800);assert(g_bms_soc.remaining_capacity_as10==q-4u);q-=4u;
 sample(1,1000,0);sample(1,1000,12832);assert(g_bms_soc.remaining_capacity_as10==q);
 sample(1,1000,6400);assert(g_bms_soc.remaining_capacity_as10==q-2u);
 /* 模型刷新保留精度；容量改变按比例重算。 */
 sample(1,1,6400);q=g_bms_soc.remaining_capacity_as10;int32_t remainder=g_soc_integral_remainder;
 ++committed_generation;assert(bms_soc_reload_configuration());
 assert(g_bms_soc.remaining_capacity_as10==q&&g_soc_integral_remainder==remainder);
 stored_profile.capacity_factory=2000;bms_soc_nominal_capacity_changed();
 assert(g_bms_soc.remaining_capacity_as10==q*2u);
 /* 人工设置立即发布；清除之前尚未完成的显示步进。 */
 setup(1,60,3330);g_soc_display_ms=4800u;
 set_soc_param(80);assert(get_soc_real()==80&&display_soc()==80&&g_soc_display_ms==0u);
 set_soc_param(20);assert(get_soc_real()==20&&display_soc()==20);
 set_soc_param(0);assert(get_soc_real()==0&&display_soc()==0);
 set_soc_param(99);assert(get_soc_real()==99&&display_soc()==99);
 bms_state_store_data_t manual=bms_soc_manual_state_snapshot(100);
 assert(manual.soc==100&&manual.full_confirmed&&manual.full_basis==soc_full_basis());
 assert(get_soc_real()==99&&display_soc()==99&&!g_bms_soc.full_confirmed);
 set_soc_param(100);assert(g_bms_soc.full_confirmed&&display_soc()==100);
 simulated_reboot();assert(get_soc_real()==100&&display_soc()==100);
 sample(1,0,1);run_ms(0,10000);assert(display_soc()==100);
 set_soc_param(40);assert(get_soc_real()==40&&display_soc()==40&&!g_bms_soc.full_confirmed);
 /* 自动满电端点仍按5s1pp追赶，人工设置不改变后续自动校准节奏。 */
 setup(1,60,3330);soc_apply_endpoint(1u);assert(display_soc()==60);
 sample(1,0,1);run_ms(0,4800);assert(display_soc()==60);run_ms(0,200);assert(display_soc()==61);
 for(unsigned chem=1;chem<=2;chem++){
  setup((uint8_t)chem,60,chem==1?3500:4180);
  set_core_voltage(chem==1?3300:3980,chem==1?3500:4180,200);
  temp_valid=0;sample(1,-500,1);run_ms(-500,59800);assert(!g_bms_soc.full_confirmed);
  run_ms(-500,1400);assert(g_bms_soc.full_confirmed&&get_soc_real()==100);
  assert(display_soc()<100);run_ms(0,200000);assert(display_soc()==100);
  bms_state_store_data_t persisted=bms_soc_state_snapshot();soc_param_lib_init(&persisted);assert(display_soc()==100);
  g_bms_soc.remaining_capacity_as10=g_bms_soc.effective_capacity_as10*98u/100u;soc_update_percent();assert(!g_bms_soc.full_confirmed);
  soc_param_lib_init(&persisted);committed_profile.full_sync_mv++;++committed_generation;
  assert(bms_soc_reload_configuration());assert(!g_bms_soc.full_confirmed);
  setup((uint8_t)chem,60,chem==1?2850:3100);sample(1,500,1);run_ms(500,4800);assert(get_soc_real()>0);
  run_ms(500,1400);assert(get_soc_real()==0&&display_soc()>0);run_ms(0,300000);assert(display_soc()==0);
 }
 setup(1,60,3500);diagnostic_healthy=0;sample(1,-500,1);run_ms(-500,70000);
 assert(!g_bms_soc.full_confirmed&&get_soc_real()<100);
 /* 孤立/缺失串位、失败诊断、伪欠压不得重置SOC。 */
 setup(1,40,2400);g_bms_report.fault_third.bits.cell_uvp=1;diagnostic_healthy=0;
 sample(1,1000,1);run_ms(1000,6000);assert(get_soc_real()>0);
 diagnostic_healthy=1;run_ms(1000,2000);assert(get_soc_real()==0);
 run_ms(-5000,20000);assert(g_bms_soc.remaining_capacity_as10>0);
 q=g_bms_soc.remaining_capacity_as10;run_ms(0,10000);assert(g_bms_soc.remaining_capacity_as10==q);
 /* 普通OCV禁平台，只向下；每次不超过1pp、间隔至少30分钟。 */
 setup(1,85,3330);sample(1,0,1);run_ms(0,7200000);assert(get_soc_real()==85);
 setup(1,85,3150);sample(1,0,1);run_ms(0,5399000);assert(get_soc_real()==85);
 run_ms(0,2000);assert(get_soc_real()==84);q=g_bms_soc.remaining_capacity_as10;
 run_ms(0,1798000);assert(g_bms_soc.remaining_capacity_as10==q);
 /* 均衡、单串进入禁区，均不能靠均值绕过资格。 */
 setup(1,85,3100);bms_soc_sample_t input={0};input.sample_valid=input.voltage_valid=1;input.cell_min_mv=3100;input.cell_max_mv=3250;input.cell_delta_mv=150;fill_cells(&input);
 for(unsigned i=0;i<30000u;i++){input.timestamp_ms+=200;direct_sample(&input);}assert(get_soc_real()==85);
 setup(1,85,3150);g_bms_report.balance_bits_low=1;sample(1,0,1);run_ms(0,7200000);assert(get_soc_real()==85);
 /* 平均电压、有效串和逐串漂移独立检查。 */
 setup(1,85,3000);memset(&input,0,sizeof(input));input.sample_valid=input.voltage_valid=1;input.cell_min_mv=3000;input.cell_max_mv=3190;input.cell_delta_mv=190;fill_cells(&input);
 uint16_t mean=0;assert(soc_cells_valid(&input,&mean)&&mean==3012);
 input.cell_mv[3]=61001;assert(!soc_cells_valid(&input,&mean));
 setup(1,85,3150);memset(&input,0,sizeof(input));input.sample_valid=input.voltage_valid=1;input.cell_count=16;input.cell_min_mv=3140;input.cell_max_mv=3160;input.cell_delta_mv=20;
 for(unsigned n=0;n<36000;n++){
  for(unsigned j=0;j<16;j++)input.cell_mv[j]=((j+(n/300u))&1u)?3140:3160;
  input.timestamp_ms+=200;direct_sample(&input);
 }assert(get_soc_real()==85);
 /* 电压无效及电流保护不能抹掉仍有效的电流积分。 */
 setup(1,60,3330);memset(&input,0,sizeof(input));input.sample_valid=1;input.current_ma=1000;input.current_fault=1;
 direct_sample(&input);q=g_bms_soc.remaining_capacity_as10;
 for(unsigned n=0;n<500;n++){input.timestamp_ms+=200;direct_sample(&input);}
 assert(g_bms_soc.remaining_capacity_as10==q-1000u);
 /* 回绕仍按真实毫秒积分；有电压时也不人为补回1%。 */
 setup(1,0,3330);set_soc_param(0);sample(1,1000,1);run_ms(1000,10000);assert(get_soc_real()==0);
 setup(1,60,3330);memset(&input,0,sizeof(input));input.sample_valid=1;input.current_ma=1000;input.timestamp_ms=UINT32_MAX-100u;
 direct_sample(&input);q=g_bms_soc.remaining_capacity_as10;input.timestamp_ms+=200u;direct_sample(&input);assert(g_bms_soc.remaining_capacity_as10==q-2u);
 run_long_duration_checks();
 puts("PASS SOC v3: integral, qualification, endpoints, 5s display, OCV mask/window/rate and persistence semantics");
 return 0;
}
