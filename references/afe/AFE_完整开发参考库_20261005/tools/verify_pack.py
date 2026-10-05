#!/usr/bin/env python3
"""Read-only validation of this offline reference pack; no external dependencies."""
import hashlib,json,re,sys
from pathlib import Path
from urllib.parse import unquote,urlsplit
ROOT=Path(__file__).resolve().parents[1]
errors=[];warnings=[];checks=0

def check(ok,msg):
 global checks
 checks+=1
 if not ok:errors.append(msg)

manifest=json.loads((ROOT/'data/source_manifest.json').read_text(encoding='utf-8'))
sources=manifest['sources'];by_id={s['source_id']:s for s in sources}
for s in sources:
 p=ROOT/s['source_path'];check(p.is_file(),f'Missing source {p}')
 if p.is_file():check(hashlib.sha256(p.read_bytes()).hexdigest()==s['sha256'],f'Source hash mismatch: {p.name}')
 rows=[json.loads(x) for x in (ROOT/s['extracted_path']/'pages.jsonl').read_text(encoding='utf-8').splitlines()]
 check(len(rows)==s['pdf_pages'],f'Page count mismatch: {s["source_id"]}')
 check([r['pdf_page'] for r in rows]==list(range(1,s['pdf_pages']+1)),f'Page sequence mismatch: {s["source_id"]}')
 for r in rows:
  p=ROOT/r['text_path'];check(p.is_file(),f'Missing page text: {p}')
  if p.is_file():check(p.read_text(encoding='utf-8')==r['text'],f'Page text mismatch: {p}')
  if r['character_count']==0:warnings.append(f'{s["filename"]} PDF p.{r["pdf_page"]}: image-only/no extractable text; use original PDF and chip reference')
for p in ROOT.rglob('*.json'):
 try:json.loads(p.read_text(encoding='utf-8'));checks+=1
 except Exception as e:errors.append(f'Invalid JSON {p.relative_to(ROOT)}: {e}')
for p in ROOT.rglob('*.md'):
 body=p.read_text(encoding='utf-8')
 # Exclude fenced verbatim source blocks to avoid treating source text as Markdown links.
 body=re.sub(r'```.*?```','',body,flags=re.S)
 for match in re.finditer(r'\]\(([^)\n]+)\)',body):
  href=match.group(1).strip().split(' "',1)[0]
  u=urlsplit(href)
  if u.scheme or href.startswith('#'):continue
  target=(p.parent/unquote(u.path)).resolve()
  check(target.is_file() or target.is_dir(),f'Broken Markdown link {p.relative_to(ROOT)} -> {href}')
  if target.suffix.lower()=='.pdf' and u.fragment.startswith('page='):
   try:
    page=int(u.fragment[5:]);s=next(x for x in sources if (ROOT/x['source_path']).resolve()==target)
    check(1<=page<=s['pdf_pages'],f'PDF page out of range {p.relative_to(ROOT)} -> {href}')
   except (ValueError,StopIteration):errors.append(f'Unverifiable PDF link {p.relative_to(ROOT)} -> {href}')
index=json.loads((ROOT/'data/register_index.json').read_text(encoding='utf-8'))
for ent in index['datasets']:
 p=ROOT/ent['path'];check(p.is_file(),f'Missing register dataset {p}')
 if p.is_file():check(hashlib.sha256(p.read_bytes()).hexdigest()==ent['sha256'],f'Register dataset hash mismatch {p}')
 if p.is_file():
  dataset=json.loads(p.read_text(encoding='utf-8'));seen=set()
  for row in dataset['registers']:
   addr=row['address'];addr=int(addr,16) if isinstance(addr,str) else addr
   key=(row.get('revision',dataset.get('revision')),addr)
   check(key not in seen,f'Duplicate revision/address: {p} {key}');seen.add(key)
   check(bool(row.get('name')) and row.get('width_bits',0)>0,f'Invalid register identity: {p} {key}')
   check(any(k in row for k in ['source','sources','source_pages']),f'Missing register source: {p} {key}')
   for group in ['bitfields','decoded_fields']:
    for field in row.get(group,[]):
     mask=field.get('mask',field.get('mask_hex'))
     if mask is None:continue
     mask=int(mask,16) if isinstance(mask,str) else mask
     if 'msb' in field and 'lsb' in field:
      lo=field['lsb'];hi=field['msb']
      check(0<=lo<=hi<row['width_bits'],f'Invalid bit bounds: {p} {key} {field.get("name")}')
      if 0<=lo<=hi<row['width_bits']:check(mask==((1<<(hi-lo+1))-1)<<lo,f'Mask mismatch: {p} {key} {field.get("name")}')
     elif 'bits' in field:check(mask==sum(1<<b for b in set(field['bits'])),f'Mask mismatch: {p} {key} {field.get("name")}')

print(json.dumps({'status':'PASS' if not errors else 'FAIL','checks':checks,'source_documents':len(sources),'pdf_pages':sum(s['pdf_pages'] for s in sources),'errors':errors,'warnings':warnings},ensure_ascii=False,indent=2))
sys.exit(1 if errors else 0)
