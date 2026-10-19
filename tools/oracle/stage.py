# copy corpus files to stage/<sha16>.jww and keep an index (sha -> original path)
import hashlib, os, sys, json
idx_path='index.json'
idx=json.load(open(idx_path)) if os.path.exists(idx_path) else {}
for root in sys.argv[1:]:
    for dp,dn,fn in os.walk(root):
        for f in fn:
            if not f.lower().endswith('.jww'): continue
            p=os.path.join(dp,f); d=open(p,'rb').read()
            h=hashlib.sha256(d).hexdigest()[:16]
            if h not in idx:
                idx[h]=p; open(f'stage/{h}.jww','wb').write(d)
json.dump(idx,open(idx_path,'w'),ensure_ascii=False,indent=0)
print(len(idx))
