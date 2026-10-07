M=0xffffffff
def mix(x):
    x^=x>>16; x=(x*0x7feb352d)&M; x^=x>>15; x=(x*0x846ca68b)&M; x^=x>>16; return x
def perm(x,bits,key):
    mask=(1<<bits)-1; shift=bits//2 if bits>1 else 1
    for r in range(3):
        k=mix((key+r*0x9e3779b9)&M)
        x=(x*(((k<<1)|1)&M)+(k>>8))&mask
        x^=x>>shift
    return x&mask
def walk(raw,ln,key):
    bits=0
    while (1<<bits)<ln: bits+=1
    if not bits: return 0
    x=raw;n=0
    while True:
        x=perm(x,bits,key); n+=1
        if x<ln: return n
import sys
best=[]
for seed in range(200000):
    key=mix(seed)  # cycle 0: key = mix(seed ^ 0)
    for raw in range(33):
        w=walk(raw,33,key)
        if w>=12: best.append((w,seed,raw))
best.sort(reverse=True); print(best[:5])
pts=[]
for target in range(1,8):
    for seed in range(1000):
        key=mix(seed); hit=[r for r in range(33) if walk(r,33,key)==target]
        if hit: pts.append((target,seed,hit[0])); break
print(pts)
