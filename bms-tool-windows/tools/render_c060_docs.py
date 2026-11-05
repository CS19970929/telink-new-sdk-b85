"""Render the C060 proposal, implementation guide and host comparison offline.
Only formal HTML outputs are written beside their Markdown sources.
"""
from pathlib import Path
import re, html, json, sys
sys.dont_write_bytecode=True
OUT=Path(__file__).resolve().parents[1]/'docs'

def render(stem):
    md=(OUT/(stem+'.md')).read_text(encoding='utf-8')
    evidence_name='NEW_OLD_HOST_COMPARISON_EVIDENCE.json' if stem=='NEW_OLD_HOST_COMPARISON' else 'UNIFIED_UPGRADE_EVIDENCE.json' if stem=='UNIFIED_UPGRADE' else 'C060_ADAPTATION_EVIDENCE.json'
    evidence=json.loads((OUT/evidence_name).read_text(encoding='utf-8'))
    roots={key:Path(evidence[key]['path']) for key in ('host','firmware','old') if key in evidence}
    paths={s['path']:roots[s['repository']]/s['path'] for s in evidence['source_sha256']}
    def inline(s):
        stash=[]
        def hold(v):
            stash.append(v);return '\x00'+str(len(stash)-1)+'\x00'
        def code(m):
            t=m.group(1)
            match=re.fullmatch(r'(.+):(\d+)',t)
            if match and match[1] in paths:
                return hold('<a class="source" href="'+html.escape(paths[match[1]].as_uri(),quote=True)+'" title="打开源码；行号 '+match[2]+'"><code>'+html.escape(t)+'</code></a>')
            return hold('<code>'+html.escape(t)+'</code>')
        s=re.sub(r'`([^`]+)`',code,s)
        s=html.escape(s)
        s=re.sub(r'\*\*(.+?)\*\*',r'<strong>\1</strong>',s)
        for i,v in enumerate(stash):s=s.replace('\x00'+str(i)+'\x00',v)
        return s
    lines=md.splitlines(); parts=[];toc=[];i=0;sec=0;insection=False
    while i<len(lines):
        line=lines[i]
        if not line.strip():i+=1;continue
        if line.startswith('# '):i+=1;continue
        m=re.match(r'^(#{2,3}) (.+)$',line)
        if m:
            level=len(m[1]);title=m[2];anchor='chapter-'+str(len(toc)+1)
            if level==2:
                if insection:parts.append('</section>')
                sec+=1;parts.append('<section class="doc-section" data-index="'+str(sec)+'">');insection=True
            parts.append(f'<h{level} id="{anchor}">{inline(title)}</h{level}>')
            toc.append((level,title,anchor));i+=1;continue
        if line.startswith('> '):
            q=[]
            while i<len(lines) and lines[i].startswith('> '):q.append(lines[i][2:]);i+=1
            parts.append('<blockquote><p>'+inline(' '.join(q))+'</p></blockquote>');continue
        if line.startswith('|'):
            rows=[]
            while i<len(lines) and lines[i].startswith('|'):
                cells=[c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch(r':?-{3,}:?',c) for c in cells):rows.append(cells)
                i+=1
            head='<thead><tr>'+''.join('<th>'+inline(c)+'</th>' for c in rows[0])+'</tr></thead>'
            body='<tbody>'+''.join('<tr>'+''.join('<td>'+inline(c)+'</td>' for c in row)+'</tr>' for row in rows[1:])+'</tbody>'
            parts.append('<div class="table-scroll"><table>'+head+body+'</table></div>');continue
        if line.startswith('```'):
            i+=1;code=[]
            while i<len(lines) and not lines[i].startswith('```'):code.append(lines[i]);i+=1
            i+=1;parts.append('<pre><code>'+html.escape('\n'.join(code))+'</code></pre>');continue
        if re.match(r'^(- |\d+\. )',line):
            ordered=not line.startswith('- ');tag='ol' if ordered else 'ul';items=[]
            while i<len(lines) and re.match(r'^(- |\d+\. )',lines[i]):
                items.append(re.sub(r'^(- |\d+\. )','',lines[i]));i+=1
            parts.append('<'+tag+'>'+''.join('<li>'+inline(t)+'</li>' for t in items)+'</'+tag+'>');continue
        p=[]
        while i<len(lines) and lines[i].strip() and not re.match(r'^(#{1,6} |\||> |```|- |\d+\. )',lines[i]):p.append(lines[i]);i+=1
        parts.append('<p>'+inline(' '.join(p))+'</p>')
    if insection:parts.append('</section>')
    nav=''.join('<a class="level'+str(level)+'" href="#'+anchor+'">'+html.escape(title)+'</a>' for level,title,anchor in toc)
    css=r'''
    :root{--ink:#172c3b;--muted:#576b7a;--blue:#0a6985;--line:#dce5e8;--paper:#fff;--bg:#f4f7f8}*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:24px}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.85 'Microsoft YaHei UI','Microsoft YaHei',system-ui,sans-serif}a{color:var(--blue);text-decoration-thickness:1px;text-underline-offset:3px}.hero{background:#102c3d;color:#fff;padding:46px max(30px,calc((100vw - 1480px)/2));border-bottom:5px solid #3ab6af}.eyebrow{font-size:13px;letter-spacing:2px;color:#a1dcd8}.hero h1{font-size:36px;line-height:1.4;margin:10px 0 14px;font-weight:700}.hero .lead{margin:0;max-width:1050px;color:#e4f2f5;font-size:18px}.meta{display:flex;gap:10px;flex-wrap:wrap;margin-top:23px;font-size:13px}.meta span{padding:4px 12px;border:1px solid #456171;border-radius:16px}.layout{max-width:1540px;margin:auto;display:grid;grid-template-columns:268px minmax(0,1fr);gap:28px;padding:30px}.sidebar{position:sticky;top:18px;align-self:start;max-height:calc(100vh - 36px);overflow:auto;padding:4px 8px 24px 0}.sidebar h2{font-size:13px;color:var(--muted);letter-spacing:1px;margin:14px 0 12px}nav a{display:block;color:#3c5565;font-size:14px;line-height:1.55;text-decoration:none;padding:7px 10px;border-left:2px solid transparent}nav a:hover,nav a:focus{color:#075974;border-left-color:#19a59e;background:#e8f2f3}.level3{padding-left:22px;font-size:12px!important;color:#6a7a84!important}main{min-width:0}.tools{display:flex;flex-wrap:wrap;align-items:center;gap:10px;background:#fff;border:1px solid var(--line);border-radius:10px;padding:15px 18px;margin-bottom:20px}.tools input{flex:1;min-width:180px;padding:9px 12px;border:1px solid #afc2cc;border-radius:6px;font:inherit;font-size:14px}.tools button,.tools a{font:inherit;font-size:13px;padding:7px 11px;border:1px solid #b8cbd3;border-radius:5px;color:#275065;background:#f8fbfc;text-decoration:none;cursor:pointer}#search-status{font-size:12px;color:var(--muted);width:100%;min-height:20px}.intro-note{font-size:13px;color:var(--muted);padding:0 4px}.doc-section{background:#fff;border:1px solid var(--line);border-radius:10px;margin-bottom:22px;padding:29px 34px 30px;box-shadow:0 3px 12px #102c3d04}h2{font-size:24px;line-height:1.5;margin:0 0 22px;padding-bottom:14px;border-bottom:2px solid #deefef;color:#10475b}h3{font-size:18px;margin:28px 0 14px;color:#195a70}p{margin:13px 0}strong{font-weight:650;color:#163f52}code{font:13px/1.6 Consolas,'Cascadia Code','Microsoft YaHei',monospace;background:#edf3f6;padding:2px 5px;border-radius:4px;overflow-wrap:anywhere;color:#17516e}pre{background:#102a3b;color:#f0f8fb;overflow:auto;padding:18px;border-radius:7px}pre code{padding:0;color:inherit;background:none}.table-scroll{overflow:auto;border:1px solid #d6e2e7;border-radius:7px;margin:18px 0 23px}table{border-collapse:collapse;width:100%;font-size:14px;line-height:1.7;min-width:600px}th{background:#eaf2f5;color:#224e62;text-align:left;font-weight:650}th,td{padding:11px 13px;border-bottom:1px solid #e1e9ed;vertical-align:top}td+td,th+th{border-left:1px solid #e9eef0}tbody tr:nth-child(even){background:#f9fbfc}tbody tr:last-child td{border-bottom:0}td code{font-size:12px}ul,ol{padding-left:25px;margin:15px 0}li{margin:9px 0;padding-left:3px}blockquote{font-size:13px;color:var(--muted);border-left:3px solid #75a9b8;margin:0 0 22px;padding:0 16px}.source{color:inherit}mark{background:#ffdf72;color:#233644;padding:1px 0}footer{max-width:1480px;margin:10px auto 40px;padding:0 30px;font-size:12px;color:var(--muted)}[hidden]{display:none!important}.empty{background:#fff;padding:25px;border:1px solid var(--line);border-radius:10px}.to-top{position:fixed;right:20px;bottom:20px;background:#0b617c;color:#fff;padding:8px 14px;border-radius:20px;text-decoration:none;font-size:13px;box-shadow:0 2px 10px #0002}@media(max-width:1000px){.layout{grid-template-columns:1fr;padding:18px;gap:0}.sidebar{position:static;max-height:none;margin-bottom:18px;padding:10px 14px;background:#eaf0f3;border-radius:8px}.sidebar nav{max-height:230px;overflow:auto;columns:2}.level3{display:none}.hero{padding:30px 22px}.hero h1{font-size:28px}.doc-section{padding:23px 20px}table{font-size:13px}}@media(max-width:600px){.sidebar nav{columns:1}.hero .lead{font-size:15px}.doc-section{padding:20px 16px}.layout{padding:12px}h2{font-size:21px}.tools{padding:12px}.meta span{font-size:11px}}@media print{@page{size:A4;margin:15mm 12mm}html{scroll-behavior:auto}body{font:10pt/1.65 'Microsoft YaHei',sans-serif;background:white;color:#111}.hero{padding:0 0 15px;background:white;color:#111;border-bottom:2px solid #194c60}.hero h1{font-size:22pt}.hero .lead,.eyebrow{color:#234}.meta span{border:1px solid #abb;color:#234}.layout{display:block;padding:15px 0;max-width:none}.sidebar,.tools,.to-top,.intro-note,.empty{display:none!important}.doc-section,.doc-section[hidden]{display:block!important;border:0;box-shadow:none;padding:0;margin:0 0 22px}h2{font-size:16pt;break-after:avoid;margin-top:12px}h3{font-size:12pt;break-after:avoid}table{min-width:0;font-size:8.6pt}th,td{padding:5px 6px}tr{break-inside:avoid}thead{display:table-header-group}.table-scroll{overflow:visible;border:1px solid #ddd}a{color:#111;text-decoration:none}code{font-size:8pt;overflow-wrap:anywhere}mark{background:none}footer{padding:0;margin:0;font-size:8pt}}
    '''
    js=r'''
    const input=document.getElementById('search'),sections=[...document.querySelectorAll('.doc-section')],status=document.getElementById('search-status'),empty=document.getElementById('empty');
    function clearMarks(){document.querySelectorAll('mark').forEach(m=>m.replaceWith(document.createTextNode(m.textContent)));sections.forEach(s=>s.normalize());}
    function search(){clearMarks();let q=input.value.trim().toLowerCase(),visible=0,hits=0;for(const s of sections){let match=!q||s.textContent.toLowerCase().includes(q);s.hidden=!match;if(!match)continue;visible++;if(!q)continue;const walker=document.createTreeWalker(s,NodeFilter.SHOW_TEXT),nodes=[];let n;while(n=walker.nextNode())if(n.textContent.toLowerCase().includes(q))nodes.push(n);for(const node of nodes){const value=node.textContent,lower=value.toLowerCase();let pos=0,index;const frag=document.createDocumentFragment();while((index=lower.indexOf(q,pos))>=0){frag.append(value.slice(pos,index));const mark=document.createElement('mark');mark.textContent=value.slice(index,index+q.length);frag.append(mark);pos=index+q.length;hits++;}frag.append(value.slice(pos));node.replaceWith(frag);}}
    status.textContent=q?`找到 ${hits} 处匹配，显示 ${visible} / ${sections.length} 章。清空搜索可恢复全文。`:`全文 ${sections.length} 章 · 离线可用 · 搜索会筛选章节并高亮正文。`;empty.hidden=visible!==0;}
    input.addEventListener('input',search);document.getElementById('clear').addEventListener('click',()=>{input.value='';search();input.focus();});document.getElementById('print').addEventListener('click',()=>window.print());document.querySelector('nav').addEventListener('click',e=>{if(e.target.closest('a')){input.value='';search();}});search();
    window.__c060DocumentQa={sections:sections.length,tables:document.querySelectorAll('table').length,headings:[...document.querySelectorAll('h2,h3')].map(h=>h.id)};
    '''
    page='''<!doctype html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light"><title>C060 新上位机完整适配方案</title><style>'''+css+'''</style></head><body id="top"><header class="hero"><div class="eyebrow">C060 · WINDOWS BMS TOOL · 方案评审稿</div><h1>C060 新上位机完整适配方案</h1><p class="lead">连接、实时与常规参数由上位机适配；完整校准、硬件保护与新版升级按明确的固件合同推进。</p><div class="meta"><span>2026-11-05</span><span>源码 + 现场日志 + 实际协议离线复现</span><span>15 章 / 可搜索 / 可打印</span><span>未修改产品代码 · 未实板写入</span></div></header><div class="layout"><aside class="sidebar"><h2>阅读目录</h2><nav aria-label="章节目录">'''+nav+'''</nav></aside><main><div class="tools"><input id="search" type="search" aria-label="搜索方案" placeholder="搜索：连接、保护、BLE、校准、升级……"><button id="clear">清空</button><button id="print">打印 / 保存 PDF</button><a href="C060_ADAPTATION_PLAN.md">Markdown 源文档</a><a href="C060_ADAPTATION_EVIDENCE.json">证据与源码摘要</a><span id="search-status" role="status"></span></div><p class="intro-note">先读第 1、3、5 章了解结论与改动归属；实施时按第 6～13 章；第 14 章区分已完成证据和后续验收。</p><div id="empty" class="empty" hidden>未找到匹配内容，请换一个关键词。</div>'''+''.join(parts)+'''</main></div><footer>本文件自包含，无外部字体、脚本或网络请求。HTML 与 Markdown 来自同一份方案正文。开发中的 C060 新 IAP 应在阶段提交后重新核对；现场装机版本须单独确认。</footer><a class="to-top" href="#top">返回顶部 ↑</a><script>'''+js+'''</script></body></html>'''
    titles={"C060_ADAPTATION_PLAN":"C060 新上位机完整适配方案","C060_IMPLEMENTATION":"C060 上位机使用与实施记录","NEW_OLD_HOST_COMPARISON":"新、旧 BMS 上位机对比","UNIFIED_UPGRADE":"统一 YMODEM 升级与串口蓝牙使用说明"}
    title=titles[stem]
    page=page.replace("C060 新上位机完整适配方案",title).replace("15 章 / 可搜索 / 可打印",str(sec)+" 章 / 可搜索 / 可打印")
    page=page.replace("未修改产品代码 · 未实板写入","前三阶段已实现 · 实板待验收")
    if stem=="C060_IMPLEMENTATION":
        page=page.replace("<span>2026-11-05</span>","<span>2026-11-06</span>")
        page=page.replace("C060 · WINDOWS BMS TOOL · 方案评审稿","C060 · WINDOWS BMS TOOL · 实施与使用说明")
        page=page.replace("C060_ADAPTATION_PLAN.md","C060_IMPLEMENTATION.md").replace("C060_ADAPTATION_EVIDENCE.json","C060_IMPLEMENTATION_EVIDENCE.json")
        page=page.replace("连接、实时与常规参数由上位机适配；完整校准、硬件保护与新版升级按明确的固件合同推进。","当前实现、使用步骤、协议边界和验证记录；连接保持兼容，参数按项目映射。")
        page=page.replace("先读第 1、3、5 章了解结论与改动归属；实施时按第 6～13 章；第 14 章区分已完成证据和后续验收。","使用步骤见正文。软件验证与实板验收分开记录，固件待办单列。")
        link='<a href="C060_ADAPTATION_PLAN.html">原始完整方案</a>'
    elif stem in ("NEW_OLD_HOST_COMPARISON","UNIFIED_UPGRADE"):
        page=page.replace("<span>2026-11-05</span>","<span>2026-11-06</span>")
        page=page.replace("C060 · WINDOWS BMS TOOL · 方案评审稿","BMS TOOL · 新旧源码对比")
        page=page.replace("C060_ADAPTATION_PLAN.md","NEW_OLD_HOST_COMPARISON.md").replace("C060_ADAPTATION_EVIDENCE.json","NEW_OLD_HOST_COMPARISON_EVIDENCE.json")
        page=page.replace("源码 + 现场日志 + 实际协议离线复现","功能矩阵 + C060 兼容边界 + 迁移建议")
        page=page.replace("前三阶段已实现 · 实板待验收","仅源码对比 · 未操作设备")
        page=page.replace("连接、实时与常规参数由上位机适配；完整校准、硬件保护与新版升级按明确的固件合同推进。","当前 C060 通信和参数读写已适配；旧版独有流程、校准及升级缺口逐项说明。")
        page=page.replace("先读第 1、3、5 章了解结论与改动归属；实施时按第 6～13 章；第 14 章区分已完成证据和后续验收。","先读结论与功能矩阵，再看 C060 限制和迁移建议。目录、搜索和打印均可离线使用。")
        page=page.replace("搜索方案","搜索对比").replace("搜索：连接、保护、BLE、校准、升级……","搜索：一键写、模板、校准、多机、升级……")
        page=page.replace("HTML 与 Markdown 来自同一份方案正文。开发中的 C060 新 IAP 应在阶段提交后重新核对；现场装机版本须单独确认。","HTML 与 Markdown 来自同一份对比正文。版本与源文件摘要见证据文件，软件验证不能替代实板验收。")
        link='<a href="C060_IMPLEMENTATION.html">当前实现与使用说明</a>'
    else:
        page=page.replace("C060 · WINDOWS BMS TOOL · 方案评审稿","C060 · WINDOWS BMS TOOL · 历史方案")
        link='<a href="C060_IMPLEMENTATION.html">当前实现与使用说明</a>'
    if stem=="UNIFIED_UPGRADE":
        page=page.replace("NEW_OLD_HOST_COMPARISON.md","UNIFIED_UPGRADE.md").replace("NEW_OLD_HOST_COMPARISON_EVIDENCE.json","UNIFIED_UPGRADE_EVIDENCE.json")
        page=page.replace("BMS TOOL · 新旧源码对比","BMS TOOL · 统一 YMODEM 升级")
        page=page.replace("功能矩阵 + C060 兼容边界 + 迁移建议","升级合同 + 蓝牙使用 + 验证证据")
        page=page.replace("仅源码对比 · 未操作设备","指定蓝牙模块实板升级已验证")
        page=page.replace("当前 C060 通信和参数读写已适配；旧版独有流程、校准及升级缺口逐项说明。","一套传输核心，按设备和包的身份自动匹配；遵循同一合同的新项目无需新增板号适配。")
        page=page.replace("先读结论与功能矩阵，再看 C060 限制和迁移建议。目录、搜索和打印均可离线使用。","先读统一合同和操作步骤，再看实际蓝牙证据与验证边界。目录、搜索和打印均可离线使用。")
        page=page.replace("搜索对比","搜索升级说明").replace("同一份对比正文","同一份升级说明正文")
    else:
        link+='<a href="UNIFIED_UPGRADE.html">统一 YMODEM 升级</a>'
    page=page.replace('<span id="search-status"',link+'<span id="search-status"')
    
    (OUT/(stem+'.html')).write_text(page,encoding='utf-8')
    # Machine checks against the rendered DOM structure and Markdown tables.
    assert sec>0 and len({anchor for _,_,anchor in toc})==len(toc)
    assert '<script src=' not in page and '<link ' not in page
    assert page.count('<table>')==len(re.findall(r'^\|(?:\s*---\s*\|)+\s*$',md,re.M))
    assert all(len(row.strip().strip('|').split('|'))==len(block[0].strip().strip('|').split('|')) for block in [b.splitlines() for b in re.findall(r'(?:^\|.*\n?)+',md,re.M)] for row in block)
    print(json.dumps({'sections':sec,'toc_entries':len(toc),'tables':page.count('<table>'),'html_bytes':len(page.encode('utf-8')),'checks':'PASS'},ensure_ascii=False))
    

if __name__=='__main__':
    for stem in ('C060_ADAPTATION_PLAN','C060_IMPLEMENTATION','NEW_OLD_HOST_COMPARISON','UNIFIED_UPGRADE'):
        render(stem)
