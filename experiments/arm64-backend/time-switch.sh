#!/bin/bash
set -e
D=$(cd "$(dirname "$0")" && pwd)
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
if [ -f src/native.c ]; then
    LIBSRC="src/native.c $LIBSRC"
    BOOT=$(echo "$BOOT" | sed 's|src/arm64.c|src/native.c src/arm64.c|')
fi
if [ -f src/jit.c ]; then
    LIBSRC=$(echo "$LIBSRC" | sed 's|src/arm64.c|src/jit.c|')
    BOOT=$(echo "$BOOT" | sed 's|src/arm64.c|src/jit.c|')
fi
C_ONLY=""
if [ -f src/symfile.c ]; then
    LIBSRC="src/symfile.c src/gdb_jit.c $LIBSRC"
    BOOT=$(echo "$BOOT" | sed 's|src/jit.c|src/symfile.c src/jit.c|')
    C_ONLY="src/gdb_jit.c"
fi

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
sed "s/^#define RUNS 11$/#define RUNS $N/" "$D/bench.c" > experiments/arm64-backend/bench-switch.c
clang $FLAGS -Wall -Wextra -Werror -o "$W/bench-c" experiments/arm64-backend/bench-switch.c $LIBSRC vendor/xxhash/xxhash.c
clang $FLAGS -fno-lto -w -Dchibi_trap=chibi_trap_translated -Dchibi_context=chibi_context_translated -c -o "$W/lib-f.o" "$W/lib-f.c"
clang $FLAGS -Wall -Wextra -Werror -o "$W/bench-t" experiments/arm64-backend/bench-switch.c "$W/lib-f.o" vendor/xxhash/xxhash.c $C_ONLY

echo "== corpus"
rm -rf "$W/corpus"
mkdir -p "$W/corpus"
python3 experiments/check-bodies/gen.py 200 100 "$W/corpus" > /dev/null

echo "== runs: $P pairs of processes, $N runs each, the C and translated builds taking turns run by run"
python3 - "$W" "$P" "$W"/corpus/*.chibi <<'PY'
import os, subprocess, sys
w, pairs, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
with open(f"{w}/c.txt", "w") as c_out, open(f"{w}/t.txt", "w") as t_out:
    for i in range(pairs):
        to_c = os.pipe()
        to_t = os.pipe()
        c = subprocess.Popen([f"{w}/bench-c"] + files, stdout=c_out, pass_fds=(to_c[0], to_t[1]), env=dict(os.environ, CHIBI_BENCH_LOCKSTEP=f"{to_c[0]} {to_t[1]}"))
        t = subprocess.Popen([f"{w}/bench-t"] + files, stdout=t_out, pass_fds=(to_t[0], to_c[1]), env=dict(os.environ, CHIBI_BENCH_LOCKSTEP=f"{to_t[0]} {to_c[1]}"))
        os.write(to_c[1] if i % 2 == 0 else to_t[1], b"t")
        os.close(to_c[1])
        os.close(to_t[1])
        if c.wait() or t.wait():
            sys.exit("time-switch.sh: a bench failed")
        os.close(to_c[0])
        os.close(to_t[0])
PY

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
print("== per pair of processes: the median ms of " + ", ".join(c) + ", C then translated")
for i in range(len(c['all four'])):
    cs = " ".join(f"{c[name][i][1]:7.1f}" for name in c)
    ts = " ".join(f"{t[name][i][1]:7.1f}" for name in c)
    print(f"{i + 1:3d}  {cs}    {ts}")
print()
print("== result: the median of each process's medians, and the best")
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
print()
print(f"{'phase':12s} {'C spread':>9s} {'tr spread':>9s}  (the slowest process's median ms over the fastest's)")
for name in c:
    cm = [x[1] for x in c[name]]; tm = [x[1] for x in t[name]]
    print(f"{name:12s} {max(cm) / min(cm):9.3f} {max(tm) / min(tm):9.3f}")
print()
def runs(path):
    return [[float(x) for x in line.split()[1:]] for line in open(path) if line.startswith('run ')]
rc = runs(sys.argv[1])
rt = runs(sys.argv[2])
print(f"{'phase':12s} {'ms ratio':>8s} {'quartiles':>13s} {'cyc ratio':>9s} {'quartiles':>13s}  (each translated run over the C run beside it, {len(rc)} pairs)")
for k, name in enumerate(c):
    pm = [b[2 * k] / a[2 * k] for a, b in zip(rc, rt)]
    qm = statistics.quantiles(pm, n=4)
    row = f"{name:12s} {statistics.median(pm):8.3f} {qm[0]:6.3f}-{qm[2]:.3f}"
    if rc[0][2 * k + 1]:
        pc = [b[2 * k + 1] / a[2 * k + 1] for a, b in zip(rc, rt)]
        qc = statistics.quantiles(pc, n=4)
        row += f" {statistics.median(pc):9.3f} {qc[0]:6.3f}-{qc[2]:.3f}"
    print(row)
PY
