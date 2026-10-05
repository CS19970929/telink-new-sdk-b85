"""Verify published comparison/import snapshot commits; retain unavailable history honestly."""
from pathlib import Path
import argparse
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fetch', action='store_true', help='fetch exact recorded SHAs from origin')
args = parser.parse_args()
records = json.loads((ROOT/'bms/products/baselines.json').read_text())
commits = {r[key] for r in records.values() for key in ('comparison_commit','import_snapshot_commit')}
for sha in sorted(commits):
    if args.fetch:
        subprocess.run(['git','fetch','--no-tags','--depth=1','origin',sha],cwd=ROOT,check=True)
    subprocess.run(['git','cat-file','-e',sha+'^{commit}'],cwd=ROOT,check=True)
for product, record in records.items():
    snapshot = record['import_snapshot_commit']
    subprocess.run(['git','cat-file','-e',snapshot+':bms/products/'+product+'/sources.txt'],cwd=ROOT,check=True)
    print(product+': published snapshot and comparison commit verified; original import '+
          ('available' if record['recorded_import_available'] else 'UNAVAILABLE (preserved, not substituted)'))
