import sys, re, os, collections, glob
TOK = re.compile(r"""\s+|//[^\n]*|/\*.*?\*/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`|\d+\.?\d*(?:e[+-]?\d+)?|[A-Za-z_$][\w$]*|==|!=|<=|>=|&&|\|\||[^\s]""", re.S)
def tokens(src):
    out=[]
    for m in TOK.finditer(src):
        t=m.group(0)
        if t[0].isspace() or t.startswith('//') or t.startswith('/*'): continue
        out.append(t)
    return out
KINDS=['createVertexTemplateEntry','createEdgeTemplateEntry','addDataEntry','addEntry','addStencilPalette','addImagePalette','createVertexTemplateFromCells','createVertexTemplate','createEdgeTemplate']
ALLOWED_CALL={'getTagsForStencil','join','get','toLowerCase','replace','round'}
def args_of(toks,i):
    # toks[i]=='(' ; return list of arg token lists and end index
    depth=0; args=[[]]; j=i
    while j<len(toks):
        t=toks[j]
        if t in '([{': depth+=1
        if t in ')]}':
            depth-=1
            if depth==0: return args,j
        if depth==1 and t==',' : args.append([]); 
        elif not (depth==1 and t=='(' and j==i): args[-1].append(t)
        j+=1
    return args,j
def simple(arg):
    # only literals, identifiers, operators, member access, allowed calls
    for k,t in enumerate(arg):
        if t=='function' or t=='new' or t=='=>': return False
        if t=='(' and k>0 and re.match(r'[A-Za-z_$]',arg[k-1]) and arg[k-1] not in ALLOWED_CALL: return False
    return True
tot=collections.Counter(); per={}
for f in sys.argv[1:]:
    toks=tokens(open(f,encoding='utf-8').read())
    c=collections.Counter()
    # track nesting inside addEntry callbacks
    for i,t in enumerate(toks):
        if t in KINDS and i+1<len(toks) and toks[i+1]=='(' and toks[i-1]=='.':
            if t in('createVertexTemplateEntry','createEdgeTemplateEntry'):
                a,_=args_of(toks,i+1)
                ok=all(simple(x) for x in a)
                c[t+(':simple' if ok else ':complex')]+=1
            else: c[t]+=1
    per[os.path.basename(f)]=c; tot.update(c)
for k,v in sorted(tot.items()): print(k,v)
