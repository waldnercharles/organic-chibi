#!/bin/bash
set -e
R=$(cd "$1" && pwd)
mkdir -p "$2"
W=$(cd "$2" && pwd)
P=${3:-5}
N=${4:-41}
cd "$R"

OS=$(uname -s)
ARCH=$(uname -m)
if [ "$OS" = Darwin ]; then
    HOST="-arch $ARCH -mmacosx-version-min=11.0"
elif [ "$ARCH" = x86_64 ]; then
    HOST="-march=x86-64-v2"
else
    HOST=""
fi
WARN="-std=c23 -Wall -Wextra -Werror"
FLAGS="-std=c23 -O3 -flto -falign-functions=64 $HOST -DNDEBUG -pthread"
LIBSRC="src/arm64.c src/check.c src/exact.c src/lexer.c src/parser.c src/arena.c src/names.c src/str.c"
BOOT="src/arena.c src/str.c src/names.c src/exact.c src/parser.c src/lexer.c src/check.c src/arm64.c src/driver.c src/c_source.c src/main.c"
SRC=$(ls src/*.c vendor/*/*.c | grep -v '^src/main.c$')

echo "== tools"
clang --version | head -1
clang $WARN -g -O2 -o "$W/translate" translate/*.c
clang $WARN -g -O2 -o "$W/chibi" src/main.c $SRC

echo "== translation"
rm -rf "$W/host"
mkdir -p "$W/host"
args=""
for f in $BOOT; do
    clang -std=c23 $HOST -DCHIBI_SIMD_GENERIC -D_FORTIFY_SOURCE=0 -Xclang -ast-dump=json -fsyntax-only "$f" > "$W/host/$(basename "$f" .c).json"
    args="$args $W/host/$(basename "$f" .c).json $f"
done
"$W/translate" "$W/host" $args
: > "$W/entry.chibi"
"$W/chibi" c --release-fast -o "$W/lib-f.c" "$W/entry.chibi" $(ls "$W"/host/*.chibi | grep -v -x "$W/host/main.chibi")
rm -f "$W"/host/*.json

echo "== benches"
sed "s/^#define RUNS 11$/#define RUNS $N/" experiments/arm64-backend/bench.c > experiments/arm64-backend/bench-switch.c
clang $FLAGS -Wall -Wextra -Werror -o "$W/bench-c" experiments/arm64-backend/bench-switch.c $LIBSRC vendor/xxhash/xxhash.c
clang $FLAGS -fno-lto -w -Dchibi_trap=chibi_trap_translated -Dchibi_context=chibi_context_translated -c -o "$W/lib-f.o" "$W/lib-f.c"
clang $FLAGS -Wall -Wextra -Werror -o "$W/bench-t" experiments/arm64-backend/bench-switch.c "$W/lib-f.o" vendor/xxhash/xxhash.c

echo "== corpus"
rm -rf "$W/corpus"
mkdir -p "$W/corpus"
python3 experiments/check-bodies/gen.py 200 100 "$W/corpus" > /dev/null

echo "== runs: $P processes per build, $N runs each, interleaved"
: > "$W/c.txt"
: > "$W/t.txt"
for i in $(seq 1 "$P"); do
    "$W/bench-c" "$W"/corpus/*.chibi >> "$W/c.txt"
    "$W/bench-t" "$W"/corpus/*.chibi >> "$W/t.txt"
done

echo "== result: the median of each process's medians, and the best"
python3 - "$W/c.txt" "$W/t.txt" <<'PY'
import sys, statistics
def load(path):
    rows = {}
    for line in open(path):
        line = line.strip()
        if 'cycles per line' not in line:
            continue
        name = line.split('  ')[0].strip()
        cyc = float(line.split('cycles per line')[0].split()[-1])
        ms = float(line.split(' ms')[0].split()[-1])
        ipc = float(line.rsplit('IPC', 1)[1])
        rows.setdefault(name, []).append((cyc, ms, ipc))
    return rows
c = load(sys.argv[1])
t = load(sys.argv[2])
print(f"{'phase':12s} {'C cyc/line':>11s} {'tr cyc/line':>11s} {'ratio':>6s} {'C ms':>8s} {'tr ms':>8s} {'ratio':>6s}  (best: cyc ratio, ms ratio)")
for name in c:
    cc = [x[0] for x in c[name]]; tc = [x[0] for x in t[name]]
    cm = [x[1] for x in c[name]]; tm = [x[1] for x in t[name]]
    mc, mt = statistics.median(cc), statistics.median(tc)
    sc, st = statistics.median(cm), statistics.median(tm)
    rc = mt / mc if mc else float('nan')
    print(f"{name:12s} {mc:11.1f} {mt:11.1f} {rc:6.3f} {sc:8.1f} {st:8.1f} {st / sc:6.3f}  ({(min(tc) / min(cc)) if min(cc) else float('nan'):.3f}, {min(tm) / min(cm):.3f})")
print()
print(f"{'phase':12s} {'C IPC':>6s} {'tr IPC':>6s} {'C ins/line':>11s} {'tr ins/line':>11s} {'ratio':>6s}")
for name in c:
    ci = statistics.median([x[2] for x in c[name]]); ti = statistics.median([x[2] for x in t[name]])
    cn = statistics.median([x[0] * x[2] for x in c[name]]); tn = statistics.median([x[0] * x[2] for x in t[name]])
    print(f"{name:12s} {ci:6.2f} {ti:6.2f} {cn:11.1f} {tn:11.1f} {tn / cn if cn else float('nan'):6.3f}")
PY
