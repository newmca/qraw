import sys, re, os, collections, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sidebar_scan import tokens, args_of
U=object()
consts={}
for m in re.finditer(r"^\s*([A-Z_0-9]+):\s*('(?:[^'\\]|\\.)*'|-?[\d.]+)", open(sys.argv[1]).read(), re.M):
    v=m.group(2); consts[m.group(1)]=v[1:-1] if v[0]=="'" else float(v)
def unq(t):
    return re.sub(r"\\(.)",lambda m:{'n':'\\n','t':'\\t'}.get(m.group(1),m.group(1)),t[1:-1]) if '\\' in t else t[1:-1]
class P:
    def __init__(s,toks,env): s.t=toks; s.i=0; s.env=env
    def peek(s): return s.t[s.i] if s.i<len(s.t) else None
    def nx(s):
        s.i+=1; return s.t[s.i-1] if s.i-1<len(s.t) else None
    def expr(s):
        v=s.term()
        while s.peek() in('+','-'):
            op=s.nx(); r=s.term()
            if v is U or r is U: v=U
            elif op=='+': v=(v+r) if (isinstance(v,str)==isinstance(r,str)) else (fmt(v)+fmt(r))
            else: v=v-r if not isinstance(v,str) else U
        return v
    def term(s):
        v=s.unary()
        while s.peek() in('*','/'):
            op=s.nx(); r=s.unary()
            if v is U or r is U or isinstance(v,str) or isinstance(r,str): v=U
            else: v=v*r if op=='*' else v/r
        return v
    def unary(s):
        if s.peek()=='-': s.nx(); v=s.unary(); return U if v is U or isinstance(v,str) else -v
        return s.post(s.prim())
    def post(s,v):
        while s.peek()=='.' :
            s.nx(); name=s.nx()
            if s.peek()=='(':
                a,j=args_of(s.t,s.i); s.i=j+1
                if name=='toLowerCase' and isinstance(v,str): v=v.lower()
                elif name=='join': v=''  # tags only
                else: v=U
            else: v=U
        return v
    def prim(s):
        t=s.nx()
        if t is None: return U
        if t=='(':
            v=s.expr()
            if s.peek()==')': s.nx()
            return v
        if t[0] in "'\"": return unq(t)
        if re.match(r'\d',t): return float(t)
        if t in('null','undefined'): return None
        if t=='true': return True
        if t=='false': return False
        if t=='mxConstants' and s.peek()=='.':
            s.nx(); n=s.nx(); return consts.get(n,U)
        if t=='mxResources' and s.peek()=='.':
            s.nx(); s.nx(); a,j=args_of(s.t,s.i); s.i=j+1
            return unq(a[0][0]) if a and len(a[0])==1 and a[0][0][0] in "'\"" else U
        if t=='this' or t=='sb':
            if s.peek()=='.':
                s.nx(); n=s.nx()
                if s.peek()=='(':
                    a,j=args_of(s.t,s.i); s.i=j+1
                    return [] if n=='getTagsForStencil' else U
            return U
        if re.match(r'[A-Za-z_$]',t) and s.peek()=='(' and t in HELPERS:
            h=HELPERS[t]; params,body=h[0],h[1]; a,j=args_of(s.t,s.i); s.i=j+1
            loc={p:ev(x,s.env) for p,x in zip(params,a)}
            for p in params: loc.setdefault(p,None)
            return ev(body,[loc]+([h[2]] if len(h)>2 else [])+s.env)
        if re.match(r'[A-Za-z_$]',t):
            for e in s.env:
                if t in e: return e[t]
            return U
        return U
def fmt(v):
    if v is None: return 'null'
    if isinstance(v,float): return str(int(v)) if v==int(v) else repr(v)
    return str(v)
def ev(toks,env):
    if not toks: return None
    p=P(toks,env); v=p.expr()
    return v if p.i>=len(toks) else U
