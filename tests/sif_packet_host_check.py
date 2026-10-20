"""Check A1.3.3 public SIF fields, invalid sampling and connection bursts; no BIN output."""
from pathlib import Path
import json, os, re, shlex, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'bms/platform/telink/sif_send.c').read_text(encoding='utf8')
source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
prefix = (ROOT / 'tests/fixtures/sif_wire_model.h').read_text(encoding='utf8')
golden = json.loads((ROOT / 'tests/fixtures/sif_wire_6604d738.json').read_text(encoding='utf8'))
tail = r'''
static void check_code(const uint8_t *expected,unsigned length){
 sif_send_private_battery_code();
 const uint8_t *p=s_packets[s_prepare_packet];
 assert(p[0]==0x3cu&&p[1]==SIF_PROTOCOL_VERSION&&p[2]==length);
 assert(s_packet_lengths[s_prepare_packet]==length+4u);
 assert(!memcmp(p+3,expected,length));
 assert(p[3u+length]==sum_verify(p,3u+length));
}
int main(void){
 sif_timer_init();assert(!(reg_tmr_ctrl&FLD_TMR0_EN)&&!(reg_irq_mask&FLD_IRQ_TMR0_EN));
 sif_set_bus_active(1u);assert((reg_tmr_ctrl&FLD_TMR0_EN)&&(reg_irq_mask&FLD_IRQ_TMR0_EN));
 for(unsigned n=0;n<8;++n){wire_case(n);
  sif_send_public_packet();print_packet(s_packets[s_prepare_packet],s_packet_lengths[s_prepare_packet]);
  sif_send_private_realtime_info();print_packet(s_packets[s_prepare_packet],s_packet_lengths[s_prepare_packet]);
  sif_send_private_cell_voltages();print_packet(s_packets[s_prepare_packet],s_packet_lengths[s_prepare_packet]);
 }
 memset(&g_bms_report,0,sizeof(g_bms_report));
 g_bms_report.charge_current_a10=123u;assert(sif_encoded_current(1u)==4877u);
 assert(sif_work_status(1u)==1u);
 g_bms_report.charge_current_a10=5000u;assert(sif_encoded_current(1u)==0u);
 g_bms_report.charge_current_a10=5001u;assert(sif_encoded_current(1u)==65535u);
 g_bms_report.charge_current_a10=0u;g_bms_report.discharge_current_a10=123u;
 assert(sif_encoded_current(1u)==5123u&&sif_work_status(1u)==0u);
 g_bms_report.discharge_current_a10=5000u;assert(sif_encoded_current(1u)==10000u);
 g_bms_report.discharge_current_a10=5001u;assert(sif_encoded_current(1u)==65535u);
 g_bms_report.discharge_current_a10=123u;g_bms_report.charge_current_a10=1u;
 assert(sif_encoded_current(1u)==65535u&&sif_work_status(1u)==255u);
 g_bms_report.discharge_current_a10=0u;g_bms_report.charge_current_a10=0u;
 assert(sif_encoded_current(1u)==5000u&&sif_work_status(1u)==255u);
 assert(sif_encoded_current(0u)==65535u&&sif_work_status(0u)==255u);
 assert(sif_temperature(650u)==65u&&sif_temperature(1900u)==190u);
 assert(sif_temperature(0u)==255u&&sif_temperature(1901u)==255u);
 assert(sif_cell_index(0u)==255u&&sif_cell_index(BMS_PRODUCT_CELL_COUNT)==BMS_PRODUCT_CELL_COUNT);
 assert(sif_cell_index(BMS_PRODUCT_CELL_COUNT+1u)==255u);
 assert(sif_extreme_cell(61001u)==65535u&&sif_extreme_cell(3300u)==3300u);
 g_bms_report.cell_voltage_mv[0]=61001u;g_bms_report.cell_voltage_mv[1]=60001u;
 sif_send_private_cell_voltages();
 assert(s_packets[s_prepare_packet][3]==255u&&s_packets[s_prepare_packet][4]==255u);
 assert(s_packets[s_prepare_packet][5]==255u&&s_packets[s_prepare_packet][6]==255u);
 assert(g_bms_report.cell_voltage_mv[0]==61001u); /* 只改线边界，不修改内部报告。 */
 measurements_valid=0u;sif_send_private_realtime_info();
 const uint8_t *invalid=s_packets[s_prepare_packet];
 assert(invalid[3]==255u&&invalid[4]==255u&&invalid[5]==255u);
 assert(invalid[6]==255u&&invalid[7]==255u&&invalid[8]==255u&&invalid[9]==255u&&invalid[10]==255u);
 for(unsigned i=16;i<=21;++i)assert(invalid[i]==255u);
 sif_send_private_cell_voltages();
 for(unsigned i=3;i<SIF_CELL_BYTES-1u;++i)assert(s_packets[s_prepare_packet][i]==255u);
 sif_send_public_packet();
 for(unsigned i=9;i<=16;++i)assert(s_packets[s_prepare_packet][i]==255u);
 measurements_valid=1u;
 g_bms_report.fault_first.bits.charge_otp=1u;assert(sif_fault_code()==0x24u);
 g_bms_report.fault_second.bits.charge_otp=1u;assert(sif_fault_code()==0x14u);
 g_bms_report.fault_third.bits.pack_uvp=1u;assert(sif_fault_code()==0x06u);
 memset(&g_bms_report.fault_first,0,sizeof(g_bms_report.fault_first));
 memset(&g_bms_report.fault_second,0,sizeof(g_bms_report.fault_second));
 memset(&g_bms_report.fault_third,0,sizeof(g_bms_report.fault_third));
 capacity_factory=78u;sif_send_public_packet();
 assert(s_packets[s_prepare_packet][7]==78u&&s_packets[s_prepare_packet][8]==0u);
 config_available=0;sif_send_public_packet();
 assert(s_packets[s_prepare_packet][7]==255u&&s_packets[s_prepare_packet][8]==255u);
 config_available=1;
 const uint8_t invalid_code[]={0xffu};
 check_code(invalid_code,1u); /* 空配置不能回退成共享编译默认 SN。 */
 memcpy(config_serial,"BMS-123",8u);check_code((const uint8_t *)"BMS-123",7u);
 user_available=0;check_code(invalid_code,1u);user_available=1;
 memset(config_serial,'X',sizeof(config_serial));
 check_code((const uint8_t *)config_serial,32u); /* 满32字节不要求 NUL，也不截断。 */
 config_serial[31]=(char)0xffu;check_code(invalid_code,1u);
 config_serial[31]=(char)0x7fu;check_code(invalid_code,1u);
 memset(config_serial,' ',sizeof(config_serial));check_code(invalid_code,1u);
 memset(config_serial,0,sizeof(config_serial));
 memcpy(config_serial,"A\0B",3u);check_code((const uint8_t *)"A",1u);
 memcpy(config_serial,"NEW-SN",7u);check_code((const uint8_t *)"NEW-SN",6u);
 s_packet_ready=0;s_active_packet=0;s_iswakeup=1;s_public_count=0;s_first_cell_packet=1;s_code_packet_due=0;
 state_mode=SIF_IDLE;
 unsigned frames=0,cells_seen=0,codes_seen=0,last_cell_tick=0;
 unsigned code_expected=0;uint8_t saved[64];unsigned length=0;
 for(unsigned tick=0;tick<400000&&frames<100;++tick){
  wire_us=tick*500u;
  SIF_STATE_E previous=state_mode;
  /* 0.2s 样本刷新，16MHz tick 从回绕前开始；ISR 每 500us。 */
  wire_case(tick%8);config_serial[0]=(char)('A'+tick%26);
  sif_prepare_task((uint32_t)(0xffff0000u+(tick/400u)*3200000u));
  if(state_mode==SEND_DATA)assert(!memcmp(saved,s_packets[s_active_packet],length));
  sif_send_data_handle();
  if(previous==SYNC_SIGNAL&&state_mode==SEND_DATA){
   uint8_t id=s_packets[s_active_packet][0];
   if(frames<3)assert(id==1u);
   else if(frames==3)assert(id==0x3bu);
   else if(frames>=4&&frames<=6)assert(id==0x3cu);
   else assert(id==0x3au||id==0x3bu||id==0x3cu);
   if(id==0x3bu){
    assert(!code_expected);code_expected=cells_seen?1u:3u;
    if(cells_seen)assert(tick-last_cell_tick>=119600u&&tick-last_cell_tick<125000u);
    last_cell_tick=tick;++cells_seen;
   }else if(id==0x3cu){
    assert(code_expected);--code_expected;
    assert(tick-last_cell_tick<14000u);++codes_seen;
   }else{assert(!code_expected);}
   assert(sum_verify(s_packets[s_active_packet],sif_send_length-1u)==s_packets[s_active_packet][sif_send_length-1u]);
   length=sif_send_length;memcpy(saved,s_packets[s_active_packet],length);++frames;
  }
 }
 assert(frames==100&&cells_seen>=3u&&codes_seen>=3u);
 // UART 在帧中途接管，重新接一线通后必须从完整同步/公有帧开始。
 state_mode=SEND_DATA;sif_sync_tosc=7u;sif_send_tosc=3u;byte_cnt=12u;bit_cnt=5;
 bus=2;sif_send_data_handle();
 assert(state_mode==SIF_IDLE&&!sif_sync_tosc&&!sif_send_tosc&&!byte_cnt&&!bit_cnt&&!s_packet_ready);
 bus=1;sif_prepare_task(0u);
 assert(s_packets[s_prepare_packet][0]==1u&&s_first_cell_packet&&!s_code_packet_due);
 s_idle_ticks=99u;s_return_to_idle=1u;state_mode=SEND_DATA;
 sif_set_bus_active(0u);
 assert(!(reg_tmr_ctrl&FLD_TMR0_EN)&&!(reg_irq_mask&FLD_IRQ_TMR0_EN));
 assert(!s_idle_ticks&&!s_return_to_idle&&!s_packet_ready&&state_mode==SIF_IDLE);
 return 0;
}
'''
for cells in (16,20,24):
    with tempfile.TemporaryDirectory(prefix='sif-wire-') as folder:
        c=Path(folder)/'check.c';exe=Path(folder)/'check.exe'
        chemistry = 2 if cells == 20 else 1
        c.write_text('#define BMS_PRODUCT_CELL_COUNT %du\n#define BMS_PRODUCT_CHEMISTRY %du\n'%(cells,chemistry)+prefix+source+tail,encoding='utf8')
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',
                       '-Wno-unused-function',str(c),'-o',str(exe)],check=True)
        output=subprocess.check_output([str(exe)],text=True).splitlines()
        assert len(output) == 24
        for i, line in enumerate(output):
            raw = bytes.fromhex(line)
            old = bytes.fromhex(golden['vectors'][str(cells)][i])
            assert sum(raw[:-1]) & 255 == raw[-1]
            assert raw[1] == 0x13
            assert len(raw) == (20 if raw[0] == 1 else raw[2] + 4)
            if raw[0] == 0x3B:
                assert raw[0] == old[0] and raw[2] == old[2]
                for offset in range(3, len(raw)-1, 2):
                    mv = int.from_bytes(old[offset:offset+2], 'little')
                    expected = 65535 if mv > 60000 else mv
                    assert int.from_bytes(raw[offset:offset+2], 'little') == expected
            else:
                start = 9 if raw[0] == 1 else 3
                current = int.from_bytes(raw[start+3:start+5], 'little')
                assert current <= 10000 or current == 65535
                assert raw[start] <= 200 or raw[start] == 255
                assert all(v <= 190 or v == 255 for v in raw[start+5:start+8])
                assert raw[start+1:start+3] == old[start+1:start+3]  # 总压缩放保持。
                assert raw[start+8] == old[start+8]  # 故障优先级保持。
                if raw[0] == 1:
                    assert raw[2:4] == b'\xff\xff' and raw[4] == (3 if chemistry == 2 else 1)
                    assert raw[5:7] == b'\xff\xff' and raw[7:9] == b'\x74\x00'
                else:
                    assert raw[13] == 255 and raw[22:27] == b'\xff' * 5
                    assert raw[27] == 0 and raw[28:32] == b'\xff' * 4
    print('PASS %dS SIF fields/SUM/SN boundaries, 60s cells+code/tick wrap/reconnect, immutable IRQ packet'%cells)
