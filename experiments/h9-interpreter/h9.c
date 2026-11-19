#define _DEFAULT_SOURCE

#include "../../src/common.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define H9_OPS(X)                                                              \
    X(OP_MOV_I64)                                                              \
    X(OP_ADD_I64)                                                              \
    X(OP_SUB_I64)                                                              \
    X(OP_MUL_I64)                                                              \
    X(OP_XOR_I64)                                                              \
    X(OP_SHR_U64)                                                              \
    X(OP_LT_I64)                                                               \
    X(OP_JUMP)                                                                 \
    X(OP_JUMP_UNLESS)                                                          \
    X(OP_LOOP_IF)                                                              \
    X(OP_LOAD_U8)                                                              \
    X(OP_STORE_U8)                                                             \
    X(OP_CALL)                                                                 \
    X(OP_RET_I64)

#define OP_NAME(name) name,

typedef enum
{
    H9_OPS(OP_NAME) NOPS
} Op;

typedef struct
{
    u8 op;
    u16 dst;
    u16 a;
    u16 b;
} Inst;
static_assert(sizeof(Inst) == 8, "an instruction is one 8-byte load");

typedef struct
{
    u16 slot;
    u64 value;
} Const;

typedef struct
{
    const Inst *code;
    u32 ncode;
    const Const *consts;
    u32 nconsts;
    u32 frame_size;
} Function;

typedef struct
{
    const Inst *code;
    u32 pc;
    u32 base;
} Return;
static_assert(sizeof(Return) == 16, "a call costs one 16-byte store");

typedef enum
{
    STATUS_OK,
    STATUS_OUT_OF_BOUNDS,
    STATUS_OUT_OF_BUDGET,
    STATUS_STACK_OVERFLOW,
} Status;

typedef struct
{
    const Function *functions;
    u8 *stack;
    u64 stack_size;
    Return *returns;
    u32 max_depth;
    u8 *heap;
    u64 heap_size;
    u64 budget;
    u64 result;
    u64 ops;
    u32 fault_pc;
} Machine;

#define LEN(a) ((u32)(sizeof(a) / sizeof((a)[0])))

