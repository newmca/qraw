import sys, re, base64, zlib, struct, collections, json
DRAW={'move','line','quad','curve','arc','rect','roundrect','ellipse','text','image','include-shape'}
XS={'x','x1','x2','x3'}; YS={'y','y1','y2','y3'}
def decode(t):
    d=zlib.decompress(base64.b64decode(t),-15)
    lens=struct.unpack('>5I',d[:20]); s=[];o=20
    for n in lens: s.append(d[o:o+n]); o+=n
    ops=s[0]; strs=s[4].decode('utf-8').split('\0')
    p=[0,0,0,0]; si=0; names=[]; an=[]
    root={'n':None,'a':{},'c':[]}; stack=[root]
    px=py=0; pn={}
    def read(k):
        n=0;m=1
        while True:
            c=s[k][p[k]]; p[k]+=1; n+=(c&127)*m; m*=128
            if not c&128: break
        return -(n+1)//2 if n%2 else n//2
    fmt=lambda n: repr(n/1000).rstrip('0').rstrip('.') if n%1000 else str(n//1000)
    while p[0]<len(ops):
        op=ops[p[0]];p[0]+=1
        if op==255: stack.pop(); continue
        if op>>1==len(names): names.append(strs[si]); si+=1
        name=names[op>>1]; cnt=ops[p[0]];p[0]+=1
        node={'n':name,'a':{},'c':[]}; stack[-1]['c'].append(node)
        if name=='shape': px=py=0; pn={}
        for _ in range(cnt):
            i=ops[p[0]];p[0]+=1
            if i==len(an): an.append(strs[si]); si+=1
            a=an[i]; ty=ops[p[0]];p[0]+=1
            if ty==0: node['a'][a]=strs[si]; si+=1
            elif name in DRAW and a in XS: px+=read(1); node['a'][a]=fmt(px)
            elif name in DRAW and a in YS: py+=read(2); node['a'][a]=fmt(py)
            else:
                k=name+' '+a; pn[k]=pn.get(k,0)+read(3); node['a'][a]=fmt(pn[k])
        if not op&1: stack.append(node)
    return root['c'][0]
def toxml(n,ind=0):
    from xml.sax.saxutils import quoteattr
    at=''.join(' %s=%s'%(k,quoteattr(v)) for k,v in n['a'].items())
    if not n['c']: return ' '*ind+'<%s%s/>\n'%(n['n'],at)
    return ' '*ind+'<%s%s>\n'%(n['n'],at)+''.join(toxml(c,ind+1) for c in n['c'])+' '*ind+'</%s>\n'%n['n']
src=open(sys.argv[1],encoding='utf-8').read()
out=sys.argv[2]
import os
stats={}; elems=collections.Counter(); attrs=collections.defaultdict(collections.Counter)
for m in re.finditer(r"^f\['([^']+)'\] = '([^']*)';",src,re.M):
    fn,t=m.group(1),m.group(2)
    r=decode(t)
    path=os.path.join(out,fn); os.makedirs(os.path.dirname(path),exist_ok=True)
    open(path,'w').write(toxml(r))
    shapes=[c for c in r['c'] if c['n']=='shape']
    stats[fn]=(len(shapes),r['n'],r['a'].get('name'))
    def walk(n):
        elems[n['n']]+=1
        for k,v in n['a'].items(): attrs[n['n']][k if k not in('x','y','x1','y1','x2','y2','x3','y3','w','h') or True else k]+=1
        for c in n['c']: walk(c)
    walk(r)
json.dump({'stats':stats,'elems':elems,'attrs':{k:dict(v) for k,v in attrs.items()}},open(os.path.join(out,'_stats.json'),'w'),indent=1)
print(len(stats), sum(v[0] for v in stats.values()))
