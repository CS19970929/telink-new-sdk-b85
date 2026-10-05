"""复位策略探针，输出实际缺口；不是把已知误恢复标成绿色的默认回归。"""
import argparse
import json
import os
import re
from pathlib import Path
import tempfile
from d014_safety_loop_host_check import SOURCES
from validation_support import ROOT, ScenarioFailure, read, run_c
from run_host_regression import fingerprint


def policy_gap(error, direction, history):
    marker = (f'RESET_POLICY_GAP direction={direction} history={history} '
              'expected=OFF_UNTIL_PHYSICAL_RELEASE actual=ON')
    observation = (rf'RESET_OBSERVATION direction={direction} history={history} '
                   rf'recorded={int(history == "durable")} first_on_sample=([1-9][0-9]*) current_a10=0')
    match = re.fullmatch(observation, error.stdout.strip())
    if error.returncode != 1 or error.stderr.strip() != marker or not match:
        raise error
    return int(match.group(1))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    if os.environ.get('BMS_PRODUCT', 'd014') != 'd014':
        parser.error('此探针只能用于 D014，请设置 BMS_PRODUCT=d014')
    output=args.output.resolve()
    if output.is_relative_to(ROOT) or output.exists():
        parser.error('输出必须是源码树外不存在的文件')
    compiled=set(read('bms/products/d014/sources.txt').splitlines())
    assert set(SOURCES)<=compiled
    results=[]
    inputs = fingerprint()
    with tempfile.TemporaryDirectory(prefix='bms-reset-probe-') as directory:
        for direction in ('discharge','charge'):
            for history in ('pending','durable'):
                image=str(Path(directory)/(direction+'-'+history+'.flash'))
                shared={'BMS_RESET_IMAGE':image,'BMS_RESET_DIRECTION':direction,'BMS_RESET_HISTORY':history}
                # Seed and boot need distinct processes, with only the Flash bytes carried over.
                # Run the seed separately so expected-failure handling never hides a seed error.
                flags=['-I',str(ROOT/'tests/fixtures/d014_safety_loop/include')]
                code=read('tests/fixtures/d014_safety_loop/reset_probe.c')
                seed=run_c(code,SOURCES,flags,name='reset-seed',runs=[{**shared,'BMS_RESET_STAGE':'seed'}])
                try:
                    observed=run_c(code,SOURCES,flags,name='reset-boot',
                                   runs=[{**shared,'BMS_RESET_STAGE':'boot'}])
                    results.append({'direction':direction,'history':history,'gate_passed':True,
                                    'seed':seed,'stdout':observed})
                except ScenarioFailure as error:
                    # Only the exact policy assertion counts as a known gap; all other failures propagate.
                    first_on = policy_gap(error,direction,history)
                    results.append({'direction':direction,'history':history,'gate_passed':False,
                                    'seed':seed,'returncode':error.returncode,
                                    'stdout':error.stdout,'stderr':error.stderr,'first_on_sample':first_on})
    assert inputs == fingerprint(), '探针运行期间输入发生变化，结果无效'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps({'kind':'reset-policy-probe','policy':'OFF_UNTIL_PHYSICAL_RELEASE',
        'sanitizers':os.environ.get('BMS_SANITIZE','0'),'results':results,'inputs':inputs,
        'boundary':'新进程模拟普通初始化的 RAM 丢失；不模拟物理 watchdog、掉电波形或完整 AFE reset'},
        ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return 0 if all(r['gate_passed'] for r in results) else 1


if __name__=='__main__':
    raise SystemExit(main())
