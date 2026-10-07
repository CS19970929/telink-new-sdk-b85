"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sh_register_scenarios_host_check():
    print("CHECK sh_register_scenarios_host_check", flush=True)
    """真实默认构造 + 完整 SH control 函数体；只模拟寄存器总线和 GPIO。"""
    import os
    import re
    from validation_support import read, profile_prefix, without_includes, run_c, evidence

    product = os.environ.get('BMS_PRODUCT', 'd014')
    assert product in ('d011', 'd013', 'd014')
    prefix = profile_prefix(product)
    prefix += '#include "sh3673520.h"\n#include "sh3673510_control.h"\n#include "sh3673510_ntc.h"\n'
    # GPIO 引脚值在 host 仅为唯一键，不声称模拟 Telink 电气行为。
    for port in 'ABCD':
        for pin in range(8):
            prefix += f'#define GPIO_P{port}{pin} {(ord(port)-65)*8+pin}\n'
    prefix += read('tests/fixtures/sh_register_scenarios.c').replace(
        '/* CONTROL */', without_includes(read('bms/afe/sh3673510/sh3673510_control.c')))
    raw = run_c(prefix, ['bms/afe/sh3673510/sh3673510_ntc.c'],
                flags=['-Wno-unused-parameter', '-Wno-unused-function'], name='sh-registers')
    evidence({'domain': 'afe_registers', 'product': product, 'trace': raw.strip(),
              'boundary': '默认/量化/完整 control + RAM 寄存器；SPI CRC/时序与芯片模拟量未覆盖'})

def check_sh_factory_register_host_check():
    print("CHECK sh_factory_register_host_check", flush=True)
    """SH原厂SCT完整四位表、自清命令位与配置失败后FET门禁。"""
    import os
    from validation_support import read, profile_prefix, without_includes, run_c, evidence
    product=os.environ.get('BMS_PRODUCT','d014')
    code=profile_prefix(product)+'\n#include "sh3673520.h"\n#include "sh3673510_control.h"\n#include "sh3673510_ntc.h"\n'
    for port in 'ABCD':
        for pin in range(8):code+=f'#define GPIO_P{port}{pin} {(ord(port)-65)*8+pin}\n'
    fixture=read('tests/fixtures/sh_register_scenarios.c').split('int main(void)')[0]
    fixture=fixture.replace('*v=regs[r];if(operation==corrupt_at)',
        'if(r==0x42)regs[r]&=0xfe; /* p16 TRG异步自清。 */\n    *v=regs[r];if(operation==corrupt_at)')
    code+=fixture.replace('/* CONTROL */',without_includes(read('bms/afe/sh3673510/sh3673510_control.c')))
    code+=r'''
    int main(void) {
     /* 独立抄录原件p37–38枚举，未读取实现表生成期望。 */
     const unsigned oracle[]={0,32,64,96,128,192,224,256,288,320,384,448,480,512,544,576};
     bms_afe_hw_profile_build_default(&requested);reset_bus(0,0);
     assert(sh3673510_control_init());
     for(unsigned req=0;req<=576;++req) {
      operation=0;previous_write=0; /* 总线夹具逐次事务计数，避免256槽数组越界。 */
      requested.sc_delay_us=req;assert(sh3673510_control_apply_protection());
      unsigned expected=0;while(oracle[expected]<req)++expected;
      assert((regs[0x4f]&15u)==expected);
      sh3673510_protection_actual_t a;assert(sh3673510_control_get_protection_actual(&a));
      assert(a.sc_delay_us==oracle[expected]);
     }
     uint8_t changed;regs[0x42]=0x47;
     assert(sh3673510_control_set_load_detection(1,&changed));assert(changed && regs[0x42]==0x4a);
     /* 稳定owned位损坏必须继续拒绝；自清位不作为持久配置比较。 */
     corrupt_at=operation+3;regs[0x42]=0x44;
     assert(!sh3673510_control_set_load_detection(1,&changed));corrupt_at=0;
     operation=0;fail_at=1;assert(!sh3673510_control_apply_protection());fail_at=0;
     assert(!sh3673510_control_set_fets(1,1));assert(sh3673510_control_set_fets(0,0));
     assert(sh3673510_control_apply_protection());assert(sh3673510_control_set_fets(1,1));
     puts("PASS 577 SCT请求原厂解码、自清CRLD、配置失败FET拒绝与已验证恢复");return 0;
    }
    '''
    out=run_c(code,['bms/afe/sh3673510/sh3673510_ntc.c'],['-Wno-unused-parameter','-Wno-unused-function'],name='sh-factory-registers')
    evidence({'domain':'afe_factory_registers','product':product,'trace':out.strip(),'boundary':'完整control函数体与原NTC表；寄存器总线替身，不是芯片仿真'})

