#define _DEFAULT_SOURCE

#include "../../src/parser.h"
#include "../../test/random/code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define FILES 400
#define RUNS 5

#define SKIP(...)
#define ARITY_0(name, ...) [name] = 0,
#define ARITY_1(name, ...) [name] = 1,
#define ARITY_3(name, ...) [name] = 3,
#define BINARY_ARITY(name, token, prec, marker) [name] = 2 + (marker != NODE_NONE),
#define NODE_ARITY(name, arity, label) [name] = arity,
#define LIST_ARITY(name, start, before, label) [name] = before,
static const u8 node_arity[NNODES] = { CHIBI_NODES(ARITY_0, ARITY_1, BINARY_ARITY, ARITY_3, NODE_ARITY, LIST_ARITY, ARITY_0) };

#define LIST_START(name, start, before, label) [name] = start,
static const u8 list_starts[NNODES] = { CHIBI_NODES(SKIP, SKIP, SKIP, SKIP, SKIP, LIST_START, SKIP) };

#define IS_MARKER(name) [name] = 1,
static const u8 is_marker[NNODES] = { CHIBI_NODES(SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, IS_MARKER) };

enum
{
    TYPE_MARKER = 0xFFFFFFFF,
};

typedef struct
{
    u32 type;
    u32 ref;
} Operand;

typedef struct
{
    u8 *ops;
    u32 *types;
    u32 *a;
    u32 *b;
    u32 count;
} Ir;

typedef struct
{
    Tokens tokens;
    Nodes nodes;
    int n;
    u32 lines;
    int ntop;
} File;

typedef struct
{
    Operand *stack;
    u32 *binding;
    u32 *local_types;
    u32 *undo;
    u32 *scope_marks;
    Ir ir;
} Floor;

static u64
check_floor(Floor *f, const File *file)
{
    Operand *stack = f->stack;
    u32 *binding = f->binding;
    u32 *undo = f->undo;
    u32 *scope_marks = f->scope_marks;
    const u8 *kinds = file->nodes.kinds;
    const u32 *tokens = file->nodes.tokens;
    const u32 *ids = file->tokens.ids;
    u32 sp = 0, nundo = 0, nscopes = 0, nlocals = 1, ir = 0;
    u64 sum = 0;

    for (int i = 0; i < file->n; i++) {
        u8 k = kinds[i];
        if (is_marker[k]) {
            if (k == NODE_BLOCK_START) {
                scope_marks[nscopes++] = nundo;
            }
            stack[sp++] = (Operand){ TYPE_MARKER, k };
            continue;
        }

        u32 type = 1;
        u32 a = 0;
        u8 start = list_starts[k];
        if (start != NODE_NONE) {
            u32 kids = 0;
            while (stack[sp - 1].type != TYPE_MARKER || stack[sp - 1].ref != start) {
                a ^= stack[--sp].ref;
                kids++;
            }
            sp--;
            if (k == NODE_BLOCK) {
                u32 mark = scope_marks[--nscopes];
                while (nundo > mark) {
                    binding[undo[--nundo]] = 0;
                }
            }
            type += kids;
        }
        u32 arity = node_arity[k];
        for (u32 j = 0; j < arity; j++) {
            Operand o = stack[--sp];
            a ^= o.ref;
            type += o.type == TYPE_MARKER ? 0 : o.type;
        }

        if (k == NODE_NAME) {
            u32 id = ids[tokens[i]];
            u32 local = binding[id];
            type = local ? f->local_types[local] : 2;
        } else if (k == NODE_VAR_INIT || k == NODE_VAR_TYPE || k == NODE_VAR_TYPE_INIT || k == NODE_PARAM_TYPE) {
            u32 id = ids[tokens[i]];
            if (binding[id] == 0) {
                undo[nundo++] = id;
            }
            f->local_types[nlocals] = type;
            binding[id] = nlocals++;
        }

        f->ir.ops[ir] = k;
        f->ir.types[ir] = type;
        f->ir.a[ir] = a;
        f->ir.b[ir] = (u32)i;
        ir++;
        stack[sp++] = (Operand){ type, ir };
    }

    while (nundo > 0) {
        binding[undo[--nundo]] = 0;
    }
    for (u32 j = 0; j < ir; j += 64) {
        sum += f->ir.types[j];
    }
    f->ir.count = ir;
    return sum + sp;
}

