#!/usr/bin/env python3
"""Offline exact-token search across curated references or page-preserving source text."""
import argparse,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('query');p.add_argument('--source',help='Exact source_id from data/source_manifest.json');p.add_argument('--chip',choices=['sh367309','sh36735xx','dvc1124_2','bq769x0']);p.add_argument('--limit',type=int,default=30);p.add_argument('--context',type=int,default=1)
a=p.parse_args();hits=0;q=a.query.casefold()
if a.source:
 m=json.loads((ROOT/'data/source_manifest.json').read_text());valid={s['source_id'] for s in m['sources']}
 if a.source not in valid:p.error('Unknown source_id; choose '+', '.join(sorted(valid)))
 paths=sorted((ROOT/'extracted'/a.source).glob('page-*.txt'))
elif a.chip:paths=sorted((ROOT/'chips'/a.chip).glob('*.md'))
else:paths=sorted((ROOT/'chips').glob('*/*.md'))
for path in paths:
 lines=path.read_text(encoding='utf-8').splitlines()
 for i,line in enumerate(lines):
  if q in line.casefold():
   print(f'\n{path.relative_to(ROOT)}:{i+1}')
   print('\n'.join(lines[max(0,i-a.context):i+a.context+1]));hits+=1
   if hits>=a.limit:raise SystemExit(0)
print(f'\nHits: {hits}')
