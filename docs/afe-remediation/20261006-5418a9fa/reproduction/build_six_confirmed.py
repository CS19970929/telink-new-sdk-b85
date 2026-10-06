from pathlib import Path
import os,subprocess,json
root=Path('D:/telink/bms-monorepo');out=Path(__file__).parent/'production-confirmed';out.mkdir(exist_ok=True)
env=dict(os.environ,PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',BMS_BUILD_ROOT=str(Path(__file__).parent/'target'))
rows=[]
for product,profile in [('d008','16s-lfp'),('d008','20s-nmc'),('d008','24s-lfp'),('d011',None),('d013',None),('d014',None)]:
    args=['C:/Tools/Python312/python.exe','-B','bms_tools/bms.py','--production','--product',product]
    if profile:args+=['--d008-profile',profile]
    key=product+('-'+profile if profile else '')
    for operation,tail in [('link',['--jobs','4']),('resources',[])]:
        cmd=args+[operation]+tail
        r=subprocess.run(cmd,cwd=root,env=env,capture_output=True)
        (out/(key+'-'+operation+'.log')).write_bytes(r.stdout+r.stderr)
        row={'key':key,'operation':operation,'command':cmd,'exit':r.returncode};rows.append(row);print(row,flush=True)
        if r.returncode:print((r.stdout+r.stderr).decode('utf8','replace')[-2000:],flush=True)
(out/'commands.json').write_text(json.dumps(rows,indent=2),encoding='utf8')