enum
{
    TREE_MARK = 0x80000000u,
};

typedef struct
{
    u32 *first;
    u32 *next;
    u8 *nfixed;
    u8 *base;
    u32 *stack;
    u32 *roots;
    u32 nroots;
} Tree;

static void
build_tree(Tree *t, const File *file)
{
    const u8 *kinds = file->nodes.kinds;
    u32 *stack = t->stack;
    u32 sp = 0;

    for (int i = 0; i < file->n; i++) {
        u8 k = kinds[i];
        if (is_marker[k]) {
            stack[sp++] = TREE_MARK | k;
            continue;
        }

        u32 first = 0;
        u32 base = 1;
        u8 start = list_starts[k];
        if (start != NODE_NONE) {
            while (stack[sp - 1] != (TREE_MARK | start)) {
                u32 c = stack[--sp];
                if (c & TREE_MARK) {
                    base++;
                    continue;
                }
                t->next[c] = first;
                first = c + 1;
            }
            sp--;
        }
        u32 fixed = 0;
        u32 arity = node_arity[k];
        for (u32 j = 0; j < arity; j++) {
            u32 c = stack[--sp];
            if (c & TREE_MARK) {
                continue;
            }
            t->next[c] = first;
            first = c + 1;
            fixed++;
        }
        t->first[i] = first;
        t->nfixed[i] = (u8)fixed;
        t->base[i] = (u8)base;
        stack[sp++] = (u32)i;
    }

    t->nroots = 0;
    for (u32 j = 0; j < sp; j++) {
        if (!(stack[j] & TREE_MARK)) {
            t->roots[t->nroots++] = stack[j];
        }
    }
}

typedef struct
{
    const Tree *tree;
    const u8 *kinds;
    const u32 *tokens;
    const u32 *ids;
    u32 *binding;
    u32 *local_types;
    u32 *undo;
    Ir *ir;
    u32 nundo;
    u32 nlocals;
    u32 nir;
} Walk;

static Operand
visit(Walk *w, u32 node)
{
    u8 k = w->kinds[node];
    u32 mark = w->nundo;
    u32 type = w->tree->base[node];
    u32 a = 0;
    u32 fixed = w->tree->nfixed[node];
    u32 j = 0;

    for (u32 c = w->tree->first[node]; c; c = w->tree->next[c - 1], j++) {
        Operand o = visit(w, c - 1);
        a ^= o.ref;
        type += j < fixed ? o.type : 1;
    }

    if (k == NODE_BLOCK) {
        while (w->nundo > mark) {
            w->binding[w->undo[--w->nundo]] = 0;
        }
    }
    if (k == NODE_NAME) {
        u32 id = w->ids[w->tokens[node]];
        u32 local = w->binding[id];
        type = local ? w->local_types[local] : 2;
    } else if (k == NODE_VAR_INIT || k == NODE_VAR_TYPE || k == NODE_VAR_TYPE_INIT || k == NODE_PARAM_TYPE) {
        u32 id = w->ids[w->tokens[node]];
        if (w->binding[id] == 0) {
            w->undo[w->nundo++] = id;
        }
        w->local_types[w->nlocals] = type;
        w->binding[id] = w->nlocals++;
    }

    u32 ir = w->nir;
    w->ir->ops[ir] = k;
    w->ir->types[ir] = type;
    w->ir->a[ir] = a;
    w->ir->b[ir] = node;
    w->nir = ir + 1;
    return (Operand){ type, ir + 1 };
}

