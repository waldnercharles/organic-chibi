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
    p = re.search(r'<pmc-events (?:id="(\d+)" fmt="[^"]*">([^<]*)</pmc-events>|ref="(\d+)"/>)', row)
    if not p: continue
    if p.group(1): ids['p'+p.group(1)] = p.group(2); v = p.group(2)
    else: v = ids['p'+p.group(3)]
    th = re.search(r'<tid (?:id="\d+" fmt="[^"]*">(\d+)</tid>)', row)
    samples.append((t/1e9, [int(a) for a in v.split()]))
samples.sort()
sel = [s for s in samples if t_from <= s[0] <= t_to]
a, b = sel[0][1], sel[-1][1]
d = [y - x for x, y in zip(a, b)]
print('samples', len(sel), 'span', sel[0][0], sel[-1][0])
print('deltas', d)
print('ratios to c0', ['%.3f' % (v / d[0]) for v in d])
