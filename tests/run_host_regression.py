"""四产品统一验证：源码指纹、故障日志、配置差异、机器报告与人工报告。"""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
import signal
import shlex
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from validation_catalog import PRODUCTS, CHECKS, SOURCES, BLIND_SPOTS

ROOT = Path(__file__).resolve().parents[1]


def targets(name):
    return CHECKS[Path(name).stem]['products']


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT).decode('utf-8', 'replace').strip()


def fingerprint():
    """包含被本机 exclude 忽略的新测试，不能只相信 Git dirty。"""
    files = set()
    for base in ('bms', 'tests', 'bms_tools', 'tools/soc_simulator', '.github/workflows'):
        files.update(p for p in (ROOT/base).rglob('*') if p.is_file() and
                     p.suffix.lower() in ('.c','.h','.py','.json','.txt','.mk','.yml','.yaml'))
    files.add(ROOT/'CMakeLists.txt')
    sdk=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk'
    files.update(p for p in sdk.rglob('*') if p.is_file() and p.suffix.lower() in ('.h','.c','.s','.a','.ld'))
    for product in PRODUCTS:
        manifest = ROOT/'bms/products'/product/'sources.txt'
        for line in manifest.read_text(encoding='utf-8').splitlines():
            value = line.strip()
            if value and not value.startswith('#'):
                path = ROOT/value
                if path.is_file(): files.add(path)
    values = {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
    return {'sha256':hashlib.sha256(json.dumps(values,sort_keys=True).encode()).hexdigest(),'files':values}


def execute(command, env, timeout):
    """超时终止进程树并保留输出；启动失败也必须成为红色报告。"""
    started = time.monotonic()
    options = {'start_new_session':True} if os.name != 'nt' else {'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP}
    try:
        process = subprocess.Popen(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,**options)
        try:
            output,_ = process.communicate(timeout=timeout)
            code=process.returncode
        except subprocess.TimeoutExpired:
            if os.name=='nt':
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
            else:
                os.killpg(process.pid,signal.SIGKILL)
            output,_=process.communicate()
            output += b'\nTIMEOUT: process tree terminated\n'
            code=124
    except OSError as error:
        output=str(error).encode('utf-8');code=127
    return code,output,round(time.monotonic()-started,3)


def commands_for(selected, only):
    found = {p.stem for p in (ROOT/'tests').glob('*_check.py')}
    if found != set(CHECKS):
        raise ValueError(f'测试登记不一致：未登记={sorted(found-set(CHECKS))} 文件缺失={sorted(set(CHECKS)-found)}')
    commands=[]
    if not only:
        commands.append(('tooling_unit_tests','shared','tooling','工具单测（内部枚举产品配置）',[sys.executable,'-m','unittest','tests.test_bms_tools','-q']))
        commands += [
            ('validation_runner_tests','shared','tooling','工具故障回归',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_validation_runner.py','-q']),
            ('source_manifests','shared','tooling','四产品实际源码清单',[sys.executable,'bms_tools/bms.py','--all-products','sources','--check'])]
    for name,meta in sorted(CHECKS.items()):
        if only and name not in only: continue
        for product in meta['products']:
            if product in selected:
                commands.append((name,product,meta['domain'],meta['evidence'],[sys.executable,str(ROOT/'tests'/(name+'.py'))]))
    if only and set(only)-set(CHECKS): raise ValueError('未知检查：'+str(set(only)-set(CHECKS)))
    if not commands: raise ValueError('没有适用测试，拒绝空绿色')
    return commands


def differences(before, after, prefix=''):
    if isinstance(before,dict) and isinstance(after,dict):
        rows=[]
        for key in sorted(set(before)|set(after)):
            rows.extend(differences(before.get(key),after.get(key),prefix+'/'+key))
        return rows
    if isinstance(before,list) and isinstance(after,list) and len(before)==len(after):
        rows=[]
        for i,(a,b) in enumerate(zip(before,after)):rows.extend(differences(a,b,prefix+'/'+str(i)))
        return rows
    return [] if before==after else [{'field':prefix,'before':before,'after':after}]


def consistency_errors(results):
    """相同输入保护场景必须逐产品得到相同输出；部分运行不伪造四产品结论。"""
    rows=[r for r in results if r['check']=='protection_scenarios_host_check' and r['exit']==0]
    if not rows: return []
    if any(not r['observations'] for r in rows):return ['保护场景缺少结构化输出']
    reference=rows[0]
    return [f"公共保护场景差异：{reference['product']} / {r['product']}" for r in rows[1:]
            if r['observations']!=reference['observations']]


def compiler_identity():
    command=shlex.split(os.environ.get('CC','cc'))
    path=shutil.which(command[0])
    code,raw,_=execute([*command,'--version'],os.environ.copy(),10)
    return {'command':command,'path':path,'version':raw.decode('utf-8','replace'), 'exit':code,
            'sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest() if path else None}


def write_reports(output, report):
    for filename,value in (('report.json',report),('results.json',report['results'])):
        (output/filename).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    integrity=report.get('integrity_errors',[])
    suite=ET.Element('testsuite',name='BMS',tests=str(len(report['results'])+len(integrity)),failures=str(sum(r['exit']!=0 for r in report['results'])+len(integrity)))
    for index,error in enumerate(integrity):
        case=ET.SubElement(suite,'testcase',classname='validation.integrity',name=str(index))
        ET.SubElement(case,'failure',message=error).text=error
    lines=['# BMS 软件验证报告','',f"- 源码：`{report['git']['head']}`；输入指纹：`{report['inputs']['sha256']}`",
           f"- 结论：**{report['verdict']}**；计划 {report['planned']} 组，完成 {len(report['results'])} 组。",
           '- PASS 仅表示已列出的软件断言成立；硬件验收未由本报告证明。','',
           '| 产品 | 完成 | 失败 |','|---|---:|---:|']
    for p in (*PRODUCTS,'shared'):
        entries=[r for r in report['results'] if r['product']==p]
        lines.append(f"| {p} | {len(entries)} | {sum(r['exit']!=0 for r in entries)} |")
    lines += ['', '## 各领域执行证据', '', '| 产品 | 领域 | 通过/完成 | 证据类别 |','|---|---|---:|---|']
    for p in PRODUCTS:
        for domain in SOURCES:
            entries=[r for r in report['results'] if r['product']==p and r['domain']==domain]
            kinds='、'.join(sorted({r['evidence_level'] for r in entries})) or '未执行'
            lines.append(f"| {p} | {domain} | {sum(r['exit']==0 for r in entries)}/{len(entries)} | {kinds} |")
    lines += ['', '## 失败与复现', '']
    for r in report['results']:
        case=ET.SubElement(suite,'testcase',classname=r['product']+'.'+r['domain'],name=r['check'],time=str(r['seconds']))
        if r['exit']:
            ET.SubElement(case,'failure',message='exit='+str(r['exit'])).text=r['failure_tail']
            lines += [f"### {r['product']} / {r['check']}", '',f"日志：[{r['log']}]({r['log']})；退出码 {r['exit']}。",'',
                      '复现环境：`BMS_PRODUCT='+r['product']+'`，命令参数见 report.json。', '', '```text',r['failure_tail'],'```','',
                      '优先检查：'+', '.join(r['source_hints'])+'；相关路径不是未经定位的根因结论。','']
    if all(r['exit']==0 for r in report['results']):lines += ['已完成的测试无失败。','']
    lines += ['- 验证完整性失败：'+error for error in integrity]
    rows=[r for r in report['results'] if r['check']=='protection_scenarios_host_check' and r['exit']==0]
    lines += ['',f"相同保护输入已在 {len(rows)} 个产品运行；结构化输出比较：{'不一致' if consistency_errors(report['results']) else '已执行产品一致' if rows else '未执行'}。",'']
    lines += ['## 配置与行为比较','',f"与指定基线的结构化变化：{len(report['behavior_changes'])} 项；未指定基线时不作历史一致性结论。",'']
    for change in report['behavior_changes']:
        lines.append(f"- `{change['field']}`：`{change['before']}` → `{change['after']}`")
    configs = [(r['product'],v) for r in report['results'] for v in r['observations'] if v.get('domain')=='configuration']
    lines += ['', '| 产品/profile | 串数 | 容量(0.1Ah) | SW CUV3(mV) | AFE CUV(mV) | AFE mask |','|---|---:|---:|---:|---:|---:|']
    for product,value in configs:
        for profile,c in value['profiles'].items():
            lines.append(f"| {product}/{profile} | {c['cells']} | {c['capacity_0p1ah']} | {c['sw']['u16VcellUvp_Third']} | {c['afe_requested']['cuv_mv']} | {c['afe_requested']['enable_mask']} |")
    lines += ['', '65 个软件字段、35 个 AFE requested 字段及两类 AFE 寄存器实写轨迹保存在 report.json；它们是开发配置，不是产品签核。','', '## 风险与盲区','']
    for risk in BLIND_SPOTS:lines.append(f"- **{risk['risk']} / {risk['area']}**：{risk['gap']}")
    if report['source_changed_during_run']:lines += ['','**源码在验证期间发生变化，本轮拒绝给出统一 PASS。**']
    (output/'report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    ET.ElementTree(suite).write(output/'junit.xml',encoding='utf-8',xml_declaration=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--product',action='append',choices=PRODUCTS)
    parser.add_argument('--only',action='append',help='按完整 stem 选择检查；报告标为部分验证')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--baseline',type=Path,help='上一轮 report.json；只比较，不自动接受变化')
    parser.add_argument('--timeout',type=float,default=300)
    parser.add_argument('--with-targets',action='store_true',help='另外执行四产品 ELF/resource；不生成 BIN')
    args=parser.parse_args()
    if args.timeout<=0:parser.error('timeout 必须为正')
    selected=tuple(args.product or PRODUCTS)
    commands=commands_for(selected,args.only)
    if args.with_targets:
        for p in selected:
            for action in ('link','resources'):
                command=[sys.executable,'bms_tools/bms.py','--product',p,action]
                if action=='link':command+=['--jobs','4']
                commands.append(('target_'+action,p,'tooling','目标 ELF/MAP',command))
    stamp=datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    default=Path.home()/'Documents/CodexOutputs/bms-monorepo/validation'/stamp
    output=(args.output or Path(os.environ.get('BMS_TEST_OUTPUT',default))).resolve()
    if output.is_relative_to(ROOT):raise ValueError('报告必须放源码树外')
    if output.exists() and any(output.iterdir()):raise ValueError('拒绝覆盖已有证据目录：'+str(output))
    baseline=json.loads(args.baseline.read_text(encoding='utf-8')) if args.baseline else None
    output.mkdir(parents=True,exist_ok=True)
    report={'schema':1,'started_utc':datetime.now(timezone.utc).isoformat(),
            'git':{'head':git('rev-parse','HEAD'),'tree':git('rev-parse','HEAD^{tree}'),'status':git('status','--porcelain=v1'),
                   'recent_commits':git('log','-5','--format=%h %s')},
            'inputs':fingerprint(),'tools':{'python':sys.version,'platform':platform.platform(),'cc':compiler_identity(),
                                         'sanitizers':os.environ.get('BMS_SANITIZE','0')},
            'planned':len(commands),'results':[],'behavior_changes':[], 'blind_spots':BLIND_SPOTS,
            'source_changed_during_run':False,'verdict':'未完成','partial':bool(args.only or args.product)}
    print('报告目录：'+str(output),flush=True)
    try:
        for name,product,domain,level,command in commands:
            case_id=name+'-'+product
            env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONUTF8='1',BMS_PRODUCT=product if product!='shared' else 'd014',
                     BMS_CASE_OUTPUT=str(output/case_id))
            code,raw,seconds=execute(command,env,args.timeout)
            log=case_id+'.log';(output/log).write_bytes(raw)
            decoded=raw.decode('utf-8','replace')
            observations=[]
            for line in decoded.splitlines():
                if line.startswith('BMS_EVIDENCE '):observations.append(json.loads(line[len('BMS_EVIDENCE '):]))
            report['results'].append({'check':name,'product':product,'domain':domain,'evidence_level':level,
                'exit':code,'seconds':seconds,'command':command,'log':log,'observations':observations,
                'source_hints':SOURCES[domain],'failure_tail':decoded[-6000:] if code else ''})
            print(('PASS ' if code==0 else 'FAIL ')+case_id+f' ({seconds}s)',flush=True)
            (output/'results.json').write_text(json.dumps(report['results'],ensure_ascii=False,indent=2),encoding='utf-8')
    finally:
        end=fingerprint()
        report['source_changed_during_run']=end['sha256']!=report['inputs']['sha256'] or git('rev-parse','HEAD')!=report['git']['head']
        observed={r['product']+'/'+r['check']:r['observations'] for r in report['results'] if r['observations']}
        if baseline:
            old={r['product']+'/'+r['check']:r['observations'] for r in baseline['results'] if r.get('observations')}
            report['behavior_changes']=differences(old,observed)
            report['source_changes']=differences(baseline['inputs']['files'],report['inputs']['files'])
            report['baseline']=str(args.baseline.resolve())
        complete=len(report['results'])==len(commands)
        report['integrity_errors']=consistency_errors(report['results'])
        if not complete: report['integrity_errors'].append('计划任务未全部完成')
        if report['source_changed_during_run']:report['integrity_errors'].append('运行期间源码内容或 HEAD 改变')
        passed=not report['integrity_errors'] and all(r['exit']==0 for r in report['results'])
        report['verdict']=('部分验证通过' if report['partial'] else '软件验证通过') if passed else '失败或未完成'
        write_reports(output,report)
    print(f"{report['verdict']}：{len(report['results'])} 组，{sum(r['exit']!=0 for r in report['results'])} 失败；{output/'report.md'}",flush=True)
    return 0 if passed else 1


if __name__=='__main__':
    sys.exit(main())