static u64
walk_tree(Floor *f, const Tree *t, const File *file)
{
    Walk w = {
        .tree = t,
        .kinds = file->nodes.kinds,
        .tokens = file->nodes.tokens,
        .ids = file->tokens.ids,
        .binding = f->binding,
        .local_types = f->local_types,
        .undo = f->undo,
        .ir = &f->ir,
        .nlocals = 1,
    };
    u64 sum = 0;

    for (u32 r = 0; r < t->nroots; r++) {
        sum += visit(&w, t->roots[r]).ref;
    }
    while (w.nundo > 0) {
        w.binding[w.undo[--w.nundo]] = 0;
    }
    for (u32 j = 0; j < w.nir; j += 64) {
        sum += f->ir.types[j];
    }
    f->ir.count = w.nir;
    return sum;
}

typedef struct
{
    u32 *start;
    u32 *count;
    u32 *kids;
    u8 *nfixed;
    u8 *base;
    u32 *stack;
    u32 *roots;
    u32 nroots;
} KidTree;

static void
build_kid_tree(KidTree *t, const File *file)
{
    const u8 *kinds = file->nodes.kinds;
    u32 *stack = t->stack;
    u32 sp = 0, pos = 0;

    for (int i = 0; i < file->n; i++) {
        u8 k = kinds[i];
        if (is_marker[k]) {
            stack[sp++] = TREE_MARK | k;
            continue;
        }

        u32 top = sp, list = sp, base = 1;
        u8 start = list_starts[k];
        if (start != NODE_NONE) {
            while (stack[sp - 1] != (TREE_MARK | start)) {
                base += (stack[--sp] & TREE_MARK) != 0;
            }
            list = --sp;
        }
        sp -= node_arity[k];

        u32 first = pos, fixed = 0;
        for (u32 j = sp; j < top; j++) {
            u32 c = stack[j];
            if (c & TREE_MARK) {
                continue;
            }
            t->kids[pos++] = c;
            fixed += j < list;
        }
        t->start[i] = first;
        t->count[i] = pos - first;
        t->nfixed[i] = (u8)fixed;
        t->base[i] = (u8)base;
        stack[sp++] = (u32)i;
    }

    t->nroots = 0;
    for (u32 j = 0; j < sp; j++) {
        if (!(stack[j] & TREE_MARK)) {
            t->roots[t->nroots++] = stack[j];
        }
    }
}

typedef struct
{
    const KidTree *tree;
    const u8 *kinds;
    const u32 *tokens;
    const u32 *ids;
    u32 *binding;
    u32 *local_types;
    u32 *undo;
    Ir *ir;
    u32 nundo;
    u32 nlocals;
    u32 nir;
} KidWalk;

static Operand
visit_kids(KidWalk *w, u32 node)
{
    const KidTree *t = w->tree;
    u8 k = w->kinds[node];
    u32 mark = w->nundo;
    u32 type = t->base[node];
    u32 a = 0;
    u32 fixed = t->nfixed[node];
    const u32 *kids = t->kids + t->start[node];
    u32 n = t->count[node];

    for (u32 j = 0; j < n; j++) {
        Operand o = visit_kids(w, kids[j]);
        a ^= o.ref;
        type += j < fixed ? o.type : 1;
    }

    if (k == NODE_BLOCK) {
        while (w->nundo > mark) {
            w->binding[w->undo[--w->nundo]] = 0;
        }
    }
    if (k == NODE_NAME) {
        u32 id = w->ids[w->tokens[node]];
        u32 local = w->binding[id];
        type = local ? w->local_types[local] : 2;
    } else if (k == NODE_VAR_INIT || k == NODE_VAR_TYPE || k == NODE_VAR_TYPE_INIT || k == NODE_PARAM_TYPE) {
        u32 id = w->ids[w->tokens[node]];
        if (w->binding[id] == 0) {
            w->undo[w->nundo++] = id;
        }
        w->local_types[w->nlocals] = type;
        w->binding[id] = w->nlocals++;
    }

    u32 ir = w->nir;
    w->ir->ops[ir] = k;
    w->ir->types[ir] = type;
    w->ir->a[ir] = a;
    w->ir->b[ir] = node;
    w->nir = ir + 1;
    return (Operand){ type, ir + 1 };
}

