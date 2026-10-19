import sys
def pairs(path):
    raw=open(path,'rb').read()
    try: t=raw.decode('utf-8')
    except UnicodeDecodeError: t=raw.decode('cp932','replace')
    L=t.replace('\r','').split('\n')
    for i in range(0,len(L)-1,2): yield L[i].strip(), L[i+1]
if __name__=='__main__':
    out=[]
    for c,v in pairs(sys.argv[1]):
        if c=='0': print(' '.join(out)[:300]); out=[]
        out.append(f'{c}={v.strip()}')
    print(' '.join(out))
