#include "check.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_MAX_DEPTH 1000
#define MEMBER_UNDEFINED 0xFFFFFFFFu
#define MEMBER_NONE 0xFFFFFFFEu
#define NO_SLOT 0xFFFFFFFFu

enum
{
    REF_VALUE = 1u << 31,
    REF_RESULT = 1u << 30,
    REF_INDEX = REF_RESULT - 1,
};

enum
{
    OPERAND_MEMBER = 0xFFFFFE00u,
    OPERAND_RETURN,
    OPERAND_BACKING,
    OPERAND_LOCAL,
    OPERAND_ADDRESS,
    OPERAND_FN,
    OPERAND_STMT,
    OPERAND_RANGE,
    OPERAND_DISCARD,
    OPERAND_MARKER = 0xFFFFFF00u,
};

enum
{
    SHORT_VALUE = 0xFFFFFFFCu,
    SHORT_FOLD,
    SHORT_PASS,
    SHORT_SKIP,
};

enum
{
    IF_BEFORE = 1,
    IF_ANY = 2,
    IF_ELSE = 4,
    IF_OPENED = 8,
};

enum
{
    OP_ARITH = 1,
    OP_INT,
    OP_SHIFT,
    OP_COMPARE,
    OP_EQUALITY,
};

#define ARITY_0(name, ...) [name] = 0,
#define ARITY_1(name, ...) [name] = 1,
#define ARITY_3(name, ...) [name] = 3,
#define BINARY_ARITY(name, token, prec, marker)                                \
    [name] = 2 + (marker != NODE_NONE),
#define NODE_ARITY(name, arity, label) [name] = arity,
#define LIST_ARITY(name, start, before, label) [name] = before,
static const u8 node_arity[NNODES] = {
    CHIBI_NODES(ARITY_0, ARITY_1, BINARY_ARITY, ARITY_3, NODE_ARITY, LIST_ARITY, ARITY_0)
};

#define LIST_START(name, start, before, label) [name] = start,
static const u8 list_starts[NNODES] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, LIST_START, CHIBI_SKIP)
};

#define IS_MARKER(name) [name] = 1,
static const u8 is_marker[NNODES] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, IS_MARKER)
};

static const u8 decl_kinds[NNODES] = {
    [NODE_CONST_INIT] = DECL_CONST, [NODE_CONST_TYPE_INIT] = DECL_CONST,
    [NODE_STRUCT] = DECL_STRUCT,    [NODE_ENUM] = DECL_ENUM,
    [NODE_UNION] = DECL_UNION,      [NODE_FN] = DECL_FN,
};

static const u8 operator_classes[NNODES] = {
    [NODE_ADD] = OP_ARITH,    [NODE_SUB] = OP_ARITH,
    [NODE_MUL] = OP_ARITH,    [NODE_DIV] = OP_ARITH,
    [NODE_MOD] = OP_INT,      [NODE_ADD_WRAP] = OP_INT,
    [NODE_SUB_WRAP] = OP_INT, [NODE_MUL_WRAP] = OP_INT,
    [NODE_BIT_AND] = OP_INT,  [NODE_BIT_OR] = OP_INT,
    [NODE_BIT_XOR] = OP_INT,  [NODE_SHL] = OP_SHIFT,
    [NODE_SHR] = OP_SHIFT,    [NODE_LT] = OP_COMPARE,
    [NODE_LE] = OP_COMPARE,   [NODE_GT] = OP_COMPARE,
    [NODE_GE] = OP_COMPARE,   [NODE_EQ] = OP_EQUALITY,
    [NODE_NE] = OP_EQUALITY,
};

static const u8 binary_irs[NNODES] = {
    [NODE_ADD] = IR_ADD,
    [NODE_SUB] = IR_SUB,
    [NODE_MUL] = IR_MUL,
    [NODE_DIV] = IR_DIV,
    [NODE_MOD] = IR_REM,
    [NODE_ADD_WRAP] = IR_ADD_WRAP,
    [NODE_SUB_WRAP] = IR_SUB_WRAP,
    [NODE_MUL_WRAP] = IR_MUL_WRAP,
    [NODE_BIT_AND] = IR_AND,
    [NODE_BIT_OR] = IR_OR,
    [NODE_BIT_XOR] = IR_XOR,
    [NODE_SHL] = IR_SHL,
    [NODE_SHR] = IR_SHR,
    [NODE_LT] = IR_LT,
    [NODE_LE] = IR_LE,
    [NODE_GT] = IR_GT,
    [NODE_GE] = IR_GE,
    [NODE_EQ] = IR_EQ,
    [NODE_NE] = IR_NE,
};

static const u8 exact_ops[NNODES] = {
    [NODE_ADD] = EXACT_ADD,      [NODE_SUB] = EXACT_SUB,
    [NODE_MUL] = EXACT_MUL,      [NODE_DIV] = EXACT_DIV,
    [NODE_MOD] = EXACT_REM,      [NODE_ADD_WRAP] = EXACT_ADD,
    [NODE_SUB_WRAP] = EXACT_SUB, [NODE_MUL_WRAP] = EXACT_MUL,
    [NODE_BIT_AND] = EXACT_AND,  [NODE_BIT_OR] = EXACT_OR,
    [NODE_BIT_XOR] = EXACT_XOR,  [NODE_SHL] = EXACT_SHL,
    [NODE_SHR] = EXACT_SHR,
};

static const u8 assign_ops[NNODES] = {
    [NODE_ADD_ASSIGN] = NODE_ADD,
    [NODE_SUB_ASSIGN] = NODE_SUB,
    [NODE_MUL_ASSIGN] = NODE_MUL,
    [NODE_ADD_WRAP_ASSIGN] = NODE_ADD_WRAP,
    [NODE_SUB_WRAP_ASSIGN] = NODE_SUB_WRAP,
    [NODE_MUL_WRAP_ASSIGN] = NODE_MUL_WRAP,
    [NODE_DIV_ASSIGN] = NODE_DIV,
    [NODE_MOD_ASSIGN] = NODE_MOD,
    [NODE_BIT_AND_ASSIGN] = NODE_BIT_AND,
    [NODE_BIT_OR_ASSIGN] = NODE_BIT_OR,
    [NODE_BIT_XOR_ASSIGN] = NODE_BIT_XOR,
    [NODE_SHL_ASSIGN] = NODE_SHL,
    [NODE_SHR_ASSIGN] = NODE_SHR,
};

#define FIXED_KIND(id, kind, bits, is_signed, text) [id] = kind,
#define FIXED_BITS(id, kind, bits, is_signed, text) [id] = bits,
#define FIXED_SIGNED(id, kind, bits, is_signed, text) [id] = is_signed,
#define FIXED_TEXT(id, kind, bits, is_signed, text) [id] = text,
static const u8 fixed_kinds[NFIXED_TYPES] = { CHIBI_FIXED_TYPES(FIXED_KIND) };
static const u8 fixed_bits[NFIXED_TYPES] = { CHIBI_FIXED_TYPES(FIXED_BITS) };
static const u8 fixed_signed[NFIXED_TYPES] = { CHIBI_FIXED_TYPES(FIXED_SIGNED) };
static const char *const fixed_texts[NFIXED_TYPES] = { [TYPE_ID_NONE] = "?",
                                                       CHIBI_FIXED_TYPES(FIXED_TEXT) };

#define BUILTIN(name) [name - NAME_LAST_KEYWORD - 1]
static_assert(NBUILTIN_TYPES == 15, "every builtin type name needs an entry in builtin_types");
static const u8 builtin_types[NBUILTIN_TYPES] = {
    BUILTIN(NAME_VOID) = TYPE_ID_VOID,
    BUILTIN(NAME_BOOL) = TYPE_ID_BOOL,
    BUILTIN(NAME_INT) = TYPE_ID_I64,
    BUILTIN(NAME_I8) = TYPE_ID_I8,
    BUILTIN(NAME_I16) = TYPE_ID_I16,
    BUILTIN(NAME_I32) = TYPE_ID_I32,
    BUILTIN(NAME_I64) = TYPE_ID_I64,
    BUILTIN(NAME_U8) = TYPE_ID_U8,
    BUILTIN(NAME_U16) = TYPE_ID_U16,
    BUILTIN(NAME_U32) = TYPE_ID_U32,
    BUILTIN(NAME_U64) = TYPE_ID_U64,
    BUILTIN(NAME_F32) = TYPE_ID_F32,
    BUILTIN(NAME_F64) = TYPE_ID_F64,
    BUILTIN(NAME_UINTPTR) = TYPE_ID_UINTPTR,
    BUILTIN(NAME_ERROR) = TYPE_ID_ERROR,
};

#define DIAGNOSTIC_TEXT(name, text) [name] = text,
static const char *const diagnostic_texts[NDIAGNOSTICS] = {
    CHIBI_DIAGNOSTICS(DIAGNOSTIC_TEXT)
};

#define IR_TEXT(name, text) [name] = text,
static const char *const ir_texts[NIR_KINDS] = { CHIBI_IR(IR_TEXT) };

static const Operand poison = { TYPE_ID_NONE, 0 };

static void *
resize(void *p, size_t count, size_t size)
{
    return realloc(p, count * size);
}

static void
report(Checker *c, u32 file, u32 token, u8 kind, u32 arg, u32 arg2)
{
    if (c->ndiags == c->diags_cap) {
        c->diags_cap = c->diags_cap ? c->diags_cap * 2 : 16;
        c->diags = resize(c->diags, c->diags_cap, sizeof *c->diags);
    }
    c->diags[c->ndiags++] = (Diagnostic){ .file = file,
                                          .token = token,
                                          .arg = arg,
                                          .arg2 = arg2,
                                          .kind = kind };
}

static void
reserve_types(Checker *c, u32 cap)
{
    Types *t = &c->types;
    t->cap = cap;
    t->kinds = resize(t->kinds, cap, sizeof *t->kinds);
    t->arg0 = resize(t->arg0, cap, sizeof *t->arg0);
    t->arg1 = resize(t->arg1, cap, sizeof *t->arg1);
    t->pointers = resize(t->pointers, cap, sizeof *t->pointers);
    t->optionals = resize(t->optionals, cap, sizeof *t->optionals);
    t->slices = resize(t->slices, cap, sizeof *t->slices);
}

static u32
add_type(Checker *c, u8 kind, u32 arg0, u64 arg1)
{
    Types *t = &c->types;
    if (t->count == t->cap) {
        reserve_types(c, t->cap * 2);
    }

    u32 id = t->count++;
    t->kinds[id] = kind;
    t->arg0[id] = arg0;
    t->arg1[id] = arg1;
    t->pointers[id] = 0;
    t->optionals[id] = 0;
    t->slices[id] = 0;
    return id;
}

static u32
pointer_type(Checker *c, u32 elem)
{
    u32 id = c->types.pointers[elem];
    if (id == 0) {
        id = add_type(c, TYPE_POINTER, elem, 0);
        c->types.pointers[elem] = id;
    }
    return id;
}

static u32
optional_type(Checker *c, u32 elem)
{
    u32 id = c->types.optionals[elem];
    if (id == 0) {
        id = add_type(c, TYPE_OPTIONAL, elem, 0);
        c->types.optionals[elem] = id;
    }
    return id;
}

static u32
slice_type(Checker *c, u32 elem)
{
    u32 id = c->types.slices[elem];
    if (id == 0) {
        id = add_type(c, TYPE_SLICE, elem, 0);
        c->types.slices[elem] = id;
    }
    return id;
}

static u64
type_key(const Checker *c, u8 kind, u32 arg0, u64 arg1)
{
    u64 h = (u64)kind << 56 ^ arg1 * 0x9E3779B97F4A7C15ull;
    if (kind == TYPE_FN) {
        const u32 *list = c->lists + arg0;
        for (u32 i = 0; i <= list[0]; i++) {
            h = (h ^ list[i]) * 0xFF51AFD7ED558CCDull;
        }
    } else {
        h = (h ^ arg0) * 0xFF51AFD7ED558CCDull;
    }
    return h ^ h >> 32;
}

static bool
same_type(const Checker *c, u32 id, u8 kind, u32 arg0, u64 arg1)
{
    const Types *t = &c->types;
    if (t->kinds[id] != kind || t->arg1[id] != arg1) {
        return false;
    }
    if (kind != TYPE_FN) {
        return t->arg0[id] == arg0;
    }

    const u32 *a = c->lists + t->arg0[id];
    const u32 *b = c->lists + arg0;
    return a[0] == b[0] && memcmp(a + 1, b + 1, a[0] * sizeof *a) == 0;
}

static void
grow_type_map(Checker *c, u32 cap)
{
    Types *t = &c->types;
    u32 *map = calloc(cap, sizeof *map);
    for (u32 i = 0; i <= t->map_mask; i++) {
        u32 id = t->map[i];
        if (id) {
            u32 j = (u32)type_key(c, t->kinds[id], t->arg0[id], t->arg1[id]) &
                    (cap - 1);
            while (map[j]) {
                j = (j + 1) & (cap - 1);
            }
            map[j] = id;
        }
    }

    free(t->map);
    t->map = map;
    t->map_mask = cap - 1;
}

static u32
intern_type(Checker *c, u8 kind, u32 arg0, u64 arg1)
{
    if ((c->types.map_count + 1) * 2 > c->types.map_mask + 1) {
        grow_type_map(c, (c->types.map_mask + 1) * 2);
    }

    u32 mask = c->types.map_mask;
    for (u32 i = (u32)type_key(c, kind, arg0, arg1) & mask;; i = (i + 1) & mask) {
        u32 id = c->types.map[i];
        if (id == 0) {
            id = add_type(c, kind, arg0, arg1);
            c->types.map[i] = id;
            c->types.map_count++;
            return id;
        }
        if (same_type(c, id, kind, arg0, arg1)) {
            return id;
        }
    }
}

static u32
fn_type(Checker *c, u32 list, u32 ret)
{
    u32 id = intern_type(c, TYPE_FN, list, ret);
    if (c->types.arg0[id] != list) {
        c->nlists = list;
    }
    return id;
}

static u32
reserve_list(Checker *c, u32 n)
{
    if (c->nlists + n + 1 > c->lists_cap) {
        c->lists_cap = (c->nlists + n + 1) * 2;
        c->lists = resize(c->lists, c->lists_cap, sizeof *c->lists);
    }

    u32 list = c->nlists;
    c->lists[list] = n;
    c->nlists += n + 1;
    return list;
}

static void
reserve_members(Checker *c, u32 n)
{
    if (c->nmembers + n > c->members_cap) {
        c->members_cap = (c->nmembers + n) * 2;
        c->members = resize(c->members, c->members_cap, sizeof *c->members);
    }
}

static void
reserve_stack(Checker *c, u32 n)
{
    if (c->sp + n > c->stack_cap) {
        c->stack_cap = (c->sp + n) * 2;
        c->stack = resize(c->stack, c->stack_cap, sizeof *c->stack);
        c->stack_tokens = resize(c->stack_tokens, c->stack_cap, sizeof *c->stack_tokens);
    }
}

static void
push(Checker *c, u32 token, u32 type, u32 ref)
{
    assert(c->sp < c->stack_cap);
    c->stack_tokens[c->sp] = token;
    c->stack[c->sp++] = (Operand){ type, ref };
}

static void
push_operand(Checker *c, u32 token, Operand o)
{
    push(c, token, o.type, o.ref);
}

static void
push_type(Checker *c, u32 token, u32 type)
{
    push(c, token, type ? TYPE_ID_TYPE : TYPE_ID_NONE, type);
}

static Operand
pop(Checker *c)
{
    assert(c->sp > 0);
    return c->stack[--c->sp];
}

static u32
list_start(const Checker *c, u8 start)
{
    u32 m = c->sp;
    while (c->stack[--m].type != OPERAND_MARKER + start) {
    }
    return m;
}

static u32
node_name(const Checker *c, u32 file, u32 node)
{
    const ParsedFile *f = &c->files[file];
    return f->tokens.ids[f->nodes.tokens[node]];
}

static bool
earlier(const Checker *c, u32 a, u32 b)
{
    const Decls *d = &c->decls;
    return d->files[a] != d->files[b] ? d->files[a] < d->files[b]
                                      : d->roots[a] < d->roots[b];
}

static void
report_cycle(Checker *c, u32 decl)
{
    Decls *d = &c->decls;
    u32 k = c->nclaims;
    while (c->claims[--k] != decl) {
    }
    for (u32 i = k; i < c->nclaims; i++) {
        if (d->flags[c->claims[i]] & DECL_CYCLE) {
            return;
        }
    }

    u32 first = k;
    for (u32 i = k + 1; i < c->nclaims; i++) {
        if (earlier(c, c->claims[i], c->claims[first])) {
            first = i;
        }
    }

    u32 at = c->claims[first];
    u32 next = c->claims[first + 1 < c->nclaims ? first + 1 : k];
    u32 token = c->files[d->files[at]].nodes.tokens[d->roots[at]];
    if (next == at) {
        report(c, d->files[at], token, DIAG_CYCLE, 0, 0);
    } else {
        report(c, d->files[at], token, DIAG_CYCLE_THROUGH, d->names[next], 0);
    }

    for (u32 i = k; i < c->nclaims; i++) {
        d->flags[c->claims[i]] |= DECL_CYCLE;
    }
}

static void check_decl(Checker *c, u32 decl);

static void check_body(Checker *c, u32 decl);

static bool
claimable(Checker *c, u32 decl, u32 file, u32 token)
{
    if (c->decls.busy[decl]) {
        report_cycle(c, decl);
        return false;
    }
    if (c->nclaims == CHECK_MAX_DEPTH) {
        report(c, file, token, DIAG_TOO_DEEP, 0, 0);
        return false;
    }
    return true;
}

static void
need(Checker *c, u32 decl, u32 file, u32 token)
{
    if (c->decls.phases[decl] < PHASE_RESOLVED && claimable(c, decl, file, token)) {
        check_decl(c, decl);
    }
}

static void
need_body(Checker *c, u32 decl, u32 file, u32 token)
{
    need(c, decl, file, token);
    if (c->decls.phases[decl] == PHASE_RESOLVED && claimable(c, decl, file, token)) {
        check_body(c, decl);
    }
}

static Operand
resolve_name(Checker *c, u32 file, u32 node)
{
    const ParsedFile *f = &c->files[file];
    u32 token = f->nodes.tokens[node];
    u32 id = f->tokens.ids[token];

    u32 builtin = id - (NAME_LAST_KEYWORD + 1);
    if (builtin < NBUILTIN_TYPES) {
        return (Operand){ TYPE_ID_TYPE, builtin_types[builtin] };
    }

    u32 decl = c->package_names[id];
    if (decl == 0) {
        report(c, file, token, DIAG_UNKNOWN_NAME, 0, 0);
        return poison;
    }

    switch (c->decls.kinds[decl]) {
        case DECL_STRUCT:
        case DECL_ENUM:
        case DECL_UNION: return (Operand){ TYPE_ID_TYPE, c->decls.types[decl] };
        case DECL_CONST:
            need(c, decl, file, token);
            return (Operand){ c->decls.types[decl], c->decls.values[decl] };
        case DECL_FN:
            report(c, file, token, DIAG_FN_VALUE, 0, 0);
            return poison;
        default: assert(false && "unknown declaration kind"); return poison;
    }
}

static bool
is_int(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return k == TYPE_INT || k == TYPE_UINTPTR || k == TYPE_UNTYPED_INT;
}

static bool
is_float(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return k == TYPE_FLOAT || k == TYPE_UNTYPED_FLOAT;
}

static bool
is_number(const Checker *c, u32 type)
{
    return is_int(c, type) || is_float(c, type);
}

static bool
is_scalar(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return is_number(c, type) || k == TYPE_BOOL || k == TYPE_ENUM || k == TYPE_POINTER;
}

static bool
is_untyped(u32 type)
{
    return type == TYPE_ID_UNTYPED_INT || type == TYPE_ID_UNTYPED_FLOAT;
}

static bool
is_aggregate(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_ARRAY;
}

static bool
is_storable(const Checker *c, u32 type)
{
    while (is_aggregate(c, type)) {
        type = c->types.arg0[type];
    }
    return is_scalar(c, type) && !is_untyped(type);
}

u64
check_type_size(const Checker *c, u32 type)
{
    const Types *t = &c->types;
    switch (t->kinds[type]) {
        case TYPE_BOOL:
        case TYPE_INT:
        case TYPE_UINTPTR:
        case TYPE_FLOAT: return t->arg0[type] / 8;
        case TYPE_POINTER: return 8;
        case TYPE_ENUM: return check_type_size(c, (u32)t->arg1[type]);
        case TYPE_ARRAY: {
            u64 n = t->arg1[type];
            u64 size = check_type_size(c, t->arg0[type]);
            return size && n > UINT64_MAX / size ? UINT64_MAX : n * size;
        }
        default: return 0;
    }
}

u32
check_type_align(const Checker *c, u32 type)
{
    while (is_aggregate(c, type)) {
        type = c->types.arg0[type];
    }
    return (u32)check_type_size(c, type);
}

static bool
is_value_type(u32 type)
{
    return type != TYPE_ID_NONE && type != TYPE_ID_TYPE && type != TYPE_ID_VOID &&
           type != TYPE_ID_UNDEFINED && type < OPERAND_MEMBER;
}

