import sys,collections,os
sys.path.insert(0,os.path.dirname(__file__))
from dxfcat import pairs
def counts(p):
    c=collections.Counter(); sec=None; prev=None
    for code,v in pairs(p):
        v=v.strip()
        if code=='2' and prev=='SECTION': sec=v
        if code=='0':
            if sec=='ENTITIES' and v not in('SECTION','ENDSEC','SEQEND','VERTEX'): c[v]+=1
            prev=v
    return c
tot=collections.Counter()
for f in sorted(os.listdir('ref')):
    a=counts('ours/'+f) if os.path.exists('ours/'+f) else collections.Counter(); b=counts('ref/'+f)
    keys=sorted(set(a)|set(b))
    diff={k:(a[k],b[k]) for k in keys if a[k]!=b[k]}
    for k in keys: tot[k+'_ours']+=a[k]; tot[k+'_ref']+=b[k]
    if diff: print(f[:16],diff)
print(sorted(tot.items()))
