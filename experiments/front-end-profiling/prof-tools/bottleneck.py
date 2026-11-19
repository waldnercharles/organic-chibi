import sys, collections
sys.path.insert(0, '/tmp/chibi-prof-scripts')
from xrows import rows
trace = sys.argv[1]; t_from = float(sys.argv[2]); t_to = float(sys.argv[3])
rs, _ = rows(trace, 'kdebug-counters-with-time-sample')
samples = []
for v in rs:
    t = int(v[0].text) / 1e9
    core = v[2]
    pmc = v[7]
    if pmc is None or pmc.tag != 'pmc-events' or core is None or core.tag != 'core': continue
    samples.append((t, int(core.text), [int(a) for a in pmc.text.split()]))
samples.sort()
sel = [s for s in samples if t_from <= s[0] <= t_to]
tot = [0]*12; used = 0
for (t0, c0, a), (t1, c1, b) in zip(sel, sel[1:]):
    d = [y - x for x, y in zip(a, b)]
    if c0 != c1 or any(x < 0 for x in d): continue
    used += 1
    tot = [x + y for x, y in zip(tot, d)]
cyc, ins, ret, active, mapu, bubble, recov, stall = tot[:8]
slot = active * 10
print(f'pairs {used}  IPC {ins/cyc:.2f}  uops/cycle retired {ret/active:.2f} mapped {mapu/active:.2f}')
print(f'useful {ret/slot:.3f}  discarded {(mapu-ret+recov*10)/slot:.3f}  delivery {bubble/slot:.3f}  processing {stall/active:.3f}')
