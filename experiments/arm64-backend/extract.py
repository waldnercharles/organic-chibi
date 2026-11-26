import re
import sys

out = open(sys.argv[1], 'wb')
count = 0
for path in sys.argv[2:]:
    src = open(path).read()
    src = re.sub(r'//[^\n]*', '', src)
    for run in re.finditer(r'(?:"(?:[^"\\\n]|\\.)*"\s*)+', src):
        pieces = re.findall(r'"((?:[^"\\\n]|\\.)*)"', run.group(0))
        text = ''.join(pieces)
        if 'fn ' not in text or '%' in text:
            continue
        raw = text.encode('utf-8').decode('unicode_escape').encode('latin-1')
        if b'\0' in raw:
            continue
        out.write(raw + b'\0')
        count += 1
print(count, file=sys.stderr)