static u64
walk_kid_tree(Floor *f, const KidTree *t, const File *file)
{
    KidWalk w = {
        .tree = t,
        .kinds = file->nodes.kinds,
        .tokens = file->nodes.tokens,
        .ids = file->tokens.ids,
        .binding = f->binding,
        .local_types = f->local_types,
        .undo = f->undo,
        .ir = &f->ir,
        .nlocals = 1,
    };
    u64 sum = 0;

    for (u32 r = 0; r < t->nroots; r++) {
        sum += visit_kids(&w, t->roots[r]).ref;
    }
    while (w.nundo > 0) {
        w.binding[w.undo[--w.nundo]] = 0;
    }
    for (u32 j = 0; j < w.nir; j += 64) {
        sum += f->ir.types[j];
    }
    f->ir.count = w.nir;
    return sum;
}

static void
cpu_counters(u64 *instructions, u64 *cycles)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    *instructions = ri.ri_instructions;
    *cycles = ri.ri_cycles;
#else
    *instructions = *cycles = 0;
#endif
}

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

int
main(void)
{
    Arena arena = { 0 };
    u64 rng = 1;
    Vocabulary v;
    vocabulary_init(&rng, &v, &arena);

    static File files[FILES];
    Lexer lx;
    lexer_init(&lx, &arena, (size_t)FILES * 24000);
    u64 lines = 0, nodes = 0;
    int max_nodes = 0;
    for (int f = 0; f < FILES; f++) {
        size_t size = 8000 + (size_t)random_below(&rng, 16000);
        size_t cap = size + CODE_SLACK + LEXER_SOURCE_PADDING;
        char *text = arena_alloc(&arena, cap, 64);
        memset(text, 0, cap);
        size_t len = random_code(&rng, text, &v, size);

        Tokens tokens = lexer_alloc_tokens(&arena, len);
        int ntok = lexer_tokenize(&lx, text, tokens);
        Nodes out = parser_alloc_nodes(&arena, ntok);
        Parser p;
        int n = parser_parse(&p, tokens, ntok, out);
        if (ntok == 0 || p.error) {
            fprintf(stderr, "floor: file %d does not parse\n", f);
            return 1;
        }

        u32 file_lines = 0;
        for (size_t i = 0; i < len; i++) {
            file_lines += text[i] == '\n';
        }
        files[f] = (File){ tokens, out, n, file_lines, p.top_count };
        lines += file_lines;
        nodes += (u64)n;
        max_nodes = n > max_nodes ? n : max_nodes;
    }

    Floor fl = {
        .stack = calloc((size_t)max_nodes, sizeof(Operand)),
        .binding = calloc(lx.names.count, sizeof(u32)),
        .local_types = calloc((size_t)max_nodes, sizeof(u32)),
        .undo = calloc((size_t)max_nodes, sizeof(u32)),
        .scope_marks = calloc((size_t)max_nodes, sizeof(u32)),
        .ir = {
            .ops = calloc((size_t)max_nodes, 1),
            .types = calloc((size_t)max_nodes, sizeof(u32)),
            .a = calloc((size_t)max_nodes, sizeof(u32)),
            .b = calloc((size_t)max_nodes, sizeof(u32)),
        },
    };

    Tree tree = {
        .first = calloc((size_t)max_nodes, sizeof(u32)),
        .next = calloc((size_t)max_nodes, sizeof(u32)),
        .nfixed = calloc((size_t)max_nodes, 1),
        .base = calloc((size_t)max_nodes, 1),
        .stack = calloc((size_t)max_nodes, sizeof(u32)),
        .roots = calloc((size_t)max_nodes, sizeof(u32)),
    };
    KidTree kid_tree = {
        .start = calloc((size_t)max_nodes, sizeof(u32)),
        .count = calloc((size_t)max_nodes, sizeof(u32)),
        .kids = calloc((size_t)max_nodes, sizeof(u32)),
        .nfixed = calloc((size_t)max_nodes, 1),
        .base = calloc((size_t)max_nodes, 1),
        .stack = calloc((size_t)max_nodes, sizeof(u32)),
        .roots = calloc((size_t)max_nodes, sizeof(u32)),
    };
    u8 *ops = calloc((size_t)max_nodes, 1);
    u32 *types = calloc((size_t)max_nodes, sizeof(u32));
    u64 mismatches = 0, top_mismatches = 0;
    for (int f = 0; f < FILES; f++) {
        check_floor(&fl, &files[f]);
        u32 count = fl.ir.count;
        memcpy(ops, fl.ir.ops, count);
        memcpy(types, fl.ir.types, count * sizeof(u32));
        build_tree(&tree, &files[f]);
        top_mismatches += tree.nroots != (u32)files[f].ntop || files[f].nodes.top_ends[files[f].ntop - 1] != (u32)files[f].n;
        for (u32 r = 0; r < tree.nroots && r < (u32)files[f].ntop; r++) {
            top_mismatches += tree.roots[r] != files[f].nodes.top_ends[r] - 1;
        }
        walk_tree(&fl, &tree, &files[f]);
        mismatches += fl.ir.count != count || memcmp(ops, fl.ir.ops, count) != 0 ||
                      memcmp(types, fl.ir.types, count * sizeof(u32)) != 0;
        build_kid_tree(&kid_tree, &files[f]);
        walk_kid_tree(&fl, &kid_tree, &files[f]);
        mismatches += fl.ir.count != count || memcmp(ops, fl.ir.ops, count) != 0 ||
                      memcmp(types, fl.ir.types, count * sizeof(u32)) != 0;
    }
    if (top_mismatches) {
        fprintf(stderr, "floor: the parser's top-level list differs from the postorder roots %llu times\n", (unsigned long long)top_mismatches);
        return 1;
    }
    if (mismatches) {
        fprintf(stderr, "floor: the tree walk differs from the postorder pass in %llu files\n", (unsigned long long)mismatches);
        return 1;
    }

    enum { MODES = 5 };
    const char *names[MODES] = { "postorder pass", "linked build", "linked + walk", "array build", "array + walk" };
    double cycles[MODES][RUNS], instructions[MODES][RUNS];
    u64 check = 0, ir_total = 0;
    for (int r = 0; r < RUNS; r++) {
        for (int m = 0; m < MODES; m++) {
            u64 i0, c0, i1, c1;
            ir_total = 0;
            cpu_counters(&i0, &c0);
            for (int f = 0; f < FILES; f++) {
                if (m == 0) {
                    check += check_floor(&fl, &files[f]);
                } else if (m == 1) {
                    build_tree(&tree, &files[f]);
                    check += tree.nroots;
                } else if (m == 2) {
                    build_tree(&tree, &files[f]);
                    check += walk_tree(&fl, &tree, &files[f]);
                } else if (m == 3) {
                    build_kid_tree(&kid_tree, &files[f]);
                    check += kid_tree.nroots;
                } else {
                    build_kid_tree(&kid_tree, &files[f]);
                    check += walk_kid_tree(&fl, &kid_tree, &files[f]);
                }
                ir_total += fl.ir.count;
            }
            cpu_counters(&i1, &c1);
            cycles[m][r] = (double)(c1 - c0);
            instructions[m][r] = (double)(i1 - i0);
        }
    }

    printf("floor: %llu lines, %llu nodes (%.2f per line), %llu IR records (%.2f per line)\n",
           (unsigned long long)lines, (unsigned long long)nodes, (double)nodes / lines,
           (unsigned long long)ir_total, (double)ir_total / lines);
    for (int m = 0; m < MODES; m++) {
        double c = median(cycles[m], RUNS), in = median(instructions[m], RUNS);
        printf("floor: %-15s %6.2f cycles per node, %6.1f cycles per line, %6.2f instructions per node, IPC %.2f\n",
               names[m], c / nodes, c / lines, in / nodes, in / c);
    }
    printf("floor: checksum %llu\n", (unsigned long long)check);
    return 0;
}