static inline u64
load_u64(const u8 *p)
{
    u64 v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline void
store_u64(u8 *p, u64 v)
{
    memcpy(p, &v, sizeof v);
}

static inline void
copy_consts(u8 *frame, const Function *f)
{
    for (u32 i = 0; i < f->nconsts; i++) {
        store_u64(frame + f->consts[i].slot, f->consts[i].value);
    }
}

#define INTERPRET interpret_switch
#define THREADED 0
#define COUNTING 0
#include "interpret.inc"

#define INTERPRET interpret_threaded
#define THREADED 1
#define COUNTING 0
#include "interpret.inc"

#define INTERPRET count_switch
#define THREADED 0
#define COUNTING 1
#include "interpret.inc"

#define INTERPRET count_threaded
#define THREADED 1
#define COUNTING 1
#include "interpret.inc"

#define TAIL_PREFIX tail_
#define TAIL_SLOTS 0
#include "tail.inc"

#define TAIL_PREFIX slot_
#define TAIL_SLOTS 1
#include "tail.inc"

static void
validate(const Function *functions, u32 nfunctions)
{
    for (u32 k = 0; k < nfunctions; k++) {
        const Function *f = &functions[k];
        u32 size = f->frame_size;
        assert(f->ncode > 0 && size >= 8);

        for (u32 i = 0; i < f->nconsts; i++) {
            assert(f->consts[i].slot + 8u <= size);
        }

        for (u32 i = 0; i < f->ncode; i++) {
            Inst in = f->code[i];
            switch ((Op)in.op) {
            case OP_MOV_I64:
            case OP_LOAD_U8:
                assert(in.dst + 8u <= size && in.a + 8u <= size);
                break;
            case OP_ADD_I64:
            case OP_SUB_I64:
            case OP_MUL_I64:
            case OP_XOR_I64:
            case OP_SHR_U64:
                assert(in.dst + 8u <= size && in.a + 8u <= size && in.b + 8u <= size);
                break;
            case OP_LT_I64:
                assert(in.dst < size && in.a + 8u <= size && in.b + 8u <= size);
                break;
            case OP_JUMP:
                assert(in.dst < f->ncode);
                break;
            case OP_JUMP_UNLESS:
            case OP_LOOP_IF:
                assert(in.dst < f->ncode && in.a < size);
                break;
            case OP_STORE_U8:
                assert(in.a + 8u <= size && in.b + 8u <= size);
                break;
            case OP_CALL:
                assert(in.dst < nfunctions && in.a + 8u <= size);
                break;
            case OP_RET_I64:
                assert(in.a + 8u <= size);
                break;
            case NOPS:
            default:
                assert(false);
                unreachable();
            }
        }

        Op last = f->code[f->ncode - 1].op;
        assert(last == OP_RET_I64 || last == OP_JUMP);
    }
}

enum
{
    COUNT_N = 0,
    COUNT_I = 8,
    COUNT_SUM = 16,
    COUNT_ONE = 24,
    COUNT_LESS = 32,
    COUNT_FRAME = 40,
};

static const Inst count_code[] = {
    { OP_ADD_I64, COUNT_SUM, COUNT_SUM, COUNT_I },
    { OP_ADD_I64, COUNT_I, COUNT_I, COUNT_ONE },
    { OP_LT_I64, COUNT_LESS, COUNT_I, COUNT_N },
    { OP_LOOP_IF, 0, COUNT_LESS, 0 },
    { OP_RET_I64, 0, COUNT_SUM, 0 },
};

static const Const count_consts[] = { { COUNT_I, 0 }, { COUNT_SUM, 0 }, { COUNT_ONE, 1 } };

static const Function count_functions[] = {
    { count_code, LEN(count_code), count_consts, LEN(count_consts), COUNT_FRAME },
};

enum
{
    CALLS_N = 0,
    CALLS_I = 8,
    CALLS_SUM = 16,
    CALLS_ONE = 24,
    CALLS_LESS = 32,
    CALLS_ARG = 40,
    CALLS_FRAME = 48,

    TRIPLE_X = 0,
    TRIPLE_THREE = 8,
    TRIPLE_ONE = 16,
    TRIPLE_T = 24,
    TRIPLE_FRAME = 32,
};

static const Inst calls_code[] = {
    { OP_MOV_I64, CALLS_ARG, CALLS_I, 0 },
    { OP_CALL, 1, CALLS_ARG, 0 },
    { OP_ADD_I64, CALLS_SUM, CALLS_SUM, CALLS_ARG },
    { OP_ADD_I64, CALLS_I, CALLS_I, CALLS_ONE },
    { OP_LT_I64, CALLS_LESS, CALLS_I, CALLS_N },
    { OP_LOOP_IF, 0, CALLS_LESS, 0 },
    { OP_RET_I64, 0, CALLS_SUM, 0 },
};

static const Const calls_consts[] = { { CALLS_I, 0 }, { CALLS_SUM, 0 }, { CALLS_ONE, 1 } };

static const Inst triple_code[] = {
    { OP_MUL_I64, TRIPLE_T, TRIPLE_X, TRIPLE_THREE },
    { OP_ADD_I64, TRIPLE_T, TRIPLE_T, TRIPLE_ONE },
    { OP_RET_I64, 0, TRIPLE_T, 0 },
};

static const Const triple_consts[] = { { TRIPLE_THREE, 3 }, { TRIPLE_ONE, 1 } };

static const Function calls_functions[] = {
    { calls_code, LEN(calls_code), calls_consts, LEN(calls_consts), CALLS_FRAME },
    { triple_code, LEN(triple_code), triple_consts, LEN(triple_consts), TRIPLE_FRAME },
};

#define TABLE_SIZE 65536

enum
{
    TABLE_ROUNDS = 0,
    TABLE_R = 8,
    TABLE_I = 16,
    TABLE_SUM = 24,
    TABLE_SIZE_SLOT = 32,
    TABLE_ONE = 40,
    TABLE_K31 = 48,
    TABLE_K3 = 56,
    TABLE_ZERO = 64,
    TABLE_T = 72,
    TABLE_LESS = 80,
    TABLE_FRAME = 88,
};

static const Inst table_code[] = {
    { OP_MOV_I64, TABLE_I, TABLE_ZERO, 0 },
    { OP_XOR_I64, TABLE_T, TABLE_I, TABLE_R },
    { OP_MUL_I64, TABLE_T, TABLE_T, TABLE_K31 },
    { OP_SHR_U64, TABLE_T, TABLE_T, TABLE_K3 },
    { OP_STORE_U8, 0, TABLE_I, TABLE_T },
    { OP_ADD_I64, TABLE_I, TABLE_I, TABLE_ONE },
    { OP_LT_I64, TABLE_LESS, TABLE_I, TABLE_SIZE_SLOT },
    { OP_LOOP_IF, 1, TABLE_LESS, 0 },
    { OP_MOV_I64, TABLE_I, TABLE_ZERO, 0 },
    { OP_LOAD_U8, TABLE_T, TABLE_I, 0 },
    { OP_ADD_I64, TABLE_SUM, TABLE_SUM, TABLE_T },
    { OP_ADD_I64, TABLE_I, TABLE_I, TABLE_ONE },
    { OP_LT_I64, TABLE_LESS, TABLE_I, TABLE_SIZE_SLOT },
    { OP_LOOP_IF, 9, TABLE_LESS, 0 },
    { OP_ADD_I64, TABLE_R, TABLE_R, TABLE_ONE },
    { OP_LT_I64, TABLE_LESS, TABLE_R, TABLE_ROUNDS },
    { OP_LOOP_IF, 0, TABLE_LESS, 0 },
    { OP_RET_I64, 0, TABLE_SUM, 0 },
};

static const Const table_consts[] = {
    { TABLE_R, 0 },     { TABLE_SUM, 0 }, { TABLE_SIZE_SLOT, TABLE_SIZE }, { TABLE_ONE, 1 },
    { TABLE_K31, 31 },  { TABLE_K3, 3 },  { TABLE_ZERO, 0 },
};

static const Function table_functions[] = {
    { table_code, LEN(table_code), table_consts, LEN(table_consts), TABLE_FRAME },
};

enum
{
    FIB_N = 0,
    FIB_TWO = 8,
    FIB_ONE = 16,
    FIB_LESS = 24,
    FIB_LEFT = 32,
    FIB_ARG = 40,
    FIB_FRAME = 48,
};

static const Inst fib_code[] = {
    { OP_LT_I64, FIB_LESS, FIB_N, FIB_TWO },
    { OP_JUMP_UNLESS, 3, FIB_LESS, 0 },
    { OP_RET_I64, 0, FIB_N, 0 },
    { OP_SUB_I64, FIB_ARG, FIB_N, FIB_ONE },
    { OP_CALL, 0, FIB_ARG, 0 },
    { OP_MOV_I64, FIB_LEFT, FIB_ARG, 0 },
    { OP_SUB_I64, FIB_ARG, FIB_N, FIB_TWO },
    { OP_CALL, 0, FIB_ARG, 0 },
    { OP_ADD_I64, FIB_LEFT, FIB_LEFT, FIB_ARG },
    { OP_RET_I64, 0, FIB_LEFT, 0 },
};

static const Const fib_consts[] = { { FIB_TWO, 2 }, { FIB_ONE, 1 } };

static const Function fib_functions[] = {
    { fib_code, LEN(fib_code), fib_consts, LEN(fib_consts), FIB_FRAME },
};

static u64
count_reference(u64 n)
{
    u64 sum = 0;
    for (u64 i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}

static u64
calls_reference(u64 n)
{
    u64 sum = 0;
    for (u64 i = 0; i < n; i++) {
        sum += i * 3 + 1;
    }
    return sum;
}

static u64
table_reference(u64 rounds)
{
    u64 sum = 0;
    for (u64 r = 0; r < rounds; r++) {
        for (u64 i = 0; i < TABLE_SIZE; i++) {
            sum += (u8)(((i ^ r) * 31) >> 3);
        }
    }
    return sum;
}

static u64
fib_reference(u64 n)
{
    u64 a = 0, b = 1;
    for (u64 i = 0; i < n; i++) {
        u64 next = a + b;
        a = b;
        b = next;
    }
    return a;
}

typedef struct
{
    const char *name;
    const Function *functions;
    u32 nfunctions;
    u64 arg;
    u64 expected;
} Program;

typedef enum
{
    DISPATCH_SWITCH,
    DISPATCH_THREADED,
    DISPATCH_TAIL,
    DISPATCH_TAIL_SLOTS,
    NDISPATCH,
} Dispatch;

static const char *const dispatch_names[NDISPATCH] = { "switch", "threaded", "tail", "tail+slot" };

static Status
interpret(Machine *m, Dispatch d, u64 arg)
{
    switch (d) {
    case DISPATCH_SWITCH:
        return interpret_switch(m, arg);
    case DISPATCH_THREADED:
        return interpret_threaded(m, arg);
    case DISPATCH_TAIL:
        return tail_interpret(m, arg);
    case DISPATCH_TAIL_SLOTS:
        return slot_interpret(m, arg);
    case NDISPATCH:
    default:
        assert(false);
        unreachable();
    }
}

static const char *
status_name(Status s)
{
    switch (s) {
    case STATUS_OK:
        return "ok";
    case STATUS_OUT_OF_BOUNDS:
        return "out of bounds";
    case STATUS_OUT_OF_BUDGET:
        return "out of budget";
    case STATUS_STACK_OVERFLOW:
        return "stack overflow";
    default:
        assert(false);
        unreachable();
    }
}

static void
expect(const char *program, const char *what, Status got, Status want)
{
    if (got != want) {
        fprintf(stderr, "h9: %s, %s: got %s, want %s\n", program, what, status_name(got), status_name(want));
        exit(1);
    }
}

static u64
count_ops(Machine *m, const Program *p)
{
    m->functions = p->functions;

    expect(p->name, "counting switch run", count_switch(m, p->arg), STATUS_OK);
    u64 ops = m->ops, result = m->result;

    expect(p->name, "counting threaded run", count_threaded(m, p->arg), STATUS_OK);
    if (result != p->expected || m->result != p->expected || m->ops != ops) {
        fprintf(
            stderr,
            "h9: %s: results %llu and %llu, ops %llu and %llu, want result %llu\n",
            p->name,
            (unsigned long long)result,
            (unsigned long long)m->result,
            (unsigned long long)ops,
            (unsigned long long)m->ops,
            (unsigned long long)p->expected
        );
        exit(1);
    }

    return ops;
}

static void
expect_rejections(Machine *m)
{
    for (Dispatch d = 0; d < NDISPATCH; d++) {
        Machine limited = *m;
        limited.functions = count_functions;
        limited.budget = 5;
        expect("count", "budget of 5", interpret(&limited, d, 100), STATUS_OUT_OF_BUDGET);

        limited = *m;
        limited.functions = table_functions;
        limited.heap_size = 100;
        expect("table", "heap of 100 bytes", interpret(&limited, d, 1), STATUS_OUT_OF_BOUNDS);

        limited = *m;
        limited.functions = fib_functions;
        limited.max_depth = 5;
        expect("fib", "depth of 5", interpret(&limited, d, 20), STATUS_STACK_OVERFLOW);

        limited = *m;
        limited.functions = fib_functions;
        limited.stack_size = FIB_FRAME * 4;
        expect("fib", "stack of 4 frames", interpret(&limited, d, 20), STATUS_STACK_OVERFLOW);
    }
}

static double
now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void
cpu_counters(u64 *instructions, u64 *cycles)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) != 0) {
        perror("proc_pid_rusage");
        exit(1);
    }
    *instructions = ri.ri_instructions;
    *cycles = ri.ri_cycles;
