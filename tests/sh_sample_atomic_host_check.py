"""Run actual SH frame publication/getters, with deterministic read failures."""
from pathlib import Path
import os, shlex, subprocess, tempfile
from project_paths import host_includes, selected_source
from bms_diag_host_check import function

ROOT=Path(__file__).resolve().parents[1]
prefix=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "bms_state.h"
#include "bms_afe_driver.h"
#include "bms_sw_protection.h"
#include "bms_sh3673510_config.h"
#include "sh3673520.h"
#include "sh3673510_control.h"
struct stCell_Info g_stCellInfoReport;
static bms_afe_aux_measurements_t s_aux;
static uint8_t s_ntc_valid[4],s_snapshot_valid,s_afe_reconfigure_required;
static uint32_t s_ntc_ohm[4];static int16_t s_mos_ntc_raw;
static unsigned stage,fail,reads,errors;static int bad_cell=-1;static int32_t bad_value;
static uint32_t tick=32000;static int invalid_ntc=-1;
#define SH3510_MISSING_CELL_MV 61001u
static int step(void){++reads;return ++stage!=fail;}
uint8_t sh3673510_control_wake(void){return step();}
sh3673520_status_t SH3673520_ReadCellVoltages(int32_t*p,uint8_t n){
 for(unsigned i=0;i<n;++i)p[i]=(int)i==bad_cell?bad_value:3300+(int32_t)i;
 return step()?SH3673520_OK:SH3673520_ERR_SPI;
}
sh3673520_status_t SH3673520_ReadPackVoltage(int32_t*p){*p=SH3673510_BOARD_CELL_COUNT*3300;return step()?SH3673520_OK:SH3673520_ERR_SPI;}
sh3673520_status_t SH3673520_ReadCurrent(sh3673520_current_raw_t*p){p->cadc_raw=123;return step()?SH3673520_OK:SH3673520_ERR_SPI;}
sh3673520_status_t SH3673520_ReadTemperatures(sh3673520_temperature_raw_t*p){for(unsigned i=0;i<4;++i)p->external_raw[i]=(int)i==invalid_ntc?0:10000+(int32_t)i*1000;return step()?SH3673520_OK:SH3673520_ERR_SPI;}
uint8_t sh3673510_control_read_status(sh3673510_control_status_t*p){memset(p,0,sizeof(*p));return step();}
sh3673520_status_t SH3673520_CurrentRawToMilliAmp(int32_t v,uint32_t r,int32_t*p){(void)v;(void)r;*p=1234;return step()?SH3673520_OK:SH3673520_ERR_SPI;}
sh3673520_status_t SH3673520_NtcRawToOhm(int32_t raw,uint32_t*r){*r=(uint32_t)raw;return raw?SH3673520_OK:SH3673520_ERR_RANGE;}
static int32_t bms_config_calibrate_current(int32_t ma){return ma;}
static uint32_t pm_get_32k_tick(void){return tick;}
static uint16_t ntc_temp(uint32_t ohm){return (uint16_t)(650u+ohm/1000u);}
static void publish_hw_status(const sh3673510_control_status_t*p){(void)p;}
void bms_sw_protection_update(const bms_sw_protection_inputs_t*p){assert(p);}
void bms_sw_protection_clear(void){}
void bms_sw_protection_record_fault_edges(void){}
static void merge_hw_protection_faults(const sh3673510_control_status_t*p){(void)p;}
static void service_short_recovery(const sh3673510_control_status_t*p){(void)p;}
static void service_hw_flag_recovery(const sh3673510_control_status_t*p){(void)p;}
void sh3673510_board_force_heater_fuse_safe(void){}
static void note_comm_error(void){++errors;s_snapshot_valid=0;}
static void note_comm_ok(void){}
static uint8_t service_afe_reconfiguration(void){s_snapshot_valid=0;s_afe_reconfigure_required=0;return 1;}
'''
tail=r'''
int main(void){
 struct stCell_Info before; bms_afe_aux_measurements_t old;
 bms_afe_feature_snapshot_t snap;
 stage=fail=0;sh3673510_bms_afe_sample();assert(s_snapshot_valid);
 before=g_stCellInfoReport;old=s_aux;
 for(unsigned f=1;f<=7;++f){stage=0;fail=f;sh3673510_bms_afe_sample();
  assert(!s_snapshot_valid&&!memcmp(&before,&g_stCellInfoReport,sizeof(before))&&!memcmp(&old,&s_aux,sizeof(old)));
  assert(!sh3673510_backend_get_feature_snapshot(&snap)&&!snap.valid);
 }
 fail=0;
 for(unsigned cell=0;cell<SH3673510_BOARD_CELL_COUNT;++cell)for(unsigned v=0;v<2;++v){
  bad_cell=(int)cell;bad_value=v?65536:-1;stage=0;sh3673510_bms_afe_sample();
  assert(!s_snapshot_valid&&!memcmp(&before,&g_stCellInfoReport,sizeof(before))&&!memcmp(&old,&s_aux,sizeof(old)));
 }
 bad_cell=-1;stage=0;++tick;sh3673510_bms_afe_sample();assert(s_snapshot_valid);
 unsigned count=reads;assert(sh3673510_backend_get_feature_snapshot(&snap));
 assert(reads==count&&snap.valid&&snap.cell_count==SH3673510_BOARD_CELL_COUNT);
 assert(snap.battery_temp_valid&&snap.battery_temp_min_x10==660&&snap.battery_temp_max_x10==661);
 for(unsigned i=SH3673510_BOARD_CELL_COUNT;i<32;++i)assert(g_stCellInfoReport.u16VCell[i]==61001);
 invalid_ntc=SH3673510_BOARD_BAT_NTC1_INDEX;stage=0;sh3673510_bms_afe_sample();
 assert(sh3673510_backend_get_feature_snapshot(&snap)&&!snap.battery_temp_valid);
 s_afe_reconfigure_required=1;stage=0;sh3673510_bms_afe_sample();
 assert(!sh3673510_backend_get_feature_snapshot(&snap));
 return 0;
}
'''
for product in ('d011','d013','d014'):
    source=selected_source(ROOT/'bms/afe/sh3673510/sh3673510_bms.c',product)
    units='\n'.join(function(source,signature) for signature in (
        'static uint16_t legacy_adc_mv(', 'static uint8_t battery_temperature_snapshot(',
        'static uint8_t publish_measurements(', 'void sh3673510_bms_afe_sample(',
        'uint8_t sh3673510_backend_get_feature_snapshot('))
    with tempfile.TemporaryDirectory(prefix='sh-frame-') as folder:
        c=Path(folder)/'check.c';exe=Path(folder)/'check.exe';c.write_text(prefix+units+tail,encoding='utf8')
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',
                       '-Wno-unused-function',*host_includes(ROOT,product),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS '+product+' every read/cell failure keeps complete old report; feature getter performs zero SPI reads')
