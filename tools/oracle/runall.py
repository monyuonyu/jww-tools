#!/usr/bin/env python3
"""Convert every staged drawing with jww2dxf and compare with the oracle's DXF in ref/."""
import json, os, subprocess, sys, collections
J = os.environ.get('JWW2DXF', '/tmp/jww2dxf')
here = os.path.dirname(os.path.abspath(__file__))
idx = json.load(open(os.path.join(here, 'index.json')))
os.makedirs(os.path.join(here, 'ours'), exist_ok=True)
agg = {k: collections.Counter() for k in ('lt', 'lw', 'color', 'layer', 'wfactor', 'hr')}
tot = collections.Counter(); files = collections.Counter(); bad = []
extra = sys.argv[1:]
for f in sorted(os.listdir(os.path.join(here, 'ref'))):
    h = f[:-4]
    src = os.path.join(here, 'stage', h + '.jww')
    if not os.path.exists(src): continue
    o = os.path.join(here, 'ours', f); js = os.path.join(here, 'ours', h + '.json')
    r1 = subprocess.run([J, '-q', '--explode', *extra, src, '-o', o]).returncode
    subprocess.run([J, '-q', '--json', src, '-o', js])
    if r1 not in (0,):
        files['convert_fail'] += 1; bad.append((h, 'convert rc=%d' % r1)); continue
    p = subprocess.run([sys.executable, os.path.join(here, 'jwwdiff.py'), o, os.path.join(here, 'ref', f), js], capture_output=True, text=True)
    try:
        res = json.loads(p.stdout.splitlines()[0])
    except Exception:
        files['diff_error'] += 1; bad.append((h, p.stderr[-300:])); continue
    for k, v in res['stats'].items(): tot[k] += v
    for k in ('lt', 'lw', 'color', 'layer', 'wfactor'):
        for row in res[k]: agg[k][json.dumps(row[:2], ensure_ascii=False)] += row[2]
    for r_, n_ in res['text_height_ratio']: agg['hr'][str(r_)] += n_
    files['ok' if p.returncode == 0 else 'mismatch'] += 1
    if p.returncode: bad.append((h, {k: v for k, v in res['stats'].items() if 'unmatched' in k or 'mismatch' in k}, os.path.basename(idx.get(h, '?'))))
print(dict(files))
types = sorted({k.split('.')[0] for k in tot})
for t in types:
    ref = tot[t + '.ref']; m = tot[t + '.matched'] if t != 'FILL' else tot['FILL.area_match']
    print(f"{t:8s} ref={ref:7d} ours={tot[t + '.ours']:7d} matched={m:7d} unmatched_ours={tot[t + '.unmatched_ours']} unmatched_ref={tot[t + '.unmatched_ref']}  rate={m / ref * 100 if ref else 100:.2f}%")
for b in bad[:30]: print(b)
if os.environ.get('ATTRS'):
    for k, c in agg.items():
        print('==', k)
        for row, n in c.most_common(40 if k != 'layer' else 0): print('  ', n, row)
    lay = agg['layer']; same = sum(n for r, n in lay.items() if json.loads(r)[0] == json.loads(r)[1])
    print('layer names identical:', same, '/', sum(lay.values()))
