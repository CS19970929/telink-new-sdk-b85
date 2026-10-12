"""Check production SIF builders against A1.3.0; retain unaffected frozen wire fields."""
from pathlib import Path
import json, os, re, shlex, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'bms/platform/telink/sif_send.c').read_text(encoding='utf8')
source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
prefix = (ROOT / 'tests/fixtures/sif_wire_model.h').read_text(encoding='utf8')
golden = json.loads((ROOT / 'tests/fixtures/sif_wire_6604d738.json').read_text(encoding='utf8'))
tail = r'''
int main(void){
 for(unsigned n=0;n<8;++n){wire_case(n);
  sif_send_public_packet();print_packet(s_packets[s_prepare_packet],s_packet_lengths[s_prepare_packet]);
  sif_send_private_realtime_info();print_packet(s_packets[s_prepare_packet],s_packet_lengths[s_prepare_packet]);
  sif_send_private_cell_voltages();print_packet(s_packets[s_prepare_packet],s_packet_lengths[s_prepare_packet]);
 }
 memset(&g_bms_report,0,sizeof(g_bms_report));
 g_bms_report.charge_current_a10=123u;assert(sif_encoded_current()==4877u);
 assert(sif_work_status()==1u);
 g_bms_report.charge_current_a10=5000u;assert(sif_encoded_current()==0u);
 g_bms_report.charge_current_a10=5001u;assert(sif_encoded_current()==65535u);
 g_bms_report.charge_current_a10=0u;g_bms_report.discharge_current_a10=123u;
 assert(sif_encoded_current()==5123u&&sif_work_status()==0u);
 g_bms_report.discharge_current_a10=5000u;assert(sif_encoded_current()==10000u);
 g_bms_report.discharge_current_a10=5001u;assert(sif_encoded_current()==65535u);
 g_bms_report.discharge_current_a10=123u;g_bms_report.charge_current_a10=1u;
 assert(sif_encoded_current()==65535u&&sif_work_status()==15u);
 g_bms_report.discharge_current_a10=0u;g_bms_report.charge_current_a10=0u;
 assert(sif_encoded_current()==5000u&&sif_work_status()==15u);
 assert(sif_temperature(650u)==65u&&sif_temperature(1900u)==190u);
 assert(sif_temperature(0u)==255u&&sif_temperature(1901u)==255u);
 assert(sif_cell_index(0u)==255u&&sif_cell_index(BMS_PRODUCT_CELL_COUNT)==BMS_PRODUCT_CELL_COUNT);
 assert(sif_cell_index(BMS_PRODUCT_CELL_COUNT+1u)==255u);
 assert(sif_extreme_cell(61001u)==65535u&&sif_extreme_cell(3300u)==3300u);
 g_bms_report.cell_voltage_mv[0]=61001u;g_bms_report.cell_voltage_mv[1]=60001u;
 sif_send_private_cell_voltages();
 assert(s_packets[s_prepare_packet][3]==0x49u&&s_packets[s_prepare_packet][4]==0xeeu);
 assert(s_packets[s_prepare_packet][5]==255u&&s_packets[s_prepare_packet][6]==255u);
 capacity_factory=78u;sif_send_public_packet();
 assert(s_packets[s_prepare_packet][7]==78u&&s_packets[s_prepare_packet][8]==0u);
 config_available=0;sif_send_public_packet();
 assert(s_packets[s_prepare_packet][7]==255u&&s_packets[s_prepare_packet][8]==255u);
 config_available=1;
 s_packet_ready=0;s_active_packet=0;s_iswakeup=1;s_public_count=0;s_first_cell_packet=1;
 state_mode=SIF_IDLE;
 unsigned frames=0,cells_seen=0,last_cell_tick=0;uint8_t saved[64];unsigned length=0;
 for(unsigned tick=0;tick<400000&&frames<100;++tick){
  SIF_STATE_E previous=state_mode;
  /* 0.2s 样本刷新，16MHz tick 从回绕前开始；ISR 每 500us。 */
  wire_case(tick%8);sif_prepare_task((uint32_t)(0xffff0000u+(tick/400u)*3200000u));
  if(state_mode==SEND_DATA)assert(!memcmp(saved,s_packets[s_active_packet],length));
  sif_send_data_handle();
  if(previous==SYNC_SIGNAL&&state_mode==SEND_DATA){
   uint8_t id=s_packets[s_active_packet][0];
   if(frames<3)assert(id==1u);
   else if(frames==3)assert(id==0x3bu);
   else assert(id==0x3au||id==0x3bu);
   if(id==0x3bu){
    if(cells_seen)assert(tick-last_cell_tick>=119600u&&tick-last_cell_tick<125000u);
    last_cell_tick=tick;++cells_seen;
   }
   assert(sum_verify(s_packets[s_active_packet],sif_send_length-1u)==s_packets[s_active_packet][sif_send_length-1u]);
   length=sif_send_length;memcpy(saved,s_packets[s_active_packet],length);++frames;
  }
 }
 assert(frames==100&&cells_seen>=3u);return 0;
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
            assert len(raw) == (20 if raw[0] == 1 else raw[2] + 4)
            if raw[0] == 0x3B:
                assert raw[:3] == old[:3]
                for offset in range(3, len(raw)-1, 2):
                    mv = int.from_bytes(old[offset:offset+2], 'little')
                    expected = 65535 if mv > 60000 and mv != 61001 else mv
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
                    assert raw[22:27] == b'\xff' * 5 and raw[27:32] == b'\x00' * 5
    print('PASS %dS SIF protocol fields/SUM, 60s cells/tick wrap, immutable IRQ packet'%cells)