static bool
is_constant(Operand o)
{
    return !(o.ref & REF_VALUE);
}

static u32
value_of(Operand o)
{
    return o.ref & REF_INDEX;
}

static bool
int_fits(Checker *c, u32 type, u32 value)
{
    return exact_is_int(&c->exacts, value) &&
           exact_fits(&c->exacts, value, c->types.arg0[type], c->types.arg1[type]);
}

static Operand
number_literal(Checker *c, u32 file, u32 node, u32 type)
{
    const ParsedFile *f = &c->files[file];
    u32 token = f->nodes.tokens[node];
    bool is_float_literal = type == TYPE_ID_UNTYPED_FLOAT;

    ExactStatus status;
    u32 limit = is_float_literal ? EXACT_FRACTION_BITS : EXACT_INT_BITS;
    u32 v = exact_parse(&c->exacts, f->src + f->tokens.starts[token], limit, &status);
    if (status != EXACT_OK) {
        report(c, file, token, is_float_literal ? DIAG_FLOAT_TOO_LARGE : DIAG_TOO_LARGE, 0, 0);
        return poison;
    }
    return (Operand){ type, v };
}

static bool
int_widens(const Checker *c, u32 from, u32 to)
{
    const Types *t = &c->types;
    u8 fk = t->kinds[from];
    u8 tk = t->kinds[to];
    if ((fk != TYPE_INT && fk != TYPE_UINTPTR) ||
        (tk != TYPE_INT && tk != TYPE_UINTPTR)) {
        return false;
    }

    u32 from_bits = t->arg0[from];
    u32 to_bits = t->arg0[to];
    bool from_signed = t->arg1[from];
    bool to_signed = t->arg1[to];
    return from_signed == to_signed
             ? from_bits <= to_bits
             : !from_signed && to_signed && from_bits < to_bits;
}

static bool
widens(const Checker *c, u32 from, u32 to)
{
    const Types *t = &c->types;
    u8 fk = t->kinds[from];
    u8 tk = t->kinds[to];
    if (fk == TYPE_FLOAT && tk == TYPE_FLOAT) {
        return t->arg0[from] < t->arg0[to];
    }
    if ((fk == TYPE_INT || fk == TYPE_UINTPTR) && tk == TYPE_FLOAT) {
        return t->arg0[from] - t->arg1[from] <= (t->arg0[to] == 32 ? 24u : 53u);
    }
    return int_widens(c, from, to);
}

static u32
emit(Checker *c, u8 kind, u32 type, u32 a, u32 b)
{
    IrCode *code = c->body.code;
    if (code->count == code->cap) {
        code->cap = code->cap ? code->cap * 2 : 256;
        code->kinds = resize(code->kinds, code->cap, sizeof *code->kinds);
        code->types = resize(code->types, code->cap, sizeof *code->types);
        code->as = resize(code->as, code->cap, sizeof *code->as);
        code->bs = resize(code->bs, code->cap, sizeof *code->bs);
    }

    u32 i = code->count++;
    code->kinds[i] = kind;
    code->types[i] = type;
    code->as[i] = a;
    code->bs[i] = b;
    return i - c->body.first;
}

static Operand
emit_value(Checker *c, u8 kind, u32 type, u32 a, u32 b)
{
    return (Operand){ type, REF_VALUE | emit(c, kind, type, a, b) };
}

static u32
ir_const(Checker *c, u32 type, u64 bits)
{
    IrCode *code = c->body.code;
    if (code->nconsts == code->consts_cap) {
        code->consts_cap = code->consts_cap ? code->consts_cap * 2 : 64;
        code->consts = resize(code->consts, code->consts_cap, sizeof *code->consts);
    }
    code->consts[code->nconsts] = (IrConst){ bits, type };
    return IR_CONST | (code->nconsts++ - c->body.first_const);
}

static u32
operand_ir(Checker *c, Operand o)
{
    assert(is_value_type(o.type) && !is_untyped(o.type));
    if (!is_constant(o)) {
        return value_of(o);
    }

    u64 bits;
    if (c->types.kinds[o.type] == TYPE_FLOAT) {
        ExactStatus status;
        bits = exact_float(&c->exacts, value_of(o), c->types.arg0[o.type], &status);
        assert(status == EXACT_OK);
    } else {
        bits = exact_bits(&c->exacts, value_of(o));
    }
    return ir_const(c, o.type, bits);
}

static u32
add_slot(Checker *c, u32 name, u32 type, u32 node)
{
    IrCode *code = c->body.code;
    if (code->nslots == code->slots_cap) {
        code->slots_cap = code->slots_cap ? code->slots_cap * 2 : 16;
        code->slots = resize(code->slots, code->slots_cap, sizeof *code->slots);
    }
    code->slots[code->nslots] = (IrSlot){ name, type, node, 0 };
    return code->nslots++ - c->body.first_slot;
}

static IrSlot *
slot_at(const Checker *c, u32 slot)
{
    return &c->body.code->slots[c->body.first_slot + slot];
}

static bool
is_place(Operand o)
{
    return o.type == OPERAND_LOCAL || o.type == OPERAND_ADDRESS;
}

static u32
place_type(const Checker *c, Operand o)
{
    if (o.type == OPERAND_LOCAL) {
        return slot_at(c, o.ref)->type;
    }
    return c->types.arg0[c->body.code->types[c->body.first + value_of(o)]];
}

static u8
binding_kind(const Checker *c, u32 file, u32 slot)
{
    switch (c->files[file].nodes.kinds[slot_at(c, slot)->node]) {
        case NODE_PARAM:
        case NODE_PARAM_INIT:
        case NODE_PARAM_TYPE:
        case NODE_PARAM_TYPE_INIT: return NODE_PARAM;
        case NODE_FOR_VALUE:
        case NODE_FOR_INDEX: return NODE_FOR_VALUE;
        default: return NODE_NONE;
    }
}

static u8
place_binding(const Checker *c, u32 file, Operand o)
{
    if (o.type == OPERAND_LOCAL) {
        return binding_kind(c, file, o.ref);
    }

    const IrCode *code = c->body.code;
    u32 v = c->body.first + value_of(o);
    while (code->kinds[v] == IR_INDEX) {
        v = c->body.first + code->as[v];
    }
    return code->kinds[v] == IR_ADDR ? binding_kind(c, file, code->as[v]) : NODE_NONE;
}

static void
weigh(Checker *c, u32 slot)
{
    IrSlot *s = slot_at(c, slot);
    if (s->weight == WEIGHT_ADDRESSED) {
        return;
    }
    u32 depth = c->body.depth < 6 ? c->body.depth : 6;
    u64 w = (u64)s->weight + (1u << (3 * depth));
    s->weight = w < WEIGHT_ADDRESSED ? (u32)w : WEIGHT_ADDRESSED - 1;
}

static u32
place_address(Checker *c, Operand o)
{
    if (o.type == OPERAND_ADDRESS) {
        return value_of(o);
    }
    u32 type = slot_at(c, o.ref)->type;
    weigh(c, o.ref);
    return emit(c, IR_ADDR, pointer_type(c, type), o.ref, 0);
}

static void
copy_place(Checker *c, Operand to, Operand from)
{
    u32 type = place_type(c, to);
    u32 dst = place_address(c, to);
    u32 src = place_address(c, from);
    emit(c, IR_COPY, type, dst, src);
}

static Operand
load_local(Checker *c, u32 slot)
{
    u32 type = slot_at(c, slot)->type;
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    weigh(c, slot);
    return emit_value(c, IR_LOAD, type, slot, 0);
}

static void
store_local(Checker *c, u32 slot, Operand v)
{
    u32 type = slot_at(c, slot)->type;
    if (type != TYPE_ID_NONE && v.type != TYPE_ID_NONE) {
        weigh(c, slot);
        emit(c, IR_STORE, type, slot, operand_ir(c, v));
    }
}

static Operand
float_constant(Checker *c, u32 file, u32 token, u32 value, u32 type)
{
    ExactStatus status;
    u32 width = c->types.arg0[type];
    u64 bits = exact_float(&c->exacts, value, width, &status);
    if (status != EXACT_OK) {
        report(c, file, token, DIAG_DOES_NOT_FIT, value, type);
        return poison;
    }
    return (Operand){ type, exact_from_float(&c->exacts, bits, width) };
}

static u64
member_hash(u64 key)
{
    return (key * 0x9E3779B97F4A7C15ull) >> 32;
}

static void
grow_member_map(Checker *c, u32 cap)
{
    u64 *keys = c->member_keys;
    u32 *slots = c->member_slots;
    u32 old = keys ? c->member_mask + 1 : 0;
    c->member_keys = calloc(cap, sizeof *c->member_keys);
    c->member_slots = malloc(cap * sizeof *c->member_slots);
    c->member_mask = cap - 1;
    for (u32 i = 0; i < old; i++) {
        if (keys[i]) {
            u32 h = (u32)member_hash(keys[i]) & c->member_mask;
            while (c->member_keys[h]) {
                h = (h + 1) & c->member_mask;
            }
            c->member_keys[h] = keys[i];
            c->member_slots[h] = slots[i];
        }
    }
    free(keys);
    free(slots);
}

static void
index_members(Checker *c, u32 decl, u32 first)
{
    u32 need_count = c->member_count + c->nmembers - first;
    u32 cap = c->member_keys ? c->member_mask + 1 : 0;
    if (2 * need_count > cap) {
        u32 grown = cap ? cap : 64;
        while (2 * need_count > grown) {
            grown *= 2;
        }
        grow_member_map(c, grown);
    }
    for (u32 i = first; i < c->nmembers; i++) {
        u64 key = (u64)decl << 32 | c->members[i].name;
        u32 h = (u32)member_hash(key) & c->member_mask;
        while (c->member_keys[h]) {
            h = (h + 1) & c->member_mask;
        }
        c->member_keys[h] = key;
        c->member_slots[h] = i;
    }
    c->member_count = need_count;
}

static u32
find_member(const Checker *c, u32 decl, u32 name)
{
    if (!c->member_keys) {
        return NO_SLOT;
    }
    u64 key = (u64)decl << 32 | name;
    for (u32 h = (u32)member_hash(key) & c->member_mask; c->member_keys[h];
         h = (h + 1) & c->member_mask) {
        if (c->member_keys[h] == key) {
            return c->member_slots[h];
        }
    }
    return NO_SLOT;
}

static u32
scalar_type(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_ENUM ? (u32)c->types.arg1[type] : type;
}

static bool
signed_type(const Checker *c, u32 type)
{
    u32 t = scalar_type(c, type);
    return c->types.kinds[t] == TYPE_INT && c->types.arg1[t];
}

static u64
order_key(const Checker *c, u32 type, u64 bits)
{
    return signed_type(c, type) ? bits ^ (1ull << 63) : bits;
}

static int
compare_keys(const void *a, const void *b)
{
    u64 x = *(const u64 *)a, y = *(const u64 *)b;
    return (x > y) - (x < y);
}

static u64
list_u64(const Checker *c, u32 at)
{
    return c->lists[at] | (u64)c->lists[at + 1] << 32;
}

static void
enum_runs(Checker *c, u32 decl)
{
    u32 type = c->decls.types[decl];
    u32 first = c->decls.members[decl];
    u32 n = c->decls.member_counts[decl];
    u64 *keys = malloc((n + 1) * sizeof *keys);
    u32 count = 0;
    for (u32 i = 0; i < n; i++) {
        u32 v = c->members[first + i].value;
        if (v < MEMBER_NONE) {
            keys[count++] = order_key(c, type, exact_bits(&c->exacts, v));
        }
    }
    qsort(keys, count, sizeof *keys, compare_keys);

    u32 runs = 0;
    for (u32 i = 0; i < count; i++) {
        runs += i == 0 || keys[i] > keys[i - 1] + 1;
    }
    u32 list = reserve_list(c, 4 * runs);
    u64 flip = order_key(c, type, 0);
    u32 at = list + 1;
    for (u32 i = 0; i < count; i++) {
        if (i > 0 && keys[i] <= keys[i - 1] + 1) {
            u64 hi = keys[i] ^ flip;
            c->lists[at - 2] = (u32)hi;
            c->lists[at - 1] = (u32)(hi >> 32);
            continue;
        }
        u64 bits = keys[i] ^ flip;
        c->lists[at] = c->lists[at + 2] = (u32)bits;
        c->lists[at + 1] = c->lists[at + 3] = (u32)(bits >> 32);
        at += 4;
    }
    c->decls.values[decl] = list;
    free(keys);
}

