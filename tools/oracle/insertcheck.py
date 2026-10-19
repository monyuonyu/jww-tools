"""Explode INSERTs of our default (block-preserving) output with ezdxf and compare with the oracle."""
import sys, os, glob, subprocess, ezdxf, json, collections
here = os.path.dirname(os.path.abspath(__file__))
tot = collections.Counter()
for ref in sorted(glob.glob(here + '/ref/*.dxf')):
    h = os.path.basename(ref)[:-4]
    src = here + '/stage/' + h + '.jww'
    d = json.load(open(here + '/ours/' + h + '.json'))
    if not d['blocks']:
        continue
    out = '/tmp/ins_' + h + '.dxf'
    subprocess.run(['/tmp/jww2dxf', '-q', '--utf8', src, '-o', out])
    doc = ezdxf.readfile(out)
    msp = doc.modelspace()
    for _ in range(40):
        ins = msp.query('INSERT')
        if not len(ins): break
        for i in ins:
            lay = i.dxf.layer
            for e in i.explode():
                if e.dxf.layer == '0':  # DXF: layer-0 members show on the INSERT's layer
                    e.dxf.layer = lay
    doc.saveas(out)
    p = subprocess.run([sys.executable, here + '/jwwdiff.py', out, ref, here + '/ours/' + h + '.json'], capture_output=True, text=True)
    tot['files'] += 1
    if p.returncode:
        tot['mismatch'] += 1
        print(h, d['version'], p.stdout[:400])
print(dict(tot))
