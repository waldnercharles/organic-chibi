import sys, re, subprocess
trace = sys.argv[1]
t_from = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
t_to = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
def export(schema):
    return subprocess.run(['xcrun','xctrace','export','--input',trace,'--xpath',
        f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],capture_output=True,text=True).stdout
x = export('CounterMetricAggregatedForProcess')
ids = {}
rows = []
for row in re.findall(r'<row>(.*?)</row>', x, re.S):
    m = re.search(r'<start-time (?:id="(\d+)" fmt="[^"]*">(\d+)</start-time>|ref="(\d+)"/>)', row)
    if m.group(1): ids[m.group(1)] = int(m.group(2)); t = int(m.group(2))
    else: t = ids[m.group(3)]
    a = re.search(r'<uint64-array (?:id="(\d+)" fmt="[^"]*">([^<]*)</uint64-array>|ref="(\d+)"/>)', row)
    if a.group(1): ids['a'+a.group(1)] = a.group(2); vals = a.group(2)
    else: vals = ids['a'+a.group(3)]
    p = re.search(r'<boolean (?:id="(\d+)" fmt="[^"]*">(\d)</boolean>|ref="(\d+)"/>)', row)
    if p.group(1): ids['b'+p.group(1)] = p.group(2); prec = p.group(2)
    else: prec = ids['b'+p.group(3)]
    if prec == '1':
        rows.append((t/1e9, [int(v) for v in vals.split()]))
sel = [v for t, v in rows if t_from <= t <= t_to]
n = len(sel)
avg = [sum(v[i] for v in sel)/n for i in range(len(sel[0]))]
print('precise rows', len(rows), 'selected', n, 'span', rows[0][0], rows[-1][0])
print('avg metrics (1e-4 units):', ' '.join(f'{a:.0f}' for a in avg))