static bool
enum_has(const Checker *c, u32 type, u64 key)
{
    u32 list = c->decls.values[c->types.arg0[type]];
    u64 flip = order_key(c, type, 0);
    u32 lo = 0, hi = c->lists[list] / 4;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        u32 at = list + 1 + 4 * mid;
        if ((list_u64(c, at + 2) ^ flip) < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < c->lists[list] / 4 && (list_u64(c, list + 1 + 4 * lo) ^ flip) <= key;
}

static u32
smallest_backing(Checker *c, u32 file, u32 lo, u32 lo_token, u32 hi, u32 hi_token)
{
    bool sign = exact_sign(&c->exacts, lo) < 0;
    for (u32 scale = 0; scale < 4; scale++) {
        u32 bits = 8u << scale;
        if (exact_fits(&c->exacts, lo, bits, sign) &&
            exact_fits(&c->exacts, hi, bits, sign)) {
            return (sign ? TYPE_ID_I8 : TYPE_ID_U8) + scale;
        }
    }
    bool low = sign && !exact_fits(&c->exacts, lo, 64, true);
    report(c, file, low ? lo_token : hi_token, DIAG_DOES_NOT_FIT, low ? lo : hi, sign ? TYPE_ID_I64 : TYPE_ID_U64);
    return TYPE_ID_NONE;
}

static NOINLINE Operand
enum_member(Checker *c, u32 file, u32 token, u32 type, u32 name)
{
    u32 decl = c->types.arg0[type];
    need(c, decl, file, token);
    u32 m = find_member(c, decl, name);
    if (m == NO_SLOT) {
        report(c, file, token, DIAG_NO_MEMBER, name, type);
        return poison;
    }
    u32 v = c->members[m].value;
    return v < MEMBER_NONE ? (Operand){ type, v } : poison;
}

static Operand
convert(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (o.type == TYPE_ID_NONE || to == TYPE_ID_NONE) {
        return poison;
    }
    if (o.type == to) {
        return o;
    }
    if (!is_value_type(o.type)) {
        if (is_place(o)) {
            u32 from = place_type(c, o);
            if (from == to) {
                return o;
            }
            report(c, file, token, DIAG_MISMATCH, from, to);
            return poison;
        }
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }

    const Types *t = &c->types;
    u8 to_kind = t->kinds[to];
    if (o.type == TYPE_ID_ENUM_LITERAL) {
        if (to_kind == TYPE_ENUM) {
            return enum_member(c, file, token, to, o.ref);
        }
        report(c, file, token, DIAG_NOT_ENUM, o.ref, to);
        return poison;
    }
    if (is_untyped(o.type)) {
        u32 v = value_of(o);
        if (to_kind == TYPE_INT || to_kind == TYPE_UINTPTR) {
            if (int_fits(c, to, v)) {
                return (Operand){ to, v };
            }
            report(c, file, token, DIAG_DOES_NOT_FIT, v, to);
            return poison;
        }
        if (to_kind == TYPE_FLOAT) {
            Operand r = float_constant(c, file, token, v, to);
            if (r.type == TYPE_ID_NONE || o.type == TYPE_ID_UNTYPED_FLOAT ||
                exact_compare(&c->exacts, r.ref, v) == 0) {
                return r;
            }
            report(c, file, token, DIAG_DOES_NOT_FIT, v, to);
            return poison;
        }
    } else if (!(o.ref & REF_RESULT) && widens(c, o.type, to)) {
        if (is_constant(o)) {
            return (Operand){ to, value_of(o) };
        }
        return emit_value(c, IR_CONVERT, to, value_of(o), 0);
    }

    report(c, file, token, DIAG_MISMATCH, o.type, to);
    return poison;
}

static bool
masked(const Checker *c, u32 value, u32 to)
{
    const IrCode *code = c->body.code;
    u32 at = c->body.first + value;
    if (code->kinds[at] != IR_AND) {
        return false;
    }

    u32 bits = c->types.arg0[to] - c->types.arg1[to];
    u32 ops[2] = { code->as[at], code->bs[at] };
    for (u32 i = 0; i < 2; i++) {
        if (ops[i] & IR_CONST) {
            int64_t mask = (int64_t)code
                               ->consts[c->body.first_const + (ops[i] & ~IR_CONST)]
                               .bits;
            if (mask >= 0 && (bits >= 63 || mask < (int64_t)1 << bits)) {
                return true;
            }
        }
    }
    return false;
}

static bool
bits_fit(u64 bits, bool from_signed, u32 to_bits, bool to_signed)
{
    if (from_signed && (int64_t)bits < 0) {
        return to_signed && (to_bits == 64 ||
                             (int64_t)bits >= -(int64_t)(1ull << (to_bits - 1)));
    }
    u32 room = to_bits - to_signed;
    return room >= 64 || bits < (1ull << room);
}

static NOINLINE Operand
to_enum(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    need(c, c->types.arg0[to], file, token);
    u32 backing = (u32)c->types.arg1[to];
    if (!backing) {
        return poison;
    }
    if (is_constant(o)) {
        u32 v = value_of(o);
        if (!int_fits(c, backing, v) ||
            !enum_has(c, to, order_key(c, to, exact_bits(&c->exacts, v)))) {
            report(c, file, token, DIAG_NOT_ENUM_VALUE, v, to);
            return poison;
        }
        return (Operand){ to, v };
    }

    u32 list = c->decls.values[c->types.arg0[to]];
    u32 words = c->lists[list] + 1;
    IrCode *code = c->body.code;
    if (code->nextra + words > code->extra_cap) {
        code->extra_cap = (code->nextra + words) * 2;
        code->extra = resize(code->extra, code->extra_cap, sizeof *code->extra);
    }
    memcpy(code->extra + code->nextra, c->lists + list, words * sizeof *code->extra);
    u32 runs = code->nextra - c->body.first_extra;
    code->nextra += words;
    return emit_value(c, IR_NARROW, to, value_of(o), runs);
}

static NOINLINE Operand
from_enum(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (is_constant(o)) {
        u32 v = value_of(o);
        if (int_fits(c, to, v)) {
            return (Operand){ to, v };
        }
        report(c, file, token, DIAG_DOES_NOT_FIT, v, to);
        return poison;
    }

    u32 backing = (u32)c->types.arg1[o.type];
    u32 list = c->decls.values[c->types.arg0[o.type]];
    u32 n = c->lists[list];
    bool from_signed = signed_type(c, o.type);
    u32 bits = c->types.arg0[to];
    bool to_signed = c->types.arg1[to];
    bool fits = n == 0 ||
                (bits_fit(list_u64(c, list + 1), from_signed, bits, to_signed) &&
                 bits_fit(list_u64(c, list + n - 1), from_signed, bits, to_signed));
    u8 kind = !fits                  ? IR_NARROW
            : widens(c, backing, to) ? IR_CONVERT
                                     : IR_TRUNCATE;
    return emit_value(c, kind, to, value_of(o), 0);
}

static ALWAYS_INLINE Operand
cast(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (o.type == TYPE_ID_NONE || to == TYPE_ID_NONE) {
        return poison;
    }
    if (o.type == to) {
        return (Operand){ to, o.ref & ~REF_RESULT };
    }

    const Types *t = &c->types;
    u8 to_kind = t->kinds[to];
    bool to_int = to_kind == TYPE_INT || to_kind == TYPE_UINTPTR;
    if (o.type == TYPE_ID_ENUM_LITERAL) {
        return convert(c, file, token, o, to);
    }
    if (to_kind == TYPE_ENUM && is_value_type(o.type) && is_int(c, o.type)) {
        return to_enum(c, file, token, o, to);
    }
    if (to_int && is_value_type(o.type) && t->kinds[o.type] == TYPE_ENUM) {
        return from_enum(c, file, token, o, to);
    }
    if (!is_value_type(o.type) || !is_number(c, o.type) ||
        (!to_int && to_kind != TYPE_FLOAT)) {
        report(c, file, token, is_value_type(o.type) ? DIAG_MISMATCH : DIAG_NOT_VALUE, o.type, to);
        return poison;
    }

    if (is_constant(o)) {
        u32 v = value_of(o);
        if (to_kind == TYPE_FLOAT) {
            return float_constant(c, file, token, v, to);
        }
        if (int_fits(c, to, v)) {
            return (Operand){ to, v };
        }
        report(c, file, token, DIAG_DOES_NOT_FIT, v, to);
        return poison;
    }

    u32 v = value_of(o);
    if (widens(c, o.type, to) || to_kind == TYPE_FLOAT) {
        return emit_value(c, IR_CONVERT, to, v, 0);
    }
    if (is_int(c, o.type) && masked(c, v, to)) {
        return emit_value(c, IR_TRUNCATE, to, v, 0);
    }
    return emit_value(c, IR_NARROW, to, v, 0);
}

static u32
peer_type(const Checker *c, u32 a, u32 b)
{
    if (a == TYPE_ID_ENUM_LITERAL || b == TYPE_ID_ENUM_LITERAL) {
        return a == TYPE_ID_ENUM_LITERAL ? b : a;
    }
    if (is_untyped(a) && is_untyped(b)) {
        return a == b ? a : TYPE_ID_UNTYPED_FLOAT;
    }
    return is_untyped(a) || widens(c, a, b) ? b : a;
}

static u32
unify(Checker *c, u32 file, u32 token, Operand *a, Operand *b)
{
    if (a->type == b->type) {
        if (a->type == TYPE_ID_ENUM_LITERAL) {
            report(c, file, token, DIAG_UNKNOWN_ENUM, a->ref, 0);
            return TYPE_ID_NONE;
        }
        return a->type;
    }

    u32 type = peer_type(c, a->type, b->type);
    if (is_untyped(type)) {
        a->type = type;
        b->type = type;
        return type;
    }
    Operand *from = type == a->type ? b : a;
    if (is_untyped(from->type) || from->type == TYPE_ID_ENUM_LITERAL ||
        (!(from->ref & REF_RESULT) && widens(c, from->type, type))) {
        *from = convert(c, file, token, *from, type);
        return from->type;
    }

    report(c, file, token, DIAG_MISMATCH, a->type, b->type);
    return TYPE_ID_NONE;
}

static bool
applies(const Checker *c, u8 cls, u32 type)
{
    switch (cls) {
        case OP_ARITH:
        case OP_COMPARE: return is_number(c, type);
        case OP_INT:
        case OP_SHIFT: return is_int(c, type);
        case OP_EQUALITY: return is_scalar(c, type);
        default: assert(false && "unknown operator class"); return false;
    }
}

static u32
wrap(Checker *c, u32 v, u32 type)
{
    Exacts *e = &c->exacts;
    u32 bits = c->types.arg0[type];
    ExactStatus status;
    u32 modulus = exact_binary(e, EXACT_SHL, EXACT_ONE, exact_int(e, bits), EXACT_INT_BITS, &status);
    u32 mask = exact_binary(e, EXACT_SUB, modulus, EXACT_ONE, EXACT_INT_BITS, &status);
    u32 r = exact_binary(e, EXACT_AND, v, mask, EXACT_INT_BITS, &status);
    if (c->types.arg1[type] && !exact_fits(e, r, bits, true)) {
        r = exact_binary(e, EXACT_SUB, r, modulus, EXACT_INT_BITS, &status);
    }
    return r;
}

static Operand
constant_result(Checker *c, u32 file, u32 token, u32 type, u32 v)
{
    if (is_untyped(type)) {
        return (Operand){ type, REF_RESULT | v };
    }
    if (c->types.kinds[type] == TYPE_FLOAT) {
        Operand r = float_constant(c, file, token, v, type);
        return (Operand){ r.type, r.type ? REF_RESULT | r.ref : 0 };
    }
    if (type != TYPE_ID_BOOL && !int_fits(c, type, v)) {
        report(c, file, token, DIAG_DOES_NOT_FIT, v, type);
        return poison;
    }
    return (Operand){ type, REF_RESULT | v };
}

static Operand
fold(Checker *c, u32 file, u32 token, u8 kind, u32 type, Operand a, Operand b)
{
    Exacts *e = &c->exacts;
    u8 cls = operator_classes[kind];
    if (cls == OP_COMPARE || cls == OP_EQUALITY) {
        int cmp = exact_compare(e, value_of(a), value_of(b));
        bool r = kind == NODE_EQ ? cmp == 0
               : kind == NODE_NE ? cmp != 0
               : kind == NODE_LT ? cmp < 0
               : kind == NODE_LE ? cmp <= 0
               : kind == NODE_GT ? cmp > 0
                                 : cmp >= 0;
        return (Operand){ TYPE_ID_BOOL, r ? EXACT_ONE : EXACT_ZERO };
    }

    bool floating = is_float(c, type);
    ExactOp op = kind == NODE_DIV && floating ? EXACT_QUO : (ExactOp)exact_ops[kind];
    ExactStatus status;
    u32 r = exact_binary(e, op, value_of(a), value_of(b), floating ? EXACT_FRACTION_BITS : EXACT_INT_BITS, &status);
    if (status == EXACT_DIVIDE_BY_ZERO) {
        report(c, file, token, DIAG_DIVIDE_BY_ZERO, 0, 0);
        return poison;
    }
    if (status == EXACT_TOO_LARGE) {
        report(c, file, token, floating ? DIAG_FLOAT_TOO_LARGE : DIAG_TOO_LARGE, 0, 0);
        return poison;
    }
    if ((kind == NODE_ADD_WRAP || kind == NODE_SUB_WRAP || kind == NODE_MUL_WRAP) &&
        !is_untyped(type)) {
        r = wrap(c, r, type);
    }
    return constant_result(c, file, token, type, r);
}

static NOINLINE bool
array_marker(const Checker *c, u32 m)
{
    u32 file = c->decls.files[c->body.decl];
    return c->files[file].tokens.kinds[c->stack_tokens[m]] == TK_LBRACK;
}

static u32
element_of(const Checker *c, u32 type, u32 depth)
{
    for (; depth > 0; depth--) {
        if (c->types.kinds[type] != TYPE_ARRAY) {
            return TYPE_ID_NONE;
        }
        type = c->types.arg0[type];
    }
    return type;
}

static ALWAYS_INLINE u32
expected_type(const Checker *c, u32 own)
{
    u32 top = c->sp - own;
    u32 depth = 0;
    for (u32 m = top; m-- > 0;) {
        Operand o = c->stack[m];
        if (o.type < OPERAND_MARKER) {
            continue;
        }

        u8 kind = (u8)(o.type - OPERAND_MARKER);
        if (kind == NODE_ANON_LITERAL_START && array_marker(c, m)) {
            depth++;
            top = m;
            continue;
        }
        if (m == 0 || (kind != NODE_VALUE && kind != NODE_CALL_ARGS &&
                       kind != NODE_LITERAL_START)) {
            return TYPE_ID_NONE;
        }

        Operand below = c->stack[m - 1];
        u32 type = TYPE_ID_NONE;
        if (kind == NODE_LITERAL_START) {
            type = below.type == TYPE_ID_TYPE && array_marker(c, m) ? below.ref
                                                                    : TYPE_ID_NONE;
        } else if (below.type == TYPE_ID_TYPE) {
            type = below.ref;
        } else if (kind == NODE_VALUE && is_place(below)) {
            type = place_type(c, below);
        } else if (kind == NODE_CALL_ARGS && below.type == OPERAND_FN) {
            u32 fn = c->decls.types[below.ref];
            const u32 *params = c->lists + c->types.arg0[fn];
            u32 index = top - m - 1;
            type = index < params[0] ? params[1 + index] : TYPE_ID_NONE;
        }
        return depth ? element_of(c, type, depth) : type;
    }
    return TYPE_ID_NONE;
}

static Operand
shift(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    if (!is_int(c, a.type) || !is_int(c, b.type)) {
        report(c, file, token, DIAG_BAD_OPERATOR, is_int(c, a.type) ? b.type : a.type, 0);
        return poison;
    }

    bool untyped = a.type == TYPE_ID_UNTYPED_INT;
    if (is_constant(b)) {
        u32 n = value_of(b);
        bool negative = exact_sign(&c->exacts, n) < 0;
        if (negative || (!untyped && !exact_fits(&c->exacts, n, 32, false)) ||
            (!untyped && exact_bits(&c->exacts, n) >= c->types.arg0[a.type])) {
            report(c, file, token, DIAG_BAD_SHIFT, n, 0);
            return poison;
        }
    }

    if (is_constant(a) && is_constant(b)) {
        ExactStatus status;
        u32 r = exact_binary(
            &c->exacts,
            (ExactOp)exact_ops[kind],
            value_of(a),
            value_of(b),
            EXACT_INT_BITS,
            &status
        );
        if (status != EXACT_OK) {
            report(c, file, token, DIAG_TOO_LARGE, 0, 0);
            return poison;
        }
        return constant_result(c, file, token, a.type, r);
    }

    if (untyped) {
        u32 t = expected_type(c, 0);
        a = convert(c, file, token, a, t && is_int(c, t) ? t : TYPE_ID_I64);
        if (a.type == TYPE_ID_NONE) {
            return poison;
        }
    }
    if (b.type == TYPE_ID_UNTYPED_INT) {
        b = (Operand){ a.type, b.ref };
    }
    u32 x = operand_ir(c, a);
    u32 y = operand_ir(c, b);
    return (Operand){ a.type,
                      REF_VALUE | REF_RESULT |
                          emit(c, binary_irs[kind], a.type, x, y) };
}

static Operand
binary(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    if (a.type == TYPE_ID_NONE || b.type == TYPE_ID_NONE) {
        return poison;
    }
    if (!is_value_type(a.type) || !is_value_type(b.type)) {
        Operand p = is_place(a) ? a : b;
        if (is_place(p)) {
            report(c, file, token, DIAG_BAD_OPERATOR, place_type(c, p), 0);
        } else {
            report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        }
        return poison;
    }

    u8 cls = operator_classes[kind];
    if (cls == OP_SHIFT) {
        return shift(c, file, token, kind, a, b);
    }

    u32 type = unify(c, file, token, &a, &b);
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    if (!applies(c, cls, type)) {
        report(c, file, token, DIAG_BAD_OPERATOR, type, 0);
        return poison;
    }
    if (is_constant(a) && is_constant(b)) {
        return fold(c, file, token, kind, type, a, b);
    }

    u8 ir = binary_irs[kind];
    if ((ir == IR_DIV || ir == IR_REM) && is_int(c, type) && is_constant(b) &&
        exact_sign(&c->exacts, value_of(b)) == 0) {
        report(c, file, token, DIAG_DIVIDE_BY_ZERO, 0, 0);
        return poison;
    }

    u32 x = operand_ir(c, a);
    u32 y = operand_ir(c, b);
    u32 result = cls >= OP_COMPARE ? TYPE_ID_BOOL : type;
    return (Operand){ result, REF_VALUE | REF_RESULT | emit(c, ir, type, x, y) };
}

static Operand
unary(Checker *c, u32 file, u32 token, u8 kind, Operand a)
{
    if (a.type == TYPE_ID_NONE) {
        return poison;
    }
    if (is_place(a)) {
        report(c, file, token, DIAG_BAD_OPERATOR, place_type(c, a), 0);
        return poison;
    }
    if (!is_value_type(a.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }

    if (kind == NODE_NOT) {
        if (a.type != TYPE_ID_BOOL) {
            report(c, file, token, DIAG_NOT_BOOL, a.type, 0);
            return poison;
        }
        if (is_constant(a)) {
            return (Operand){ TYPE_ID_BOOL,
                              exact_sign(&c->exacts, value_of(a)) ? EXACT_ZERO
                                                                  : EXACT_ONE };
        }
        return emit_value(c, IR_NOT, TYPE_ID_BOOL, value_of(a), 0);
    }

    bool needs_int = kind != NODE_NEG;
    if (needs_int ? !is_int(c, a.type) : !is_number(c, a.type)) {
        report(c, file, token, DIAG_BAD_OPERATOR, a.type, 0);
        return poison;
    }

    if (!is_constant(a)) {
        u8 ir = kind == NODE_NEG      ? IR_NEG
              : kind == NODE_NEG_WRAP ? IR_NEG_WRAP
                                      : IR_BIT_NOT;
        return (Operand){ a.type,
                          REF_VALUE | REF_RESULT |
                              emit(c, ir, a.type, value_of(a), 0) };
    }

    Exacts *e = &c->exacts;
    ExactStatus status;
    bool floating = is_float(c, a.type);
    u32 limit = floating ? EXACT_FRACTION_BITS : EXACT_INT_BITS;
    u32 r = kind == NODE_BIT_NOT
              ? exact_binary(e, EXACT_XOR, value_of(a), EXACT_MINUS_ONE, limit, &status)
              : exact_binary(e, EXACT_SUB, EXACT_ZERO, value_of(a), limit, &status);
    if (kind != NODE_NEG && !is_untyped(a.type)) {
        r = wrap(c, r, a.type);
    }
    return constant_result(c, file, token, a.type, r);
}

static u32
type_operand(Checker *c, u32 file, u32 token, Operand o)
{
    if (o.type == TYPE_ID_TYPE) {
        return o.ref;
    }
    if (o.type != TYPE_ID_NONE) {
        report(c, file, token, DIAG_NOT_TYPE, 0, 0);
    }
    return TYPE_ID_NONE;
}

static u64
array_length(Checker *c, u32 file, u32 token, Operand n, bool *ok)
{
    *ok = false;
    if (n.type == TYPE_ID_NONE) {
        return 0;
    }
    if (!is_int(c, n.type) || !is_constant(n)) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        return 0;
    }

    u32 v = value_of(n);
    if (exact_sign(&c->exacts, v) < 0) {
        report(c, file, token, DIAG_NEGATIVE_LENGTH, 0, 0);
        return 0;
    }
    if (!exact_fits(&c->exacts, v, 64, true)) {
        report(c, file, token, DIAG_DOES_NOT_FIT, v, TYPE_ID_I64);
        return 0;
    }

    *ok = true;
    return exact_bits(&c->exacts, v);
}

static void
claim_contained(Checker *c, u32 type, u32 file, u32 token)
{
    const Types *t = &c->types;
    while (t->kinds[type] == TYPE_ARRAY || t->kinds[type] == TYPE_OPTIONAL ||
           t->kinds[type] == TYPE_ERROR_UNION) {
        type = t->arg0[type];
    }
    if (t->kinds[type] == TYPE_STRUCT || t->kinds[type] == TYPE_UNION) {
        need(c, t->arg0[type], file, token);
    }
}

static void
push_member(Checker *c, u32 file, u32 node, u32 type, u32 value)
{
    if (c->nscratch == c->scratch_cap) {
        c->scratch_cap *= 2;
        c->scratch = resize(c->scratch, c->scratch_cap, sizeof *c->scratch);
    }

    c->scratch[c->nscratch] = (Member){ node_name(c, file, node), type, node, value };
    push(c, c->files[file].nodes.tokens[node], OPERAND_MEMBER, c->nscratch++);
}

static u32
member_value(Operand o)
{
    return o.type == TYPE_ID_NONE ? MEMBER_NONE : value_of(o);
}

static u32
default_value(Checker *c, u32 file, u32 token, Operand v, u32 type)
{
    if (v.type == TYPE_ID_UNDEFINED) {
        return MEMBER_UNDEFINED;
    }
    return member_value(convert(c, file, token, v, type));
}

static u32
inferred_type(Checker *c, u32 file, u32 token, Operand v)
{
    assert(v.type != TYPE_ID_UNDEFINED);
    if (v.type == TYPE_ID_UNTYPED_INT) {
        return TYPE_ID_I64;
    }
    if (v.type == TYPE_ID_UNTYPED_FLOAT) {
        return TYPE_ID_F32;
    }
    if (v.type == TYPE_ID_TYPE) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        return TYPE_ID_NONE;
    }
    if (v.type == TYPE_ID_ENUM_LITERAL) {
        report(c, file, token, DIAG_UNKNOWN_ENUM, v.ref, 0);
        return TYPE_ID_NONE;
    }
    return v.type;
}

