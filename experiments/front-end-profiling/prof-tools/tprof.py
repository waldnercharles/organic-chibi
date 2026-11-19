import sys, re, subprocess, collections
sys.path.insert(0, '/tmp/chibi-prof-scripts')
from xrows import rows
trace, binary, load = sys.argv[1], sys.argv[2], sys.argv[3]
t_from = float(sys.argv[4]); t_to = float(sys.argv[5])
nlines = int(sys.argv[6]) if len(sys.argv) > 6 else 30
rs, ids = rows(trace, 'time-profile')
pcs = collections.Counter()
for v in rs:
    t = int(v[0].text) / 1e9
    if not (t_from <= t <= t_to): continue
    bt = [c for c in v if c is not None and c.tag in ('backtrace', 'tagged-backtrace')]
    if not bt: continue
    fr = bt[0].find('frame')
    if fr is None: continue
    if 'ref' in fr.attrib: fr = ids[fr.attrib['ref']]
    pcs[int(fr.attrib['addr'], 16) & ~3] += 1
allpcs = sorted(pcs)
sym = {}
for i in range(0, len(allpcs), 400):
    chunk = allpcs[i:i+400]
    r = subprocess.run(['atos','-i','-o',binary,'-l',load] + [hex(p) for p in chunk], capture_output=True, text=True).stdout
    blocks = r.strip('\n').split('\n\n')
    for pc, b in zip(chunk, blocks):
        l = b.split('\n'); sym[pc] = (l[0], l[-1])
def short(s):
    m = re.match(r'(\S+) \(in [^)]*\) \(([^:]+):(\d+)\)', s)
    return f'{m.group(1)} {m.group(2)}:{m.group(3)}' if m else s[:60]
total = sum(pcs.values())
fn = collections.Counter(); outer = collections.Counter(); line = collections.Counter()
for pc, n in pcs.items():
    a, b = sym.get(pc, ('?', '?'))
    fn[a.split(' ')[0]] += n; outer[b.split(' ')[0]] += n; line[short(a)] += n
print('samples', total)
print('by outermost (non-inlined) function:')
for k, n in outer.most_common(12): print(f'   {100*n/total:5.1f}%  {k}')
print('by innermost function:')
for k, n in fn.most_common(16): print(f'   {100*n/total:5.1f}%  {k}')
print('by source line:')
for k, n in line.most_common(nlines): print(f'   {100*n/total:5.1f}%  {k}')
