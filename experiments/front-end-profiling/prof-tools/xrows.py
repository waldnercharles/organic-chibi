import subprocess, xml.etree.ElementTree as ET
def export(trace, schema):
    return subprocess.run(['xcrun','xctrace','export','--input',trace,'--xpath',
        f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],capture_output=True,text=True).stdout
def rows(trace, schema):
    root = ET.fromstring(export(trace, schema))
    ids = {}
    def reg(e):
        if 'id' in e.attrib: ids[e.attrib['id']] = e
        for c in e: reg(c)
    out = []
    for row in root.iter('row'):
        reg(row)
        vals = []
        for c in row:
            if 'ref' in c.attrib: c = ids[c.attrib['ref']]
            vals.append(c)
        out.append(vals)
    return out, ids
