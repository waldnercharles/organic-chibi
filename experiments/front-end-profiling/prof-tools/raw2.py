import sys, re, subprocess
trace = sys.argv[1]
t_from = float(sys.argv[2]); t_to = float(sys.argv[3])
x = subprocess.run(['xcrun','xctrace','export','--input',trace,'--xpath',
    '/trace-toc/run[@number="1"]/data/table[@schema="kdebug-counters-with-time-sample"]'],capture_output=True,text=True).stdout
ids = {}
samples = []
for row in re.findall(r'<row>(.*?)</row>', x, re.S):
    m = re.search(r'<sample-time (?:id="(\d+)" fmt="[^"]*">(\d+)</sample-time>|ref="(\d+)"/>)', row)
    if m.group(1): ids['t'+m.group(1)] = int(m.group(2)); t = int(m.group(2))
    else: t = ids['t'+m.group(3)]
    c = re.search(r'<core (?:id="(\d+)" fmt="[^"]*">(\d+)</core>|ref="(\d+)"/>)', row)
    if c:
        if c.group(1): ids['c'+c.group(1)] = int(c.group(2)); core = int(c.group(2))
        else: core = ids['c'+c.group(3)]
    else: core = -1
    p = re.search(r'<pmc-events (?:id="(\d+)" fmt="[^"]*">([^<]*)</pmc-events>|ref="(\d+)"/>)', row)
    if not p: continue
    if p.group(1): ids['p'+p.group(1)] = p.group(2); v = p.group(2)
    else: v = ids['p'+p.group(3)]
    samples.append((t/1e9, core, [int(a) for a in v.split()]))
samples.sort()
sel = [s for s in samples if t_from <= s[0] <= t_to]
tot = [0]*12; used = 0
cores = {}
for (t0, c0, a), (t1, c1, b) in zip(sel, sel[1:]):
    d = [y - x for x, y in zip(a, b)]
    if c0 != c1 or any(v < 0 for v in d): continue
    used += 1; cores[c0] = cores.get(c0, 0) + 1
    tot = [x + y for x, y in zip(tot, d)]
print('pairs used', used, 'of', len(sel) - 1, 'cores', cores)
print('totals', tot)
print('ratio to c0', ['%.3f' % (v / tot[0]) for v in tot])