static ALWAYS_INLINE u32
value_type(Checker *c, u32 file, u32 token, Operand v)
{
    if (v.type == TYPE_ID_NONE) {
        return TYPE_ID_NONE;
    }
    if (!is_value_type(v.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return TYPE_ID_NONE;
    }
    if (v.type == TYPE_ID_ENUM_LITERAL) {
        report(c, file, token, DIAG_UNKNOWN_ENUM, v.ref, 0);
        return TYPE_ID_NONE;
    }
    return v.type == TYPE_ID_UNTYPED_INT   ? TYPE_ID_I64
         : v.type == TYPE_ID_UNTYPED_FLOAT ? TYPE_ID_F32
                                           : v.type;
}

static u32
collect_members(Checker *c, u32 file, u32 from, u32 kind)
{
    u32 first = c->nmembers;
    reserve_members(c, c->sp - from);
    for (u32 i = from; i < c->sp; i++) {
        if (c->stack[i].type != OPERAND_MEMBER) {
            continue;
        }

        Member m = c->scratch[c->stack[i].ref];
        u32 token = c->files[file].nodes.tokens[m.node];
        u32 builtin = m.name - (NAME_LAST_KEYWORD + 1);
        if (kind == DIAG_DUPLICATE_PARAM && builtin < NBUILTIN_TYPES) {
            report(c, file, token, DIAG_RESERVED, 0, 0);
        } else if (c->bound[m.name] && (kind != DIAG_DUPLICATE_PARAM || m.name != NAME_DISCARD)) {
            report(c, file, token, kind, 0, c->bound[m.name] - 1);
            continue;
        }

        c->bound[m.name] = m.node + 1;
        c->members[c->nmembers++] = m;
    }

    for (u32 i = first; i < c->nmembers; i++) {
        c->bound[c->members[i].name] = 0;
    }
    return first;
}

static void
close_struct(Checker *c, u32 decl, u32 file)
{
    u32 m = list_start(c, NODE_STRUCT_START);
    u32 first = collect_members(c, file, m + 1, DIAG_DUPLICATE_MEMBER);
    c->decls.members[decl] = first;
    c->decls.member_counts[decl] = c->nmembers - first;
    c->nscratch = c->stack[m].ref;
    c->sp = m;
}

static void
close_enum(Checker *c, u32 decl, u32 file)
{
    u32 m = list_start(c, NODE_ENUM_START);
    u32 backing = m + 1 < c->sp && c->stack[m + 1].type == OPERAND_BACKING
                    ? c->stack[m + 1].ref
                    : TYPE_ID_NONE;
    c->types.arg1[c->decls.types[decl]] = backing;

    u32 next = EXACT_ZERO;
    bool known = true;
    bool any = false;
    u32 lo = EXACT_ZERO, hi = EXACT_ZERO, lo_token = 0, hi_token = 0;
    for (u32 i = m + 1; i < c->sp; i++) {
        if (c->stack[i].type != OPERAND_MEMBER) {
            continue;
        }

        Member *mem = &c->scratch[c->stack[i].ref];
        u32 token = c->files[file].nodes.tokens[mem->node];
        if (c->files[file].nodes.kinds[mem->node] == NODE_ENUM_VALUE) {
            Operand v = { mem->type, mem->value };
            if (backing) {
                v = convert(c, file, token, v, backing);
            }
            known = v.type != TYPE_ID_NONE;
            next = known ? value_of(v) : EXACT_ZERO;
        } else if (known && backing && !int_fits(c, backing, next)) {
            report(c, file, token, DIAG_DOES_NOT_FIT, next, backing);
            known = false;
        }

        mem->type = TYPE_ID_NONE;
        mem->value = known ? next : MEMBER_NONE;
        if (known) {
            if (!any || exact_compare(&c->exacts, next, lo) < 0) {
                lo = next;
                lo_token = token;
            }
            if (!any || exact_compare(&c->exacts, next, hi) > 0) {
                hi = next;
                hi_token = token;
            }
            any = true;
            ExactStatus status;
            next = exact_binary(&c->exacts, EXACT_ADD, next, EXACT_ONE, EXACT_INT_BITS, &status);
            known = status == EXACT_OK;
        }
    }

    if (!backing) {
        backing = any ? smallest_backing(c, file, lo, lo_token, hi, hi_token)
                      : TYPE_ID_U8;
        c->types.arg1[c->decls.types[decl]] = backing;
    }

    u32 first = collect_members(c, file, m + 1, DIAG_DUPLICATE_MEMBER);
    c->decls.members[decl] = first;
    c->decls.member_counts[decl] = c->nmembers - first;
    c->nscratch = c->stack[m].ref;
    c->sp = m;
    index_members(c, decl, first);
    if (backing) {
        enum_runs(c, decl);
    } else {
        c->decls.values[decl] = reserve_list(c, 0);
    }
}

static void
match_tag(Checker *c, u32 decl, u32 file, u32 tag)
{
    const Decls *d = &c->decls;
    u32 tag_decl = c->types.arg0[tag];
    u32 first = d->members[decl];
    u32 count = d->member_counts[decl];
    u32 tag_first = d->members[tag_decl];
    u32 tag_count = d->member_counts[tag_decl];

    for (u32 i = 0; i < tag_count; i++) {
        c->bound[c->members[tag_first + i].name] = 1;
    }
    for (u32 i = 0; i < count; i++) {
        Member m = c->members[first + i];
        if (c->bound[m.name] == 0) {
            report(c, file, c->files[file].nodes.tokens[m.node], DIAG_TAG_MISMATCH, 0, tag);
        } else {
            c->bound[m.name] = 2;
        }
    }

    u32 token = c->files[file].nodes.tokens[d->roots[decl]];
    for (u32 i = 0; i < tag_count; i++) {
        u32 name = c->members[tag_first + i].name;
        if (c->bound[name] == 1) {
            report(c, file, token, DIAG_TAG_MISSING, name, tag);
        }
        c->bound[name] = 0;
    }
}

static void
close_union(Checker *c, u32 decl, u32 file)
{
    u32 m = list_start(c, NODE_UNION_START);
    u32 tag = m + 1 < c->sp && c->stack[m + 1].type == OPERAND_BACKING
                ? c->stack[m + 1].ref
                : TYPE_ID_NONE;
    c->types.arg1[c->decls.types[decl]] = tag;

    u32 first = collect_members(c, file, m + 1, DIAG_DUPLICATE_MEMBER);
    c->decls.members[decl] = first;
    c->decls.member_counts[decl] = c->nmembers - first;
    c->nscratch = c->stack[m].ref;
    c->sp = m;

    if (tag && c->types.kinds[tag] == TYPE_ENUM) {
        match_tag(c, decl, file, tag);
    }
}

static u32
close_fn_type(Checker *c)
{
    u32 m = list_start(c, NODE_FN_START);
    u32 ret = TYPE_ID_VOID;
    u32 nparams = c->sp - m - 1;
    if (nparams > 0 && c->stack[c->sp - 1].type == OPERAND_RETURN) {
        ret = c->stack[c->sp - 1].ref;
        nparams--;
    }

    u32 list = reserve_list(c, nparams);
    for (u32 i = 0; i < nparams; i++) {
        Operand o = c->stack[m + 1 + i];
        c->lists[list + 1 + i] = o.type == OPERAND_MEMBER ? c->scratch[o.ref].type
                               : o.type == TYPE_ID_TYPE ? o.ref
                                                        : TYPE_ID_NONE;
    }

    c->nscratch = c->stack[m].ref;
    c->sp = m;
    return fn_type(c, list, ret);
}

static void
unsupported(Checker *c, u32 file, u32 node)
{
    const ParsedFile *f = &c->files[file];
    u8 kind = f->nodes.kinds[node];
    report(c, file, f->nodes.tokens[node], DIAG_UNSUPPORTED, 0, 0);

    if (list_starts[kind] != NODE_NONE) {
        u32 m = list_start(c, list_starts[kind]);
        c->nscratch = c->stack[m].ref;
        c->sp = m;
    }
    assert(c->sp >= node_arity[kind]);
    c->sp -= node_arity[kind];
    push(c, f->nodes.tokens[node], TYPE_ID_NONE, 0);
}

static void
set_const(Checker *c, u32 decl, Operand v)
{
    assert(v.type != TYPE_ID_UNDEFINED);
    c->decls.types[decl] = v.type;
    c->decls.values[decl] = value_of(v);
}

static void
bind(Checker *c, u32 file, u32 node, u32 slot)
{
    u32 name = node_name(c, file, node);
    if (name == NAME_DISCARD) {
        return;
    }

    u64 previous = c->bindings[name];
    if ((u32)(previous >> 32) == c->body.owner) {
        u32 token = c->files[file].nodes.tokens[node];
        report(c, file, token, DIAG_SHADOW, 0, slot_at(c, (u32)previous)->node);
        return;
    }

    if (c->nscope == c->scope_cap) {
        c->scope_cap = c->scope_cap ? c->scope_cap * 2 : 64;
        c->scope = resize(c->scope, c->scope_cap, sizeof *c->scope);
    }
    c->scope[c->nscope++] = (Binding){ name, previous };
    c->bindings[name] = (u64)c->body.owner << 32 | slot;
}

static void
unbind_to(Checker *c, u32 n)
{
    while (c->nscope > n) {
        Binding b = c->scope[--c->nscope];
        c->bindings[b.name] = b.previous;
    }
}

static u32
local_type(Checker *c, u32 file, u32 token, u32 type)
{
    if (is_aggregate(c, type) && !is_storable(c, type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return TYPE_ID_NONE;
    }
    return type;
}

static u32
declare_local(Checker *c, u32 file, u32 node, u32 type, Operand v)
{
    u32 name = node_name(c, file, node);
    u32 slot;
    if (v.type == OPERAND_LOCAL && slot_at(c, v.ref)->name == 0) {
        slot = v.ref;
        slot_at(c, slot)->name = name;
        slot_at(c, slot)->node = node;
    } else {
        slot = add_slot(c, name, type, node);
        if (is_place(v)) {
            copy_place(c, (Operand){ OPERAND_LOCAL, slot }, v);
        } else if (v.type != TYPE_ID_UNDEFINED) {
            store_local(c, slot, v);
        }
    }

    u32 token = c->files[file].nodes.tokens[node];
    if (name - (NAME_LAST_KEYWORD + 1) < NBUILTIN_TYPES) {
        report(c, file, token, DIAG_RESERVED, 0, 0);
        return slot;
    }
    if (name == NAME_DISCARD) {
        report(c, file, token, DIAG_DISCARD, 0, 0);
        return slot;
    }
    bind(c, file, node, slot);
    return slot;
}

static Loop *
innermost_loop(Checker *c)
{
    for (u32 k = c->nloops; k-- > c->body.loops;) {
        if (!c->loops[k].block) {
            return &c->loops[k];
        }
    }
    return NULL;
}

static Loop *
lookup_label(const Checker *c, u32 file, u32 token)
{
    u32 id = c->files[file].tokens.ids[token];
    for (u32 k = c->nloops; k-- > c->body.loops;) {
        if (c->loops[k].label == id) {
            return &c->loops[k];
        }
    }
    return NULL;
}

static Loop *
find_label(Checker *c, u32 file, u32 token)
{
    Loop *target = lookup_label(c, file, token);
    if (target == NULL) {
        report(c, file, token, DIAG_UNKNOWN_LABEL, 0, 0);
    }
    return target;
}

static u32
pending_label(const Checker *c, u32 below)
{
    if (below == 0) {
        return 0;
    }
    Operand o = c->stack[below - 1];
    bool label = o.type == OPERAND_MARKER + NODE_LABEL_START ||
                 o.type == OPERAND_MARKER + NODE_VALUE_LABEL_START;
    return label ? o.ref : 0;
}

static void
push_loop(Checker *c, Loop loop)
{
    if (c->nloops == c->loops_cap) {
        c->loops_cap = c->loops_cap ? c->loops_cap * 2 : 16;
        c->loops = resize(c->loops, c->loops_cap, sizeof *c->loops);
    }
    loop.before = c->body.reachable;
    c->loops[c->nloops++] = loop;
}

static u32
condition(Checker *c, u32 file, u32 token, Operand cond)
{
    if (cond.type != TYPE_ID_NONE && cond.type != TYPE_ID_BOOL) {
        report(c, file, token, is_value_type(cond.type) ? DIAG_NOT_BOOL : DIAG_NOT_VALUE, cond.type, 0);
    }
    if (cond.type != TYPE_ID_BOOL) {
        return ir_const(c, TYPE_ID_BOOL, 0);
    }
    return operand_ir(c, cond);
}

static Operand
call_fn(Checker *c, u32 file, u32 token, u32 decl, u32 first_arg, u32 nargs)
{
    const Decls *d = &c->decls;
    u32 fn = d->types[decl];
    u32 ret = (u32)c->types.arg1[fn];
    if (ret == TYPE_ID_INFERRED || ret == TYPE_ID_NONE) {
        return poison;
    }
    if (is_aggregate(c, ret)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }

    u32 list = c->types.arg0[fn];
    u32 n = c->lists[list];
    u32 defaults = 0;
    while (defaults < n &&
           c->members[d->members[decl] + n - 1 - defaults].value < MEMBER_NONE) {
        defaults++;
    }
    if (nargs > n || nargs < n - defaults) {
        report(c, file, token, DIAG_ARG_COUNT, n, nargs);
        return poison;
    }

    IrCode *code = c->body.code;
    if (code->nextra + n + 1 > code->extra_cap) {
        code->extra_cap = (code->nextra + n + 1) * 2;
        code->extra = resize(code->extra, code->extra_cap, sizeof *code->extra);
    }
    u32 extra = code->nextra;
    code->nextra += n + 1;
    code->extra[extra] = n;
    u32 relative = extra - c->body.first_extra;

    bool ok = true;
    for (u32 i = 0; i < n; i++) {
        u32 type = c->lists[list + 1 + i];
        if (is_aggregate(c, type)) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return poison;
        }
        Operand v = i < nargs
                      ? convert(c, file, c->stack_tokens[first_arg + i], c->stack[first_arg + i], type)
                      : (Operand){ type, c->members[d->members[decl] + i].value };
        ok &= v.type != TYPE_ID_NONE;
        c->body.code->extra[extra + 1 + i] = ok ? operand_ir(c, v) : 0;
    }
    if (!ok) {
        return poison;
    }

    u32 i = emit(c, IR_CALL, ret, decl, relative);
    c->body.calls++;
    return ret == TYPE_ID_VOID ? (Operand){ TYPE_ID_VOID, 0 }
                               : (Operand){ ret, REF_VALUE | i };
}

static void
call(Checker *c, u32 file)
{
    u32 m = list_start(c, NODE_CALL_ARGS);
    Operand callee = c->stack[m - 1];
    u32 callee_token = c->stack_tokens[m - 1];
    u32 nargs = c->sp - m - 1;
    for (u32 i = m + 1; i < c->sp; i++) {
        if (c->stack[i].type == TYPE_ID_NONE) {
            callee = poison;
        }
    }

    Operand r = poison;
    if (callee.type == TYPE_ID_TYPE) {
        if (nargs == 1) {
            r = cast(c, file, c->stack_tokens[m + 1], c->stack[m + 1], callee.ref);
        } else {
            report(c, file, callee_token, DIAG_ARG_COUNT, 1, nargs);
        }
    } else if (callee.type == OPERAND_FN) {
        r = call_fn(c, file, callee_token, callee.ref, m + 1, nargs);
    } else if (callee.type != TYPE_ID_NONE) {
        report(c, file, callee_token, DIAG_NOT_CALLABLE, 0, 0);
    }

    c->sp = m - 1;
    push_operand(c, callee_token, r);
}

static void
logical_left(Checker *c, u32 file, u32 token, u8 kind)
{
    Operand a = c->stack[c->sp - 1];
    u32 a_token = c->stack_tokens[c->sp - 1];
    u32 mode = SHORT_FOLD;
    if (a.type != TYPE_ID_NONE && a.type != TYPE_ID_BOOL) {
        report(c, file, a_token, is_value_type(a.type) ? DIAG_NOT_BOOL : DIAG_NOT_VALUE, a.type, 0);
        c->stack[c->sp - 1] = poison;
    } else if (a.type == TYPE_ID_BOOL && c->body.code) {
        if (is_constant(a)) {
            bool decides = (exact_sign(&c->exacts, value_of(a)) == 0) ==
                           (kind == NODE_AND_LEFT);
            mode = decides ? SHORT_SKIP : SHORT_PASS;
            if (decides) {
                emit(c, IR_IF, 0, ir_const(c, TYPE_ID_BOOL, 0), 0);
            }
        } else {
            emit(c, IR_IF, TYPE_ID_BOOL, value_of(a), 0);
            if (kind == NODE_OR_LEFT) {
                emit(c, IR_ELSE, TYPE_ID_BOOL, ir_const(c, TYPE_ID_BOOL, 1), 0);
            }
            mode = SHORT_VALUE;
            c->stack[c->sp - 1] = (Operand){ OPERAND_STMT, 0 };
        }
    }
    push(c, token, OPERAND_MARKER + kind, mode);
}

static Operand
logical(Checker *c, u32 file, u8 kind)
{
    u32 b_token = c->stack_tokens[c->sp - 1];
    Operand b = pop(c);
    u32 mode = pop(c).ref;
    Operand a = pop(c);
    if (b.type != TYPE_ID_NONE && b.type != TYPE_ID_BOOL) {
        report(c, file, b_token, is_value_type(b.type) ? DIAG_NOT_BOOL : DIAG_NOT_VALUE, b.type, 0);
        b = poison;
    }

    if (mode == SHORT_PASS) {
        return b;
    }
    if (mode == SHORT_SKIP) {
        if (b.type != TYPE_ID_NONE && !is_constant(b)) {
            emit(c, IR_DROP, 0, value_of(b), 0);
        }
        emit(c, IR_END, 0, 0, 0);
        return a;
    }
    if (mode == SHORT_VALUE) {
        u32 v = b.type == TYPE_ID_BOOL ? operand_ir(c, b)
                                       : ir_const(c, TYPE_ID_BOOL, 0);
        if (kind == NODE_AND) {
            emit(c, IR_ELSE, TYPE_ID_BOOL, v, 0);
            v = ir_const(c, TYPE_ID_BOOL, 0);
        }
        return emit_value(c, IR_END, TYPE_ID_BOOL, v, 0);
    }
    if (a.type == TYPE_ID_NONE || b.type == TYPE_ID_NONE) {
        return poison;
    }

    assert(is_constant(a) && is_constant(b));
    bool x = exact_sign(&c->exacts, value_of(a)) != 0;
    bool y = exact_sign(&c->exacts, value_of(b)) != 0;
    bool r = kind == NODE_AND ? x && y : x || y;
    return (Operand){ TYPE_ID_BOOL, r ? EXACT_ONE : EXACT_ZERO };
}

static void
assign_at(Checker *c, u32 file, u32 node, u32 token, u8 kind, Operand target, Operand v, u32 v_token)
{
    u32 type = place_type(c, target);
    u32 address = value_of(target);
    u32 hidden = 0;
    if (kind != NODE_ASSIGN) {
        u8 op = assign_ops[kind];
        if (op == NODE_NONE) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return;
        }
        u32 pointer = pointer_type(c, type);
        hidden = add_slot(c, 0, pointer, node);
        store_local(c, hidden, (Operand){ pointer, target.ref });
        Operand old = emit_value(c, IR_LOAD_AT, type, value_of(load_local(c, hidden)), 0);
        v = binary(c, file, token, op, old, v);
        v_token = token;
    }

    Operand converted = convert(c, file, v_token, v, type);
    if (converted.type == TYPE_ID_NONE) {
        return;
    }
    if (kind != NODE_ASSIGN) {
        address = value_of(load_local(c, hidden));
    }
    emit(c, IR_STORE_AT, type, address, operand_ir(c, converted));
}

static void
assign(Checker *c, u32 file, u32 node, u32 token, u8 kind)
{
    u32 v_token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    pop(c);
    u32 target_token = c->stack_tokens[c->sp - 1];
    Operand target = pop(c);
    push(c, token, OPERAND_STMT, 0);

    if (target.type == OPERAND_DISCARD) {
        if (kind != NODE_ASSIGN) {
            report(c, file, target_token, DIAG_DISCARD, 0, 0);
        } else if (is_place(v)) {
            if (v.type == OPERAND_ADDRESS) {
                emit(c, IR_DROP, 0, value_of(v), 0);
            }
        } else if (v.type != TYPE_ID_NONE && !is_value_type(v.type)) {
            report(c, file, v_token, DIAG_NOT_VALUE, 0, 0);
        } else if (v.type != TYPE_ID_NONE && !is_constant(v)) {
            emit(c, IR_DROP, 0, value_of(v), 0);
        }
        return;
    }
    if (is_place(target)) {
        u8 binding = place_binding(c, file, target);
        if (binding != NODE_NONE) {
            report(
                c,
                file,
                target_token,
                binding == NODE_PARAM ? DIAG_ASSIGN_PARAM : DIAG_ASSIGN_LOOP_VARIABLE,
                0,
                0
            );
            return;
        }
    }
    if (is_place(target) && is_aggregate(c, place_type(c, target))) {
        u32 type = place_type(c, target);
        if (kind != NODE_ASSIGN) {
            report(c, file, token, DIAG_BAD_OPERATOR, type, 0);
            return;
        }
        Operand from = convert(c, file, v_token, v, type);
        if (from.type != TYPE_ID_NONE) {
            copy_place(c, target, from);
        }
        return;
    }
    if (target.type == OPERAND_ADDRESS) {
        assign_at(c, file, node, token, kind, target, v, v_token);
        return;
    }
    if (target.type != OPERAND_LOCAL) {
        if (target.type != TYPE_ID_NONE) {
            report(c, file, target_token, DIAG_NOT_ASSIGNABLE, 0, 0);
        }
        return;
    }

    u32 slot = target.ref;
    u32 type = slot_at(c, slot)->type;
    if (kind != NODE_ASSIGN) {
        u8 op = assign_ops[kind];
        if (op == NODE_NONE) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return;
        }
        v = binary(c, file, token, op, load_local(c, slot), v);
        v_token = token;
    }
    store_local(c, slot, convert(c, file, v_token, v, type));
}

static void
expression_statement(Checker *c, u32 file, u32 token)
{
    u32 v_token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    push(c, token, OPERAND_STMT, 0);
    if (v.type != TYPE_ID_NONE && v.type != TYPE_ID_VOID) {
        report(c, file, v_token, DIAG_UNUSED, 0, 0);
    }
}

static Operand
address_of(Checker *c, u32 file, u32 token, Operand target)
{
    if (target.type == TYPE_ID_NONE) {
        return poison;
    }
    if (!is_place(target)) {
        report(c, file, token, DIAG_NOT_ADDRESSABLE, 0, 0);
        return poison;
    }

    u8 binding = place_binding(c, file, target);
    if (binding != NODE_NONE) {
        report(c, file, token, binding == NODE_PARAM ? DIAG_ADDRESS_PARAM : DIAG_ADDRESS_LOOP_VARIABLE, 0, 0);
        return poison;
    }
    if (target.type == OPERAND_ADDRESS) {
        return (Operand){ pointer_type(c, place_type(c, target)), target.ref };
    }

    u32 slot = target.ref;
    u32 type = slot_at(c, slot)->type;
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    slot_at(c, slot)->weight = WEIGHT_ADDRESSED;
    return emit_value(c, IR_ADDR, pointer_type(c, type), slot, 0);
}

