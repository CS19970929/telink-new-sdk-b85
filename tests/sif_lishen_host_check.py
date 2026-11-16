"""锂神 V1.1.1：编译真实 SIF 生产函数，核对字节、500us 全波形、ISR 缓冲与接管。"""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'bms/platform/telink/sif_send.c').read_text(encoding='utf8')
# 与现有 A1.3.3 测试相同的替身边界：只移除平台头，生产函数体原样编译。
source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
fixture = (ROOT / 'tests/fixtures/sif_wire_model.h').read_text(encoding='utf8')
program = r'''
#define BMS_PRODUCT_SIF_PROTOCOL 2u
#define BMS_PRODUCT_CELL_COUNT 23u
#define BMS_PRODUCT_CHEMISTRY 1u
''' + fixture + r'''
typedef struct {
 uint8_t battery_temp_valid;
 uint16_t battery_temp_max_x10;
} bms_afe_feature_snapshot_t;
static uint8_t feature_available=1, feature_temp_valid=1;
static uint16_t feature_temp=300u;
static unsigned aux_calls, feature_calls;
static uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *out){
 ++feature_calls;
 out->battery_temp_valid=feature_temp_valid;
 out->battery_temp_max_x10=feature_temp;
 return feature_available;
}
''' + source + r'''
static void check_packet(unsigned soc,unsigned voltage,int temperature,
                         const uint8_t expected[6]){
 g_bms_report.soc.soc_percent=(uint16_t)soc;
 g_bms_report.pack_voltage_10mv=(uint16_t)voltage;
 feature_temp=(uint16_t)((temperature+40)*10);
 measurements_valid=feature_available=feature_temp_valid=1u;
 assert(sif_send_lishen_packet());
 assert(s_packet_lengths[s_prepare_packet]==6u);
 assert(!memcmp(s_packets[s_prepare_packet],expected,6u));
 assert(sum_verify(s_packets[s_prepare_packet],5u)==expected[5]);
}
static void require_level(const uint8_t *samples,unsigned at,int level){
 if(samples[at]!=(uint8_t)level){
  fprintf(stderr,"wave mismatch: tick=%u expected=%d actual=%u\n",
          at,level,(unsigned)samples[at]);
  abort();
 }
}
static void check_wave(const uint8_t *samples,unsigned start,const uint8_t packet[6]){
 unsigned i,j,at=start;
 for(i=0;i<124u;++i)require_level(samples,at++,0);
 for(i=0;i<4u;++i)require_level(samples,at++,1);
 for(i=0;i<48u;++i){
  unsigned value=(packet[i/8u]>>(7u-(i%8u)))&1u;
  unsigned low=value?4u:8u;
  for(j=0;j<low;++j)require_level(samples,at++,0);
  for(j=low;j<12u;++j)require_level(samples,at++,1);
 }
 for(i=0;i<50u;++i)require_level(samples,at++,0);
 for(i=0;i<4u;++i)require_level(samples,at++,1);
}
int main(void){
 uint8_t const example[6]={0x00u,0x32u,0x30u,0xf6u,0x00u,0x58u};
 uint8_t const full[6]={0x00u,0x64u,0x37u,0xffu,0x00u,0x9au};
 uint8_t const minus40[6]={0x00u,0x00u,0x01u,0xd8u,0x00u,0xd9u};
 static uint8_t levels[6500];
 uint8_t frames[2][6], active_copy[6];
 unsigned starts[2]={0u,0u}, starts_count=0,frames_count=0;
 unsigned t,i;
 int active_frame=0;

 assert(SIF_TIMER_INTERVAL_US==500u && SIF_SEND_COUNT==12u);
 assert(SIF_SYNC==128u && SIF_SYNC_LOW_TICKS==124u);
 assert(SIF_STOP_TICKS==54u && SIF_STOP_LOW_TICKS==50u);
 s_prepare_packet=0u;
 check_packet(50u,4800u,-10,example);
 check_packet(100u,5500u,-1,full);
 check_packet(0u,100u,-40,minus40);
 /* 不存在 FF 无效温度或假0：文档没有无效码定义，整帧不发布。 */
 measurements_valid=0u; assert(!sif_send_lishen_packet()); measurements_valid=1u;
 feature_available=0u; assert(!sif_send_lishen_packet()); feature_available=1u;
 feature_temp_valid=0u; assert(!sif_send_lishen_packet()); feature_temp_valid=1u;
 g_bms_report.soc.soc_percent=101u;assert(!sif_send_lishen_packet());
 g_bms_report.soc.soc_percent=50u;g_bms_report.pack_voltage_10mv=0u;
 assert(!sif_send_lishen_packet());
 g_bms_report.pack_voltage_10mv=25550u;assert(!sif_send_lishen_packet());
 g_bms_report.pack_voltage_10mv=4800u;feature_temp=1900u;
 assert(!sif_send_lishen_packet());

 /* 重新开始：第1帧发送中更新SOC/电压/温度，第1帧不可被覆盖。 */
 feature_temp=300u;
 g_bms_report.soc.soc_percent=50u;
 g_bms_report.pack_voltage_10mv=4800u;
 measurements_valid=feature_temp_valid=feature_available=1u;
 sif_timer_init();
 assert(!(reg_tmr_ctrl&FLD_TMR0_EN) && !(reg_irq_mask&FLD_IRQ_TMR0_EN));
 sif_set_bus_active(1u);
 assert((reg_tmr_ctrl&FLD_TMR0_EN) && (reg_irq_mask&FLD_IRQ_TMR0_EN));
 for(t=0;t<6500u;++t){
  wire_us=t*500u;
  if(t==2400u){
   g_bms_report.soc.soc_percent=100u;
   g_bms_report.pack_voltage_10mv=5500u;
   feature_temp=390u;
  }
  sif_prepare_task(1u+t/400u);
  if(state_mode==SYNC_SIGNAL && sif_sync_tosc==0u){
   assert(starts_count<2u);starts[starts_count++]=t;
  }
  SIF_STATE_E previous=state_mode;
  sif_send_data_handle();
  levels[t]=(uint8_t)tx_high;
  if(previous==SYNC_SIGNAL && state_mode==SEND_DATA){
   assert(frames_count<2u && sif_send_length==6u);
   memcpy(frames[frames_count],s_packets[s_active_packet],6u);
   memcpy(active_copy,frames[frames_count],6u);
   ++frames_count;active_frame=1;
  }
  if(active_frame){
   assert(!memcmp(active_copy,s_packets[s_active_packet],6u));
   if(state_mode==SIF_IDLE)active_frame=0;
  }
 }
 assert(starts_count==2u && frames_count==2u);
 assert(starts[0]==2000u);
 assert(starts[1]-starts[0]==2758u); /* 1.379s 帧起点间隔，符合 1~2s。 */
 assert(!memcmp(frames[0],example,6u));
 assert(!memcmp(frames[1],full,6u));
 for(i=0;i<2u;++i)check_wave(levels,starts[i],frames[i]);

 /* 待发数据失效必须撤销旧 ready；不能把旧样本当新有效帧发送。 */
 sif_set_bus_active(1u);
 g_bms_report.soc.soc_percent=50u;
 g_bms_report.pack_voltage_10mv=4800u;
 feature_temp=300u;
 sif_prepare_task(10000u);
 assert(s_packet_ready);
 feature_temp_valid=0u;
 sif_prepare_task(10001u);
 assert(!s_packet_ready);
 for(t=0;t<2400u;++t){
  wire_us=t*500u; sif_send_data_handle();
  assert(state_mode==SIF_IDLE && tx_high);
 }
 feature_temp_valid=1u;
 sif_prepare_task(10002u);
 assert(s_packet_ready);
 for(t=0;t<2200u;++t){
  wire_us=t*500u; sif_send_data_handle();
 }
 assert(state_mode==SEND_DATA && sif_send_length==6u);

 /* UART 中途接管重置状态；停止 TIMER0 后不残留待发包/低电平。 */
 bus=2;sif_send_data_handle();
 assert(state_mode==SIF_IDLE && !s_packet_ready && !sif_sync_tosc && !sif_send_tosc);
 bus=1;sif_set_bus_active(0u);
 assert(!(reg_tmr_ctrl&FLD_TMR0_EN) && !(reg_irq_mask&FLD_IRQ_TMR0_EN));
 assert(!s_idle_ticks && !s_packet_ready && state_mode==SIF_IDLE);
 printf("LISHEN_PASS frames=2 bytes=6 bit_us=6000 start_ms=1379 invalid_skips=8 uart_reset=1\n");
 return 0;
}
''';
with tempfile.TemporaryDirectory(prefix='sif-lishen-') as folder:
    c = Path(folder) / 'lishen.c'
    exe = Path(folder) / 'lishen.exe'
    c.write_text(program, encoding='utf8')
    command = shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
        str(c), '-o', str(exe)]
    try:
        subprocess.run(command, check=True)
        output = subprocess.check_output([str(exe)], text=True)
    except (subprocess.CalledProcessError, OSError):
        if os.environ.get('BMS_CASE_OUTPUT'):
            target = Path(os.environ['BMS_CASE_OUTPUT'])
            target.mkdir(parents=True, exist_ok=True)
            (target / 'lishen.c').write_text(program, encoding='utf8')
            (target / 'compile-command.json').write_text(
                json.dumps(command, ensure_ascii=False), encoding='utf8')
        raise
    assert 'LISHEN_PASS frames=2' in output
    print(output, end='')
    print('BMS_EVIDENCE ' + json.dumps({
        'domain': 'protocol', 'product': 'd008',
        'protocol': 'LISHEN_V111', 'frame_bytes': 6,
        'nominal_tick_us': 500, 'nominal_start_interval_ms': 1379,
        'proof': '真实 SIF TU 编译与 GPIO 时序替身，校验和/缓冲所有权/失效门禁/切总线',
        'blind_spot': '不证明真实 TC32 IRQ 最坏延迟、GPIO 外部电平及实板仪表'
    }, ensure_ascii=False))
