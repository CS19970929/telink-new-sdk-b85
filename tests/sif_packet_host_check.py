"""Compare actual SIF builders with frozen 6604d738 TC32 packed wire vectors."""
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
 s_packet_ready=0;s_active_packet=0;s_iswakeup=1;s_public_count=0;s_private_count=0;
 state_mode=SIF_IDLE;
 unsigned frames=0;uint8_t saved[64];unsigned length=0;
 for(unsigned tick=0;tick<130000&&frames<20;++tick){
  SIF_STATE_E previous=state_mode;
  wire_case(tick%8);sif_prepare_task(tick/400u);
  if(state_mode==SEND_DATA)assert(!memcmp(saved,s_packets[s_active_packet],length));
  sif_send_data_handle();
  if(previous==SYNC_SIGNAL&&state_mode==SEND_DATA){
   uint8_t expected=frames<3?1u:(frames<18?0x3au:(frames==18?0x3bu:0x3au));
   assert(s_packets[s_active_packet][0]==expected);
   length=sif_send_length;memcpy(saved,s_packets[s_active_packet],length);++frames;
  }
 }
 assert(frames==20);return 0;
}
'''
for cells in (16,20,24):
    with tempfile.TemporaryDirectory(prefix='sif-wire-') as folder:
        c=Path(folder)/'check.c';exe=Path(folder)/'check.exe'
        c.write_text('#define BMS_PRODUCT_CELL_COUNT %du\n'%cells+prefix+source+tail,encoding='utf8')
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',
                       '-Wno-unused-function',str(c),'-o',str(exe)],check=True)
        output=subprocess.check_output([str(exe)],text=True).splitlines()
        assert output==golden['vectors'][str(cells)], cells
    print('PASS %dS SIF legacy wire vectors, first 3 public/15 realtime/cell cadence, immutable IRQ packet'%cells)
