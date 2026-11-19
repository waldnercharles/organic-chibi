import sys, re, subprocess, collections
trace, binary, load = sys.argv[1], sys.argv[2], sys.argv[3]
t_from = float(sys.argv[4]); t_to = float(sys.argv[5])
sys.path.insert(0, '/tmp/chibi-prof-scripts')
from xrows import rows
tot = collections.Counter()
rs, _ = rows(trace, 'MetricAggregationForProcess')
for v in rs:
    t = int(v[0].text) / 1e9
    if not (t_from <= t <= t_to): continue
    if v[-1].text != '1': continue
    tot[v[-2].text] += int(v[3].text)
print('metric totals in window:')
for k, n in sorted(tot.items()):
    print(f'  {k:32s} {n:16d}  per kcycle {1000*n/tot["cycle"]:.3f}' if tot.get('cycle') else f'  {k} {n}')
by_event = collections.defaultdict(collections.Counter)
rs, _ = rows(trace, 'SamplingModeSamples')
for v in rs:
    t = int(v[0].text) / 1e9
    if not (t_from <= t <= t_to): continue
    ev = v[1].text
    pc = int(v[6].text)
    by_event[ev][pc] += 1
allpcs = sorted({pc for c in by_event.values() for pc in c})
sym = {}
B = 400
for i in range(0, len(allpcs), B):
    chunk = allpcs[i:i+B]
    r = subprocess.run(['atos','-i','-o',binary,'-l',load] + [hex(p) for p in chunk], capture_output=True, text=True).stdout
    # atos -i prints one block per address separated by blank line
    blocks = r.strip('\n').split('\n\n')
    if len(blocks) != len(chunk):
        # fallback: without -i
        r = subprocess.run(['atos','-o',binary,'-l',load] + [hex(p) for p in chunk], capture_output=True, text=True).stdout
        blocks = r.strip('\n').split('\n')
    for pc, b in zip(chunk, blocks):
        lines = b.split('\n')
        sym[pc] = (lines[0], lines[-1])
def short(s):
    m = re.match(r'(\S+) \(in [^)]*\) \(([^:]+):(\d+)\)', s)
    return f'{m.group(1)} {m.group(2)}:{m.group(3)}' if m else s[:70]
def func(s):
    return s.split(' ')[0]
for ev, c in sorted(by_event.items()):
    total = sum(c.values())
    print(f'\n== {ev}: {total} samples')
    fn = collections.Counter(); outer = collections.Counter(); line = collections.Counter()
    for pc, n in c.items():
        inner, out = sym.get(pc, ('?','?'))
        fn[func(inner)] += n; outer[func(out)] += n; line[short(inner)] += n
    print(' by innermost function:')
    for k, n in fn.most_common(12): print(f'   {100*n/total:5.1f}%  {k}')
    print(' by outermost (non-inlined) function:')
    for k, n in outer.most_common(8): print(f'   {100*n/total:5.1f}%  {k}')
    print(' by source line:')
    for k, n in line.most_common(25): print(f'   {100*n/total:5.1f}%  {k}')