def check_sh_current_report_range_host_check():
    print("CHECK sh_current_report_range_host_check", flush=True)
    """实际SH测量发布：旧0.1A无符号协议饱和，极值不得回绕为零。"""
    import re
    from project_paths import selected_source
    from validation_support import ROOT, read, run_c, evidence
    fixture=read('tests/fixtures/sh_recovery_evidence.c').split('#if defined(MODE_TEST)')[0]
    profile=re.search(r'typedef struct\s*\{.*?\} bms_afe_hw_profile_t;',read('bms/core/bms_afe_hw_profile.h'),re.S).group()
    fixture=fixture.replace('/* PROFILE TYPE */',profile)
    fixture=fixture.replace('static int32_t bms_config_calibrate_current(int32_t ma) { return ma; }',
     'static int32_t calibration_result;\nstatic int32_t bms_config_calibrate_current(int32_t ma) { (void)ma;return calibration_result; }')
    fixture=fixture.replace('/* PRODUCTION */',selected_source(ROOT/'bms/afe/sh3673510/sh3673510_bms.c'))
    fixture+=r'''
    int main(void) {
     device.flag2=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
     sh3673510_bms_afe_init();for(unsigned i=0;i<12;++i)sample();
     const int32_t input[]={0,49,50,6553450,6553550,6553600,INT32_MAX,-49,-50,-6553450,-6553550,-6553600,INT32_MIN};
     const uint16_t expected[]={0,0,1,65535,65535,65535,65535,0,1,65535,65535,65535,65535};
     for(unsigned i=0;i<sizeof(input)/sizeof(input[0]);++i) {
      calibration_result=input[i];sample();
      assert(g_bms_report.discharge_current_a10==(input[i]>=0?expected[i]:0));
      assert(g_bms_report.charge_current_a10==(input[i]<0?expected[i]:0));
     }
     puts("PASS 13个校准电流边界，0.1A饱和与INT32_MIN有符号极值");return 0;
    }
    '''
    out=run_c(fixture,name='sh-current-report-range')
    evidence({'domain':'current_report','trace':out.strip(),'boundary':'实际SH后端函数体；校准结果注入，校准算法由独立数学回归覆盖；非实板极限精度'})

def check_sh3673510_board_host_check():
    print("CHECK sh3673510_board_host_check", flush=True)
    """Execute the selected SH board init and the actual sample-to-protection mapping."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import os
    import re
    import shlex
    import subprocess
    import tempfile

    ROOT = Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    product = os.environ.get('BMS_PRODUCT', 'd014')
    app = selected_source(APP / 'app.c')
    start = app.index('static void board_init(void)')
    board = app[start:app.index('\n}\n', start) + 3]
    source = selected_source(APP / 'sh3673510_bms.c')
    start = source.index('        memset(&sw, 0, sizeof(sw));')
    end = source.index('        bms_sw_protection_update(&sw);', start)
    mapping = source[start:end]
    expected = int(product != 'd013')
    code = r'''
    #include <stdint.h>
    #include <string.h>
    #include <assert.h>
    #include "bms_sw_protection.h"
    #include "bms_product.h"
    #include "sh3673520_reg.h"
    #define AS_GPIO 1
    #define GPIO_PA0 0
    #define GPIO_PD3 3
    #define GPIO_PD4 4
    static int function[8], input[8], output[8], value[8], output_inhibit;
    static void bms_afe_set_output_enabled(int enabled){output_inhibit=!enabled;}
    static void gpio_set_func(int p,int v){function[p]=v;}
    static void gpio_write(int p,int v){value[p]=v;}
    static void gpio_set_input_en(int p,int v){input[p]=v;}
    static void gpio_set_output_en(int p,int v){
     if(p==GPIO_PD4 && v)assert(function[p]==AS_GPIO && value[p]==1 && input[p]==0);
     output[p]=v;
    }
    ''' + board + r'''
    #define MOS_TEMP1 3
    static struct {uint16_t temperature_x10[4];} g_bms_report;
    static uint8_t s_ntc_valid[4];
    static uint8_t s_sample_pending, s_charger_removed, s_load_removed;
    static struct {uint8_t bstatus2;} status;
    static uint8_t battery_temperature_snapshot(uint16_t *low,uint16_t *high){*low=*high=650;return 1;}
    static bms_sw_protection_inputs_t sample(void){bms_sw_protection_inputs_t sw;
    ''' + mapping + r'''
     return sw;
    }
    int main(void){
     for(int i=0;i<8;++i)function[i]=input[i]=output[i]=value[i]=-1;
     board_init();
     assert(output_inhibit);
     assert(function[GPIO_PD4]==AS_GPIO && value[GPIO_PD4]==1);
     assert(input[GPIO_PD4]==0 && output[GPIO_PD4]==1);
     assert(input[GPIO_PA0]==1 && output[GPIO_PA0]==0);
     assert(input[GPIO_PD3]==1 && output[GPIO_PD3]==0);
     for(int valid=0;valid<=1;++valid){
      s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX]=valid;
      g_bms_report.temperature_x10[MOS_TEMP1]=1350;
      bms_sw_protection_inputs_t in=sample();
      assert(in.battery_temp_valid && in.battery_temp_min==650);
      assert(in.mos_temp_required==EXPECTED_MOS);
      assert(in.mos_temp_valid==(EXPECTED_MOS && valid));
      assert(in.mos_temp==((EXPECTED_MOS && valid)?1350:0));
     }
     return 0;
    }
    '''
    with tempfile.TemporaryDirectory(prefix='sh-board-') as d:
        c=Path(d)/'check.c';c.write_text(code);exe=Path(d)/'check'
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',
            '-DEXPECTED_MOS=%d'%expected,*host_includes(ROOT),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS '+product+' communication power GPIO sequencing and MOS capability/validity mapping')

if __name__ == "__main__":
    check_sh_register_scenarios_host_check()
    check_sh_factory_register_host_check()
    check_sh_current_report_range_host_check()
    check_sh3673510_board_host_check()