stats=collections.Counter(); samples=[]; HELPERS={}
for f in sys.argv[2:]:
    T=tokens(open(f,encoding='utf-8').read())
    # find function defs
    funcs={}; HELPERS.clear()
    for i in range(len(T)-8):
        if T[i]=='function' and re.match(r'[A-Za-z_$]',T[i+1]) and T[i+2]=='(':
            a,j=args_of(T,i+2)
            if T[j+1]=='{' and T[j+2]=='return':
                k=j+3; d=0
                while not(d==0 and T[k]==';'):
                    if T[k] in '([{': d+=1
                    if T[k] in ')]}': d-=1
                    k+=1
                if T[k+1]=='}': HELPERS[T[i+1]]=([x[0] for x in a if x],T[j+3:k])
    for i in range(len(T)-6):
        if T[i]=='prototype' and T[i+1]=='.' and T[i+3]=='=' and T[i+4]=='function' and T[i+5]=='(':
            a,j=args_of(T,i+5); params=[x[0] for x in a if x]
            # body
            d=0;k=j+1
            start=k
            while True:
                if T[k]=='{': d+=1
                elif T[k]=='}':
                    d-=1
                    if d==0: break
                k+=1
            funcs[T[i+2]]=(params,start,k)
    pb={}
    fileenv={}
    for it in range(3):
        for name,(params,st,en) in funcs.items():
            env={}; 
            for idx,p in enumerate(params): env[p]=pb.get(name,{}).get(p,U)
            i=st; first=None
            while i<en:
                t=T[i]
                if t=='var' or (re.match(r'[A-Za-z_$]',t) and T[i+1]=='=' and T[i-1] in(';','{','}')):
                    if t=='var': i+=1
                    while True:
                        n=T[i]
                        if T[i+1]!='=': i+=1; break
                        j=i+2; d=0
                        while not (d==0 and T[j] in(',',';','var')):
                            if T[j] in '([{': d+=1
                            if T[j] in ')]}': d-=1
                            j+=1
                        if T[i+2]=='function' and T[i+3]=='(':
                            a2,j2=args_of(T,i+3)
                            if T[j2+1]=='{' and T[j2+2]=='return':
                                k=j2+3; d=0
                                while not(d==0 and T[k]==';'):
                                    if T[k] in '([{': d+=1
                                    if T[k] in ')]}': d-=1
                                    k+=1
                                HELPERS[n]=([x[0] for x in a2 if x],T[j2+3:k],env)
                        env[n]=ev(T[i+2:j],[env,fileenv])
                        if first is None: first=i+2
                        i=j+1
                        if T[j] in(';','var'):
                            if T[j]!=';': i=j
                            break
                    i=first if first is not None else i; first=None
                    continue
                if t in('this','sb') and T[i+1]=='.' and T[i+2] in funcs and T[i+3]=='(':
                    a,j=args_of(T,i+3)
                    callee=T[i+2]; cp=funcs[callee][0]
                    b=pb.setdefault(callee,{})
                    for p,x in zip(cp,a): 
                        v=ev(x,[env,fileenv])
                        if p not in b or b[p] is U: b[p]=v
                if it==2 and T[i] in('createVertexTemplateEntry','createEdgeTemplateEntry') and T[i-1]=='.' and T[i+1]=='(':
                    a,j=args_of(T,i+1)
                    vals=[ev(x,[env,fileenv]) for x in a[:5]]
                    ok=all(v is not U for v in vals[:3])
                    stats[(T[i],ok)]+=1
                    if ok and os.path.basename(f) in('Sidebar-AWS4.js','Sidebar-Cisco.js','Sidebar-Basic.js') and stats[('dbg',f)]<1:
                        stats[('dbg',f)]+=1; print('OK',os.path.basename(f),[fmt(v)[:200] for v in vals])
                    if not ok and len(samples)<15: samples.append((os.path.basename(f),' '.join(sum(a[:3],[]))[:160]))
                i+=1
for k,v in sorted(stats.items(),key=str): print(k,v)
for s in samples: print(s)