#else
    *instructions = *cycles = 0;
#endif
}

#define RUNS 5

typedef struct
{
    double seconds;
    double cycles;
    double instructions;
} Sample;

static double
median(double *v, int n)
{
    for (int i = 1; i < n; i++) {
        double x = v[i];
        int j = i;
        for (; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    return v[n / 2];
}

static Sample
measure(Machine *m, Dispatch d, const Program *p)
{
    m->functions = p->functions;

    double seconds[RUNS], cycles[RUNS], instructions[RUNS];
    for (int r = 0; r < RUNS; r++) {
        u64 i0, c0, i1, c1;
        cpu_counters(&i0, &c0);
        double t0 = now_sec();

        Status s = interpret(m, d, p->arg);

        double t1 = now_sec();
        cpu_counters(&i1, &c1);

        expect(p->name, dispatch_names[d], s, STATUS_OK);
        if (m->result != p->expected) {
            fprintf(stderr, "h9: %s, %s: wrong result\n", p->name, dispatch_names[d]);
            exit(1);
        }

        seconds[r] = t1 - t0;
        cycles[r] = (double)(c1 - c0);
        instructions[r] = (double)(i1 - i0);
    }

    return (Sample){
        median(seconds, RUNS),
        median(cycles, RUNS),
        median(instructions, RUNS),
    };
}

#define STACK_SIZE ((u64)1 << 20)
#define MAX_DEPTH 4096
#define H9_THRESHOLD 1e8

int
main(int argc, char **argv)
{
    bool quick = argc > 1 && strcmp(argv[1], "quick") == 0;

    Program programs[] = {
        { "count", count_functions, LEN(count_functions), quick ? 1000 : 100000000, 0 },
        { "calls", calls_functions, LEN(calls_functions), quick ? 1000 : 10000000, 0 },
        { "table", table_functions, LEN(table_functions), quick ? 2 : 128, 0 },
        { "fib", fib_functions, LEN(fib_functions), quick ? 15 : 32, 0 },
    };
    programs[0].expected = count_reference(programs[0].arg);
    programs[1].expected = calls_reference(programs[1].arg);
    programs[2].expected = table_reference(programs[2].arg);
    programs[3].expected = fib_reference(programs[3].arg);

    for (u32 i = 0; i < LEN(programs); i++) {
        validate(programs[i].functions, programs[i].nfunctions);
    }

    Machine m = {
        .stack = calloc(STACK_SIZE, 1),
        .stack_size = STACK_SIZE,
        .returns = calloc(MAX_DEPTH, sizeof(Return)),
        .max_depth = MAX_DEPTH,
        .heap = calloc(TABLE_SIZE, 1),
        .heap_size = TABLE_SIZE,
        .budget = UINT64_MAX,
    };
    if (m.stack == NULL || m.returns == NULL || m.heap == NULL) {
        fprintf(stderr, "h9: out of memory\n");
        return 1;
    }

    expect_rejections(&m);

    u64 ops[LEN(programs)];
    for (u32 i = 0; i < LEN(programs); i++) {
        ops[i] = count_ops(&m, &programs[i]);
    }

    if (quick) {
        printf("h9: all results, op counts and rejections match\n");
        return 0;
    }

    printf("h9: median of %d runs per row\n", RUNS);
    printf("program  dispatch        ops     Mops/s  cycles/op  instr/op    IPC\n");

    double slowest = 1e300;
    for (u32 i = 0; i < LEN(programs); i++) {
        for (Dispatch d = 0; d < NDISPATCH; d++) {
            Sample s = measure(&m, d, &programs[i]);
            double n = (double)ops[i];
            double rate = n / s.seconds;
            slowest = rate < slowest ? rate : slowest;

            printf(
                "%-8s %-9s %10llu  %9.0f  %9.2f  %8.2f  %5.2f\n",
                programs[i].name,
                dispatch_names[d],
                (unsigned long long)ops[i],
                rate / 1e6,
                s.cycles / n,
                s.instructions / n,
                s.instructions / s.cycles
            );
        }
    }

    printf(
        "H9 %s: slowest row %.0f Mops/s, threshold %.0f Mops/s\n",
        slowest >= H9_THRESHOLD ? "passes" : "fails",
        slowest / 1e6,
        H9_THRESHOLD / 1e6
    );

    return 0;
}