static Operand
deref(Checker *c, u32 file, u32 token, Operand p, bool place)
{
    if (p.type == TYPE_ID_NONE) {
        return poison;
    }
    if (is_place(p)) {
        report(c, file, token, DIAG_NOT_POINTER, place_type(c, p), 0);
        return poison;
    }
    if (!is_value_type(p.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (c->types.kinds[p.type] != TYPE_POINTER) {
        report(c, file, token, DIAG_NOT_POINTER, p.type, 0);
        return poison;
    }

    u32 type = c->types.arg0[p.type];
    if (is_aggregate(c, type) && is_storable(c, type)) {
        return (Operand){ OPERAND_ADDRESS, p.ref };
    }
    if (!is_scalar(c, type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    if (place) {
        return (Operand){ OPERAND_ADDRESS, p.ref };
    }
    return emit_value(c, IR_LOAD_AT, type, value_of(p), 0);
}

static Operand
index_place(Checker *c, u32 file, u32 token, Operand base, Operand index, u32 index_token, bool place)
{
    if (base.type == TYPE_ID_NONE || index.type == TYPE_ID_NONE) {
        return poison;
    }

    u32 type = is_place(base) ? place_type(c, base) : base.type;
    if (!is_value_type(type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (!is_aggregate(c, type)) {
        report(c, file, token, DIAG_NOT_INDEXABLE, type, 0);
        return poison;
    }

    u32 index_type = is_place(index) ? place_type(c, index) : index.type;
    if (!is_value_type(index_type)) {
        report(c, file, index_token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (!is_int(c, index_type)) {
        report(c, file, index_token, DIAG_NOT_INDEX, index_type, 0);
        return poison;
    }

    u32 at = value_of(index);
    if (is_constant(index)) {
        if (!exact_fits(&c->exacts, at, 64, false) ||
            exact_bits(&c->exacts, at) >= c->types.arg1[type]) {
            report(c, file, index_token, DIAG_INDEX_RANGE, at, type);
            return poison;
        }
        at = ir_const(c, TYPE_ID_I64, exact_bits(&c->exacts, at));
    }

    u32 elem = c->types.arg0[type];
    u32 pointer = pointer_type(c, elem);
    u32 address = emit(c, IR_INDEX, pointer, place_address(c, base), at);
    if (place || is_aggregate(c, elem)) {
        return (Operand){ OPERAND_ADDRESS, REF_VALUE | address };
    }
    return emit_value(c, IR_LOAD_AT, elem, address, 0);
}

static Operand
array_field(Checker *c, u32 file, u32 token, Operand o)
{
    u32 type = place_type(c, o);
    if (o.type == OPERAND_ADDRESS) {
        emit(c, IR_DROP, 0, value_of(o), 0);
    }

    u32 name = c->files[file].tokens.ids[token];
    if (name != NAME_LEN) {
        report(c, file, token, DIAG_NO_MEMBER, name, type);
        return poison;
    }
    return (Operand){ TYPE_ID_I64,
                      exact_int(&c->exacts, (int64_t)c->types.arg1[type]) };
}

static void
array_literal(Checker *c, u32 file, u32 node, u8 kind)
{
    bool typed = kind == NODE_ARRAY_LITERAL;
    u32 m = list_start(c, typed ? NODE_LITERAL_START : NODE_ANON_LITERAL_START);
    u32 n = c->sp - m - 1;
    u32 base = typed ? m - 1 : m;
    u32 token = c->stack_tokens[base];

    u32 type = TYPE_ID_NONE;
    if (typed) {
        u32 elem = type_operand(c, file, token, c->stack[base]);
        if (elem && is_storable(c, elem)) {
            type = intern_type(c, TYPE_ARRAY, elem, n);
        } else if (elem) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        }
    } else {
        type = expected_type(c, c->sp - m);
        if (type == TYPE_ID_NONE) {
            report(c, file, token, DIAG_LITERAL_TYPE, 0, 0);
        } else if (!is_aggregate(c, type)) {
            report(c, file, token, DIAG_NOT_ARRAY, 0, type);
            type = TYPE_ID_NONE;
        } else if (c->types.arg1[type] != n) {
            report(c, file, token, DIAG_LITERAL_COUNT, n, type);
            type = TYPE_ID_NONE;
        } else if (!is_storable(c, type)) {
            type = TYPE_ID_NONE;
        }
    }

    bool ok = type != TYPE_ID_NONE;
    Operand array = { OPERAND_LOCAL, ok ? add_slot(c, 0, type, node) : 0 };
    u32 elem = c->types.arg0[type];
    for (u32 k = 0; type && k < n; k++) {
        Operand x = convert(c, file, c->stack_tokens[m + 1 + k], c->stack[m + 1 + k], elem);
        if (x.type == TYPE_ID_NONE) {
            ok = false;
            continue;
        }

        u32 index = ir_const(c, TYPE_ID_I64, k);
        u32 address = emit(c, IR_INDEX, pointer_type(c, elem), place_address(c, array), index);
        if (is_place(x)) {
            emit(c, IR_COPY, elem, address, place_address(c, x));
        } else {
            emit(c, IR_STORE_AT, elem, address, operand_ir(c, x));
        }
    }

    c->sp = base;
    push_operand(c, token, ok ? array : poison);
}

static bool
returns_nothing(const Checker *c, u32 type)
{
    return type == TYPE_ID_VOID || (c->types.kinds[type] == TYPE_ERROR_UNION &&
                                    c->types.arg0[type] == TYPE_ID_VOID);
}

static bool
may_widen(const Checker *c, Operand v)
{
    return !is_constant(v) && !(v.ref & REF_RESULT) &&
           (widens(c, v.type, TYPE_ID_I64) || widens(c, v.type, TYPE_ID_F64));
}

static void
add_peer(Checker *c, Peers *peers, Break b)
{
    if (c->nbreaks == c->breaks_cap) {
        c->breaks_cap = c->breaks_cap ? c->breaks_cap * 2 : 16;
        c->breaks = resize(c->breaks, c->breaks_cap, sizeof *c->breaks);
    }
    c->breaks[c->nbreaks++] = b;
    peers->result |= is_untyped(b.value.type) ? 0 : b.value.ref & REF_RESULT;
    if (b.value.type != TYPE_ID_VOID) {
        peers->type = peers->type == TYPE_ID_NONE
                        ? b.value.type
                        : peer_type(c, peers->type, b.value.type);
    }
    if (peers->last != 0) {
        c->breaks[peers->last - 1].next = c->nbreaks;
    } else {
        peers->first = c->nbreaks;
    }
    peers->last = c->nbreaks;
}

static void
exit_to_peers(Checker *c, u32 file, u32 token, Peers *peers, u8 kind, u32 loop, Operand v)
{
    if (v.type == TYPE_ID_NONE) {
        return;
    }
    if (!is_value_type(v.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return;
    }

    if (may_widen(c, v)) {
        emit(c, IR_NOP, 0, 0, 0);
    }
    u32 value = is_constant(v) ? 0 : value_of(v);
    u32 at = kind == IR_RET ? emit(c, IR_RET, 0, value, 0)
                            : emit(c, IR_EXIT, 0, loop, value);
    add_peer(c, peers, (Break){ v, token, at, 0 });
}

static void
return_value(Checker *c, u32 file, u32 token, bool has_value)
{
    u32 v_token = has_value ? c->stack_tokens[c->sp - 1] : token;
    Operand v = has_value ? pop(c) : poison;
    push(c, token, OPERAND_STMT, 0);
    c->body.reachable = false;

    u32 ret = c->body.ret;
    if (ret == TYPE_ID_INFERRED && has_value) {
        c->body.returns_value = true;
        exit_to_peers(c, file, v_token, &c->body.returns, IR_RET, 0, v);
        return;
    }
    if (ret == TYPE_ID_INFERRED) {
        u32 at = emit(c, IR_RET, TYPE_ID_VOID, 0, 0);
        add_peer(c, &c->body.returns, (Break){ { TYPE_ID_VOID, 0 }, token, at, 0 });
        return;
    }
    if (!has_value) {
        if (returns_nothing(c, ret)) {
            emit(c, IR_RET, ret, 0, 0);
        } else {
            report(c, file, token, DIAG_RETURN_EMPTY, 0, ret);
        }
        return;
    }
    if (returns_nothing(c, ret)) {
        if (v.type != TYPE_ID_NONE) {
            report(c, file, token, DIAG_RETURN_VALUE, 0, 0);
        }
        return;
    }

    v = convert(c, file, v_token, v, ret);
    if (v.type != TYPE_ID_NONE) {
        emit(c, IR_RET, ret, operand_ir(c, v), 0);
    }
}

static void
give(Checker *c, u32 file, u32 token, Loop *target, Operand v)
{
    target->broken = true;
    if (target->peer) {
        exit_to_peers(c, file, token, &target->peers, IR_EXIT, target->loop, v);
        return;
    }
    Operand x = convert(c, file, token, v, target->type);
    if (is_place(x)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return;
    }
    if (x.type != TYPE_ID_NONE) {
        emit(c, IR_EXIT, x.type, target->loop, operand_ir(c, x));
    }
}

static void
jump(Checker *c, u32 file, u32 token, u8 kind)
{
    bool is_break = kind <= NODE_BREAK_VALUE;
    u32 form = kind - (is_break ? NODE_BREAK : NODE_CONTINUE);
    u32 v_token = form == 2 ? c->stack_tokens[c->sp - 1] : token;
    Operand v = form == 2 ? pop(c) : poison;
    push(c, token, OPERAND_STMT, 0);
    Loop *target = form ? find_label(c, file, token) : innermost_loop(c);
    c->body.reachable = false;
    if (target == NULL) {
        if (form == 0) {
            report(c, file, token, DIAG_OUTSIDE_LOOP, 0, 0);
        }
        return;
    }
    if (!is_break) {
        if (form == 2 || target->dispatch) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        } else if (target->block) {
            report(c, file, token, DIAG_CONTINUE_BLOCK, 0, 0);
        } else if (target->next == target->loop) {
            emit(c, IR_REPEAT, 0, target->loop, 0);
        } else {
            emit(c, IR_EXIT, 0, target->next, 0);
        }
        return;
    }

    target->broken = true;
    if (form < 2) {
        if (target->value) {
            report(c, file, token, DIAG_BREAK_EMPTY, 0, 0);
        } else {
            emit(c, IR_EXIT, 0, target->loop, 0);
        }
        return;
    }
    if (!target->value) {
        report(c, file, token, DIAG_BREAK_VALUE, 0, 0);
        return;
    }
    give(c, file, v_token, target, v);
}

static u32
result_type(Checker *c, u32 file, u32 node, u32 own, u8 start, u8 end_kind, u8 item)
{
    const u8 *kinds = c->files[file].nodes.kinds;
    u32 depth = 0;
    for (u32 i = node + 1;; i++) {
        depth += kinds[i] == start;
        bool end = kinds[i] == end_kind || kinds[i] == item;
        if (end && depth-- == 0) {
            if (kinds[i] == item) {
                u32 m = c->sp - own;
                while (c->stack[--m].type < OPERAND_MARKER) {
                }
                u32 marker = c->stack[m].type - OPERAND_MARKER;
                bool call = marker == NODE_CALL_ARGS &&
                            c->stack[m - 1].type == OPERAND_FN;
                bool literal = marker == NODE_LITERAL_START ||
                               marker == NODE_ANON_LITERAL_START;
                return call || literal ? expected_type(c, own) : TYPE_ID_NONE;
            }
            u8 next = kinds[i + 1];
            bool direct = c->sp > own && c->stack[c->sp - own - 1].type ==
                                             OPERAND_MARKER + NODE_VALUE;
            if (next == NODE_RETURN_VALUE) {
                return c->body.ret == TYPE_ID_INFERRED ? TYPE_ID_NONE : c->body.ret;
            }
            if (next == NODE_BREAK_VALUE) {
                const Loop *to = lookup_label(c, file, c->files[file].nodes.tokens[i + 1]);
                return to && to->value && !to->peer ? to->type : TYPE_ID_NONE;
            }
            if (next == NODE_CASE) {
                const Switch *s = &c->switches[c->nswitches - 1];
                const Loop *to = s->value ? &c->loops[s->loop] : NULL;
                return to && !to->peer ? to->type : TYPE_ID_NONE;
            }
            if (direct && (next == NODE_VAR_TYPE_INIT || next == NODE_ASSIGN)) {
                return expected_type(c, own);
            }
            return TYPE_ID_NONE;
        }
    }
}

static void
open_labeled_block(Checker *c, u32 file, u32 node)
{
    Operand label = c->stack[c->sp - 1];
    bool value = label.type == OPERAND_MARKER + NODE_VALUE_LABEL_START;
    u32 expected = value
                     ? result_type(c, file, node, 1, NODE_VALUE_LABEL_START, NODE_LABELED_VALUE, NODE_LABELED_ITEM)
                     : TYPE_ID_NONE;
    Loop target = {
        .loop = emit(c, IR_BLOCK, 0, 0, 0),
        .next = NO_SLOT,
        .slot = NO_SLOT,
        .block = true,
        .value = value,
        .label = label.ref,
        .type = expected,
        .marker = c->sp,
        .peer = value && expected == TYPE_ID_NONE,
    };
    push_loop(c, target);
}

static u32
settle_peers(Checker *c, u32 file, Peers peers, u32 *operands)
{
    u32 type = peers.type;
    if (type == TYPE_ID_NONE) {
        return TYPE_ID_NONE;
    }
    if (type == TYPE_ID_ENUM_LITERAL) {
        Break b = c->breaks[peers.first - 1];
        report(c, file, b.token, DIAG_UNKNOWN_ENUM, b.value.ref, 0);
        return TYPE_ID_NONE;
    }
    type = type == TYPE_ID_UNTYPED_INT   ? TYPE_ID_I64
         : type == TYPE_ID_UNTYPED_FLOAT ? TYPE_ID_F32
                                         : type;

    IrCode *code = c->body.code;
    for (u32 k = peers.first; k != 0; k = c->breaks[k - 1].next) {
        Break b = c->breaks[k - 1];
        u32 at = c->body.first + b.exit;
        if (b.value.type == TYPE_ID_VOID) {
            report(c, file, b.token, DIAG_RETURN_EMPTY, 0, type);
            continue;
        }
        if (is_constant(b.value)) {
            Operand x = convert(c, file, b.token, b.value, type);
            if (x.type == TYPE_ID_NONE) {
                code->kinds[at] = IR_NOP;
                continue;
            }
            operands[at] = operand_ir(c, x);
        } else if (b.value.type != type) {
            if (!may_widen(c, b.value) || !widens(c, b.value.type, type)) {
                report(c, file, b.token, DIAG_MISMATCH, b.value.type, type);
                code->kinds[at] = IR_NOP;
                continue;
            }
            code->kinds[at - 1] = IR_CONVERT;
            code->types[at - 1] = type;
            code->as[at - 1] = value_of(b.value);
            operands[at] = b.exit - 1;
        }
        code->types[at] = type;
    }
    return type;
}

static Operand
close_labeled_block(Checker *c, u32 file, u32 token)
{
    Loop target = c->loops[--c->nloops];
    bool reachable = c->body.reachable;
    c->body.reachable = target.before && (reachable || target.broken);
    if (!target.value) {
        emit(c, IR_END, 0, 0, 0);
        return (Operand){ OPERAND_STMT, 0 };
    }
    if (reachable) {
        report(c, file, token, DIAG_BLOCK_END, 0, 0);
    }
    c->body.reachable = target.before && target.broken;
    if (target.peer) {
        target.type = settle_peers(c, file, target.peers, c->body.code->bs);
    }
    if (target.type == TYPE_ID_NONE) {
        emit(c, IR_END, 0, 0, 0);
        return poison;
    }
    u32 i = emit(c, IR_END, target.type, 0, 0);
    return (Operand){ target.type, REF_VALUE | target.peers.result | i };
}

static Switch *
top_switch(Checker *c)
{
    return &c->switches[c->nswitches - 1];
}

static NOINLINE void
begin_switch(Checker *c, u32 file, u32 node, u8 kind, u32 token)
{
    bool value = kind == NODE_VALUE_SWITCH_START;
    Switch s = {
        .at = NO_SLOT,
        .token = token,
        .first_case = c->ncases,
        .default_arm = NO_SLOT,
        .label = value ? 0 : pending_label(c, c->sp),
        .loop = NO_SLOT,
        .value = value,
    };
    if (value) {
        u32 expected = result_type(c, file, node, 0, NODE_VALUE_SWITCH_START, NODE_VALUE_SWITCH, NODE_SWITCH_ITEM);
        s.loop = c->nloops;
        push_loop(
            c,
            (Loop){ .loop = emit(c, IR_BLOCK, 0, 0, 0),
                    .next = NO_SLOT,
                    .slot = NO_SLOT,
                    .block = true,
                    .value = true,
                    .type = expected,
                    .marker = NO_SLOT,
                    .peer = expected == TYPE_ID_NONE }
        );
    }
    if (c->nswitches == c->switches_cap) {
        c->switches_cap = c->switches_cap ? c->switches_cap * 2 : 16;
        c->switches = resize(c->switches, c->switches_cap, sizeof *c->switches);
    }
    c->switches[c->nswitches++] = s;
    push(c, token, OPERAND_MARKER + kind, 0);
}

static NOINLINE void
open_switch(Checker *c, u32 file)
{
    Switch *s = top_switch(c);
    u32 token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    u32 type = value_type(c, file, token, v);
    u8 k = c->types.kinds[type];
    if (type && k != TYPE_INT && k != TYPE_UINTPTR && k != TYPE_ENUM &&
        type != TYPE_ID_BOOL) {
        report(c, file, token, DIAG_SWITCH_TYPE, type, 0);
        type = TYPE_ID_NONE;
    }
    v = type ? convert(c, file, token, v, type) : poison;
    s->type = v.type;
    s->before = c->body.reachable;
    s->at = s->type
              ? emit(c, IR_SWITCH, s->type, operand_ir(c, v), 0)
              : emit(c, IR_SWITCH, TYPE_ID_BOOL, ir_const(c, TYPE_ID_BOOL, 0), 0);
    if (s->label) {
        push_loop(
            c,
            (Loop){ .loop = s->at,
                    .next = NO_SLOT,
                    .slot = NO_SLOT,
                    .block = true,
                    .dispatch = true,
                    .label = s->label,
                    .marker = NO_SLOT }
        );
    }
}

static bool
in_case_labels(const Checker *c)
{
    u32 m = c->sp;
    while (m > 0 && c->stack[--m].type < OPERAND_MARKER) {
    }
    return c->stack[m].type == OPERAND_MARKER + NODE_CASE_START;
}

static void
add_case(Checker *c, u64 lo, u64 hi, u32 token)
{
    if (c->ncases == c->cases_cap) {
        c->cases_cap *= 2;
        c->cases = resize(c->cases, c->cases_cap, sizeof *c->cases);
    }
    c->cases[c->ncases++] = (CaseRange){ lo, hi, top_switch(c)->arms, token };
}

static bool
case_value(Checker *c, u32 file, u32 token, Operand o, u64 *key)
{
    Switch *s = top_switch(c);
    if (s->type == TYPE_ID_NONE || o.type == TYPE_ID_NONE) {
        s->skipped = true;
        return false;
    }
    Operand x = convert(c, file, token, o, s->type);
    if (x.type != TYPE_ID_NONE && !is_constant(x)) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        x = poison;
    }
    if (x.type == TYPE_ID_NONE) {
        s->skipped = true;
        return false;
    }
    *key = order_key(c, s->type, exact_bits(&c->exacts, value_of(x)));
    return true;
}

static NOINLINE void
case_range(Checker *c, u32 file, u32 token, bool inclusive)
{
    u32 hi_token = c->stack_tokens[c->sp - 1];
    Operand hi = pop(c);
    u32 lo_token = c->stack_tokens[c->sp - 1];
    Operand lo = pop(c);
    push(c, token, OPERAND_RANGE, 0);

    Switch *s = top_switch(c);
    u8 k = c->types.kinds[s->type];
    if (s->type && k != TYPE_INT && k != TYPE_UINTPTR) {
        report(c, file, token, DIAG_NOT_INTEGER, 0, 0);
        s->skipped = true;
        return;
    }
    u64 a = 0, b = 0;
    bool ok = case_value(c, file, lo_token, lo, &a);
    ok &= case_value(c, file, hi_token, hi, &b);
    if (!ok) {
        return;
    }
    if (inclusive ? b < a : b <= a) {
        report(c, file, token, DIAG_EMPTY_RANGE, 0, 0);
        s->skipped = true;
        return;
    }
    add_case(c, a, inclusive ? b : b - 1, token);
}

static NOINLINE void
case_labels(Checker *c, u32 file)
{
    Switch *s = top_switch(c);
    u32 m = list_start(c, NODE_CASE_START);
    for (u32 k = m + 1; k < c->sp; k++) {
        Operand o = c->stack[k];
        u32 token = c->stack_tokens[k];
        if (o.type == OPERAND_RANGE) {
            continue;
        }
        if (o.type == OPERAND_DISCARD) {
            if (s->default_arm != NO_SLOT) {
                report(c, file, token, DIAG_CASE_TWICE, 0, s->default_token);
            } else {
                s->default_arm = s->arms;
                s->default_token = token;
            }
            continue;
        }
        u64 key;
        if (case_value(c, file, token, o, &key)) {
            add_case(c, key, key, token);
        }
    }
    c->sp = m + 1;
}

static NOINLINE void
close_arm(Checker *c, u32 file, u32 token)
{
    Switch *s = top_switch(c);
    u32 m = list_start(c, NODE_CASE_BODY);
    unbind_to(c, c->stack[m].ref);
    bool has_item = c->sp > m + 1;
    Operand v = has_item ? c->stack[c->sp - 1] : (Operand){ OPERAND_STMT, 0 };
    u32 v_token = has_item ? c->stack_tokens[c->sp - 1] : token;
    if (!s->value) {
        s->reaches |= c->body.reachable;
    } else if (v.type == OPERAND_STMT) {
        if (c->body.reachable) {
            report(c, file, token, DIAG_ARM_END, 0, 0);
        }
    } else {
        give(c, file, v_token, &c->loops[s->loop], v);
    }
    c->body.reachable = false;
    c->sp = m - 1;
}

static int
compare_cases(const void *a, const void *b)
{
    const CaseRange *x = a, *y = b;
    if (x->lo != y->lo) {
        return x->lo < y->lo ? -1 : 1;
    }
    return (x->token > y->token) - (x->token < y->token);
}

static u32
exact_from_key(Checker *c, u32 type, u64 key)
{
    u64 bits = key ^ order_key(c, type, 0);
    if (signed_type(c, type) || bits <= INT64_MAX) {
        return exact_int(&c->exacts, (int64_t)bits);
    }
    ExactStatus status;
    u32 half = exact_int(&c->exacts, (int64_t)(bits >> 1));
    u32 twice = exact_binary(&c->exacts, EXACT_ADD, half, half, EXACT_INT_BITS, &status);
    return exact_binary(
        &c->exacts,
        EXACT_ADD,
        twice,
        exact_int(&c->exacts, (int64_t)(bits & 1)),
        EXACT_INT_BITS,
        &status
    );
}

static u64
first_missing(const CaseRange *cases, u32 n, u32 *j, u64 lo, u64 hi, bool *missing)
{
    u64 next = lo;
    for (;;) {
        while (*j < n && cases[*j].hi < next) {
            (*j)++;
        }
        if (*j == n || cases[*j].lo > next) {
            *missing = true;
            return next;
        }
        if (cases[*j].hi >= hi) {
            *missing = false;
            return 0;
        }
        next = cases[*j].hi + 1;
    }
}

static NOINLINE void
report_missing(Checker *c, u32 file, Switch *s, const CaseRange *cases, u32 n)
{
    u32 type = s->type;
    u32 j = 0;
    bool missing = false;
    if (c->types.kinds[type] == TYPE_ENUM) {
        u32 list = c->decls.values[c->types.arg0[type]];
        u64 flip = order_key(c, type, 0);
        for (u32 r = 0; r < c->lists[list] / 4 && !missing; r++) {
            u32 at = list + 1 + 4 * r;
            u64 key = first_missing(cases, n, &j, list_u64(c, at) ^ flip, list_u64(c, at + 2) ^ flip, &missing);
            if (!missing) {
                continue;
            }
            u32 decl = c->types.arg0[type];
            u32 first = c->decls.members[decl];
            for (u32 i = 0; i < c->decls.member_counts[decl]; i++) {
                Member m = c->members[first + i];
                if (m.value < MEMBER_NONE &&
                    order_key(c, type, exact_bits(&c->exacts, m.value)) == key) {
                    report(c, file, s->token, DIAG_MISSING_MEMBER, m.name, 0);
                    break;
                }
            }
        }
        return;
    }

    u32 bits = type == TYPE_ID_BOOL ? 1 : c->types.arg0[type];
    bool sign = signed_type(c, type);
    u64 lo = sign ? (1ull << 63) - (1ull << (bits - 1)) : 0;
    u64 hi = sign       ? (1ull << 63) + ((1ull << (bits - 1)) - 1)
           : bits == 64 ? UINT64_MAX
                        : (1ull << bits) - 1;
    u64 key = first_missing(cases, n, &j, lo, hi, &missing);
    if (missing) {
        report(c, file, s->token, DIAG_MISSING_CASE, exact_from_key(c, type, key), type);
    }
}

static NOINLINE void
finish_cases(Checker *c, u32 file)
{
    Switch *s = top_switch(c);
    CaseRange *cases = c->cases + s->first_case;
    u32 n = c->ncases - s->first_case;
    qsort(cases, n, sizeof *cases, compare_cases);

    u32 cover = 0;
    for (u32 k = 1; k < n; k++) {
        if (cases[k].lo <= cases[cover].hi) {
            bool later = cases[k].token > cases[cover].token;
            report(
                c,
                file,
                later ? cases[k].token : cases[cover].token,
                DIAG_CASE_TWICE,
                0,
                later ? cases[cover].token : cases[k].token
            );
        }
        if (cases[k].hi > cases[cover].hi) {
            cover = k;
        }
    }
    if (s->type && !s->skipped && s->default_arm == NO_SLOT) {
        report_missing(c, file, s, cases, n);
    }

    IrCode *code = c->body.code;
    u32 words = 2 + 5 * n;
    if (code->nextra + words > code->extra_cap) {
        code->extra_cap = (code->nextra + words) * 2;
        code->extra = resize(code->extra, code->extra_cap, sizeof *code->extra);
    }
    u32 *x = code->extra + code->nextra;
    code->bs[c->body.first + s->at] = code->nextra - c->body.first_extra;
    code->nextra += words;
    x[0] = n;
    x[1] = s->default_arm;
    u64 flip = s->type ? order_key(c, s->type, 0) : 0;
    for (u32 k = 0; k < n; k++) {
        u64 lo = cases[k].lo ^ flip, hi = cases[k].hi ^ flip;
        u32 *e = x + 2 + 5 * k;
        e[0] = (u32)lo;
        e[1] = (u32)(lo >> 32);
        e[2] = (u32)hi;
        e[3] = (u32)(hi >> 32);
        e[4] = cases[k].arm;
    }
}

static NOINLINE Operand
close_switch(Checker *c, u32 file, u8 kind)
{
    if (top_switch(c)->at == NO_SLOT) {
        open_switch(c, file);
    }
    emit(c, IR_END, 0, 0, 0);
    finish_cases(c, file);
    c->sp = list_start(c, kind == NODE_SWITCH ? NODE_SWITCH_START : NODE_VALUE_SWITCH_START);

    Switch s = c->switches[--c->nswitches];
    c->ncases = s.first_case;
    bool reaches = s.reaches;
    if (s.label) {
        reaches |= c->loops[--c->nloops].broken;
    }
    if (!s.value) {
        c->body.reachable = s.before && reaches;
        return (Operand){ OPERAND_STMT, 0 };
    }

    Loop target = c->loops[--c->nloops];
    c->body.reachable = target.before && target.broken;
    if (target.peer) {
        target.type = settle_peers(c, file, target.peers, c->body.code->bs);
    }
    if (target.type == TYPE_ID_NONE) {
        emit(c, IR_END, 0, 0, 0);
        return poison;
    }
    u32 i = emit(c, IR_END, target.type, 0, 0);
    return (Operand){ target.type, REF_VALUE | target.peers.result | i };
}

static void
close_if(Checker *c, u32 token)
{
    u32 m = list_start(c, NODE_IF_START);
    u32 state = c->stack[m].ref;
    bool any = (state & IF_ANY) || c->body.reachable ||
               (!(state & IF_ELSE) && (state & IF_BEFORE));
    for (u32 n = state / IF_OPENED; n > 0; n--) {
        emit(c, IR_END, 0, 0, 0);
    }
    c->body.reachable = any;
    c->sp = m;
    push(c, token, OPERAND_STMT, 0);
}

static void
close_while(Checker *c, u32 token)
{
    u32 m = c->sp - 4;
    assert(c->stack[m].type == OPERAND_MARKER + NODE_WHILE_START);
    Loop loop = c->loops[--c->nloops];
    emit(c, IR_REPEAT, 0, loop.loop, 0);
    emit(c, IR_END, 0, 0, 0);
    emit(c, IR_END, 0, 0, 0);
    c->body.depth--;

    bool forever = c->stack[m + 2].ref;
    c->body.reachable = loop.before && (!forever || loop.broken);
    c->sp = m;
    push(c, token, OPERAND_STMT, 0);
}

static void
for_range(Checker *c, u32 file, u32 node, u32 token)
{
    u32 end_token = c->stack_tokens[c->sp - 1];
    Operand end = pop(c);
    Operand start = pop(c);
    Operand bad = start.type >= OPERAND_MEMBER ? start : end;
    if (bad.type >= OPERAND_MEMBER) {
        if (is_place(bad)) {
            report(c, file, token, DIAG_BAD_OPERATOR, place_type(c, bad), 0);
        } else {
            report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        }
        push(c, end_token, TYPE_ID_NONE, 0);
        return;
    }
    if (start.type == TYPE_ID_UNTYPED_INT && end.type == TYPE_ID_UNTYPED_INT) {
        start = convert(c, file, token, start, TYPE_ID_I64);
        end = convert(c, file, end_token, end, TYPE_ID_I64);
    }

    u32 type = start.type && end.type ? unify(c, file, token, &start, &end)
                                      : TYPE_ID_NONE;
    if (type && !is_int(c, type)) {
        report(c, file, token, DIAG_BAD_OPERATOR, type, 0);
        type = TYPE_ID_NONE;
    }
    if (type == TYPE_ID_NONE) {
        push(c, end_token, TYPE_ID_NONE, 0);
        return;
    }

    u32 slot = add_slot(c, 0, type, node);
    store_local(c, slot, start);
    store_local(c, add_slot(c, 0, type, node), end);
    push(c, token, OPERAND_RANGE, slot);
}

static void
bind_loop_variable(Checker *c, u32 file, u32 node, u32 slot)
{
    if (node_name(c, file, node) - (NAME_LAST_KEYWORD + 1) < NBUILTIN_TYPES) {
        report(c, file, c->files[file].nodes.tokens[node], DIAG_RESERVED, 0, 0);
        return;
    }
    bind(c, file, node, slot);
}

static void
for_array(Checker *c, u32 file, u32 node, u32 label, Operand array, bool indexed)
{
    u32 type = place_type(c, array);
    u32 elem = c->types.arg0[type];
    u32 base = NO_SLOT;
    if (array.type == OPERAND_ADDRESS) {
        u32 pointer = pointer_type(c, type);
        base = add_slot(c, 0, pointer, node);
        store_local(c, base, (Operand){ pointer, array.ref });
    }

    u32 index = add_slot(c, 0, TYPE_ID_I64, node);
    store_local(c, index, (Operand){ TYPE_ID_I64, EXACT_ZERO });
    u32 name = node_name(c, file, node);
    u32 value = add_slot(c, name, elem, node);
    bind_loop_variable(c, file, node, value);
    if (indexed) {
        slot_at(c, index)->name = node_name(c, file, node + 1);
        slot_at(c, index)->node = node + 1;
        bind_loop_variable(c, file, node + 1, index);
    }

    u32 loop = emit(c, IR_LOOP, 0, 0, 0);
    c->body.depth++;
    u32 i = value_of(load_local(c, index));
    u32 n = ir_const(c, TYPE_ID_I64, c->types.arg1[type]);
    emit(c, IR_IF, 0, emit(c, IR_LT, TYPE_ID_I64, i, n), 0);
    push_loop(c, (Loop){ loop, emit(c, IR_BLOCK, 0, 0, 0), index, .label = label });
    if (name == NAME_DISCARD) {
        return;
    }

    Operand place = base == NO_SLOT
                      ? array
                      : (Operand){ OPERAND_ADDRESS, load_local(c, base).ref };
    u32 address = place_address(c, place);
    u32 at = emit(c, IR_INDEX, pointer_type(c, elem), address, value_of(load_local(c, index)));
    if (is_aggregate(c, elem)) {
        u32 to = place_address(c, (Operand){ OPERAND_LOCAL, value });
        emit(c, IR_COPY, elem, to, at);
    } else {
        store_local(c, value, emit_value(c, IR_LOAD_AT, elem, at, 0));
    }
}

static void
for_value(Checker *c, u32 file, u32 node, u32 token, bool inclusive)
{
    u32 m = list_start(c, NODE_FOR_START);
    u32 label = pending_label(c, m);
    Operand range = pop(c);
    push(c, token, OPERAND_STMT, 0);
    const ParsedFile *f = &c->files[file];
    bool indexed = f->nodes.kinds[node + 1] == NODE_FOR_INDEX;
    if (is_place(range) && is_aggregate(c, place_type(c, range))) {
        for_array(c, file, node, label, range, indexed);
        return;
    }
    if (indexed) {
        report(c, file, f->nodes.tokens[node + 1], DIAG_UNSUPPORTED, 0, 0);
    }
    if (range.type != OPERAND_RANGE) {
        if (range.type != TYPE_ID_NONE) {
            report(c, file, c->files[file].nodes.tokens[node - 1], DIAG_UNSUPPORTED, 0, 0);
        }
        u32 loop = emit(c, IR_LOOP, 0, 0, 0);
        c->body.depth++;
        emit(c, IR_IF, 0, ir_const(c, TYPE_ID_BOOL, 0), 0);
        push_loop(c, (Loop){ loop, emit(c, IR_BLOCK, 0, 0, 0), NO_SLOT, .label = label });
        return;
    }

    u32 slot = range.ref;
    IrSlot *s = slot_at(c, slot);
    s->name = node_name(c, file, node);
    s->node = node;
    u32 type = s->type;
    bind_loop_variable(c, file, node, slot);

    u32 loop = emit(c, IR_LOOP, 0, 0, 0);
    c->body.depth++;
    u32 i = value_of(load_local(c, slot));
    u32 n = value_of(load_local(c, slot + 1));
    u32 test = emit(c, inclusive ? IR_LE : IR_LT, type, i, n);
    emit(c, IR_IF, 0, test, 0);
    push_loop(c, (Loop){ loop, emit(c, IR_BLOCK, 0, 0, 0), slot, inclusive, .label = label });
}

static void
close_for(Checker *c, u32 token)
{
    u32 m = list_start(c, NODE_FOR_START);
    Loop loop = c->loops[--c->nloops];
    emit(c, IR_END, 0, 0, 0);
    if (loop.slot != NO_SLOT) {
        u32 type = slot_at(c, loop.slot)->type;
        if (loop.inclusive) {
            u32 i = value_of(load_local(c, loop.slot));
            u32 n = value_of(load_local(c, loop.slot + 1));
            emit(c, IR_IF, 0, emit(c, IR_EQ, type, i, n), 0);
            emit(c, IR_EXIT, 0, loop.loop, 0);
            emit(c, IR_END, 0, 0, 0);
        }
        u32 i = value_of(load_local(c, loop.slot));
        u32 next = emit(c, IR_ADD_WRAP, type, i, ir_const(c, type, 1));
        weigh(c, loop.slot);
        emit(c, IR_STORE, type, loop.slot, next);
        emit(c, IR_REPEAT, 0, loop.loop, 0);
    }
    emit(c, IR_END, 0, 0, 0);
    emit(c, IR_END, 0, 0, 0);
    c->body.depth--;

    unbind_to(c, c->stack[m].ref);
    c->body.reachable = loop.before;
    c->sp = m;
    push(c, token, OPERAND_STMT, 0);
}

static u32
literal_end(const u8 *kinds, u32 i)
{
    u32 depth = 0;
    for (u32 j = i + 1;; j++) {
        u8 k = kinds[j];
        if (k == NODE_FN_START) {
            depth++;
        } else if (k == NODE_FN || k == NODE_FN_LITERAL || k == NODE_FN_TYPE) {
            if (depth == 0) {
                return j;
            }
            depth--;
        }
    }
}

static void
body_marker(Checker *c, u32 file, u32 node, u8 kind, u32 token)
{
    switch (kind) {
        case NODE_BLOCK_START:
            if (pending_label(c, c->sp)) {
                open_labeled_block(c, file, node);
            }
            push(c, token, OPERAND_MARKER + kind, c->nscope);
            return;
        case NODE_FOR_START:
            push(c, token, OPERAND_MARKER + kind, c->nscope);
            return;
        case NODE_IF_START:
            push(c, token, OPERAND_MARKER + kind, c->body.reachable ? IF_BEFORE : 0);
            return;
        case NODE_IF_THEN: {
            u32 cond_token = c->stack_tokens[c->sp - 1];
            emit(c, IR_IF, 0, condition(c, file, cond_token, pop(c)), 0);
            push(c, cond_token, OPERAND_STMT, 0);
            c->stack[list_start(c, NODE_IF_START)].ref += IF_OPENED;
            break;
        }
        case NODE_ELSE_IF:
        case NODE_ELSE: {
            Operand *start = &c->stack[list_start(c, NODE_IF_START)];
            start->ref |= c->body.reachable ? IF_ANY : 0;
            start->ref |= kind == NODE_ELSE ? IF_ELSE : 0;
            c->body.reachable = start->ref & IF_BEFORE;
            emit(c, IR_ELSE, 0, 0, 0);
            break;
        }
        case NODE_WHILE_START: {
            u32 loop = emit(c, IR_LOOP, 0, 0, 0);
            c->body.depth++;
            push_loop(c, (Loop){ loop, loop, NO_SLOT, .label = pending_label(c, c->sp) });
            break;
        }
        case NODE_LABEL_START:
        case NODE_VALUE_LABEL_START:
            push(c, token, OPERAND_MARKER + kind, c->files[file].tokens.ids[token]);
            return;
        case NODE_SWITCH_START:
        case NODE_VALUE_SWITCH_START:
            begin_switch(c, file, node, kind, token);
            return;
        case NODE_CASE_START:
            if (top_switch(c)->at == NO_SLOT) {
                open_switch(c, file);
            }
            push(c, token, OPERAND_MARKER + kind, 0);
            return;
        case NODE_CASE_BODY: {
            case_labels(c, file);
            Switch *s = top_switch(c);
            if (s->arms > 0) {
                emit(c, IR_CASE, 0, s->arms, 0);
            }
            s->arms++;
            c->body.reachable = s->before;
            push(c, token, OPERAND_MARKER + kind, c->nscope);
            return;
        }
        case NODE_WHILE_BODY: {
            u32 cond_token = c->stack_tokens[c->sp - 1];
            Operand cond = pop(c);
            bool forever = cond.type == TYPE_ID_BOOL && is_constant(cond) &&
                           exact_sign(&c->exacts, value_of(cond)) != 0;
            emit(c, IR_IF, 0, condition(c, file, cond_token, cond), 0);
            push(c, cond_token, OPERAND_STMT, 0);
            push(c, token, OPERAND_MARKER + kind, forever);
            return;
        }
        case NODE_AND_LEFT:
        case NODE_OR_LEFT: logical_left(c, file, token, kind); return;
        default: break;
    }
    push(c, token, OPERAND_MARKER + kind, c->nscratch);
}

static void
walk(Checker *c, u32 decl, u32 begin, u32 end)
{
    u32 file = c->decls.files[decl];
    const u8 *kinds = c->files[file].nodes.kinds;
    const u32 *tokens = c->files[file].nodes.tokens;
    const u32 *ids = c->files[file].tokens.ids;
    bool in_body = c->body.code != NULL;
    u32 base = c->sp;

    for (u32 i = begin; i < end; i++) {
        u8 kind = kinds[i];
        u32 token = tokens[i];
        if (is_marker[kind]) {
            if (kind == NODE_FN_BODY) {
                if (!in_body && list_start(c, NODE_FN_START) == base) {
                    c->decls.bodies[decl] = i;
                    return;
                }
                i = literal_end(kinds, i) - 1;
                continue;
            }
            if (in_body) {
                body_marker(c, file, i, kind, token);
            } else if (kind == NODE_AND_LEFT || kind == NODE_OR_LEFT) {
                logical_left(c, file, token, kind);
            } else {
                push(c, token, OPERAND_MARKER + kind, c->nscratch);
            }
            continue;
        }

        u32 child = i > begin ? tokens[i - 1] : token;

        switch (kind) {
            case NODE_NAME: {
                u32 id = ids[token];
                if (in_body && id == NAME_DISCARD) {
                    if (kinds[i + 1] == NODE_VALUE || in_case_labels(c)) {
                        push(c, token, OPERAND_DISCARD, 0);
                    } else {
                        report(c, file, token, DIAG_DISCARD, 0, 0);
                        push_operand(c, token, poison);
                    }
                    break;
                }
                if (in_body) {
                    u64 binding = c->bindings[id];
                    if ((u32)(binding >> 32) == c->body.owner) {
                        u32 slot = (u32)binding;
                        if (kinds[i + 1] == NODE_VALUE || kinds[i + 1] == NODE_ADDR ||
                            is_aggregate(c, slot_at(c, slot)->type)) {
                            push(c, token, OPERAND_LOCAL, slot);
                        } else {
                            push_operand(c, token, load_local(c, slot));
                        }
                        break;
                    }
                    u32 decl_id = c->package_names[id];
                    if (kinds[i + 1] == NODE_CALL_ARGS && decl_id &&
                        c->decls.kinds[decl_id] == DECL_FN) {
                        need(c, decl_id, file, token);
                        if (c->types.arg1[c->decls.types[decl_id]] == TYPE_ID_INFERRED) {
                            need_body(c, decl_id, file, token);
                        }
                        push(c, token, OPERAND_FN, decl_id);
                        break;
                    }
                }
                push_operand(c, token, resolve_name(c, file, i));
                break;
            }
            case NODE_INT:
                push_operand(c, token, number_literal(c, file, i, TYPE_ID_UNTYPED_INT));
                break;
            case NODE_ENUM_LITERAL:
                push(c, token, TYPE_ID_ENUM_LITERAL, ids[token]);
                break;
            case NODE_FIELD: {
                Operand o = c->stack[c->sp - 1];
                if (o.type == TYPE_ID_TYPE && c->types.kinds[o.ref] == TYPE_ENUM) {
                    pop(c);
                    push_operand(c, token, enum_member(c, file, token, o.ref, ids[token]));
                } else if (is_place(o) && is_aggregate(c, place_type(c, o))) {
                    pop(c);
                    push_operand(c, token, array_field(c, file, token, o));
                } else {
                    unsupported(c, file, i);
                }
                break;
            }
            case NODE_INDEX:
                if (in_body) {
                    u32 index_token = c->stack_tokens[c->sp - 1];
                    Operand index = pop(c);
                    u32 base_token = c->stack_tokens[c->sp - 1];
                    Operand base = pop(c);
                    bool place = kinds[i + 1] == NODE_VALUE || kinds[i + 1] == NODE_ADDR;
                    push_operand(
                        c,
                        base_token,
                        index_place(c, file, base_token, base, index, index_token, place)
                    );
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_ARRAY_LITERAL:
            case NODE_ANON_ARRAY_LITERAL:
                if (in_body) {
                    array_literal(c, file, i, kind);
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_FLOAT:
                push_operand(c, token, number_literal(c, file, i, TYPE_ID_UNTYPED_FLOAT));
                break;
            case NODE_TRUE:
            case NODE_FALSE:
                push(c, token, TYPE_ID_BOOL, kind == NODE_TRUE ? EXACT_ONE : EXACT_ZERO);
                break;
            case NODE_UNDEFINED: push(c, token, TYPE_ID_UNDEFINED, 0); break;
            case NODE_NEG:
            case NODE_NEG_WRAP:
            case NODE_NOT:
            case NODE_BIT_NOT:
                push_operand(c, token, unary(c, file, token, kind, pop(c)));
                break;
            case NODE_ADDR:
                if (in_body) {
                    u32 target_token = c->stack_tokens[c->sp - 1];
                    Operand target = pop(c);
                    push_operand(c, token, address_of(c, file, target_token, target));
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_DEREF:
                if (in_body) {
                    bool place = kinds[i + 1] == NODE_VALUE || kinds[i + 1] == NODE_ADDR;
                    push_operand(c, token, deref(c, file, token, pop(c), place));
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_MUL:
            case NODE_MUL_WRAP:
            case NODE_DIV:
            case NODE_MOD:
            case NODE_SHL:
            case NODE_SHR:
            case NODE_BIT_AND:
            case NODE_ADD:
            case NODE_SUB:
            case NODE_ADD_WRAP:
            case NODE_SUB_WRAP:
            case NODE_BIT_OR:
            case NODE_BIT_XOR:
            case NODE_EQ:
            case NODE_NE:
            case NODE_LT:
            case NODE_LE:
            case NODE_GT:
            case NODE_GE: {
                Operand b = pop(c);
                Operand a = pop(c);
                push_operand(c, token, binary(c, file, token, kind, a, b));
                break;
            }
            case NODE_AND:
            case NODE_OR: push_operand(c, token, logical(c, file, kind)); break;
            case NODE_CALL: call(c, file); break;
            case NODE_POINTER_TYPE: {
                u32 t = type_operand(c, file, child, pop(c));
                push_type(c, token, t ? pointer_type(c, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_OPTIONAL_TYPE: {
                u32 t = type_operand(c, file, child, pop(c));
                push_type(c, token, t ? optional_type(c, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_SLICE_TYPE: {
                u32 t = type_operand(c, file, child, pop(c));
                push_type(c, token, t ? slice_type(c, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_ARRAY_TYPE: {
                u32 t = type_operand(c, file, child, pop(c));
                bool ok;
                u64 n = array_length(c, file, token, pop(c), &ok);
                push_type(c, token, t && ok ? intern_type(c, TYPE_ARRAY, t, n) : TYPE_ID_NONE);
                break;
            }
            case NODE_ERROR_UNION: {
                u32 t = type_operand(c, file, child, pop(c));
                u32 e = type_operand(c, file, token, pop(c));
                if (e && e != TYPE_ID_ERROR) {
                    report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
                    e = TYPE_ID_NONE;
                }
                push_type(c, token, t && e ? intern_type(c, TYPE_ERROR_UNION, t, e) : TYPE_ID_NONE);
                break;
            }
            case NODE_ERROR_UNION_INFERRED: {
                u32 t = type_operand(c, file, child, pop(c));
                push_type(c, token, t ? intern_type(c, TYPE_ERROR_UNION, t, 0) : TYPE_ID_NONE);
                break;
            }
            case NODE_FN_TYPE: push_type(c, token, close_fn_type(c)); break;
            case NODE_MEMBER_TYPE: {
                u32 t = type_operand(c, file, child, pop(c));
                if (t) {
                    claim_contained(c, t, file, token);
                }
                push_member(c, file, i, t, MEMBER_NONE);
                break;
            }
            case NODE_MEMBER_TYPE_INIT:
            case NODE_PARAM_TYPE_INIT: {
                Operand v = pop(c);
                pop(c);
                u32 t = type_operand(c, file, token, pop(c));
                if (t && kind == NODE_MEMBER_TYPE_INIT) {
                    claim_contained(c, t, file, token);
                }
                push_member(c, file, i, t, default_value(c, file, token, v, t));
                break;
            }
            case NODE_MEMBER_INIT: {
                Operand v = pop(c);
                u32 t = inferred_type(c, file, token, v);
                push_member(c, file, i, t, t ? member_value(convert(c, file, token, v, t)) : MEMBER_NONE);
                break;
            }
            case NODE_PARAM_TYPE: {
                u32 t = type_operand(c, file, child, pop(c));
                push_member(c, file, i, t, MEMBER_NONE);
                break;
            }
            case NODE_ENUM_MEMBER:
            case NODE_EMPTY_MEMBER:
                push_member(c, file, i, TYPE_ID_NONE, MEMBER_NONE);
                break;
            case NODE_ENUM_VALUE: {
                Operand v = pop(c);
                if (v.type != TYPE_ID_NONE && (!is_int(c, v.type) || !is_constant(v))) {
                    report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
                    v = poison;
                }
                push_member(c, file, i, v.type, value_of(v));
                break;
            }
            case NODE_ENUM_BACKING:
            case NODE_UNION_TAG: {
                u32 t = type_operand(c, file, child, pop(c));
                u8 k = c->types.kinds[t];
                if (t && k != TYPE_INT && !(kind == NODE_UNION_TAG && k == TYPE_ENUM)) {
                    report(c, file, child, DIAG_NOT_INTEGER, 0, 0);
                    t = TYPE_ID_NONE;
                }
                if (t && k == TYPE_ENUM) {
                    need(c, c->types.arg0[t], file, token);
                }
                push(c, token, OPERAND_BACKING, t);
                break;
            }
            case NODE_FN_RETURN:
                push(c, token, OPERAND_RETURN, type_operand(c, file, child, pop(c)));
                break;
            case NODE_STRUCT:
            case NODE_ENUM:
            case NODE_UNION:
            case NODE_CONST_INIT:
            case NODE_CONST_TYPE_INIT:
                if (in_body) {
                    unsupported(c, file, i);
                } else if (kind == NODE_STRUCT) {
                    close_struct(c, decl, file);
                } else if (kind == NODE_ENUM) {
                    close_enum(c, decl, file);
                } else if (kind == NODE_UNION) {
                    close_union(c, decl, file);
                } else if (kind == NODE_CONST_INIT) {
                    set_const(c, decl, pop(c));
                } else {
                    Operand v = pop(c);
                    pop(c);
                    u32 t = type_operand(c, file, token, pop(c));
                    set_const(c, decl, convert(c, file, token, v, t));
                }
                break;
            case NODE_VAR_INIT: {
                u32 v_token = c->stack_tokens[c->sp - 1];
                Operand v = pop(c);
                u32 t = is_place(v) ? place_type(c, v)
                                    : value_type(c, file, v_token, v);
                declare_local(c, file, i, t, t ? convert(c, file, v_token, v, t) : poison);
                push(c, token, OPERAND_STMT, 0);
                break;
            }
            case NODE_VAR_TYPE: {
                u32 t = local_type(c, file, child, type_operand(c, file, child, pop(c)));
                u32 elem = t;
                while (is_aggregate(c, elem)) {
                    elem = c->types.arg0[elem];
                }
                bool zero = is_number(c, elem) || elem == TYPE_ID_BOOL;
                if (elem && c->types.kinds[elem] == TYPE_ENUM) {
                    zero = enum_has(c, elem, order_key(c, elem, 0));
                    if (!zero) {
                        report(c, file, token, DIAG_NO_ZERO_MEMBER, 0, elem);
                    }
                } else if (elem && c->types.kinds[elem] == TYPE_POINTER) {
                    report(c, file, token, DIAG_NEVER_NIL, 0, elem);
                } else if (elem && !zero) {
                    report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
                }

                bool aggregate = is_aggregate(c, t);
                Operand v = !zero     ? poison
                          : aggregate ? (Operand){ TYPE_ID_UNDEFINED, 0 }
                                      : (Operand){ t, EXACT_ZERO };
                u32 slot = declare_local(c, file, i, t, v);
                if (zero && aggregate) {
                    emit(c, IR_ZERO, t, place_address(c, (Operand){ OPERAND_LOCAL, slot }), 0);
                }
                push(c, token, OPERAND_STMT, 0);
                break;
            }
            case NODE_VAR_TYPE_INIT: {
                u32 v_token = c->stack_tokens[c->sp - 1];
                Operand v = pop(c);
                pop(c);
                u32 t_token = c->stack_tokens[c->sp - 1];
                u32 t = local_type(c, file, t_token, type_operand(c, file, t_token, pop(c)));
                declare_local(
                    c,
                    file,
                    i,
                    t,
                    v.type == TYPE_ID_UNDEFINED ? v : convert(c, file, v_token, v, t)
                );
                push(c, token, OPERAND_STMT, 0);
                break;
            }
            case NODE_ASSIGN:
            case NODE_ADD_ASSIGN:
            case NODE_SUB_ASSIGN:
            case NODE_MUL_ASSIGN:
            case NODE_ADD_WRAP_ASSIGN:
            case NODE_SUB_WRAP_ASSIGN:
            case NODE_MUL_WRAP_ASSIGN:
            case NODE_DIV_ASSIGN:
            case NODE_MOD_ASSIGN:
            case NODE_BIT_AND_ASSIGN:
            case NODE_BIT_OR_ASSIGN:
            case NODE_BIT_XOR_ASSIGN:
            case NODE_SHL_ASSIGN:
            case NODE_SHR_ASSIGN:
            case NODE_OR_ELSE_ASSIGN: assign(c, file, i, token, kind); break;
            case NODE_EXPR_STMT: expression_statement(c, file, token); break;
            case NODE_RETURN:
            case NODE_RETURN_VALUE:
                return_value(c, file, token, kind == NODE_RETURN_VALUE);
                break;
            case NODE_BREAK:
            case NODE_BREAK_LABEL:
            case NODE_BREAK_VALUE:
            case NODE_CONTINUE:
            case NODE_CONTINUE_LABEL:
            case NODE_CONTINUE_VALUE: jump(c, file, token, kind); break;
            case NODE_BLOCK: {
                u32 m = list_start(c, NODE_BLOCK_START);
                unbind_to(c, c->stack[m].ref);
                c->sp = m;
                Loop *top = c->nloops > c->body.loops ? &c->loops[c->nloops - 1] : NULL;
                if (top && top->block && top->marker == m) {
                    push_operand(c, token, close_labeled_block(c, file, token));
                } else {
                    push(c, token, OPERAND_STMT, 0);
                }
                break;
            }
            case NODE_LABELED: {
                c->sp = list_start(c, NODE_LABEL_START);
                push(c, token, OPERAND_STMT, 0);
                break;
            }
            case NODE_LABELED_VALUE:
            case NODE_LABELED_ITEM: {
                Operand v = pop(c);
                c->sp = list_start(c, NODE_VALUE_LABEL_START);
                push_operand(c, token, v);
                break;
            }
            case NODE_IF: close_if(c, token); break;
            case NODE_WHILE: close_while(c, token); break;
            case NODE_RANGE:
            case NODE_RANGE_INCLUSIVE:
                if (in_body && kinds[i + 1] == NODE_FOR_VALUE) {
                    for_range(c, file, i, token);
                } else if (in_body && in_case_labels(c)) {
                    case_range(c, file, token, kind == NODE_RANGE_INCLUSIVE);
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_FOR_VALUE:
                for_value(c, file, i, token, kinds[i - 1] == NODE_RANGE_INCLUSIVE);
                break;
            case NODE_FOR_INDEX:
                if (!in_body) {
                    unsupported(c, file, i);
                }
                break;
            case NODE_FOR: close_for(c, token); break;
            case NODE_CASE:
                if (in_body) {
                    close_arm(c, file, token);
                } else {
                    c->sp = list_start(c, NODE_CASE_START);
                }
                break;
            case NODE_SWITCH:
            case NODE_VALUE_SWITCH:
            case NODE_SWITCH_ITEM:
                if (in_body) {
                    push_operand(c, token, close_switch(c, file, kind));
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_SWITCH_VALUE:
            case NODE_SWITCH_REF:
                if (in_body) {
                    report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
                    c->stack[c->sp - 1] = poison;
                }
                break;
            default: unsupported(c, file, i);
        }
    }
}

static void
finish_signature(Checker *c, u32 decl)
{
    u32 file = c->decls.files[decl];
    u32 m = list_start(c, NODE_FN_START);
    u32 ret = c->decls.flags[decl] & (DECL_PUB | DECL_EXPORT) ? TYPE_ID_VOID
                                                              : TYPE_ID_INFERRED;
    u32 end = c->sp;
    if (end > m + 1 && c->stack[end - 1].type == OPERAND_RETURN) {
        ret = c->stack[--end].ref;
    }

    c->sp = end;
    u32 first = collect_members(c, file, m + 1, DIAG_DUPLICATE_PARAM);
    u32 count = c->nmembers - first;
    u32 list = reserve_list(c, count);
    for (u32 i = 0; i < count; i++) {
        c->lists[list + 1 + i] = c->members[first + i].type;
    }

    c->decls.types[decl] = fn_type(c, list, ret);
    c->decls.members[decl] = first;
    c->decls.member_counts[decl] = count;
    c->nscratch = c->stack[m].ref;
    c->sp = m;
}

static void
check_decl(Checker *c, u32 decl)
{
    Decls *d = &c->decls;
    u32 first = d->firsts[decl];
    u32 root = d->roots[decl];
    [[maybe_unused]] u32 base = c->sp;
    Body outer = c->body;
    c->body = (Body){ 0 };

    d->busy[decl] = 1;
    c->claims[c->nclaims++] = decl;
    reserve_stack(c, root - first + 1);

    walk(c, decl, first, root + 1);
    if (d->kinds[decl] == DECL_FN) {
        finish_signature(c, decl);
    }

    assert(c->sp == base);
    c->nclaims--;
    d->busy[decl] = 0;
    d->phases[decl] = PHASE_RESOLVED;
    c->body = outer;
}

static void *
grow(void *p, u32 *cap, u32 need, size_t size)
{
    if (need > *cap) {
        *cap = need * 2;
        p = realloc(p, *cap * size);
    }
    return p;
}

static void
reserve_code(IrCode *code, u32 count, u32 nextra, u32 nslots, u32 nconsts)
{
    if (code->count + count > code->cap) {
        code->cap = (code->count + count) * 2;
        code->kinds = resize(code->kinds, code->cap, sizeof *code->kinds);
        code->types = resize(code->types, code->cap, sizeof *code->types);
        code->as = resize(code->as, code->cap, sizeof *code->as);
        code->bs = resize(code->bs, code->cap, sizeof *code->bs);
    }
    code->extra = grow(
        code->extra,
        &code->extra_cap,
        code->nextra + nextra,
        sizeof *code->extra
    );
    code->slots = grow(
        code->slots,
        &code->slots_cap,
        code->nslots + nslots,
        sizeof *code->slots
    );
    code->consts = grow(
        code->consts,
        &code->consts_cap,
        code->nconsts + nconsts,
        sizeof *code->consts
    );
}

static void
append_code(IrCode *to, const IrCode *from, u32 first, u32 first_extra, u32 first_slot, u32 first_const)
{
    u32 count = from->count - first;
    u32 nextra = from->nextra - first_extra;
    u32 nslots = from->nslots - first_slot;
    u32 nconsts = from->nconsts - first_const;
    reserve_code(to, count, nextra, nslots, nconsts);

    if (count) {
        memcpy(to->kinds + to->count, from->kinds + first, count * sizeof *to->kinds);
        memcpy(to->types + to->count, from->types + first, count * sizeof *to->types);
        memcpy(to->as + to->count, from->as + first, count * sizeof *to->as);
        memcpy(to->bs + to->count, from->bs + first, count * sizeof *to->bs);
    }
    if (nextra) {
        memcpy(
            to->extra + to->nextra,
            from->extra + first_extra,
            nextra * sizeof *to->extra
        );
    }
    if (nslots) {
        memcpy(
            to->slots + to->nslots,
            from->slots + first_slot,
            nslots * sizeof *to->slots
        );
    }
    if (nconsts) {
        memcpy(
            to->consts + to->nconsts,
            from->consts + first_const,
            nconsts * sizeof *to->consts
        );
    }
    to->count += count;
    to->nextra += nextra;
    to->nslots += nslots;
    to->nconsts += nconsts;
}

static u32
finish_ir(Checker *c, u32 decl, u32 nparams)
{
    Ir *ir = &c->ir;
    const Body *b = &c->body;
    IrCode *code = b->code;
    IrFunc fn = {
        .decl = decl,
        .nparams = nparams,
        .first = b->first,
        .count = code->count - b->first,
        .first_extra = b->first_extra,
        .nextra = code->nextra - b->first_extra,
        .first_slot = b->first_slot,
        .nslots = code->nslots - b->first_slot,
        .first_const = b->first_const,
        .nconsts = code->nconsts - b->first_const,
        .calls = b->calls,
    };

    if (code != &ir->code) {
        IrCode *deferred = &c->deferred;
        fn.first = deferred->count;
        fn.first_extra = deferred->nextra;
        fn.first_slot = deferred->nslots;
        fn.first_const = deferred->nconsts;
        append_code(deferred, code, b->first, b->first_extra, b->first_slot, b->first_const);
    } else if (ir->nfuncs > c->flushed) {
        IrCode *deferred = &c->deferred;
        for (u32 i = c->flushed; i < ir->nfuncs; i++) {
            ir->funcs[i].first += code->count;
            ir->funcs[i].first_extra += code->nextra;
            ir->funcs[i].first_slot += code->nslots;
            ir->funcs[i].first_const += code->nconsts;
        }
        append_code(code, deferred, 0, 0, 0, 0);
        deferred->count = deferred->nextra = deferred->nslots = deferred->nconsts = 0;
    }

    ir->funcs = grow(ir->funcs, &ir->funcs_cap, ir->nfuncs + 1, sizeof *ir->funcs);
    ir->funcs[ir->nfuncs++] = fn;
    if (code == &ir->code) {
        c->flushed = ir->nfuncs;
    }
    return ir->nfuncs;
}

static void
check_body(Checker *c, u32 decl)
{
    Decls *d = &c->decls;
    u32 file = d->files[decl];
    u32 begin = d->bodies[decl] + 1;
    u32 end = d->roots[decl];
    u32 fn = d->types[decl];
    [[maybe_unused]] u32 base = c->sp;

    d->busy[decl] = 1;
    c->claims[c->nclaims++] = decl;

    Body outer = c->body;
    IrCode *code = c->nbodies == 0 ? &c->ir.code : &c->builders[c->nbodies];
    if (c->nbodies > 0) {
        code->count = code->nextra = code->nslots = code->nconsts = 0;
    }
    c->nbodies++;
    c->body = (Body){
        .code = code,
        .first = code->count,
        .first_extra = code->nextra,
        .first_slot = code->nslots,
        .first_const = code->nconsts,
        .decl = decl,
        .owner = ++c->owners,
        .ret = (u32)c->types.arg1[fn],
        .loops = c->nloops,
        .breaks = c->nbreaks,
        .reachable = true,
    };
    u32 scope = c->nscope;

    const u32 *tokens = c->files[file].nodes.tokens;
    if (is_aggregate(c, c->body.ret)) {
        report(c, file, tokens[d->roots[decl]], DIAG_UNSUPPORTED, 0, 0);
        c->body.ret = TYPE_ID_NONE;
    }

    u32 first = d->members[decl];
    u32 nparams = d->member_counts[decl];
    for (u32 i = 0; i < nparams; i++) {
        Member m = c->members[first + i];
        if (is_aggregate(c, m.type)) {
            report(c, file, tokens[m.node], DIAG_UNSUPPORTED, 0, 0);
            m.type = TYPE_ID_NONE;
        }
        u32 slot = add_slot(c, m.name, m.type, m.node);
        if (m.name - (NAME_LAST_KEYWORD + 1) >= NBUILTIN_TYPES) {
            bind(c, file, m.node, slot);
        }
    }

    reserve_stack(c, end - begin + 1);
    walk(c, decl, begin, end);
    assert(c->sp == base + 1);
    c->sp = base;

    if (c->body.ret == TYPE_ID_INFERRED) {
        u32 type = settle_peers(c, file, c->body.returns, c->body.code->as);
        c->body.ret = c->body.returns_value ? type : TYPE_ID_VOID;
    }
    if (c->body.reachable) {
        if (returns_nothing(c, c->body.ret)) {
            emit(c, IR_RET, c->body.ret, 0, 0);
        } else if (c->body.ret != TYPE_ID_NONE) {
            report(
                c,
                file,
                c->files[file].nodes.tokens[end - 1],
                DIAG_MISSING_RETURN,
                0,
                c->body.ret
            );
        }
    }
    if (c->types.arg1[fn] == TYPE_ID_INFERRED) {
        d->types[decl] = intern_type(c, TYPE_FN, c->types.arg0[fn], c->body.ret);
    }

    unbind_to(c, scope);
    c->nloops = c->body.loops;
    c->nbreaks = c->body.breaks;
    d->funcs[decl] = finish_ir(c, decl, nparams);
    c->body = outer;
    c->nbodies--;

    c->nclaims--;
    d->busy[decl] = 0;
    d->phases[decl] = PHASE_CHECKED;
}

static void
declare(Checker *c)
{
    Decls *d = &c->decls;
    for (u32 f = 0; f < c->nfiles; f++) {
        const ParsedFile *file = &c->files[f];
        const u8 *kinds = file->nodes.kinds;
        const u32 *tokens = file->nodes.tokens;

        u32 first = 0;
        for (u32 s = 0; s < file->top_count; s++) {
            u32 end = file->nodes.top_ends[s];
            u32 root = end - 1;
            u8 flags = 0;
            while (kinds[root] == NODE_PUB || kinds[root] == NODE_EXPORT) {
                flags |= kinds[root] == NODE_PUB ? DECL_PUB : DECL_EXPORT;
                root--;
            }

            u8 kind = decl_kinds[kinds[root]];
            if (kind == DECL_NONE) {
                if (kinds[root] == NODE_VAR_INIT || kinds[root] == NODE_VAR_TYPE ||
                    kinds[root] == NODE_VAR_TYPE_INIT) {
                    report(c, f, tokens[root], DIAG_UNSUPPORTED, 0, 0);
                } else if (kinds[root] != NODE_PACKAGE && kinds[root] != NODE_IMPORT_DECL) {
                    report(c, f, tokens[first], file->entry ? DIAG_UNSUPPORTED : DIAG_CODE_OUTSIDE_ENTRY, 0, 0);
                }
                first = end;
                continue;
            }

            u32 name = node_name(c, f, root);
            if (name - (NAME_LAST_KEYWORD + 1) < NBUILTIN_TYPES) {
                report(c, f, tokens[root], DIAG_RESERVED, 0, 0);
                first = end;
                continue;
            }

            u32 id = d->count++;
            d->names[id] = name;
            d->kinds[id] = kind;
            d->flags[id] = flags;
            d->files[id] = f;
            d->firsts[id] = first;
            d->roots[id] = root;
            d->phases[id] = PHASE_DECLARED;
            if (kind == DECL_STRUCT || kind == DECL_ENUM || kind == DECL_UNION) {
                u8 type_kind = kind == DECL_STRUCT ? TYPE_STRUCT
                             : kind == DECL_ENUM   ? TYPE_ENUM
                                                   : TYPE_UNION;
                d->types[id] = add_type(c, type_kind, id, 0);
            }

            u32 prev = c->package_names[name];
            if (prev) {
                report(c, f, tokens[root], DIAG_DUPLICATE, 0, prev);
            } else {
                c->package_names[name] = id;
            }
            first = end;
        }
    }
}

static int
compare_diagnostics(const void *a, const void *b)
{
    const Diagnostic *x = a;
    const Diagnostic *y = b;
    if (x->file != y->file) {
        return x->file < y->file ? -1 : 1;
    }
    if (x->token != y->token) {
        return x->token < y->token ? -1 : 1;
    }
    if (x->kind != y->kind) {
        return x->kind < y->kind ? -1 : 1;
    }
    if (x->arg != y->arg) {
        return x->arg < y->arg ? -1 : 1;
    }
    return x->arg2 < y->arg2 ? -1 : x->arg2 > y->arg2;
}

void
check_init(Checker *c, const Names *names)
{
    *c = (Checker){ .names = names };
    c->package_names = calloc(names->count, sizeof *c->package_names);
    c->bound = calloc(names->count, sizeof *c->bound);
    c->bindings = calloc(names->count, sizeof *c->bindings);

    Types *t = &c->types;
    reserve_types(c, 256);
    for (u32 id = 0; id < NFIXED_TYPES; id++) {
        add_type(c, fixed_kinds[id], fixed_bits[id], fixed_signed[id]);
    }
    t->map_mask = 63;
    t->map = calloc(t->map_mask + 1, sizeof *t->map);

    exacts_init(&c->exacts);

    c->lists_cap = 64;
    c->lists = malloc(c->lists_cap * sizeof *c->lists);
    c->lists[0] = 0;
    c->nlists = 1;

    c->members_cap = 64;
    c->members = malloc(c->members_cap * sizeof *c->members);
    c->scratch_cap = 64;
    c->scratch = malloc(c->scratch_cap * sizeof *c->scratch);
    c->stack_cap = 256;
    c->stack = malloc(c->stack_cap * sizeof *c->stack);
    c->stack_tokens = malloc(c->stack_cap * sizeof *c->stack_tokens);
    c->claims = malloc(CHECK_MAX_DEPTH * sizeof *c->claims);
    c->cases_cap = 64;
    c->cases = malloc(c->cases_cap * sizeof *c->cases);
    c->builders = calloc(CHECK_MAX_DEPTH, sizeof *c->builders);
}

void
check_package(Checker *c, const ParsedFile *files, u32 nfiles)
{
    c->files = files;
    c->nfiles = nfiles;

    u32 cap = 1;
    for (u32 f = 0; f < nfiles; f++) {
        cap += files[f].top_count;
    }

    Decls *d = &c->decls;
    d->names = calloc(cap, sizeof *d->names);
    d->kinds = calloc(cap, sizeof *d->kinds);
    d->flags = calloc(cap, sizeof *d->flags);
    d->phases = calloc(cap, sizeof *d->phases);
    d->busy = calloc(cap, sizeof *d->busy);
    d->files = calloc(cap, sizeof *d->files);
    d->firsts = calloc(cap, sizeof *d->firsts);
    d->roots = calloc(cap, sizeof *d->roots);
    d->types = calloc(cap, sizeof *d->types);
    d->values = calloc(cap, sizeof *d->values);
    d->members = calloc(cap, sizeof *d->members);
    d->member_counts = calloc(cap, sizeof *d->member_counts);
    d->bodies = calloc(cap, sizeof *d->bodies);
    d->funcs = calloc(cap, sizeof *d->funcs);
    d->count = 1;

    declare(c);
    if (c->types.cap < 2 * d->count) {
        reserve_types(c, 2 * d->count);
    }
    u32 map_cap = c->types.map_mask + 1;
    while (map_cap < 2 * d->count) {
        map_cap *= 2;
    }
    if (map_cap > c->types.map_mask + 1) {
        grow_type_map(c, map_cap);
    }

    for (u32 decl = 1; decl < d->count; decl++) {
        if (d->phases[decl] < PHASE_RESOLVED) {
            check_decl(c, decl);
        }
    }
    u32 nodes = 0;
    for (u32 f = 0; f < nfiles; f++) {
        nodes += files[f].count;
    }
    reserve_code(&c->ir.code, nodes, nodes / 4, nodes / 4, nodes / 4);
    for (u32 decl = 1; decl < d->count; decl++) {
        if (d->kinds[decl] == DECL_FN && d->phases[decl] == PHASE_RESOLVED) {
            check_body(c, decl);
        }
    }

    if (c->ndiags > 1) {
        qsort(c->diags, c->ndiags, sizeof *c->diags, compare_diagnostics);
    }
}

static void
print_name(FILE *out, const Checker *c, u32 name)
{
    Name n = names_get(c->names, name);
    fwrite(n.text, 1, n.len, out);
}

static void
print_token(FILE *out, const Checker *c, const ParsedFile *f, u32 token)
{
    if (lexer_kind_has_id(f->tokens.kinds[token])) {
        print_name(out, c, f->tokens.ids[token]);
        return;
    }
    u32 start = f->tokens.starts[token];
    fwrite(f->src + start, 1, lexer_token_length(f->src, f->tokens.kinds[token], start), out);
}

static void
print_type(FILE *out, const Checker *c, u32 type)
{
    if (type < NFIXED_TYPES) {
        fputs(fixed_texts[type], out);
        return;
    }

    const Types *t = &c->types;
    switch (t->kinds[type]) {
        case TYPE_POINTER:
            fputc('*', out);
            print_type(out, c, t->arg0[type]);
            return;
        case TYPE_OPTIONAL:
            fputc('?', out);
            print_type(out, c, t->arg0[type]);
            return;
        case TYPE_SLICE:
            fputs("[]", out);
            print_type(out, c, t->arg0[type]);
            return;
        case TYPE_ARRAY:
            fprintf(out, "[%llu]", (unsigned long long)t->arg1[type]);
            print_type(out, c, t->arg0[type]);
            return;
        case TYPE_ERROR_UNION:
            if (t->arg1[type]) {
                print_type(out, c, (u32)t->arg1[type]);
            }
            fputc('!', out);
            print_type(out, c, t->arg0[type]);
            return;
        case TYPE_FN: {
            const u32 *list = c->lists + t->arg0[type];
            fputs("fn(", out);
            for (u32 i = 1; i <= list[0]; i++) {
                fputs(i > 1 ? ", " : "", out);
                print_type(out, c, list[i]);
            }
            fputs(") -> ", out);
            print_type(out, c, (u32)t->arg1[type]);
            return;
        }
        case TYPE_STRUCT:
        case TYPE_ENUM:
        case TYPE_UNION:
            print_name(out, c, c->decls.names[t->arg0[type]]);
            return;
        default: assert(false && "not a built type");
    }
}

static void
print_float(FILE *out, u64 bits, u32 width)
{
    char buf[40];
    if (width == 32) {
        u32 b = (u32)bits;
        float f;
        memcpy(&f, &b, sizeof f);
        for (int digits = 1; digits <= 9; digits++) {
            snprintf(buf, sizeof buf, "%.*g", digits, (double)f);
            if (strtof(buf, NULL) == f) {
                break;
            }
        }
    } else {
        double d;
        memcpy(&d, &bits, sizeof d);
        for (int digits = 1; digits <= 17; digits++) {
            snprintf(buf, sizeof buf, "%.*g", digits, d);
            if (strtod(buf, NULL) == d) {
                break;
            }
        }
    }
    fputs(buf, out);
}

static void
print_constant(FILE *out, const Checker *c, u32 type, u32 value)
{
    u8 kind = c->types.kinds[type];
    if (type == TYPE_ID_BOOL) {
        fputs(exact_sign(&c->exacts, value) ? "true" : "false", out);
    } else if (kind == TYPE_FLOAT) {
        ExactStatus status;
        u32 width = c->types.arg0[type];
        print_float(out, exact_float(&c->exacts, value, width, &status), width);
    } else {
        exact_print(out, &c->exacts, value);
    }
}

static void
print_location(FILE *out, const ParsedFile *f, u32 token)
{
    u32 pos = f->tokens.starts[token];
    u32 line = 1;
    u32 col = 1;
    for (u32 i = 0; i < pos; i++) {
        line += f->src[i] == '\n';
        col = f->src[i] == '\n' ? 1 : col + 1;
    }
    fprintf(out, "%u:%u", line, col);
}

void
check_print_diagnostics(FILE *out, const Checker *c)
{
    for (u32 i = 0; i < c->ndiags; i++) {
        Diagnostic d = c->diags[i];
        const ParsedFile *f = &c->files[d.file];
        fprintf(out, "%s:", f->path);
        print_location(out, f, d.token);
        fputs(": ", out);

        for (const char *p = diagnostic_texts[d.kind]; *p; p++) {
            if (*p != '%') {
                fputc(*p, out);
                continue;
            }

            switch (*++p) {
                case 't': print_token(out, c, f, d.token); break;
                case 'n': print_name(out, c, d.arg); break;
                case 'd': {
                    const ParsedFile *g = &c->files[c->decls.files[d.arg2]];
                    fprintf(out, "%s:", g->path);
                    print_location(out, g, g->nodes.tokens[c->decls.roots[d.arg2]]);
                    break;
                }
                case 'm':
                    print_location(out, f, f->nodes.tokens[d.arg2]);
                    break;
                case 'l': print_location(out, f, d.arg2); break;
                case 'w': print_constant(out, c, d.arg2, d.arg); break;
                case 'v': exact_print(out, &c->exacts, d.arg); break;
                case 'c': fprintf(out, "%u", d.arg); break;
                case 'C': fprintf(out, "%u", d.arg2); break;
                case 'T': print_type(out, c, d.arg); break;
                case 'U': print_type(out, c, d.arg2); break;
                default: assert(false && "unknown placeholder");
            }
        }
        fputc('\n', out);
    }
}

static void
print_value(FILE *out, const Checker *c, u32 type, u32 value)
{
    if (value == MEMBER_UNDEFINED) {
        fputs(" = undefined", out);
    } else if (value != MEMBER_NONE) {
        fputs(" = ", out);
        print_constant(out, c, type, value);
    }
}

static void
print_members(FILE *out, const Checker *c, u32 decl, const char *open, const char *close)
{
    const Decls *d = &c->decls;
    u32 count = d->member_counts[decl];
    fputs(count ? open : "", out);
    for (u32 i = 0; i < count; i++) {
        Member m = c->members[d->members[decl] + i];
        fputs(i ? ", " : "", out);
        print_name(out, c, m.name);
        if (d->kinds[decl] != DECL_ENUM && (d->kinds[decl] != DECL_UNION || m.type)) {
            fputs(": ", out);
            print_type(out, c, m.type);
        }
        print_value(out, c, m.type, m.value);
    }
    fputs(count ? close : "", out);
}

static void
print_decl(FILE *out, const Checker *c, u32 decl)
{
    const Decls *d = &c->decls;
    fputs(d->flags[decl] & DECL_PUB ? "pub " : "", out);
    fputs(d->flags[decl] & DECL_EXPORT ? "export " : "", out);
    u32 type = d->types[decl];
    switch (d->kinds[decl]) {
        case DECL_CONST:
            fputs("const ", out);
            print_name(out, c, d->names[decl]);
            if (type == TYPE_ID_TYPE) {
                fputs(" = ", out);
                print_type(out, c, d->values[decl]);
            } else if (type == TYPE_ID_NONE) {
                fputs(" = ?", out);
            } else {
                if (!is_untyped(type)) {
                    fputs(": ", out);
                    print_type(out, c, type);
                }
                fputs(" = ", out);
                print_constant(out, c, type, d->values[decl]);
            }
            break;
        case DECL_STRUCT:
            fputs("struct ", out);
            print_name(out, c, d->names[decl]);
            fputs(d->member_counts[decl] ? "" : " {}", out);
            print_members(out, c, decl, " { ", " }");
            break;
        case DECL_ENUM:
        case DECL_UNION:
            fputs(d->kinds[decl] == DECL_ENUM ? "enum " : "union ", out);
            print_name(out, c, d->names[decl]);
            if (c->types.arg1[type]) {
                fputs(": ", out);
                print_type(out, c, (u32)c->types.arg1[type]);
            }
            fputs(d->member_counts[decl] ? "" : " {}", out);
            print_members(out, c, decl, " { ", " }");
            break;
        case DECL_FN:
            fputs("fn ", out);
            print_name(out, c, d->names[decl]);
            fputs(d->member_counts[decl] ? "" : "()", out);
            print_members(out, c, decl, "(", ")");
            fputs(" -> ", out);
            print_type(out, c, (u32)c->types.arg1[type]);
            break;
        default: assert(false && "unknown declaration kind");
    }
    fputc('\n', out);
}

void
check_dump(FILE *out, const Checker *c)
{
    for (u32 decl = 1; decl < c->decls.count; decl++) {
        print_decl(out, c, decl);
    }
}

static void
print_bits(FILE *out, const Checker *c, u32 type, u64 bits)
{
    if (type == TYPE_ID_BOOL) {
        fputs(bits ? "true" : "false", out);
        return;
    }
    if (c->types.kinds[type] == TYPE_ENUM) {
        u32 decl = c->types.arg0[type];
        u32 first = c->decls.members[decl];
        for (u32 i = 0; i < c->decls.member_counts[decl]; i++) {
            Member m = c->members[first + i];
            if (m.value < MEMBER_NONE && exact_bits(&c->exacts, m.value) == bits) {
                fputc('.', out);
                print_name(out, c, m.name);
                return;
            }
        }
    }
    if (signed_type(c, type)) {
        fprintf(out, "%lld", (long long)bits);
    } else {
        fprintf(out, "%llu", (unsigned long long)bits);
    }
}

static void
print_operand(FILE *out, const Checker *c, const IrFunc *fn, u32 operand)
{
    if (!(operand & IR_CONST)) {
        fprintf(out, "%%%u", operand);
        return;
    }

    IrConst k = c->ir.code.consts[fn->first_const + (operand & ~IR_CONST)];
    if (c->types.kinds[k.type] == TYPE_FLOAT) {
        print_float(out, k.bits, c->types.arg0[k.type]);
    } else {
        print_bits(out, c, k.type, k.bits);
    }
}

static void
print_slot(FILE *out, const Checker *c, const IrFunc *fn, u32 slot)
{
    IrSlot s = c->ir.code.slots[fn->first_slot + slot];
    if (s.name) {
        print_name(out, c, s.name);
    } else {
        fprintf(out, "$%u", slot);
    }
}

static void
print_function(FILE *out, const Checker *c, const IrFunc *fn)
{
    const IrCode *code = &c->ir.code;
    fputs("    slots:", out);
    for (u32 i = 0; i < fn->nslots; i++) {
        fputs(i ? ", " : " ", out);
        print_slot(out, c, fn, i);
        fputs(": ", out);
        print_type(out, c, code->slots[fn->first_slot + i].type);
    }
    fputc('\n', out);

    u32 depth = 1;
    u8 openers[PARSER_MAX_DEPTH * 4];
    for (u32 i = 0; i < fn->count; i++) {
        u32 at = fn->first + i;
        u8 kind = code->kinds[at];
        u32 type = code->types[at];
        u32 a = code->as[at];
        u32 b = code->bs[at];
        depth -= kind == IR_END || kind == IR_ELSE || kind == IR_CASE;
        assert(depth < sizeof openers);
        fprintf(out, "%*s", 4 * depth, "");

        bool value = kind < IR_CALL || (kind == IR_CALL && type != TYPE_ID_VOID) ||
                     (kind == IR_END && type != TYPE_ID_NONE);
        if (value) {
            fprintf(out, "%%%u = ", i);
        }
        fputs(ir_texts[kind], out);

        switch (kind) {
            case IR_LOAD:
            case IR_ADDR:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_slot(out, c, fn, a);
                break;
            case IR_STORE:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_slot(out, c, fn, a);
                fputs(", ", out);
                print_operand(out, c, fn, b);
                break;
            case IR_LOAD_AT:
            case IR_ZERO:
            case IR_NEG:
            case IR_NEG_WRAP:
            case IR_NOT:
            case IR_BIT_NOT:
            case IR_CONVERT:
            case IR_NARROW:
            case IR_TRUNCATE:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                break;
            case IR_CALL: {
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_name(out, c, c->decls.names[a]);
                const u32 *args = code->extra + fn->first_extra + b;
                fputc('(', out);
                for (u32 j = 1; j <= args[0]; j++) {
                    fputs(j > 1 ? ", " : "", out);
                    print_operand(out, c, fn, args[j]);
                }
                fputc(')', out);
                break;
            }
            case IR_DROP:
                fputc(' ', out);
                print_operand(out, c, fn, a);
                break;
            case IR_RET:
                fputc(' ', out);
                print_type(out, c, type);
                if (!returns_nothing(c, type)) {
                    fputc(' ', out);
                    print_operand(out, c, fn, a);
                }
                break;
            case IR_BLOCK:
            case IR_LOOP: fprintf(out, " @%u", i); break;
            case IR_IF:
                fprintf(out, " @%u ", i);
                if (type != TYPE_ID_NONE) {
                    print_type(out, c, type);
                    fputc(' ', out);
                }
                print_operand(out, c, fn, a);
                break;
            case IR_ELSE:
            case IR_END:
                if (type != TYPE_ID_NONE) {
                    fputc(' ', out);
                    print_type(out, c, type);
                    if (openers[depth] == IR_IF) {
                        fputc(' ', out);
                        print_operand(out, c, fn, a);
                    }
                }
                break;
            case IR_EXIT:
                fprintf(out, " @%u", a);
                if (type != TYPE_ID_NONE) {
                    fputc(' ', out);
                    print_type(out, c, type);
                    fputc(' ', out);
                    print_operand(out, c, fn, b);
                }
                break;
            case IR_REPEAT: fprintf(out, " @%u", a); break;
            case IR_NOP: break;
            case IR_SWITCH: {
                fprintf(out, " @%u ", i);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                const u32 *x = code->extra + fn->first_extra + b;
                fputs(" [", out);
                for (u32 k = 0; k < x[0]; k++) {
                    const u32 *e = x + 2 + 5 * k;
                    u64 lo = e[0] | (u64)e[1] << 32, hi = e[2] | (u64)e[3] << 32;
                    fputs(k ? ", " : "", out);
                    print_bits(out, c, type, lo);
                    if (hi != lo) {
                        fputs("..=", out);
                        print_bits(out, c, type, hi);
                    }
                    fprintf(out, " => %u", e[4]);
                }
                if (x[1] != NO_SLOT) {
                    fprintf(out, "%s_ => %u", x[0] ? ", " : "", x[1]);
                }
                fputc(']', out);
                break;
            }
            case IR_CASE: fprintf(out, " %u", a); break;
            default:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                fputs(", ", out);
                print_operand(out, c, fn, b);
        }
        fputc('\n', out);
        if (kind == IR_BLOCK || kind == IR_LOOP || kind == IR_IF || kind == IR_SWITCH) {
            openers[depth] = kind;
        }
        depth += kind == IR_BLOCK || kind == IR_LOOP || kind == IR_IF ||
                 kind == IR_ELSE || kind == IR_SWITCH || kind == IR_CASE;
    }
}

void
check_dump_ir(FILE *out, const Checker *c)
{
    for (u32 decl = 1; decl < c->decls.count; decl++) {
        if (c->decls.funcs[decl]) {
            print_decl(out, c, decl);
            print_function(out, c, &c->ir.funcs[c->decls.funcs[decl] - 1]);
        }
    }
}

void
check_free(Checker *c)
{
    Decls *d = &c->decls;
    free(d->names);
    free(d->kinds);
    free(d->flags);
    free(d->phases);
    free(d->busy);
    free(d->files);
    free(d->firsts);
    free(d->roots);
    free(d->types);
    free(d->values);
    free(d->members);
    free(d->member_counts);
    free(d->bodies);
    free(d->funcs);

    Types *t = &c->types;
    free(t->kinds);
    free(t->arg0);
    free(t->arg1);
    free(t->pointers);
    free(t->optionals);
    free(t->slices);
    free(t->map);

    for (u32 i = 0; i <= CHECK_MAX_DEPTH + 1; i++) {
        IrCode *code = i < CHECK_MAX_DEPTH  ? &c->builders[i]
                     : i == CHECK_MAX_DEPTH ? &c->ir.code
                                            : &c->deferred;
        free(code->kinds);
        free(code->types);
        free(code->as);
        free(code->bs);
        free(code->extra);
        free(code->slots);
        free(code->consts);
    }
    free(c->builders);
    free(c->ir.funcs);

    exacts_free(&c->exacts);
    free(c->package_names);
    free(c->bound);
    free(c->bindings);
    free(c->scope);
    free(c->loops);
    free(c->breaks);
    free(c->cases);
    free(c->switches);
    free(c->member_keys);
    free(c->member_slots);
    free(c->members);
    free(c->scratch);
    free(c->lists);
    free(c->diags);
    free(c->stack);
    free(c->stack_tokens);
    free(c->claims);
}
