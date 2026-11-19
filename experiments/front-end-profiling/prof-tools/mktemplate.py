import sys, base64, json, re, subprocess
mode, analysis, display, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
x = open('/tmp/cpucounters.xml').read()
m = re.search(r'(<string>optionsEncoded</string>\s*<data>)([^<]*)(</data>)', x)
opts = json.loads(base64.b64decode(''.join(m.group(2).split())))
opts['selectedCountingMode'] = {'countingMode': mode, 'analysisMode': analysis}
opts['selectedCountingModeDisplayName'] = display
enc = base64.b64encode(json.dumps(opts, separators=(',', ':')).encode()).decode()
x = x[:m.start(2)] + '\n' + enc + '\n' + x[m.end(2):]
open(out + '.xml', 'w').write(x)
subprocess.run(['plutil', '-convert', 'binary1', '-o', out, out + '.xml'], check=True)
print(json.dumps(opts))
