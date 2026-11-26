import subprocess
import sys

pairs = []
for raw in open(sys.argv[1]):
    code, text = raw.rstrip('\n').split('\t', 1)
    pairs.append((code, text))

source = '.intel_syntax noprefix\n' + ''.join(text + '\n' for _, text in pairs)
out = subprocess.run(['llvm-mc', '-triple=x86_64', '-show-encoding'], input=source,
                     capture_output=True, text=True)
if out.returncode != 0:
    print(out.stderr[:2000])
    sys.exit(1)
encodings = []
for line in out.stdout.splitlines():
    if '# encoding:' in line:
        body = line.split('# encoding: [', 1)[1].rstrip(']')
        encodings.append(''.join(b[2:].rjust(2, '0') for b in body.split(',')))

assert len(encodings) == len(pairs), (len(encodings), len(pairs))
bad = [(text, code, want) for (code, text), want in zip(pairs, encodings) if code != want]
for text, code, want in bad[:40]:
    print(f'{text}: encoder {code}, llvm-mc {want}')
print(f'x64 encode: {len(pairs) - len(bad)} of {len(pairs)} instructions match llvm-mc')
sys.exit(1 if bad else 0)
