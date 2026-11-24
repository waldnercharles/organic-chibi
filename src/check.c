#include "check.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_MAX_DEPTH 1000
#define MEMBER_UNDEFINED 0xFFFFFFFFu
#define MEMBER_NONE 0xFFFFFFFEu

enum
{
    REF_VALUE = 1u << 31,
    REF_RESULT = 1u << 30,
    REF_NONNIL = 1u << 29,
    REF_INDEX = REF_NONNIL - 1,
};

enum
{
    OPERAND_MEMBER = 0xFFFFFE00u,
    OPERAND_RETURN,
    OPERAND_VARIADIC,
    OPERAND_REST,
    OPERAND_BACKING,
    OPERAND_LOCAL,
    OPERAND_ADDRESS,
    OPERAND_FN,
    OPERAND_BUILTIN,
    OPERAND_PACKAGE,
    OPERAND_STMT,
    OPERAND_RANGE,
    OPERAND_DISCARD,
    OPERAND_FIELD,
    OPERAND_TARGET,
    OPERAND_MARKER = 0xFFFFFF00u,
};

enum
{
    DATA_LIMIT = 1u << 28,
    GLOBALS_LIMIT = 1u << 31,
};

enum
{
    SHORT_TESTED = 0xFFFFFFFBu,
    SHORT_VALUE,
    SHORT_FOLD,
    SHORT_PASS,
    SHORT_SKIP,
};

enum
{
    FACT_PENDING = 1u << 31,
    FACT_HIDDEN = 1u << 30,
    FACT_REGION = UINT32_MAX,
    NO_TEST = UINT32_MAX,
};

enum
{
    NARROW_OFF,
    NARROW_ON,
    NARROW_NEVER,
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

static const bool takes_place[256] = {
    [NODE_ADDR] = true,
    [NODE_VALUE] = true,
    [NODE_OR_ELSE_TARGET] = true,
    [NODE_ASSIGN_TARGET] = true,
};

static const u8 decl_kinds[NNODES] = {
    [NODE_CONST_INIT] = DECL_CONST,  [NODE_CONST_TYPE_INIT] = DECL_CONST,
    [NODE_STRUCT] = DECL_STRUCT,     [NODE_ENUM] = DECL_ENUM,
    [NODE_UNION] = DECL_UNION,       [NODE_FN] = DECL_FN,
    [NODE_VAR_INIT] = DECL_VAR,      [NODE_VAR_TYPE] = DECL_VAR,
    [NODE_VAR_TYPE_INIT] = DECL_VAR,
};

static const u8 operator_classes[256] = {
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

static const u8 binary_irs[256] = {
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

static const u8 wrapping_irs[NIR_KINDS] = {
    [IR_ADD] = IR_ADD_WRAP,
    [IR_SUB] = IR_SUB_WRAP,
    [IR_MUL] = IR_MUL_WRAP,
    [IR_NEG] = IR_NEG_WRAP,
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

#define FIXED_KIND(id, kind, arg0, arg1, text) [id] = kind,
#define FIXED_ARG0(id, kind, arg0, arg1, text) [id] = arg0,
#define FIXED_ARG1(id, kind, arg0, arg1, text) [id] = arg1,
#define FIXED_TEXT(id, kind, arg0, arg1, text) [id] = text,
static const u8 fixed_kinds[NFIXED_TYPES] = { CHIBI_FIXED_TYPES(FIXED_KIND) };
static const u8 fixed_arg0[NFIXED_TYPES] = { CHIBI_FIXED_TYPES(FIXED_ARG0) };
static const u8 fixed_arg1[NFIXED_TYPES] = { CHIBI_FIXED_TYPES(FIXED_ARG1) };
static const char *const fixed_texts[NFIXED_TYPES] = { [TYPE_ID_NONE] = "?",
                                                       CHIBI_FIXED_TYPES(FIXED_TEXT) };

#define BUILTIN(name) [name - NAME_LAST_KEYWORD - 1]
static const u8 lane_vectors[NFIXED_TYPES] = {
    [TYPE_ID_U8] = TYPE_ID_U8X16,  [TYPE_ID_I8] = TYPE_ID_I8X16,
    [TYPE_ID_U16] = TYPE_ID_U16X8, [TYPE_ID_I16] = TYPE_ID_I16X8,
    [TYPE_ID_U32] = TYPE_ID_U32X4, [TYPE_ID_I32] = TYPE_ID_I32X4,
    [TYPE_ID_U64] = TYPE_ID_U64X2, [TYPE_ID_I64] = TYPE_ID_I64X2,
};

static_assert(NBUILTIN_TYPES == 19, "every builtin type name needs an entry in builtin_types");
static const u8 builtin_types[NBUILTIN_TYPES] = {
    BUILTIN(NAME_VOID) = TYPE_ID_VOID,
    BUILTIN(NAME_BOOL) = TYPE_ID_BOOL,
    BUILTIN(NAME_INT) = TYPE_ID_I64,
    BUILTIN(NAME_I8) = TYPE_ID_I8,
    BUILTIN(NAME_I16) = TYPE_ID_I16,
    BUILTIN(NAME_I32) = TYPE_ID_I32,
    BUILTIN(NAME_I64) = TYPE_ID_I64,
    BUILTIN(NAME_I128) = TYPE_ID_I128,
    BUILTIN(NAME_U8) = TYPE_ID_U8,
    BUILTIN(NAME_U16) = TYPE_ID_U16,
    BUILTIN(NAME_U32) = TYPE_ID_U32,
    BUILTIN(NAME_U64) = TYPE_ID_U64,
    BUILTIN(NAME_U128) = TYPE_ID_U128,
    BUILTIN(NAME_F32) = TYPE_ID_F32,
    BUILTIN(NAME_F64) = TYPE_ID_F64,
    BUILTIN(NAME_UINTPTR) = TYPE_ID_UINTPTR,
    BUILTIN(NAME_ERROR) = TYPE_ID_ERROR,
    BUILTIN(NAME_STRING) = TYPE_ID_STRING,
    BUILTIN(NAME_CSTRING) = TYPE_ID_CSTRING,
};

#define DIAGNOSTIC_TEXT(name, text) [name] = text,
static const char *const diagnostic_texts[NDIAGNOSTICS] = {
    CHIBI_DIAGNOSTICS(DIAGNOSTIC_TEXT)
};

#define IR_TEXT(name, text) [name] = text,
static const char *const ir_texts[NIR_KINDS] = { CHIBI_IR(IR_TEXT) };

static const Operand poison = { TYPE_ID_NONE, 0 };

static NOINLINE void *
resize(void *p, size_t count, size_t size)
{
    return realloc(p, count * size);
}

static NOINLINE COLD void
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

static bool
is_c_fn(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_FN && (c->types.arg1[type] & FN_C_CALL);
}

static bool
is_never_nil(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return k == TYPE_POINTER || k == TYPE_MANY || is_c_fn(c, type);
}

static u32
optional_type(Checker *c, u32 elem)
{
    u32 id = c->types.optionals[elem];
    if (id == 0) {
        id = add_type(c, TYPE_OPTIONAL, elem, is_never_nil(c, elem));
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
fn_type(Checker *c, u32 list, u64 ret)
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

static u64
list_u64(const Checker *c, u32 at)
{
    return c->lists[at] | (u64)c->lists[at + 1] << 32;
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
is_pointer(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return k == TYPE_POINTER || k == TYPE_MANY;
}

static bool
is_optional_pointer(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_OPTIONAL &&
           is_pointer(c, c->types.arg0[type]);
}

static bool
is_nilable(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_OPTIONAL && c->types.arg1[type];
}

static NOINLINE bool
value_elem(const Checker *c, u32 elem)
{
    if (c->types.kinds[elem] == TYPE_ENUM) {
        elem = (u32)c->types.arg1[elem];
    }
    u8 k = c->types.kinds[elem];
    return k == TYPE_BOOL || k == TYPE_FLOAT || k == TYPE_UINTPTR ||
           (k == TYPE_INT && c->types.arg0[elem] <= 64);
}

static bool
is_value_optional(const Checker *c, u32 type)
{
    return check_is_flag_optional(c, type) && value_elem(c, c->types.arg0[type]);
}

static bool
is_scalar(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return is_number(c, type) || k == TYPE_BOOL || k == TYPE_ENUM ||
           k == TYPE_POINTER || k == TYPE_MANY || is_nilable(c, type) ||
           is_c_fn(c, type);
}

static bool
is_untyped(u32 type)
{
    return type == TYPE_ID_UNTYPED_INT || type == TYPE_ID_UNTYPED_FLOAT;
}

static bool
is_context_literal(u32 type)
{
    static_assert(TYPE_ID_NIL == TYPE_ID_ENUM_LITERAL + 1, "`nil` must follow the enum literal");
    return type - TYPE_ID_ENUM_LITERAL <= 1;
}

static bool
is_array(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_ARRAY;
}

static bool
is_slice(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_SLICE;
}

static bool
is_byte_slice(const Checker *c, u32 type)
{
    return type == c->types.slices[TYPE_ID_U8];
}

static bool
is_struct(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_STRUCT;
}

static bool
is_tuple(const Checker *c, u32 type)
{
    return is_struct(c, type) && (c->decls.flags[c->types.arg0[type]] & DECL_TUPLE);
}

static bool
is_vector(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_VECTOR;
}

static u32
lane_vector(const Checker *c, u32 type)
{
    if (!is_array(c, type)) {
        return 0;
    }
    u32 elem = c->types.arg0[type];
    u32 vector = elem < NFIXED_TYPES ? lane_vectors[elem] : 0;
    return vector && c->types.arg1[type] == c->types.arg1[vector] ? vector : 0;
}

static NOINLINE u32
lane_array(Checker *c, u32 vector)
{
    return intern_type(c, TYPE_ARRAY, c->types.arg0[vector], c->types.arg1[vector]);
}

static bool
is_aggregate(const Checker *c, u32 type)
{
    static_assert(TYPE_STRUCT == TYPE_ARRAY + 1, "arrays and structs must be adjacent kinds");
    return (u8)(c->types.kinds[type] - TYPE_ARRAY) <= 1;
}

static bool
is_storable(const Checker *c, u32 type)
{
    while (is_array(c, type) || is_slice(c, type)) {
        type = c->types.arg0[type];
    }
    if (is_struct(c, type)) {
        return c->decls.flags[c->types.arg0[type]] & DECL_STORED;
    }
    return is_scalar(c, type) && !is_untyped(type);
}

static bool
is_indexable(const Checker *c, u32 type)
{
    static_assert(TYPE_ARRAY == TYPE_SLICE + 1, "arrays and slices must be adjacent kinds");
    return (u8)(c->types.kinds[type] - TYPE_SLICE) <= 1;
}

static bool
is_compound(const Checker *c, u32 type)
{
    return (u8)(c->types.kinds[type] - TYPE_SLICE) <= 2;
}

static bool
has_slow_read(const Checker *c, u32 type)
{
    static_assert(TYPE_SLICE == TYPE_OPTIONAL + 1, "optionals must precede the compound kinds");
    return (u8)(c->types.kinds[type] - TYPE_OPTIONAL) <= 3;
}

static bool
has_slow_store(const Checker *c, u32 type)
{
    return (u8)(c->types.kinds[type] - TYPE_OPTIONAL) <= 1;
}

static bool
is_held(const Checker *c, u32 type)
{
    static_assert(TYPE_SLICE == TYPE_OPTIONAL + 1, "optionals must precede the compound kinds");
    return (u8)(c->types.kinds[type] - TYPE_OPTIONAL) > 3 || is_storable(c, type);
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
        case TYPE_POINTER:
        case TYPE_MANY:
        case TYPE_FN: return 8;
        case TYPE_OPTIONAL: return is_nilable(c, type) ? 8 : 0;
        case TYPE_SLICE:
        case TYPE_VECTOR: return 16;
        case TYPE_ENUM: return check_type_size(c, (u32)t->arg1[type]);
        case TYPE_ARRAY: {
            u64 n = t->arg1[type];
            u64 size = check_type_size(c, t->arg0[type]);
            return size && n > UINT64_MAX / size ? UINT64_MAX : n * size;
        }
        case TYPE_STRUCT: return t->arg1[type];
        default: return 0;
    }
}

u32
check_type_align(const Checker *c, u32 type)
{
    while (is_array(c, type)) {
        type = c->types.arg0[type];
    }
    if (is_struct(c, type)) {
        u32 layout = c->decls.values[c->types.arg0[type]];
        return layout ? c->lists[layout + 1] : 1;
    }
    return is_slice(c, type) ? 8 : (u32)check_type_size(c, type);
}

u64
check_field_offset(const Checker *c, u32 field)
{
    return list_u64(c, field);
}

static bool
all_floats(const Checker *c, u32 type, u32 *base)
{
    while (is_array(c, type)) {
        type = c->types.arg0[type];
    }
    if (c->types.kinds[type] == TYPE_FLOAT) {
        bool same = *base == TYPE_ID_NONE || *base == type;
        *base = type;
        return same;
    }
    if (!is_struct(c, type)) {
        return false;
    }

    u32 decl = c->types.arg0[type];
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    for (u32 i = first; i < end; i++) {
        if (!all_floats(c, c->members[i].type, base)) {
            return false;
        }
    }
    return true;
}

u32
check_struct_regs(const Checker *c, u32 type)
{
    u64 size = check_type_size(c, type);
    if (size == 0) {
        return 0;
    }

    u32 base = TYPE_ID_NONE;
    if (size <= 32 && all_floats(c, type, &base) && base != TYPE_ID_NONE) {
        u64 m = check_type_size(c, base);
        if (size / m <= 4) {
            return (u32)(size / m) | REGS_FLOAT | (m == 8 ? REGS_DOUBLE : 0);
        }
    }
    return size <= 16 ? (u32)(size + 7) / 8 : 1 | REGS_POINTER;
}

u32
check_place_arg(ArgCursor *at, u32 regs, u32 size, u32 align, u32 how)
{
    u32 n = regs & REGS_COUNT;
    u32 *next = regs & REGS_FLOAT ? &at->floats : &at->ints;
    if (how != (PLACE_APPLE | PLACE_VARIADIC)) {
        if (*next + n <= 8) {
            u32 r = *next;
            *next += n;
            return r;
        }
        *next = 8;
    }

    bool packed = how == PLACE_APPLE;
    u32 slot = packed || align > 8 ? align : 8;
    u32 bytes = packed ? size : (size + 7) & ~7u;
    at->stack = (at->stack + slot - 1) & ~(slot - 1);
    u32 offset = at->stack;
    at->stack += bytes;
    return PLACE_STACK | offset;
}

static NOINLINE bool
is_passed_compound(const Checker *c, u32 type)
{
    if (!is_storable(c, type)) {
        return is_value_optional(c, type);
    }
    return is_slice(c, type) || is_nilable(c, type) ||
           (is_aggregate(c, type) && check_struct_regs(c, type));
}

static ALWAYS_INLINE bool
is_passed(const Checker *c, u32 type)
{
    return (u8)(c->types.kinds[type] - TYPE_OPTIONAL) > 3 ||
           is_passed_compound(c, type);
}

static NOINLINE void
report_unpassed(Checker *c, u32 file, u32 token, u32 type)
{
    report(c, file, token, DIAG_UNSUPPORTED, type, 0);
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

static bool
is_pair_type(const Checker *c, u32 type)
{
    return is_slice(c, type) || is_value_optional(c, type);
}

static bool
is_called_generally(const Checker *c, u32 type)
{
    return is_compound(c, type) || check_is_flag_optional(c, type);
}

static ALWAYS_INLINE bool
is_pair(const Checker *c, Operand o)
{
    return o.type < OPERAND_MEMBER && is_pair_type(c, o.type);
}

static bool
is_flagged(const Checker *c, Operand o)
{
    return o.type >= NFIXED_TYPES && o.type < OPERAND_MEMBER &&
           check_is_flag_optional(c, o.type);
}

static u32
second_type(const Checker *c, u32 type)
{
    return is_slice(c, type) ? TYPE_ID_I64 : TYPE_ID_BOOL;
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
        code->cap *= 2;
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
emit_extra(Checker *c, u8 kind, u32 type, u32 a, const u32 *words, u32 n)
{
    IrCode *code = c->body.code;
    if (code->nextra + n > code->extra_cap) {
        code->extra_cap = (code->nextra + n) * 2;
        code->extra = resize(code->extra, code->extra_cap, sizeof *code->extra);
    }
    u32 extra = code->nextra;
    memcpy(code->extra + extra, words, n * sizeof *words);
    code->nextra += n;
    return emit(c, kind, type, a, extra - c->body.first_extra);
}

static void
mark_line(Checker *c, u32 token)
{
    Body *b = &c->body;
    IrCode *code = b->code;
    u32 end = code->count - b->first;
    if (end == b->marked) {
        return;
    }

    if (code->nlines == code->lines_cap) {
        code->lines_cap *= 2;
        code->lines = resize(code->lines, code->lines_cap, sizeof *code->lines);
    }
    code->lines[code->nlines++] = (IrLine){ end, token };
    b->marked = end;
}

static NOINLINE u32
ir_const_128(Checker *c, u32 type, u64 low, u64 high)
{
    IrCode *code = c->body.code;
    if (code->nconsts + 2 > code->consts_cap) {
        code->consts_cap *= 2;
        code->consts = resize(code->consts, code->consts_cap, sizeof *code->consts);
    }
    code->consts[code->nconsts] = (IrConst){ low, type };
    code->consts[code->nconsts + 1] = (IrConst){ high, type };
    code->nconsts += 2;
    return IR_CONST | (code->nconsts - 2 - c->body.first_const);
}

static u32
ir_const_64(Checker *c, u32 type, u64 bits)
{
    assert(!check_is_int128(type));
    IrCode *code = c->body.code;
    if (code->nconsts == code->consts_cap) {
        code->consts_cap *= 2;
        code->consts = resize(code->consts, code->consts_cap, sizeof *code->consts);
    }
    code->consts[code->nconsts] = (IrConst){ bits, type };
    return IR_CONST | (code->nconsts++ - c->body.first_const);
}

static u32
ir_const(Checker *c, u32 type, u64 bits)
{
    if (check_is_int128(type)) {
        assert((int64_t)bits >= 0);
        return ir_const_128(c, type, bits, 0);
    }
    return ir_const_64(c, type, bits);
}

static u64
const_bits(const Checker *c, u32 operand)
{
    return c->body.code->consts[c->body.first_const + (operand & ~IR_CONST)].bits;
}

static u64
constant_bits(Checker *c, Operand o)
{
    if (c->types.kinds[o.type] == TYPE_FLOAT) {
        ExactStatus status;
        u64 bits = exact_float(&c->exacts, value_of(o), c->types.arg0[o.type], &status);
        assert(status == EXACT_OK);
        return bits;
    }
    return exact_bits(&c->exacts, value_of(o));
}

static u32
operand_ir(Checker *c, Operand o)
{
    assert(is_value_type(o.type) && !is_untyped(o.type) && !is_slice(c, o.type));
    assert(!check_is_flag_optional(c, o.type));
    if (!is_constant(o)) {
        return value_of(o);
    }
    if (check_is_int128(o.type)) {
        u32 v = value_of(o);
        return ir_const_128(c, o.type, exact_bits(&c->exacts, v), exact_bits_high(&c->exacts, v));
    }
    return ir_const_64(c, o.type, constant_bits(c, o));
}

static Operand
make_pair(Checker *c, u32 type, u32 first, u32 second)
{
    if (c->npairs == c->pairs_cap) {
        c->pairs_cap *= 2;
        c->pairs = resize(c->pairs, c->pairs_cap, sizeof *c->pairs);
    }
    c->pairs[c->npairs] = (Pair){ first, second };
    return (Operand){ type, REF_VALUE | c->npairs++ };
}

static Pair
pair_of(const Checker *c, Operand o)
{
    return c->pairs[value_of(o)];
}

static u32
add_slot(Checker *c, u32 name, u32 type, u32 node)
{
    IrCode *code = c->body.code;
    if (code->nslots == code->slots_cap) {
        code->slots_cap *= 2;
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

static u32
root_value(const Checker *c, Operand o)
{
    const IrCode *code = c->body.code;
    u32 v = c->body.first + value_of(o);
    while (code->kinds[v] == IR_INDEX || code->kinds[v] == IR_FIELD) {
        v = c->body.first + code->as[v];
    }
    return v;
}

static u32
place_root(const Checker *c, Operand o)
{
    if (o.type == OPERAND_LOCAL) {
        return o.ref;
    }

    u32 v = root_value(c, o);
    u8 kind = c->body.code->kinds[v];
    return kind == IR_ADDR ? c->body.code->as[v]
         : kind == IR_DATA ? DATA_ROOT
                           : NO_SLOT;
}

static u8
place_binding(const Checker *c, u32 file, Operand o)
{
    u32 slot = place_root(c, o);
    return slot != NO_SLOT ? binding_kind(c, file, slot) : NODE_NONE;
}

static NOINLINE u8
expose(Checker *c, u32 file, Operand o)
{
    u32 slot = place_root(c, o);
    if (slot >= DATA_ROOT) {
        return slot == DATA_ROOT ? NODE_CONST_INIT : NODE_NONE;
    }
    u8 binding = binding_kind(c, file, slot);
    if (binding == NODE_NONE) {
        slot_at(c, slot)->weight = WEIGHT_ADDRESSED;
    }
    return binding;
}

static NOINLINE void
refuse_root(Checker *c, u32 file, u32 token, Operand o, u8 binding, u8 diag)
{
    static_assert(DIAG_ASSIGN_CONSTANT == DIAG_ASSIGN_PARAM + 2, "assignment diagnostics must be adjacent");
    static_assert(DIAG_ADDRESS_CONSTANT == DIAG_ADDRESS_PARAM + 2, "address diagnostics must be adjacent");
    static_assert(DIAG_SLICE_CONSTANT == DIAG_SLICE_PARAM + 2, "slice diagnostics must be adjacent");
    u32 slot = place_root(c, o);
    u32 name = slot != DATA_ROOT
                 ? slot_at(c, slot)->name
                 : c->decls.names[c->body.code->bs[root_value(c, o)]];
    u8 offset = binding == NODE_PARAM ? 0 : binding == NODE_FOR_VALUE ? 1 : 2;
    report(c, file, token, diag + offset, name, 0);
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

static Operand
load_at(Checker *c, u32 type, u32 address)
{
    u32 first = emit(c, IR_LOAD_AT, type, address, 0);
    if (is_pair_type(c, type)) {
        return make_pair(c, type, first, emit(c, IR_SECOND, second_type(c, type), 0, 0));
    }
    return (Operand){ type, REF_VALUE | first };
}

static NOINLINE Operand
read_slice(Checker *c, Operand o)
{
    if (o.type == OPERAND_ADDRESS) {
        return load_at(c, place_type(c, o), value_of(o));
    }
    u32 type = slot_at(c, o.ref)->type;
    u32 first = value_of(load_local(c, o.ref));
    u32 second = value_of(load_local(c, o.ref + 1));
    return make_pair(c, type, first, second);
}

static NOINLINE void
note_unread(Checker *c, Operand place)
{
    u32 n = c->nunread;
    while (n > 0 && c->unread[n - 1].at >= c->sp) {
        n--;
    }
    if (n == c->unread_cap) {
        c->unread_cap *= 2;
        c->unread = resize(c->unread, c->unread_cap, sizeof *c->unread);
    }
    c->unread[n] = (Unread){ c->sp, place, NO_SLOT, 0 };
    c->nunread = n + 1;
    c->unread_done = c->unread_done < n ? c->unread_done : n;
}

static bool
is_nonnil(Operand o)
{
    return (o.ref & (REF_VALUE | REF_NONNIL)) == (REF_VALUE | REF_NONNIL);
}

static bool
is_narrowed(const Checker *c, u32 slot)
{
    return slot < c->body.narrowed_cap && c->body.narrowed[slot] == NARROW_ON;
}

static NOINLINE void
scan_addressed(Checker *c)
{
    u32 decl = c->body.decl;
    const ParsedFile *f = &c->files[c->decls.files[decl]];
    const u8 *kinds = f->nodes.kinds;
    u32 end = c->decls.roots[decl];
    for (u32 i = c->decls.bodies[decl] + 2; i < end; i++) {
        if (kinds[i] != NODE_ADDR || kinds[i - 1] != NODE_NAME) {
            continue;
        }
        if (c->naddressed == c->addressed_cap) {
            c->addressed_cap *= 2;
            c->addressed = resize(c->addressed, c->addressed_cap, sizeof *c->addressed);
        }
        c->addressed[c->naddressed++] = f->tokens.ids[f->nodes.tokens[i - 1]];
    }
    c->body.scanned = true;
}

static bool
narrowable(Checker *c, u32 slot)
{
    if (binding_kind(c, c->decls.files[c->body.decl], slot) != NODE_NONE) {
        return true;
    }
    if (!c->body.scanned) {
        scan_addressed(c);
    }

    u32 name = slot_at(c, slot)->name;
    for (u32 k = c->body.addressed; k < c->naddressed; k++) {
        if (c->addressed[k] == name) {
            return false;
        }
    }
    return true;
}

static bool
claim_narrow(Checker *c, u32 slot)
{
    Body *b = &c->body;
    if (slot >= b->narrowed_cap) {
        u32 cap = b->narrowed_cap;
        b->narrowed_cap = slot < 32 ? 64 : slot * 2;
        b->narrowed = resize(b->narrowed, b->narrowed_cap, 1);
        memset(b->narrowed + cap, NARROW_OFF, b->narrowed_cap - cap);
    }
    if (b->narrowed[slot] != NARROW_OFF) {
        return false;
    }
    b->narrowed[slot] = narrowable(c, slot) ? NARROW_ON : NARROW_NEVER;
    return b->narrowed[slot] == NARROW_ON;
}

static void
push_fact(Checker *c, u32 fact)
{
    if (c->nfacts == c->facts_cap) {
        c->facts_cap *= 2;
        c->facts = resize(c->facts, c->facts_cap, sizeof *c->facts);
    }
    c->facts[c->nfacts++] = fact;
}

static void
narrow(Checker *c, u32 slot)
{
    if (claim_narrow(c, slot)) {
        push_fact(c, slot);
    }
}

static void
drop_narrow(Checker *c, u32 slot)
{
    if (is_narrowed(c, slot)) {
        c->body.narrowed[slot] = NARROW_OFF;
    }
}

static void
undo_facts(Checker *c, u32 mark)
{
    while (c->nfacts > mark) {
        u32 fact = c->facts[--c->nfacts];
        if (fact < FACT_PENDING) {
            c->body.narrowed[fact] = NARROW_OFF;
        }
    }
}

static NOINLINE void
hide_facts(Checker *c)
{
    for (u32 k = c->body.facts; k < c->nfacts; k++) {
        u32 fact = c->facts[k];
        if (fact < FACT_HIDDEN && c->body.narrowed[fact] == NARROW_ON) {
            c->body.narrowed[fact] = NARROW_OFF;
            c->facts[k] = fact | FACT_HIDDEN;
        }
    }
}

static NOINLINE void
show_facts(Checker *c, u32 mark)
{
    undo_facts(c, mark);
    for (u32 k = c->body.facts; k < mark; k++) {
        u32 fact = c->facts[k];
        if ((fact & (FACT_PENDING | FACT_HIDDEN)) == FACT_HIDDEN) {
            c->body.narrowed[fact & ~FACT_HIDDEN] = NARROW_ON;
            c->facts[k] = fact & ~FACT_HIDDEN;
        }
    }
}

static NOINLINE Operand
read_optional(Checker *c, u32 slot)
{
    c->body.narrows = true;
    weigh(c, slot);
    u32 type = slot_at(c, slot)->type;
    bool narrowed = is_narrowed(c, slot);
    if (check_is_flag_optional(c, type)) {
        u32 first = emit(c, IR_LOAD, type, slot, 0);
        if (narrowed) {
            Operand o = make_pair(c, type, first, ir_const(c, TYPE_ID_BOOL, 1));
            return (Operand){ type, o.ref | REF_NONNIL };
        }
        weigh(c, slot + 1);
        return make_pair(c, type, first, emit(c, IR_LOAD, TYPE_ID_BOOL, slot + 1, 0));
    }
    if (!narrowed) {
        return emit_value(c, IR_LOAD, type, slot, 0);
    }
    u32 at = emit(c, IR_LOAD, c->types.arg0[type], slot, 0);
    return (Operand){ type, REF_VALUE | REF_NONNIL | at };
}

static NOINLINE Operand
read_compound(Checker *c, u32 slot, u32 token, u8 next)
{
    Operand o = { OPERAND_LOCAL, slot };
    u32 file = c->decls.files[c->body.decl];
    u32 type = slot_at(c, slot)->type;
    if (next == NODE_FIELD || binding_kind(c, file, slot) != NODE_NONE) {
        return o;
    }
    if (is_slice(c, type)) {
        return read_slice(c, o);
    }
    if (c->files[file].tokens.kinds[token + 1] != TK_LBRACK) {
        note_unread(c, o);
    }
    return o;
}

static Operand
read_local(Checker *c, u32 slot, u32 token, u8 next)
{
    u32 type = slot_at(c, slot)->type;
    if (has_slow_read(c, type)) {
        if (c->types.kinds[type] == TYPE_OPTIONAL) {
            return read_optional(c, slot);
        }
        return read_compound(c, slot, token, next);
    }
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

static NOINLINE void
store_pair(Checker *c, u32 slot, Operand v)
{
    u32 type = slot_at(c, slot)->type;
    if (type == TYPE_ID_NONE || v.type == TYPE_ID_NONE) {
        return;
    }

    Pair p = pair_of(c, v);
    weigh(c, slot);
    emit(c, IR_STORE, type, slot, p.first);
    weigh(c, slot + 1);
    emit(c, IR_STORE, second_type(c, type), slot + 1, p.second);
}

static void
store_at(Checker *c, u32 type, u32 address, Operand v)
{
    if (is_place(v)) {
        emit(c, IR_COPY, type, address, place_address(c, v));
    } else if (is_pair(c, v)) {
        Pair p = pair_of(c, v);
        u32 words[2] = { p.first, p.second };
        emit_extra(c, IR_STORE_PAIR, type, address, words, 2);
    } else {
        emit(c, IR_STORE_AT, type, address, operand_ir(c, v));
    }
}

static void
drop_pair(Checker *c, Operand v)
{
    Pair p = pair_of(c, v);
    if (!(p.first & IR_CONST)) {
        emit(c, IR_DROP, 0, p.first, 0);
    }
    if (!(p.second & IR_CONST)) {
        emit(c, IR_DROP, 0, p.second, 0);
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

static u32
bits_exact(Checker *c, u32 type, u64 bits)
{
    if (c->types.kinds[type] == TYPE_FLOAT) {
        return exact_from_float(&c->exacts, bits, c->types.arg0[type]);
    }
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

static Operand
value_half(Checker *c, Operand o)
{
    u32 type = c->types.arg0[o.type];
    u32 first = pair_of(c, o).first;
    if (first & IR_CONST) {
        return (Operand){ type, bits_exact(c, type, const_bits(c, first)) };
    }
    return (Operand){ type, REF_VALUE | first };
}

static Operand
narrowed_value(Checker *c, Operand o)
{
    if (!c->types.arg1[o.type]) {
        return value_half(c, o);
    }
    return (Operand){ c->types.arg0[o.type], o.ref & ~REF_NONNIL };
}

static NOINLINE Operand
unflag(Checker *c, u32 file, u32 token, Operand o)
{
    if (is_nonnil(o)) {
        return value_half(c, o);
    }
    report(c, file, token, DIAG_MAYBE_NIL, o.type, 0);
    return poison;
}

static int
compare_keys(const void *a, const void *b)
{
    u64 x = *(const u64 *)a, y = *(const u64 *)b;
    return (x > y) - (x < y);
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

static NOINLINE Operand
slice_of_place(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    u32 from = place_type(c, o);
    if (from == to) {
        return read_slice(c, o);
    }
    if (to == TYPE_ID_STRING && is_byte_slice(c, from)) {
        return (Operand){ to, read_slice(c, o).ref };
    }
    if (!is_array(c, from) || c->types.arg0[from] != c->types.arg0[to]) {
        report(c, file, token, from == TYPE_ID_STRING ? DIAG_STRING_READ_ONLY : DIAG_MISMATCH, from, to);
        return poison;
    }
    u8 binding = expose(c, file, o);
    if (binding != NODE_NONE) {
        refuse_root(c, file, token, o, binding, DIAG_SLICE_PARAM);
        return poison;
    }

    u32 n = ir_const(c, TYPE_ID_I64, c->types.arg1[from]);
    return make_pair(c, to, place_address(c, o), n);
}

static u32
early_block(const Checker *c, u32 at, Operand place)
{
    u32 lo = 0;
    u32 hi = c->nunread;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (c->unread[mid].at < at) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == c->nunread) {
        return NO_SLOT;
    }

    Unread u = c->unread[lo];
    bool same = u.at == at && u.place.type == place.type && u.place.ref == place.ref;
    bool written = u.block != NO_SLOT && u.writes != c->body.calls + c->body.blocks;
    return same && written ? u.block : NO_SLOT;
}

static void
fill_block(Checker *c, u32 block, u32 slot, u8 kind, u32 b, u32 type, Operand from)
{
    IrCode *code = c->body.code;
    u32 at = c->body.first + block;
    weigh(c, slot);
    code->kinds[at] = IR_ADDR;
    code->types[at] = pointer_type(c, slot_at(c, slot)->type);
    code->as[at] = slot;

    u32 to = block;
    if (kind != IR_NOP) {
        code->kinds[at + 1] = kind;
        code->types[at + 1] = pointer_type(c, type);
        code->as[at + 1] = block;
        code->bs[at + 1] = b;
        to = block + 1;
    }

    u32 source = value_of(from);
    if (from.type == OPERAND_LOCAL) {
        weigh(c, from.ref);
        code->kinds[at + 2] = IR_ADDR;
        code->types[at + 2] = pointer_type(c, type);
        code->as[at + 2] = from.ref;
        source = block + 2;
    }

    code->kinds[at + 3] = IR_COPY;
    code->types[at + 3] = type;
    code->as[at + 3] = to;
    code->bs[at + 3] = source;
}

static NOINLINE Operand
load_lanes(Checker *c, u32 at, Operand place, u32 vector)
{
    u32 block = c->nunread ? early_block(c, at, place) : NO_SLOT;
    if (block == NO_SLOT) {
        return emit_value(c, IR_LOAD_AT, vector, place_address(c, place), 0);
    }

    IrCode *code = c->body.code;
    u32 first = c->body.first + block;
    u32 address = value_of(place);
    if (place.type == OPERAND_LOCAL) {
        weigh(c, place.ref);
        code->kinds[first + 2] = IR_ADDR;
        code->types[first + 2] = pointer_type(c, place_type(c, place));
        code->as[first + 2] = place.ref;
        address = block + 2;
    }
    code->kinds[first + 3] = IR_LOAD_AT;
    code->types[first + 3] = vector;
    code->as[first + 3] = address;
    return (Operand){ vector, REF_VALUE | (block + 3) };
}

static NOINLINE Operand
materialize(Checker *c, u32 type, Operand v)
{
    if (v.type == TYPE_ID_NONE) {
        return poison;
    }
    Operand place = { OPERAND_LOCAL,
                      add_slot(c, 0, type, c->decls.roots[c->body.decl]) };
    emit(c, IR_STORE_AT, v.type, place_address(c, place), operand_ir(c, v));
    return place;
}

static Operand convert(Checker *c, u32 file, u32 token, Operand o, u32 to);

static bool
in_body(const Checker *c)
{
    return c->body.owner != 0;
}

static Operand
splat(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    u32 elem = c->types.arg0[to];
    if (!is_untyped(o.type) && o.type != elem &&
        ((o.ref & REF_RESULT) || !widens(c, o.type, elem))) {
        report(c, file, token, DIAG_MISMATCH, o.type, to);
        return poison;
    }
    if (!in_body(c)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }

    Operand e = convert(c, file, token, o, elem);
    if (e.type == TYPE_ID_NONE) {
        return poison;
    }
    if (is_constant(e)) {
        return (Operand){ to, value_of(e) };
    }
    return emit_value(c, IR_SPLAT, to, value_of(e), 0);
}

static bool
points_to_bytes(const Checker *c, u32 type)
{
    const Types *t = &c->types;
    u32 target = t->kinds[type] == TYPE_OPTIONAL ? t->arg0[type] : type;
    return is_pointer(c, target) && t->arg0[target] == TYPE_ID_U8;
}

static NOINLINE Operand
string_pointer(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (!points_to_bytes(c, to) && to != TYPE_ID_STRING) {
        report(c, file, token, DIAG_MISMATCH, o.type, to);
        return poison;
    }
    if (!in_body(c)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    if (to == TYPE_ID_STRING) {
        u32 bytes = intern_type(c, TYPE_MANY, TYPE_ID_U8, 0);
        u32 at = emit(c, IR_DATA, bytes, c->string_offsets[value_of(o)], 0);
        return make_pair(c, to, at, ir_const(c, TYPE_ID_I64, c->string_lengths[value_of(o)]));
    }

    bool optional = c->types.kinds[to] == TYPE_OPTIONAL;
    u32 target = optional ? c->types.arg0[to] : to;
    u32 at = emit(c, IR_DATA, target, c->string_offsets[value_of(o)], 0);
    return (Operand){ to, REF_VALUE | (optional ? REF_NONNIL : 0) | at };
}

static NOINLINE Operand
wrap_flagged(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (!in_body(c)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    if (o.type == TYPE_ID_NIL) {
        return make_pair(c, to, ir_const(c, to, 0), ir_const(c, TYPE_ID_BOOL, 0));
    }
    if (c->types.kinds[o.type] == TYPE_OPTIONAL) {
        report(c, file, token, DIAG_MISMATCH, o.type, to);
        return poison;
    }

    Operand v = convert(c, file, token, o, c->types.arg0[to]);
    if (v.type == TYPE_ID_NONE) {
        return poison;
    }
    Operand r = make_pair(c, to, operand_ir(c, v), ir_const(c, TYPE_ID_BOOL, 1));
    return (Operand){ to, r.ref | REF_NONNIL };
}

static NOINLINE Operand
convert_optional(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    const Types *t = &c->types;
    u32 from = o.type;
    if (t->kinds[from] == TYPE_OPTIONAL && is_nonnil(o)) {
        return convert(c, file, token, narrowed_value(c, o), to);
    }
    if (is_value_optional(c, to)) {
        return wrap_flagged(c, file, token, o, to);
    }
    if (!is_nilable(c, to)) {
        if (from == TYPE_ID_NIL) {
            report(c, file, token, t->kinds[to] == TYPE_OPTIONAL ? DIAG_UNSUPPORTED : DIAG_NIL_MISMATCH, 0, to);
        } else {
            report(c, file, token, t->arg0[from] == to ? DIAG_MAYBE_NIL : DIAG_MISMATCH, from, to);
        }
        return poison;
    }
    if (from == TYPE_ID_NIL) {
        return (Operand){ to, EXACT_ZERO };
    }

    u32 inner = t->arg0[to];
    if (from == inner) {
        return (Operand){ to, is_constant(o) ? o.ref : o.ref | REF_NONNIL };
    }
    if (t->kinds[inner] == TYPE_MANY && inner != TYPE_ID_CSTRING &&
        t->kinds[from] == TYPE_POINTER && is_array(c, t->arg0[from]) &&
        t->arg0[t->arg0[from]] == t->arg0[inner]) {
        u32 at = emit(c, IR_INDEX, inner, value_of(o), ir_const(c, TYPE_ID_I64, 0));
        return (Operand){ to, REF_VALUE | REF_NONNIL | at };
    }
    report(c, file, token, DIAG_MISMATCH, from, to);
    return poison;
}

static NOINLINE Operand
reshape(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    const Types *t = &c->types;
    u32 from = o.type;
    if (from == TYPE_ID_UNTYPED_STRING) {
        return string_pointer(c, file, token, o, to);
    }
    if (from == TYPE_ID_NIL || t->kinds[to] == TYPE_OPTIONAL ||
        t->kinds[from] == TYPE_OPTIONAL) {
        return convert_optional(c, file, token, o, to);
    }
    if (t->kinds[to] == TYPE_MANY && to != TYPE_ID_CSTRING &&
        t->kinds[from] == TYPE_POINTER && is_array(c, t->arg0[from]) &&
        t->arg0[t->arg0[from]] == t->arg0[to]) {
        return emit_value(c, IR_INDEX, to, value_of(o), ir_const(c, TYPE_ID_I64, 0));
    }
    if (to == TYPE_ID_STRING && is_byte_slice(c, from)) {
        return (Operand){ to, o.ref };
    }
    if (from == TYPE_ID_STRING && is_slice(c, to)) {
        report(c, file, token, DIAG_STRING_READ_ONLY, from, to);
        return poison;
    }
    if (from == TYPE_ID_CSTRING && to == intern_type(c, TYPE_MANY, TYPE_ID_U8, 0)) {
        return (Operand){ to, o.ref };
    }
    if (t->kinds[to] == TYPE_VECTOR) {
        return splat(c, file, token, o, to);
    }
    u32 vector = lane_vector(c, to);
    if (vector) {
        return materialize(c, to, from == vector ? o : splat(c, file, token, o, vector));
    }
    report(c, file, token, DIAG_MISMATCH, from, to);
    return poison;
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
    if (is_place(o) && is_slice(c, to)) {
        return slice_of_place(c, file, token, o, to);
    }
    if (!is_value_type(o.type)) {
        if (is_place(o)) {
            u32 from = place_type(c, o);
            if (from == to) {
                return o;
            }
            if (is_vector(c, to) && lane_vector(c, from) == to) {
                return load_lanes(c, NO_SLOT, o, to);
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
        if (is_value_optional(c, to)) {
            return convert_optional(c, file, token, o, to);
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

    return reshape(c, file, token, o, to);
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
    bool wide = check_is_int128(code->types[at]);
    u32 ops[2] = { code->as[at], code->bs[at] };
    for (u32 i = 0; i < 2; i++) {
        if (ops[i] & IR_CONST) {
            const IrConst *k = &code->consts[c->body.first_const + (ops[i] & ~IR_CONST)];
            u64 mask = k->bits;
            if (wide ? k[1].bits != 0 : (int64_t)mask < 0) {
                continue;
            }
            if (bits >= 64 || mask >> bits == 0) {
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
        return to_signed && (to_bits >= 64 ||
                             (int64_t)bits >= -(int64_t)(1ull << (to_bits - 1)));
    }
    u32 room = to_bits - to_signed;
    return room >= 64 || bits < (1ull << room);
}

static Operand
wrap_to(Checker *c, u32 v, u32 from, u32 to, u32 bits)
{
    if (bits > c->types.arg0[from]) {
        v = emit(c, IR_CONVERT, TYPE_ID_I8 + (u32)__builtin_ctz(bits) - 3, v, 0);
    }
    return emit_value(c, IR_TRUNCATE, to, v, 0);
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
    if (c->release_fast) {
        if (int_widens(c, o.type, backing)) {
            return emit_value(c, IR_CONVERT, to, value_of(o), 0);
        }
        return wrap_to(c, value_of(o), o.type, to, c->types.arg0[backing]);
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
    if (!fits && c->release_fast) {
        return wrap_to(c, value_of(o), backing, to, bits);
    }
    u8 kind = !fits                  ? IR_NARROW
            : widens(c, backing, to) ? IR_CONVERT
                                     : IR_TRUNCATE;
    return emit_value(c, kind, to, value_of(o), 0);
}

static NOINLINE void
refuse_cast(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    u32 from = is_place(o) ? place_type(c, o) : o.type;
    u8 diag = is_vector(c, from) || lane_vector(c, from) || lane_vector(c, to)
                ? DIAG_UNSUPPORTED
            : !is_value_type(o.type) ? DIAG_NOT_VALUE
                                     : DIAG_MISMATCH;
    report(c, file, token, diag, o.type, to);
}

static ALWAYS_INLINE Operand cast(Checker *c, u32 file, u32 token, Operand o, u32 to);

static NOINLINE Operand
cast_flagged(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (c->types.kinds[to] == TYPE_OPTIONAL && !is_nonnil(o)) {
        return convert(c, file, token, o, to);
    }
    o = unflag(c, file, token, o);
    if (o.type == TYPE_ID_NONE) {
        return poison;
    }
    return cast(c, file, token, o, to);
}

static ALWAYS_INLINE Operand
cast(Checker *c, u32 file, u32 token, Operand o, u32 to)
{
    if (o.type == TYPE_ID_NONE || to == TYPE_ID_NONE) {
        return poison;
    }
    if (is_slice(c, to)) {
        return convert(c, file, token, o, to);
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
        if (is_flagged(c, o)) {
            return cast_flagged(c, file, token, o, to);
        }
        if (o.type == TYPE_ID_NIL || o.type == TYPE_ID_UNTYPED_STRING ||
            o.type == TYPE_ID_CSTRING || to_kind == TYPE_OPTIONAL ||
            (is_value_type(o.type) && t->kinds[o.type] == TYPE_OPTIONAL)) {
            return convert(c, file, token, o, to);
        }
        if (to == TYPE_ID_CSTRING &&
            o.type == intern_type(c, TYPE_MANY, TYPE_ID_U8, 0)) {
            return (Operand){ to, o.ref };
        }
        refuse_cast(c, file, token, o, to);
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
    if (c->release_fast && is_int(c, o.type)) {
        return wrap_to(c, v, o.type, to, t->arg0[to]);
    }
    if (is_int(c, o.type) && masked(c, v, to)) {
        return emit_value(c, IR_TRUNCATE, to, v, 0);
    }
    return emit_value(c, IR_NARROW, to, v, 0);
}

static u32
peer_type(const Checker *c, u32 a, u32 b)
{
    if (is_context_literal(a) || is_context_literal(b)) {
        return is_context_literal(a) ? b : a;
    }
    if (is_untyped(a) && is_untyped(b)) {
        return a == b ? a : TYPE_ID_UNTYPED_FLOAT;
    }
    return is_untyped(a) || widens(c, a, b) || c->types.kinds[b] == TYPE_OPTIONAL
             ? b
             : a;
}

static NOINLINE u32
unify_vector(Checker *c, u32 file, u32 token, Operand *a, Operand *b)
{
    bool vector = is_vector(c, a->type);
    if (vector == is_vector(c, b->type)) {
        report(c, file, token, DIAG_MISMATCH, a->type, b->type);
        return TYPE_ID_NONE;
    }

    Operand *from = vector ? b : a;
    *from = convert(c, file, token, *from, vector ? a->type : b->type);
    return from->type;
}

static NOINLINE u32
unify_string(Checker *c, u32 file, u32 token, Operand *a, Operand *b)
{
    Operand *from = a->type == TYPE_ID_STRING ? b : a;
    if (from->type != TYPE_ID_UNTYPED_STRING) {
        return unify_vector(c, file, token, a, b);
    }
    *from = convert(c, file, token, *from, TYPE_ID_STRING);
    return from->type;
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
    if (is_untyped(from->type) || is_context_literal(from->type) ||
        (!(from->ref & REF_RESULT) && widens(c, from->type, type)) ||
        (c->types.kinds[type] == TYPE_OPTIONAL && from->type != TYPE_ID_UNTYPED_STRING)) {
        *from = convert(c, file, token, *from, type);
        return from->type;
    }
    if (a->type == TYPE_ID_STRING || b->type == TYPE_ID_STRING) {
        return unify_string(c, file, token, a, b);
    }
    return unify_vector(c, file, token, a, b);
}

static bool
applies(const Checker *c, u8 cls, u32 type)
{
    switch (cls) {
        case OP_ARITH:
        case OP_COMPARE: return is_number(c, type);
        case OP_INT:
        case OP_SHIFT: return is_int(c, type);
        case OP_EQUALITY: return is_scalar(c, type) && type != TYPE_ID_CSTRING;
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

static bool
keeps_bits(const Checker *c, u32 type)
{
    u8 k = c->types.kinds[type];
    return k == TYPE_INT || k == TYPE_UINTPTR || k == TYPE_FLOAT ||
           k == TYPE_POINTER || k == TYPE_MANY || k == TYPE_VECTOR ||
           is_c_fn(c, type) || is_nilable(c, type);
}

static bool
refuse_bitcast(Checker *c, u32 file, u32 token, u32 type)
{
    u8 k = c->types.kinds[type];
    if (k == TYPE_SLICE) {
        report(c, file, token, DIAG_BITCAST_SLICE, 0, 0);
    } else if (k == TYPE_ARRAY || k == TYPE_STRUCT ||
               (k == TYPE_OPTIONAL && !is_nilable(c, type))) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
    } else if (!keeps_bits(c, type)) {
        report(c, file, token, DIAG_BITCAST_TYPE, 0, type);
    } else {
        return false;
    }
    return true;
}

static u32
pointee_align(const Checker *c, u32 type)
{
    if (is_optional_pointer(c, type)) {
        type = c->types.arg0[type];
    }
    u8 k = c->types.kinds[type];
    u32 a = k == TYPE_POINTER || k == TYPE_MANY
              ? check_type_align(c, c->types.arg0[type])
              : 1;
    return a ? a : 1;
}

static bool
exact_can_hold(u64 bits, u32 width)
{
    u32 exponent_bits = width == 32 ? 8 : 11;
    u64 ones = (1ull << exponent_bits) - 1;
    return (bits >> (width - 1 - exponent_bits) & ones) != ones &&
           bits != 1ull << (width - 1);
}

static NOINLINE Operand
bitcast(Checker *c, u32 file, u32 token, u32 type_token, u32 to, u32 value_token, Operand o)
{
    if (o.type == TYPE_ID_NONE || to == TYPE_ID_NONE) {
        return poison;
    }
    if (is_flagged(c, o)) {
        o = unflag(c, file, value_token, o);
        if (o.type == TYPE_ID_NONE) {
            return poison;
        }
    }
    to = lane_vector(c, to) ? lane_vector(c, to) : to;
    u32 from = is_place(o) ? place_type(c, o) : o.type;
    if (is_place(o) && lane_vector(c, from)) {
        o = load_lanes(c, NO_SLOT, o, lane_vector(c, from));
        from = o.type;
    }
    if (from == TYPE_ID_ENUM_LITERAL || is_untyped(from)) {
        report(c, file, value_token, DIAG_BITCAST_UNTYPED, 0, 0);
        return poison;
    }
    if (!is_value_type(from)) {
        report(c, file, value_token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (refuse_bitcast(c, file, value_token, from) ||
        refuse_bitcast(c, file, type_token, to)) {
        return poison;
    }
    if (check_type_size(c, from) != check_type_size(c, to)) {
        report(c, file, token, DIAG_BITCAST_SIZE, from, to);
        return poison;
    }
    if (from == to) {
        return (Operand){ to, o.ref & ~REF_RESULT };
    }

    u32 need = pointee_align(c, to);
    if (is_constant(o)) {
        u64 bits = constant_bits(c, o);
        if (is_int(c, to)) {
            u32 v = is_int(c, from) ? value_of(o)
                                    : exact_int(&c->exacts, (int64_t)bits);
            return (Operand){ to, wrap(c, v, to) };
        }
        if (is_float(c, to)) {
            u32 width = c->types.arg0[to];
            bits &= width == 64 ? ~0ull : 0xFFFFFFFFull;
            if (exact_can_hold(bits, width)) {
                return (Operand){ to, exact_from_float(&c->exacts, bits, width) };
            }
        } else if (bits & (need - 1)) {
            report(c, file, value_token, DIAG_MISALIGNED, value_of(o), to);
            return poison;
        } else if (bits == 0 && is_never_nil(c, to)) {
            report(c, file, value_token, DIAG_NEVER_NIL, 0, to);
            return poison;
        } else if (bits == 0 && is_nilable(c, to)) {
            return (Operand){ to, EXACT_ZERO };
        }
        if (!in_body(c)) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return poison;
        }
        need = 1;
    }

    u32 v = emit(c, IR_BITCAST, to, operand_ir(c, o), 0);
    if (is_never_nil(c, to) && !is_never_nil(c, from) && !is_constant(o)) {
        v = emit(c, IR_CHECK_NIL, to, v, 0);
    }
    if (need > pointee_align(c, from) && !c->release_fast) {
        v = emit(c, IR_CHECK_ALIGN, to, v, need);
    }
    return (Operand){ to, REF_VALUE | v };
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
        if (c->types.kinds[type] != TYPE_ARRAY && c->types.kinds[type] != TYPE_SLICE) {
            return TYPE_ID_NONE;
        }
        type = c->types.arg0[type];
    }
    return type;
}

static u32 field_expected(const Checker *c, u32 m, u32 top);

static NOINLINE u32
target_expected(const Checker *c, u32 m, u32 top)
{
    u32 t = m;
    while (c->stack[--t].type < OPERAND_MARKER) {
    }
    u32 target = t + top - m;
    if (target >= m || !is_place(c->stack[target])) {
        return TYPE_ID_NONE;
    }
    return place_type(c, c->stack[target]);
}

static ALWAYS_INLINE u32
expected_at(const Checker *c, u32 top)
{
    u32 depth = 0;
    for (u32 m = top; m-- > 0;) {
        Operand o = c->stack[m];
        if (o.type < OPERAND_MARKER) {
            continue;
        }

        u8 kind = (u8)(o.type - OPERAND_MARKER);
        bool literal = kind == NODE_LITERAL_START || kind == NODE_ANON_LITERAL_START;
        bool array = literal && array_marker(c, m);
        if (kind == NODE_ANON_LITERAL_START && array) {
            depth++;
            top = m;
            continue;
        }
        if (kind == NODE_RETURN_START) {
            u32 type = c->body.ret == TYPE_ID_INFERRED ? TYPE_ID_NONE : c->body.ret;
            return depth ? element_of(c, type, depth) : type;
        }
        if (kind == NODE_VALUE_IF_THEN || kind == NODE_VALUE_IF_ELSE) {
            u32 then = kind == NODE_VALUE_IF_THEN ? m : m - 2;
            u32 type = c->stack[then - 1].ref;
            if (type == TYPE_ID_NONE && kind == NODE_VALUE_IF_ELSE &&
                is_place(c->stack[m - 1])) {
                type = place_type(c, c->stack[m - 1]);
            }
            return depth ? element_of(c, type, depth) : type;
        }
        if (m == 0 || (kind != NODE_VALUE && kind != NODE_CALL_ARGS && !literal)) {
            return kind == NODE_DESTRUCTURE_VALUES
                     ? element_of(c, target_expected(c, m, top), depth)
                     : TYPE_ID_NONE;
        }

        Operand below = c->stack[m - 1];
        u32 type = TYPE_ID_NONE;
        if ((literal && !array) || below.type == OPERAND_FIELD) {
            type = field_expected(c, m, top);
        } else if (kind == NODE_LITERAL_START) {
            type = below.type == TYPE_ID_TYPE ? below.ref : TYPE_ID_NONE;
        } else if (below.type == TYPE_ID_TYPE) {
            type = below.ref;
        } else if (kind == NODE_VALUE && is_place(below)) {
            type = place_type(c, below);
        } else if (kind == NODE_CALL_ARGS && below.type == OPERAND_FN) {
            u32 fn = c->decls.types[below.ref];
            const u32 *params = c->lists + c->types.arg0[fn];
            u32 index = top - m - 1;
            if ((c->types.arg1[fn] & FN_REST) && index + 1 >= params[0]) {
                type = c->types.arg0[params[params[0]]];
            } else {
                type = index < params[0] ? params[1 + index] : TYPE_ID_NONE;
            }
        }
        return depth ? element_of(c, type, depth) : type;
    }
    return TYPE_ID_NONE;
}

static NOINLINE u32
field_expected(const Checker *c, u32 m, u32 top)
{
    u32 name = NO_SLOT;
    if (c->stack[m].type == OPERAND_MARKER + NODE_VALUE) {
        name = c->stack[m - 1].ref;
        m--;
        while (c->stack[--m].type < OPERAND_MARKER) {
        }
    }

    u32 type = TYPE_ID_NONE;
    if (c->stack[m].type == OPERAND_MARKER + NODE_LITERAL_START) {
        type = c->stack[m - 1].type == TYPE_ID_TYPE ? c->stack[m - 1].ref : TYPE_ID_NONE;
    } else if (c->stack[m].type == OPERAND_MARKER + NODE_ANON_LITERAL_START) {
        type = expected_at(c, m);
    }
    if (!is_struct(c, type) || array_marker(c, m)) {
        return TYPE_ID_NONE;
    }

    u32 decl = c->types.arg0[type];
    u32 member = name != NO_SLOT ? find_member(c, decl, name)
               : top - m - 1 < c->decls.member_counts[decl]
                   ? c->decls.members[decl] + top - m - 1
                   : NO_SLOT;
    return member != NO_SLOT ? c->members[member].type : TYPE_ID_NONE;
}

static ALWAYS_INLINE u32
expected_type(const Checker *c, u32 own)
{
    return expected_at(c, c->sp - own);
}

static NOINLINE Operand
shift_lanes(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    if (!is_vector(c, a.type) || !is_int(c, b.type)) {
        bool lhs = is_int(c, a.type) || is_vector(c, a.type);
        report(c, file, token, DIAG_BAD_OPERATOR, lhs ? b.type : a.type, 0);
        return poison;
    }

    u32 elem = c->types.arg0[a.type];
    if (is_constant(b)) {
        u32 n = value_of(b);
        if (exact_sign(&c->exacts, n) < 0 || !exact_fits(&c->exacts, n, 32, false) ||
            exact_bits(&c->exacts, n) >= c->types.arg0[elem]) {
            report(c, file, token, DIAG_BAD_SHIFT, n, 0);
            return poison;
        }
    }
    if (b.type == TYPE_ID_UNTYPED_INT) {
        b = (Operand){ elem, b.ref };
    }

    u32 x = operand_ir(c, a);
    u32 y = operand_ir(c, b);
    if (c->release_fast && !is_constant(b)) {
        y = emit(c, IR_AND, b.type, y, ir_const(c, b.type, c->types.arg0[elem] - 1));
    }
    return (Operand){ a.type,
                      REF_VALUE | REF_RESULT |
                          emit(c, binary_irs[kind], a.type, x, y) };
}

static Operand
shift(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    if (!is_int(c, a.type) || !is_int(c, b.type)) {
        return shift_lanes(c, file, token, kind, a, b);
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
    if (c->release_fast && !is_constant(b)) {
        y = emit(c, IR_AND, b.type, y, ir_const(c, b.type, c->types.arg0[a.type] - 1));
    }
    return (Operand){ a.type,
                      REF_VALUE | REF_RESULT |
                          emit(c, binary_irs[kind], a.type, x, y) };
}

static NOINLINE Operand
advance(Checker *c, u32 file, u32 token, u8 kind, Operand p, Operand n)
{
    if (c->types.kinds[p.type] == TYPE_OPTIONAL) {
        if (!is_nonnil(p)) {
            report(
                c,
                file,
                token,
                is_optional_pointer(c, p.type) ? DIAG_MAYBE_NIL : DIAG_BAD_OPERATOR,
                p.type,
                0
            );
            return poison;
        }
        p = narrowed_value(c, p);
        if (c->types.kinds[p.type] != TYPE_MANY) {
            report(c, file, token, DIAG_BAD_OPERATOR, p.type, 0);
            return poison;
        }
    }
    if (kind != NODE_ADD) {
        report(c, file, token, DIAG_BAD_OPERATOR, p.type, 0);
        return poison;
    }
    if (!is_int(c, n.type)) {
        report(c, file, token, DIAG_NOT_OFFSET, n.type, 0);
        return poison;
    }
    if (check_is_int128(n.type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }

    u32 at = value_of(n);
    if (is_constant(n)) {
        if (!exact_fits(&c->exacts, at, 64, false)) {
            report(c, file, token, DIAG_INDEX_RANGE, at, p.type);
            return poison;
        }
        at = ir_const(c, TYPE_ID_I64, exact_bits(&c->exacts, at));
    }
    return emit_value(c, IR_INDEX, p.type, value_of(p), at);
}

static NOINLINE Operand
lanewise(Checker *c, u32 file, u32 token, u8 kind, u32 type, Operand a, Operand b)
{
    if (!is_vector(c, type) || kind == NODE_DIV || kind == NODE_MOD) {
        u8 diag = type == TYPE_ID_NIL ? DIAG_NIL_TYPE
                : type == TYPE_ID_STRING && operator_classes[kind] == OP_EQUALITY
                    ? DIAG_UNSUPPORTED
                    : DIAG_BAD_OPERATOR;
        report(c, file, token, diag, type, 0);
        return poison;
    }
    if (operator_classes[kind] == OP_ARITH) {
        report(c, file, token, DIAG_WRAPPING_ONLY, type, 0);
        return poison;
    }
    if (operator_classes[kind] >= OP_COMPARE) {
        static_assert(NODE_GE - NODE_EQ == NAME_GE - NAME_EQ, "comparisons must match their `simd` names");
        report(c, file, token, DIAG_LANE_COMPARE, NAME_EQ + kind - NODE_EQ, type);
        return poison;
    }

    u32 x = operand_ir(c, a);
    u32 y = operand_ir(c, b);
    return (Operand){ type, REF_VALUE | REF_RESULT | emit(c, binary_irs[kind], type, x, y) };
}

static NOINLINE u8
place_operator_diag(const Checker *c, u8 kind, u32 type)
{
    if (!is_array(c, type)) {
        return DIAG_BAD_OPERATOR;
    }
    u8 cls = operator_classes[kind];
    if (cls == OP_EQUALITY) {
        return DIAG_UNSUPPORTED;
    }
    bool lanes = cls < OP_COMPARE && kind != NODE_NOT;
    return lanes && is_number(c, c->types.arg0[type]) ? DIAG_UNSUPPORTED
                                                      : DIAG_BAD_OPERATOR;
}

static NOINLINE Operand
binary_places(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    Operand ops[2] = { a, b };
    for (u32 k = 0; k < 2; k++) {
        if (!is_place(ops[k])) {
            continue;
        }
        u32 type = place_type(c, ops[k]);
        if (type == TYPE_ID_STRING) {
            ops[k] = read_slice(c, ops[k]);
            continue;
        }
        u32 vector = lane_vector(c, type);
        if (!vector) {
            report(c, file, token, place_operator_diag(c, kind, type), type, 0);
            return poison;
        }
        ops[k] = load_lanes(c, c->sp + k, ops[k], vector);
    }
    if (!is_value_type(ops[0].type) || !is_value_type(ops[1].type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (operator_classes[kind] == OP_SHIFT) {
        return shift(c, file, token, kind, ops[0], ops[1]);
    }

    u32 type = unify(c, file, token, &ops[0], &ops[1]);
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    return lanewise(c, file, token, kind, type, ops[0], ops[1]);
}

static Operand binary(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b);

static NOINLINE Operand
present_test(Checker *c, u8 kind, Operand o)
{
    Pair p = pair_of(c, o);
    if (!(p.first & IR_CONST)) {
        emit(c, IR_DROP, 0, p.first, 0);
    }
    if (p.second & IR_CONST) {
        bool present = const_bits(c, p.second) != 0;
        u32 v = present == (kind == NODE_NE) ? EXACT_ONE : EXACT_ZERO;
        return (Operand){ TYPE_ID_BOOL, v };
    }
    if (kind == NODE_NE) {
        return (Operand){ TYPE_ID_BOOL, REF_VALUE | p.second };
    }
    return emit_value(c, IR_NOT, TYPE_ID_BOOL, p.second, 0);
}

static NOINLINE Operand
flagged_binary(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    if (operator_classes[kind] == OP_EQUALITY &&
        (a.type == TYPE_ID_NIL || b.type == TYPE_ID_NIL)) {
        return present_test(c, kind, a.type == TYPE_ID_NIL ? b : a);
    }
    if (is_flagged(c, a)) {
        a = unflag(c, file, token, a);
    }
    if (a.type != TYPE_ID_NONE && is_flagged(c, b)) {
        b = unflag(c, file, token, b);
    }
    return binary(c, file, token, kind, a, b);
}

static Operand
binary(Checker *c, u32 file, u32 token, u8 kind, Operand a, Operand b)
{
    if (a.type == TYPE_ID_NONE || b.type == TYPE_ID_NONE) {
        return poison;
    }
    if (!is_value_type(a.type) || !is_value_type(b.type)) {
        return binary_places(c, file, token, kind, a, b);
    }

    u8 cls = operator_classes[kind];
    if (a.type >= NFIXED_TYPES || b.type >= NFIXED_TYPES) {
        if (is_flagged(c, a) || is_flagged(c, b)) {
            return flagged_binary(c, file, token, kind, a, b);
        }
        if (a.type >= NFIXED_TYPES && cls == OP_ARITH &&
            (c->types.kinds[a.type] == TYPE_MANY ||
             c->types.kinds[a.type] == TYPE_OPTIONAL)) {
            return advance(c, file, token, kind, a, b);
        }
    }
    if (cls == OP_SHIFT) {
        return shift(c, file, token, kind, a, b);
    }

    u32 type = unify(c, file, token, &a, &b);
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    if (!applies(c, cls, type)) {
        return lanewise(c, file, token, kind, type, a, b);
    }
    if (is_constant(a) && is_constant(b)) {
        return fold(c, file, token, kind, type, a, b);
    }

    u8 ir = binary_irs[kind];
    if (c->release_fast && wrapping_irs[ir] && is_int(c, type)) {
        ir = wrapping_irs[ir];
    }
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

static NOINLINE Operand
unary_lanes(Checker *c, u32 file, u32 token, u8 kind, Operand a)
{
    if (kind == NODE_NOT) {
        report(c, file, token, DIAG_NOT_BOOL, a.type, 0);
        return poison;
    }
    if (kind == NODE_NEG) {
        report(c, file, token, DIAG_WRAPPING_ONLY, a.type, 0);
        return poison;
    }

    u8 ir = kind == NODE_NEG_WRAP ? IR_NEG_WRAP : IR_BIT_NOT;
    return (Operand){ a.type, REF_VALUE | REF_RESULT | emit(c, ir, a.type, operand_ir(c, a), 0) };
}

static NOINLINE Operand
unary_place(Checker *c, u32 file, u32 token, u8 kind, Operand a)
{
    u32 type = place_type(c, a);
    u32 vector = lane_vector(c, type);
    if (!vector) {
        report(c, file, token, place_operator_diag(c, kind, type), type, 0);
        return poison;
    }
    return unary_lanes(c, file, token, kind, load_lanes(c, c->sp, a, vector));
}

static ALWAYS_INLINE Operand unary(Checker *c, u32 file, u32 token, u8 kind, Operand a);

static NOINLINE Operand
refuse_unary(Checker *c, u32 file, u32 token, u8 kind, Operand a)
{
    if (!is_flagged(c, a)) {
        report(c, file, token, kind == NODE_NOT ? DIAG_NOT_BOOL : DIAG_BAD_OPERATOR, a.type, 0);
        return poison;
    }
    a = unflag(c, file, token, a);
    if (a.type == TYPE_ID_NONE) {
        return poison;
    }
    return unary(c, file, token, kind, a);
}

static ALWAYS_INLINE Operand
unary(Checker *c, u32 file, u32 token, u8 kind, Operand a)
{
    if (a.type == TYPE_ID_NONE) {
        return poison;
    }
    if (is_place(a)) {
        return unary_place(c, file, token, kind, a);
    }
    if (!is_value_type(a.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }

    if (is_vector(c, a.type)) {
        return unary_lanes(c, file, token, kind, a);
    }

    if (kind == NODE_NOT) {
        if (a.type != TYPE_ID_BOOL) {
            return refuse_unary(c, file, token, kind, a);
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
        return refuse_unary(c, file, token, kind, a);
    }

    if (!is_constant(a)) {
        u8 ir = kind == NODE_NEG      ? IR_NEG
              : kind == NODE_NEG_WRAP ? IR_NEG_WRAP
                                      : IR_BIT_NOT;
        if (c->release_fast && wrapping_irs[ir] && is_int(c, a.type)) {
            ir = wrapping_irs[ir];
        }
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

static u32
contained_decl(const Checker *c, u32 type)
{
    const Types *t = &c->types;
    while (t->kinds[type] == TYPE_ARRAY || t->kinds[type] == TYPE_OPTIONAL ||
           t->kinds[type] == TYPE_ERROR_UNION) {
        type = t->arg0[type];
    }
    if (t->kinds[type] == TYPE_STRUCT || t->kinds[type] == TYPE_UNION ||
        t->kinds[type] == TYPE_ENUM) {
        return t->arg0[type];
    }
    return 0;
}

static void
claim_contained(Checker *c, u32 type, u32 file, u32 token)
{
    u32 decl = contained_decl(c, type);
    if (decl) {
        need(c, decl, file, token);
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
member_default(Checker *c, u32 file, u32 token, Operand o)
{
    if (o.type != TYPE_ID_NONE && is_aggregate(c, o.type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return MEMBER_NONE;
    }
    return member_value(o);
}

static u32
default_value(Checker *c, u32 file, u32 token, Operand v, u32 type)
{
    if (v.type == TYPE_ID_UNDEFINED) {
        return MEMBER_UNDEFINED;
    }
    return member_default(c, file, token, convert(c, file, token, v, type));
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
    if (v.type == TYPE_ID_NIL) {
        report(c, file, token, DIAG_NIL_TYPE, 0, 0);
        return TYPE_ID_NONE;
    }
    if (v.type == TYPE_ID_UNTYPED_STRING) {
        return TYPE_ID_STRING;
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
    if (v.type == TYPE_ID_NIL) {
        report(c, file, token, DIAG_NIL_TYPE, 0, 0);
        return TYPE_ID_NONE;
    }
    return v.type == TYPE_ID_UNTYPED_INT    ? TYPE_ID_I64
         : v.type == TYPE_ID_UNTYPED_FLOAT  ? TYPE_ID_F32
         : v.type == TYPE_ID_UNTYPED_STRING ? TYPE_ID_STRING
         : is_vector(c, v.type)             ? lane_array(c, v.type)
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

static bool
has_zero(const Checker *c, u32 type)
{
    while (is_array(c, type)) {
        type = c->types.arg0[type];
    }
    switch (c->types.kinds[type]) {
        case TYPE_POINTER:
        case TYPE_MANY:
        case TYPE_FN: return false;
        case TYPE_ENUM: return enum_has(c, type, order_key(c, type, 0));
        case TYPE_STRUCT:
            return !(c->decls.flags[c->types.arg0[type]] & DECL_NO_ZERO);
        default: return true;
    }
}

static bool
has_defaults(const Checker *c, u32 type)
{
    while (is_array(c, type)) {
        type = c->types.arg0[type];
    }
    return is_struct(c, type) && (c->decls.flags[c->types.arg0[type]] & DECL_DEFAULTS);
}

static bool
sets_default(const Checker *c, Member m)
{
    return m.value < MEMBER_NONE
             ? exact_sign(&c->exacts, m.value) != 0
             : m.value == MEMBER_NONE && has_defaults(c, m.type);
}

static bool
has_layout(const Checker *c, u32 type)
{
    while (is_array(c, type)) {
        type = c->types.arg0[type];
    }
    return is_slice(c, type) || is_storable(c, type);
}

static void
lay_out_struct(Checker *c, u32 decl)
{
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    u32 layout = reserve_list(c, 1 + 2 * (end - first));
    u64 size = 0;
    u32 align = 1;
    u16 flags = DECL_STORED;
    for (u32 i = first; i < end; i++) {
        Member m = c->members[i];
        if (!has_layout(c, m.type)) {
            flags &= (u16)~DECL_STORED;
        }
        flags |= sets_default(c, m) ? DECL_DEFAULTS : 0;
        flags |= m.value == MEMBER_NONE && !has_zero(c, m.type) ? DECL_NO_ZERO : 0;

        u32 a = check_type_align(c, m.type);
        a = a ? a : 1;
        align = a > align ? a : align;
        size = size > UINT64_MAX - a ? UINT64_MAX : (size + a - 1) & ~(u64)(a - 1);
        u32 at = layout + 2 + 2 * (i - first);
        c->lists[at] = (u32)size;
        c->lists[at + 1] = (u32)(size >> 32);
        u64 n = check_type_size(c, m.type);
        size = n > UINT64_MAX - size ? UINT64_MAX : size + n;
    }

    size = size > UINT64_MAX - align ? UINT64_MAX
                                     : (size + align - 1) & ~(u64)(align - 1);
    c->types.arg1[c->decls.types[decl]] = size;
    c->lists[layout + 1] = align;
    c->decls.values[decl] = layout;
    c->decls.flags[decl] |= flags;
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
    index_members(c, decl, first);
    lay_out_struct(c, decl);
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

static void *
resize_zeroed(void *p, u32 old, u32 cap, size_t size)
{
    u8 *q = resize(p, cap, size);
    memset(q + (size_t)old * size, 0, (size_t)(cap - old) * size);
    return q;
}

static NOINLINE void
reserve_decl(Checker *c)
{
    Decls *d = &c->decls;
    u32 old = d->cap;
    u32 cap = 2 * old;
    d->names = resize_zeroed(d->names, old, cap, sizeof *d->names);
    d->kinds = resize_zeroed(d->kinds, old, cap, sizeof *d->kinds);
    d->flags = resize_zeroed(d->flags, old, cap, sizeof *d->flags);
    d->phases = resize_zeroed(d->phases, old, cap, sizeof *d->phases);
    d->busy = resize_zeroed(d->busy, old, cap, sizeof *d->busy);
    d->files = resize_zeroed(d->files, old, cap, sizeof *d->files);
    d->firsts = resize_zeroed(d->firsts, old, cap, sizeof *d->firsts);
    d->roots = resize_zeroed(d->roots, old, cap, sizeof *d->roots);
    d->types = resize_zeroed(d->types, old, cap, sizeof *d->types);
    d->values = resize_zeroed(d->values, old, cap, sizeof *d->values);
    d->members = resize_zeroed(d->members, old, cap, sizeof *d->members);
    d->member_counts = resize_zeroed(d->member_counts, old, cap, sizeof *d->member_counts);
    d->bodies = resize_zeroed(d->bodies, old, cap, sizeof *d->bodies);
    d->funcs = resize_zeroed(d->funcs, old, cap, sizeof *d->funcs);
    d->cap = cap;
}

static u64
tuple_key(const Checker *c, u32 first, u32 n)
{
    u64 h = n * 0x9E3779B97F4A7C15ull;
    for (u32 i = first; i < first + n; i++) {
        h = (h ^ c->members[i].type) * 0xFF51AFD7ED558CCDull;
    }
    return h ^ h >> 32;
}

static NOINLINE void
grow_tuple_map(Checker *c, u32 cap)
{
    u32 *map = calloc(cap, sizeof *map);
    for (u32 i = 0; i <= c->tuple_mask; i++) {
        u32 type = c->tuple_map[i];
        if (type) {
            u32 decl = c->types.arg0[type];
            u32 n = c->decls.member_counts[decl];
            u32 j = (u32)tuple_key(c, c->decls.members[decl], n) & (cap - 1);
            while (map[j]) {
                j = (j + 1) & (cap - 1);
            }
            map[j] = type;
        }
    }

    free(c->tuple_map);
    c->tuple_map = map;
    c->tuple_mask = cap - 1;
}

static bool
same_tuple(const Checker *c, u32 type, u32 first, u32 n)
{
    u32 decl = c->types.arg0[type];
    if (c->decls.member_counts[decl] != n) {
        return false;
    }
    const Member *m = c->members + c->decls.members[decl];
    for (u32 i = 0; i < n; i++) {
        if (m[i].type != c->members[first + i].type) {
            return false;
        }
    }
    return true;
}

static u32
add_tuple(Checker *c, u32 file, u32 node, u32 first, u32 n)
{
    Decls *d = &c->decls;
    if (d->count == d->cap) {
        reserve_decl(c);
    }

    u32 decl = d->count++;
    d->kinds[decl] = DECL_STRUCT;
    d->flags[decl] = DECL_TUPLE;
    d->files[decl] = file;
    d->roots[decl] = node;
    d->members[decl] = first;
    d->member_counts[decl] = n;
    d->types[decl] = add_type(c, TYPE_STRUCT, decl, 0);
    d->phases[decl] = PHASE_DECLARED;
    return d->types[decl];
}

static u32
tuple_type(Checker *c, u32 file, u32 node, u32 first, u32 n)
{
    if ((c->tuple_count + 1) * 2 > c->tuple_mask + 1) {
        grow_tuple_map(c, (c->tuple_mask + 1) * 2);
    }

    u32 mask = c->tuple_mask;
    for (u32 i = (u32)tuple_key(c, first, n) & mask;; i = (i + 1) & mask) {
        u32 type = c->tuple_map[i];
        if (type == 0) {
            type = add_tuple(c, file, node, first, n);
            c->tuple_map[i] = type;
            c->tuple_count++;
            return type;
        }
        if (same_tuple(c, type, first, n)) {
            c->nmembers = first;
            return type;
        }
    }
}

static NOINLINE u32
close_tuple_type(Checker *c, u32 file, u32 node, u32 token)
{
    u32 m = list_start(c, NODE_TUPLE_START);
    u32 n = c->sp - m - 1;
    if (c->files[file].nodes.kinds[node] == NODE_RESULTS) {
        c->decls.flags[c->body.decl] |= DECL_RESULTS;
        if (n == 1) {
            c->sp = m;
            return type_operand(c, file, c->stack_tokens[m + 1], c->stack[m + 1]);
        }
    }
    bool ok = n >= 2;
    if (!ok) {
        report(c, file, token, DIAG_TUPLE_SIZE, 0, 0);
    }
    bool deferred = false;
    for (u32 i = m + 1; i < c->sp; i++) {
        u32 t = type_operand(c, file, c->stack_tokens[i], c->stack[i]);
        u32 decl = contained_decl(c, t);
        deferred = deferred || c->decls.busy[decl];
        if (decl && !deferred) {
            need(c, decl, file, c->stack_tokens[i]);
        }
        c->stack[i] = (Operand){ TYPE_ID_TYPE, t };
        ok = ok && t != TYPE_ID_NONE;
    }

    u32 first = c->nmembers;
    reserve_members(c, n);
    for (u32 i = 0; ok && i < n; i++) {
        c->members[c->nmembers++] = (Member){ .type = c->stack[m + 1 + i].ref,
                                              .value = MEMBER_NONE };
    }
    c->sp = m;
    if (!ok) {
        return TYPE_ID_NONE;
    }

    u32 type = tuple_type(c, file, node, first, n);
    u32 decl = c->types.arg0[type];
    if (!deferred && c->decls.phases[decl] < PHASE_RESOLVED) {
        lay_out_struct(c, decl);
        c->decls.phases[decl] = PHASE_RESOLVED;
    }
    return type;
}

static NOINLINE void
finish_tuple(Checker *c, u32 decl)
{
    Decls *d = &c->decls;
    u32 file = d->files[decl];
    u32 token = c->files[file].nodes.tokens[d->roots[decl]];
    for (u32 i = 0; i < d->member_counts[decl]; i++) {
        claim_contained(c, c->members[d->members[decl] + i].type, file, token);
    }
    lay_out_struct(c, decl);
    d->phases[decl] = PHASE_RESOLVED;
}

static bool
holds_128_bit(const Checker *c, u32 type, bool ints)
{
    while (is_array(c, type)) {
        type = c->types.arg0[type];
    }
    if (!is_struct(c, type) || !is_storable(c, type) || check_type_align(c, type) < 16) {
        return ints && check_is_int128(type);
    }

    u32 decl = c->types.arg0[type];
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    for (u32 i = first; i < end; i++) {
        if (holds_128_bit(c, c->members[i].type, ints)) {
            return true;
        }
    }
    return false;
}

static void
refuse_non_c(Checker *c, u32 file, u32 token, u32 type, u8 not_c)
{
    if (is_slice(c, type) || is_array(c, type) || is_tuple(c, type) ||
        check_is_flag_optional(c, type)) {
        report(c, file, token, not_c, 0, type);
    } else if (c->types.kinds[type] == TYPE_FN && !(c->types.arg1[type] & FN_C_CALL)) {
        report(c, file, token, DIAG_C_FN_TYPE, 0, type);
    } else if (holds_128_bit(c, type, true)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
    }
}

static NOINLINE u32
c_fn_type(Checker *c, u32 file, u32 token, u32 fn)
{
    assert(c->types.kinds[fn] == TYPE_FN);
    const u32 *params = c->lists + c->types.arg0[fn];
    for (u32 i = 1; i <= params[0]; i++) {
        refuse_non_c(c, file, token, params[i], DIAG_NOT_C);
    }
    refuse_non_c(c, file, token, (u32)c->types.arg1[fn], DIAG_NOT_C);
    return intern_type(c, TYPE_FN, c->types.arg0[fn], c->types.arg1[fn] | FN_C_CALL);
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
        c->scope_cap *= 2;
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

static ALWAYS_INLINE u32
local_type(Checker *c, u32 file, u32 token, u32 type)
{
    if (!is_held(c, type) && !is_value_optional(c, type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return TYPE_ID_NONE;
    }
    return type;
}

static NOINLINE void
declare_optional(Checker *c, u32 slot, Operand v)
{
    if (v.type == TYPE_ID_UNDEFINED) {
        return;
    }
    store_local(c, slot, v);
    if (is_nonnil(v)) {
        narrow(c, slot);
    }
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
        if (has_slow_store(c, type) && is_pair_type(c, type)) {
            add_slot(c, name, second_type(c, type), node);
            if (v.type != TYPE_ID_UNDEFINED) {
                store_pair(c, slot, v);
            }
            if (is_nonnil(v)) {
                narrow(c, slot);
            }
        } else if (has_slow_store(c, type)) {
            declare_optional(c, slot, v);
        } else if (is_place(v)) {
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
        c->loops_cap *= 2;
        c->loops = resize(c->loops, c->loops_cap, sizeof *c->loops);
    }
    loop.before = c->body.reachable;
    loop.facts = c->nfacts;
    loop.defers = c->ndefers;
    c->loops[c->nloops++] = loop;
}

static NOINLINE Operand
not_bool(Checker *c, u32 file, u32 token, Operand o)
{
    if (is_flagged(c, o)) {
        o = unflag(c, file, token, o);
        if (o.type == TYPE_ID_NONE || o.type == TYPE_ID_BOOL) {
            return o;
        }
    }
    report(c, file, token, is_value_type(o.type) ? DIAG_NOT_BOOL : DIAG_NOT_VALUE, o.type, 0);
    return poison;
}

static u32
condition(Checker *c, u32 file, u32 token, Operand cond)
{
    if (cond.type != TYPE_ID_NONE && cond.type != TYPE_ID_BOOL) {
        cond = not_bool(c, file, token, cond);
    }
    if (cond.type != TYPE_ID_BOOL) {
        return ir_const(c, TYPE_ID_BOOL, 0);
    }
    return operand_ir(c, cond);
}

static void
drop_unread(Checker *c, u32 at)
{
    while (c->nunread > 0 && c->unread[c->nunread - 1].at >= at) {
        c->nunread--;
    }
    c->unread_done = c->unread_done < c->nunread ? c->unread_done : c->nunread;
}

static bool
slot_may_change(Checker *c, u32 slot)
{
    if (slot >= DATA_ROOT) {
        return slot == NO_SLOT;
    }
    u32 file = c->decls.files[c->body.decl];
    return slot_at(c, slot)->name != 0 && binding_kind(c, file, slot) == NODE_NONE;
}

static NOINLINE u32
copy_root(Checker *c, Operand v, u32 type, u32 *copies, bool writes)
{
    u32 slot = writes ? NO_SLOT : place_root(c, v);
    if (slot_may_change(c, slot)) {
        *copies += (u32)((check_type_size(c, type) + 7) & ~(u64)7);
    }
    return slot;
}

static bool
may_change(Checker *c, Operand o)
{
    return slot_may_change(c, place_root(c, o));
}

static NOINLINE void
reserve_copies(Checker *c, u32 below)
{
    drop_unread(c, c->sp);
    u32 writes = c->body.calls + c->body.blocks;
    u32 k = c->unread_done;
    for (; k < c->nunread && c->unread[k].at < below; k++) {
        Unread *u = &c->unread[k];
        Operand o = c->stack[u->at];
        if (o.type != u->place.type || o.ref != u->place.ref || !may_change(c, o)) {
            continue;
        }
        u->block = emit(c, IR_NOP, 0, 0, 0);
        emit(c, IR_NOP, 0, 0, 0);
        emit(c, IR_NOP, 0, 0, 0);
        emit(c, IR_NOP, 0, 0, 0);
        u->writes = writes;
    }
    c->unread_done = k;
}

static NOINLINE Operand
promote(Checker *c, u32 file, u32 token, Operand o)
{
    if (is_flagged(c, o)) {
        o = unflag(c, file, token, o);
        if (o.type == TYPE_ID_NONE) {
            return poison;
        }
    }
    if (o.type == TYPE_ID_UNTYPED_INT) {
        u32 v = value_of(o);
        u32 to = int_fits(c, TYPE_ID_I32, v) ? TYPE_ID_I32
               : int_fits(c, TYPE_ID_I64, v) ? TYPE_ID_I64
                                             : TYPE_ID_U64;
        return convert(c, file, token, o, to);
    }
    if (o.type == TYPE_ID_UNTYPED_FLOAT || o.type == TYPE_ID_F32) {
        return cast(c, file, token, o, TYPE_ID_F64);
    }
    if (o.type == TYPE_ID_UNTYPED_STRING) {
        return convert(c, file, token, o, intern_type(c, TYPE_MANY, TYPE_ID_U8, 0));
    }

    u32 type = is_place(o) ? place_type(c, o) : o.type;
    if (!is_value_type(type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (is_place(o) || !is_scalar(c, type)) {
        report(c, file, token, DIAG_NOT_VARIADIC, 0, type);
        return poison;
    }
    if (check_is_int128(type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    return o;
}

static NOINLINE void
note_stack_args(Checker *c, u32 list, u32 nargs, u32 first)
{
    u32 n = c->lists[list];
    ArgCursor at = { first, 0, 0 };
    for (u32 i = 0; i < n; i++) {
        u32 type = c->lists[list + 1 + i];
        bool flagged = is_value_optional(c, type);
        if (flagged) {
            type = c->types.arg0[type];
        }
        bool wide = check_is_int128(type);
        u32 regs = is_slice(c, type)         ? 2
                 : is_aggregate(c, type)     ? check_struct_regs(c, type)
                 : is_float(c, type) || wide ? 1 | REGS_FLOAT
                                             : 1;
        u32 size = regs & REGS_POINTER ? 8 : (u32)check_type_size(c, type);
        check_place_arg(&at, regs, size, wide ? 16 : 8, 0);
        if (flagged) {
            check_place_arg(&at, 1, 1, 8, 0);
        }
    }

    u32 bytes = at.stack + 8 * (nargs > n ? nargs - n : 0);
    if (bytes > c->body.stack_args) {
        c->body.stack_args = bytes;
    }
}

static u32
enclosing_slot(const Checker *c, u32 file, u32 end, u32 base)
{
    u8 next = c->files[file].nodes.kinds[end + 1];
    if (next == NODE_VALUE_IF_ELSE) {
        assert(c->stack[base - 1].type == OPERAND_MARKER + NODE_VALUE_IF_THEN);
        return c->stack[base - 5].ref;
    }
    if (next == NODE_VALUE_IF || next == NODE_VALUE_IF_ITEM) {
        assert(c->stack[base - 1].type == OPERAND_MARKER + NODE_VALUE_IF_ELSE);
        bool held = c->stack[base - 1].ref != 0 &&
                    c->stack[base - 2].type == OPERAND_LOCAL;
        return held ? c->stack[base - 2].ref : NO_SLOT;
    }
    return NO_SLOT;
}

static u32
result_slot(Checker *c, u32 file, u32 node, u32 base, u32 type)
{
    u32 slot = enclosing_slot(c, file, node, base);
    return slot != NO_SLOT && slot_at(c, slot)->type == type
             ? slot
             : add_slot(c, 0, type, node);
}

static bool
fill_array(Checker *c, u32 file, u32 first, u32 n, u32 slot, u32 elem)
{
    bool ok = true;
    Operand array = { OPERAND_LOCAL, slot };
    u32 vector = lane_vector(c, elem);
    for (u32 k = 0; k < n; k++) {
        Operand item = c->stack[first + k];
        u32 to = vector && !is_place(item) ? vector : elem;
        Operand x = convert(c, file, c->stack_tokens[first + k], item, to);
        if (x.type == TYPE_ID_NONE) {
            ok = false;
            continue;
        }

        u32 index = ir_const(c, TYPE_ID_I64, k);
        u32 block = c->nunread && is_place(x) && is_aggregate(c, elem)
                      ? early_block(c, first + k, x)
                      : NO_SLOT;
        if (block != NO_SLOT) {
            fill_block(c, block, slot, IR_INDEX, index, elem, x);
            continue;
        }
        u32 address = emit(c, IR_INDEX, pointer_type(c, elem), place_address(c, array), index);
        store_at(c, to, address, x);
    }
    return ok;
}

static NOINLINE Operand
call_general(Checker *c, u32 file, u32 node, u32 token, u32 decl, u32 first_arg, u32 nargs)
{
    const Decls *d = &c->decls;
    u32 fn = d->types[decl];
    u32 ret = (u32)c->types.arg1[fn];
    bool to_c = c->types.arg1[fn] & FN_C_CALL;
    if (!is_passed(c, ret)) {
        report_unpassed(c, file, token, ret);
        return poison;
    }

    u32 list = c->types.arg0[fn];
    u32 n = c->lists[list];
    u32 total = nargs > n ? nargs : n;

    IrCode *code = c->body.code;
    if (code->nextra + 2 * total + 2 > code->extra_cap) {
        code->extra_cap = (code->nextra + 2 * total + 2) * 2;
        code->extra = resize(code->extra, code->extra_cap, sizeof *code->extra);
    }
    u32 extra = code->nextra;
    code->nextra += 2 * total + 2;
    u32 relative = extra - c->body.first_extra;

    bool ok = true;
    u32 w = extra + 1;
    u32 copies = 0;
    u32 used[2] = { to_c ? 0 : FIRST_INT_ARG, 0 };
    for (u32 i = 0; i < n; i++) {
        u32 type = c->lists[list + 1 + i];
        if (!is_passed(c, type)) {
            report_unpassed(c, file, token, type);
            return poison;
        }
        Operand v = i < nargs
                      ? convert(c, file, c->stack_tokens[first_arg + i], c->stack[first_arg + i], type)
                      : (Operand){ type, c->members[d->members[decl] + i].value };
        ok &= v.type != TYPE_ID_NONE;
        if (!ok) {
            continue;
        }
        if (is_pair_type(c, type)) {
            Pair p = pair_of(c, v);
            code->extra[w++] = p.first;
            code->extra[w++] = p.second;
            used[0]++;
            used[!is_slice(c, type) && is_float(c, c->types.arg0[type])]++;
        } else if (is_aggregate(c, type)) {
            u32 block = c->nunread && i < nargs
                          ? early_block(c, first_arg + i, c->stack[first_arg + i])
                          : NO_SLOT;
            if (block != NO_SLOT) {
                u32 copy = add_slot(c, 0, type, node);
                fill_block(c, block, copy, IR_NOP, 0, type, v);
                v = (Operand){ OPERAND_LOCAL, copy };
            }
            u32 regs = check_struct_regs(c, type);
            u32 root = regs & REGS_POINTER ? copy_root(c, v, type, &copies, to_c) : 0;
            u32 address = place_address(c, v);
            code->extra[w++] = emit(c, IR_LOAD_AT, type, address, root);
            used[(regs & REGS_FLOAT) != 0] += regs & REGS_COUNT;
        } else {
            code->extra[w++] = operand_ir(c, v);
            used[is_float(c, type) || check_is_int128(type)]++;
        }
    }
    for (u32 i = n; i < nargs; i++) {
        Operand v = promote(c, file, c->stack_tokens[first_arg + i], c->stack[first_arg + i]);
        ok &= v.type != TYPE_ID_NONE;
        if (ok) {
            code->extra[w++] = operand_ir(c, v);
        }
    }
    if (!ok) {
        return poison;
    }
    if (used[0] > 8 || used[1] > 8 || nargs > n) {
        note_stack_args(c, list, nargs, to_c ? 0 : FIRST_INT_ARG);
    }

    code->extra[extra] = w - extra - 1;
    c->body.copies = copies > c->body.copies ? copies : c->body.copies;
    u32 slot = 0;
    if (is_aggregate(c, ret)) {
        slot = result_slot(c, file, node, first_arg - 2, ret);
        weigh(c, slot);
        code->extra[w++] = slot;
    }
    if (code->nextra == extra + 2 * total + 2) {
        code->nextra = w;
    }

    if (c->nunread != c->unread_done) {
        reserve_copies(c, first_arg - 2);
    }
    u32 i = emit(c, to_c ? IR_CALL_C : IR_CALL, ret, decl, relative);
    c->body.calls++;
    drop_unread(c, first_arg - 2);
    if (is_pair_type(c, ret)) {
        return make_pair(c, ret, i, emit(c, IR_SECOND, second_type(c, ret), 0, 0));
    }
    if (is_aggregate(c, ret)) {
        return (Operand){ OPERAND_LOCAL, slot };
    }
    return ret == TYPE_ID_VOID ? (Operand){ TYPE_ID_VOID, 0 }
                               : (Operand){ ret, REF_VALUE | i };
}

static ALWAYS_INLINE Operand
call_fn(Checker *c, u32 file, u32 node, u32 token, u32 decl, u32 first_arg, u32 nargs)
{
    const Decls *d = &c->decls;
    u32 fn = d->types[decl];
    u32 ret = (u32)c->types.arg1[fn];
    if (ret == TYPE_ID_INFERRED || ret == TYPE_ID_NONE) {
        return poison;
    }
    bool to_c = c->types.arg1[fn] & FN_C_CALL;

    u32 list = c->types.arg0[fn];
    u32 n = c->lists[list];
    if (c->types.arg1[fn] & FN_REST) {
        u32 type = c->lists[list + n];
        if (nargs < n - 1) {
            report(c, file, token, DIAG_ARG_COUNT_VARIADIC, n - 1, nargs);
            return poison;
        }
        if (!is_passed(c, type)) {
            report_unpassed(c, file, token, type);
            return poison;
        }

        u32 rest = first_arg + n - 1;
        u32 elem = c->types.arg0[type];
        u32 slot = add_slot(c, 0, intern_type(c, TYPE_ARRAY, elem, nargs - n + 1), node);
        if (!fill_array(c, file, rest, nargs - n + 1, slot, elem)) {
            return poison;
        }
        c->stack[rest] = (Operand){ OPERAND_LOCAL, slot };
        nargs = n;
    }
    u32 defaults = 0;
    while (defaults < n &&
           c->members[d->members[decl] + n - 1 - defaults].value < MEMBER_NONE) {
        defaults++;
    }
    if (nargs > n || nargs < n - defaults) {
        bool variadic = c->types.arg1[fn] & FN_VARIADIC;
        if (!variadic || nargs < n - defaults) {
            u8 kind = variadic ? DIAG_ARG_COUNT_VARIADIC : DIAG_ARG_COUNT;
            report(c, file, token, kind, variadic ? n - defaults : n, nargs);
            return poison;
        }
    }
    if (d->flags[decl] & DECL_GENERAL_CALL) {
        return call_general(c, file, node, token, decl, first_arg, nargs);
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
        assert(!is_compound(c, type));
        Operand v = i < nargs
                      ? convert(c, file, c->stack_tokens[first_arg + i], c->stack[first_arg + i], type)
                      : (Operand){ type, c->members[d->members[decl] + i].value };
        ok &= v.type != TYPE_ID_NONE;
        c->body.code->extra[extra + 1 + i] = ok ? operand_ir(c, v) : 0;
    }
    if (!ok) {
        return poison;
    }

    if (c->nunread != c->unread_done) {
        reserve_copies(c, first_arg - 2);
    }
    u32 i = emit(c, to_c ? IR_CALL_C : IR_CALL, ret, decl, relative);
    c->body.calls++;
    return ret == TYPE_ID_VOID ? (Operand){ TYPE_ID_VOID, 0 }
                               : (Operand){ ret, REF_VALUE | i };
}

static bool
int_argument(Checker *c, u32 file, u32 name, u32 m)
{
    u32 token = c->stack_tokens[m];
    if (is_flagged(c, c->stack[m])) {
        c->stack[m] = unflag(c, file, token, c->stack[m]);
    }
    Operand a = c->stack[m];
    if (a.type == TYPE_ID_NONE) {
        return false;
    }
    if (!is_place(a) && !is_value_type(a.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return false;
    }
    if (is_place(a) || !is_int(c, a.type)) {
        u32 type = is_place(a) ? place_type(c, a) : a.type;
        report(c, file, token, DIAG_BUILTIN_INT, name, type);
        return false;
    }
    return true;
}

static bool
many_argument(Checker *c, u32 file, u32 name, u32 m)
{
    Operand p = c->stack[m];
    if (p.type == TYPE_ID_NONE) {
        return false;
    }
    if (!is_place(p) && !is_value_type(p.type)) {
        report(c, file, c->stack_tokens[m], DIAG_NOT_VALUE, 0, 0);
        return false;
    }
    if (is_place(p) || c->types.kinds[p.type] != TYPE_MANY) {
        u32 type = is_place(p) ? place_type(c, p) : p.type;
        report(c, file, c->stack_tokens[m], DIAG_BUILTIN_MANY, name, type);
        return false;
    }
    return true;
}

static NOINLINE Operand
call_unaligned_load(Checker *c, u32 file, u32 token, u32 first_arg, u32 nargs)
{
    if (nargs != 2) {
        report(c, file, token, DIAG_ARG_COUNT, 2, nargs);
        return poison;
    }
    Operand t = c->stack[first_arg];
    u32 t_token = c->stack_tokens[first_arg];
    if (t.type != TYPE_ID_TYPE) {
        if (t.type != TYPE_ID_NONE) {
            report(c, file, t_token, DIAG_LOAD_TYPE, 0, 0);
        }
        return poison;
    }
    u32 vector = lane_vector(c, t.ref);
    if (!is_number(c, t.ref) && !vector) {
        report(c, file, t_token, DIAG_UNALIGNED_TYPE, NAME_UNALIGNED_LOAD, t.ref);
        return poison;
    }
    if (!many_argument(c, file, NAME_UNALIGNED_LOAD, first_arg + 1)) {
        return poison;
    }
    u32 p = operand_ir(c, c->stack[first_arg + 1]);
    return emit_value(c, IR_LOAD_AT, vector ? vector : t.ref, p, 0);
}

static NOINLINE Operand
lanes_argument(Checker *c, u32 at)
{
    Operand o = c->stack[at];
    u32 vector = is_place(o) ? lane_vector(c, place_type(c, o)) : 0;
    return vector ? load_lanes(c, at, o, vector) : o;
}

static NOINLINE Operand
call_unaligned_store(Checker *c, u32 file, u32 token, u32 first_arg, u32 nargs)
{
    if (nargs != 2) {
        report(c, file, token, DIAG_ARG_COUNT, 2, nargs);
        return poison;
    }
    Operand v = lanes_argument(c, first_arg + 1);
    u32 v_token = c->stack_tokens[first_arg + 1];
    if (!many_argument(c, file, NAME_UNALIGNED_STORE, first_arg) ||
        v.type == TYPE_ID_NONE) {
        return poison;
    }
    if (!is_place(v) && !is_value_type(v.type)) {
        report(c, file, v_token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (is_untyped(v.type)) {
        report(c, file, v_token, DIAG_STORE_UNTYPED, 0, 0);
        return poison;
    }
    if (is_place(v) || (!is_number(c, v.type) && !is_vector(c, v.type))) {
        u32 type = is_place(v) ? place_type(c, v) : v.type;
        report(c, file, v_token, DIAG_UNALIGNED_TYPE, NAME_UNALIGNED_STORE, type);
        return poison;
    }

    u32 p = operand_ir(c, c->stack[first_arg]);
    emit(c, IR_STORE_AT, v.type, p, operand_ir(c, v));
    return (Operand){ TYPE_ID_VOID, 0 };
}

static NOINLINE Operand
call_lookup(Checker *c, u32 file, u32 token, u32 first_arg, u32 nargs)
{
    if (nargs != 2) {
        report(c, file, token, DIAG_ARG_COUNT, 2, nargs);
        return poison;
    }
    Operand table = lanes_argument(c, first_arg);
    Operand index = lanes_argument(c, first_arg + 1);
    if (table.type == TYPE_ID_NONE || index.type == TYPE_ID_NONE) {
        return poison;
    }
    bool bytes = table.type == TYPE_ID_U8X16 || table.type == TYPE_ID_I8X16;
    if (!bytes || index.type != table.type) {
        u32 a = is_place(table) ? place_type(c, table) : table.type;
        u32 b = is_place(index) ? place_type(c, index) : index.type;
        report(c, file, token, DIAG_LOOKUP_TYPES, a, b);
        return poison;
    }
    return emit_value(c, IR_LOOKUP, table.type, operand_ir(c, table), operand_ir(c, index));
}

static NOINLINE Operand
call_top_bits(Checker *c, u32 file, u32 token, u32 first_arg, u32 nargs)
{
    if (nargs != 1) {
        report(c, file, token, DIAG_ARG_COUNT, 1, nargs);
        return poison;
    }
    Operand v = lanes_argument(c, first_arg);
    u32 v_token = c->stack_tokens[first_arg];
    if (v.type == TYPE_ID_NONE) {
        return poison;
    }
    if (!is_place(v) && !is_value_type(v.type)) {
        report(c, file, v_token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (is_place(v) || !is_vector(c, v.type)) {
        u32 type = is_place(v) ? place_type(c, v) : v.type;
        report(c, file, v_token, DIAG_BUILTIN_VECTOR, NAME_TOP_BITS, type);
        return poison;
    }
    u32 type = c->types.arg1[v.type] == 16 ? TYPE_ID_U16 : TYPE_ID_U8;
    u32 i = emit(c, IR_TOP_BITS, v.type, operand_ir(c, v), 0);
    return (Operand){ type, REF_VALUE | i };
}

static NOINLINE Operand
compare_lanes(Checker *c, u32 file, u32 token, u32 name, u32 first_arg, u32 nargs)
{
    if (nargs != 2) {
        report(c, file, token, DIAG_ARG_COUNT, 2, nargs);
        return poison;
    }
    Operand ops[2];
    for (u32 k = 0; k < 2; k++) {
        Operand o = lanes_argument(c, first_arg + k);
        u32 o_token = c->stack_tokens[first_arg + k];
        if (o.type == TYPE_ID_NONE) {
            return poison;
        }
        if (is_place(o)) {
            report(c, file, o_token, DIAG_BUILTIN_VECTOR, name, place_type(c, o));
            return poison;
        }
        if (!is_value_type(o.type)) {
            report(c, file, o_token, DIAG_NOT_VALUE, 0, 0);
            return poison;
        }
        ops[k] = o;
    }
    if (!is_vector(c, ops[0].type) && !is_vector(c, ops[1].type)) {
        u32 a_token = c->stack_tokens[first_arg];
        report(c, file, a_token, DIAG_BUILTIN_VECTOR, name, ops[0].type);
        return poison;
    }

    u32 type = unify(c, file, token, &ops[0], &ops[1]);
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    static_assert(IR_GE - IR_EQ == NAME_GE - NAME_EQ, "lane comparisons must match their IR kinds");
    u8 kind = (u8)(IR_EQ + name - NAME_EQ);
    u32 x = operand_ir(c, ops[0]);
    u32 y = operand_ir(c, ops[1]);
    return (Operand){ type, REF_VALUE | REF_RESULT | emit(c, kind, type, x, y) };
}

static u32
reserve_bytes(Checker *c, u32 file, u32 token, u64 size, u32 align)
{
    Ir *ir = &c->ir;
    u32 at = (ir->ndata + align - 1) & ~(align - 1);
    if (size > DATA_LIMIT - at) {
        report(c, file, token, DIAG_DATA_TOO_LARGE, 0, 0);
        return NO_SLOT;
    }

    u32 end = at + (u32)size;
    if (end >= ir->data_cap) {
        ir->data_cap = 2 * end + 64;
        ir->data = resize(ir->data, ir->data_cap, 1);
    }
    ir->ndata = end;
    return at;
}

static u32
reserve_data(Checker *c, u32 file, u32 token, u32 type)
{
    u32 from = c->ir.ndata;
    u32 at = reserve_bytes(c, file, token, check_type_size(c, type), check_type_align(c, type));
    if (at != NO_SLOT) {
        memset(c->ir.data + from, 0, c->ir.ndata - from);
    }
    return at;
}

static void
put_bits(Checker *c, u8 *to, u32 type, Operand v)
{
    u64 bits[2] = { constant_bits(c, v), 0 };
    if (check_is_int128(type)) {
        bits[1] = exact_bits_high(&c->exacts, value_of(v));
    }
    memcpy(to, bits, check_type_size(c, type));
}

static u32
field_layout(const Checker *c, u32 decl, u32 member)
{
    return c->decls.values[decl] + 2 + 2 * (member - c->decls.members[decl]);
}

static Operand
hold(Checker *c, u32 node, Operand v)
{
    if (is_constant(v)) {
        return v;
    }
    u32 slot = add_slot(c, 0, v.type, node);
    store_local(c, slot, v);
    return (Operand){ OPERAND_LOCAL, slot };
}

static Operand
held(Checker *c, Operand v)
{
    return v.type == OPERAND_LOCAL ? load_local(c, v.ref) : v;
}

static u32
flagged_type(Checker *c, u32 file, u32 node, u32 type)
{
    u32 first = c->nmembers;
    reserve_members(c, 2);
    c->members[c->nmembers++] = (Member){ .type = type, .value = MEMBER_NONE };
    c->members[c->nmembers++] = (Member){ .type = TYPE_ID_BOOL, .value = MEMBER_NONE };

    u32 tuple = tuple_type(c, file, node, first, 2);
    u32 decl = c->types.arg0[tuple];
    if (c->decls.phases[decl] < PHASE_RESOLVED) {
        lay_out_struct(c, decl);
        c->decls.phases[decl] = PHASE_RESOLVED;
    }
    return tuple;
}

static NOINLINE Operand
call_overflow(Checker *c, u32 file, u32 node, u32 token, u32 name, u32 first_arg, u32 nargs)
{
    if (nargs != 2) {
        report(c, file, token, DIAG_ARG_COUNT, 2, nargs);
        return poison;
    }
    if (!int_argument(c, file, name, first_arg) ||
        !int_argument(c, file, name, first_arg + 1)) {
        return poison;
    }

    Operand a = c->stack[first_arg];
    Operand b = c->stack[first_arg + 1];
    u32 type = unify(c, file, token, &a, &b);
    if (type == TYPE_ID_UNTYPED_INT) {
        a = convert(c, file, c->stack_tokens[first_arg], a, TYPE_ID_I64);
        b = convert(c, file, c->stack_tokens[first_arg + 1], b, TYPE_ID_I64);
        type = a.type == TYPE_ID_NONE || b.type == TYPE_ID_NONE ? TYPE_ID_NONE
                                                                : TYPE_ID_I64;
    }
    if (type == TYPE_ID_NONE) {
        return poison;
    }

    u32 tuple = flagged_type(c, file, node, type);
    u32 below = first_arg - 3;
    bool split = in_body(c) &&
                 c->stack[below].type == OPERAND_MARKER + NODE_DESTRUCTURE_VALUE;
    bool wraps = !split || c->stack[below - 2].type != OPERAND_DISCARD;
    bool tests = !split || c->stack[below - 1].type != OPERAND_DISCARD || !wraps;

    u32 k = name - NAME_ADD_OVERFLOW;
    Operand value = poison;
    Operand overflowed = poison;
    if (is_constant(a) && is_constant(b)) {
        static const ExactOp ops[3] = { EXACT_ADD, EXACT_SUB, EXACT_MUL };
        ExactStatus status;
        u32 r = exact_binary(&c->exacts, ops[k], value_of(a), value_of(b), EXACT_INT_BITS, &status);
        assert(status == EXACT_OK);
        bool fits = int_fits(c, type, r);
        value = (Operand){ type, wrap(c, r, type) };
        overflowed = (Operand){ TYPE_ID_BOOL, fits ? EXACT_ZERO : EXACT_ONE };
    } else {
        if (wraps && tests) {
            a = hold(c, node, a);
            b = hold(c, node, b);
        }
        if (wraps) {
            u32 x = operand_ir(c, held(c, a));
            u32 y = operand_ir(c, held(c, b));
            value = emit_value(c, (u8)(IR_ADD_WRAP + k), type, x, y);
        }
        if (tests) {
            u32 x = operand_ir(c, held(c, a));
            u32 y = operand_ir(c, held(c, b));
            u32 test = emit(c, (u8)(IR_ADD_OVERFLOWS + k), type, x, y);
            overflowed = (Operand){ TYPE_ID_BOOL, REF_VALUE | test };
        }
    }
    if (split) {
        return make_pair(c, tuple, value.ref, overflowed.ref);
    }

    u32 decl = c->types.arg0[tuple];
    u32 first = c->decls.members[decl];
    if (!in_body(c)) {
        u32 at = reserve_data(c, file, token, tuple);
        if (at == NO_SLOT) {
            return poison;
        }
        u8 *to = c->ir.data + at;
        put_bits(c, to, type, value);
        put_bits(c, to + check_field_offset(c, field_layout(c, decl, first + 1)), TYPE_ID_BOOL, overflowed);
        return (Operand){ tuple, at };
    }

    Operand place = { OPERAND_LOCAL, add_slot(c, 0, tuple, node) };
    u32 address = place_address(c, place);
    u32 at = emit(c, IR_FIELD, pointer_type(c, type), address, field_layout(c, decl, first));
    store_at(c, type, at, value);
    address = place_address(c, place);
    at = emit(c, IR_FIELD, pointer_type(c, TYPE_ID_BOOL), address, field_layout(c, decl, first + 1));
    store_at(c, TYPE_ID_BOOL, at, overflowed);
    return place;
}

static NOINLINE Operand
call_size_of(Checker *c, u32 file, u32 token, u32 name, u32 first_arg, u32 nargs)
{
    if (nargs != 1) {
        report(c, file, token, DIAG_ARG_COUNT, 1, nargs);
        return poison;
    }
    Operand t = c->stack[first_arg];
    u32 t_token = c->stack_tokens[first_arg];
    if (t.type != TYPE_ID_TYPE) {
        report(c, file, t_token, DIAG_SIZE_TYPE, name, 0);
        return poison;
    }

    claim_contained(c, t.ref, file, t_token);
    if (!has_layout(c, t.ref)) {
        u32 base = t.ref;
        while (is_array(c, base)) {
            base = c->types.arg0[base];
        }
        if (!is_struct(c, base) || !(c->decls.flags[c->types.arg0[base]] & DECL_CYCLE)) {
            report(c, file, t_token, DIAG_UNSUPPORTED, 0, 0);
        }
        return poison;
    }

    u64 n = name == NAME_SIZE_OF ? check_type_size(c, t.ref)
                                 : check_type_align(c, t.ref);
    if (n > INT64_MAX) {
        report(c, file, t_token, DIAG_SIZE_TOO_LARGE, t.ref, 0);
        return poison;
    }
    return (Operand){ TYPE_ID_UNTYPED_INT, exact_int(&c->exacts, (int64_t)n) };
}

static NOINLINE Operand
call_assert(Checker *c, u32 file, u32 token, u32 first_arg, u32 nargs)
{
    if (nargs != 1) {
        report(c, file, token, DIAG_ARG_COUNT, 1, nargs);
        return poison;
    }
    Operand a = c->stack[first_arg];
    u32 a_token = c->stack_tokens[first_arg];
    if (is_flagged(c, a)) {
        a = unflag(c, file, a_token, a);
        if (a.type == TYPE_ID_NONE) {
            return poison;
        }
    }
    u32 type = is_place(a) ? place_type(c, a) : a.type;
    if (type != TYPE_ID_BOOL) {
        report(c, file, a_token, is_value_type(type) ? DIAG_NOT_BOOL : DIAG_NOT_VALUE, type, 0);
        return poison;
    }
    if (!in_body(c)) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        return poison;
    }

    a = convert(c, file, a_token, a, TYPE_ID_BOOL);
    u8 kind = c->release_fast ? IR_ASSUME : IR_ASSERT;
    if (!is_constant(a)) {
        emit(c, kind, 0, value_of(a), 0);
    } else if (exact_sign(&c->exacts, value_of(a)) == 0) {
        emit(c, kind, 0, ir_const(c, TYPE_ID_BOOL, 0), 0);
    }
    return (Operand){ TYPE_ID_VOID, 0 };
}

static u32
count_bits_128(u8 kind, u64 low, u64 high)
{
    if (kind == IR_COUNT_ONES) {
        return (u32)(__builtin_popcountll(low) + __builtin_popcountll(high));
    }
    if ((low | high) == 0) {
        return 128;
    }
    if (kind == IR_TRAILING_ZEROS) {
        return low ? (u32)__builtin_ctzll(low) : 64 + (u32)__builtin_ctzll(high);
    }
    return high ? (u32)__builtin_clzll(high) : 64 + (u32)__builtin_clzll(low);
}

static NOINLINE Operand
call_builtin(Checker *c, u32 file, u32 node, Operand callee, u32 first_arg, u32 nargs)
{
    u32 token = c->stack_tokens[first_arg - 2];
    if (callee.type != OPERAND_BUILTIN) {
        report(c, file, token, DIAG_NOT_CALLABLE, 0, 0);
        return poison;
    }
    switch (callee.ref) {
        case NAME_UNALIGNED_LOAD:
            return call_unaligned_load(c, file, token, first_arg, nargs);
        case NAME_UNALIGNED_STORE:
            return call_unaligned_store(c, file, token, first_arg, nargs);
        case NAME_LOOKUP: return call_lookup(c, file, token, first_arg, nargs);
        case NAME_TOP_BITS:
            return call_top_bits(c, file, token, first_arg, nargs);
        case NAME_EQ:
        case NAME_NE:
        case NAME_LT:
        case NAME_LE:
        case NAME_GT:
        case NAME_GE:
            return compare_lanes(c, file, token, callee.ref, first_arg, nargs);
        case NAME_SIZE_OF:
        case NAME_ALIGN_OF:
            return call_size_of(c, file, token, callee.ref, first_arg, nargs);
        case NAME_ASSERT: return call_assert(c, file, token, first_arg, nargs);
        default: break;
    }
    if (callee.ref >= NAME_ADD_OVERFLOW) {
        return call_overflow(c, file, node, token, callee.ref, first_arg, nargs);
    }
    if (nargs != 1) {
        report(c, file, token, DIAG_ARG_COUNT, 1, nargs);
        return poison;
    }
    if (!int_argument(c, file, callee.ref, first_arg)) {
        return poison;
    }

    Operand a = c->stack[first_arg];
    if (a.type == TYPE_ID_UNTYPED_INT) {
        a = convert(c, file, c->stack_tokens[first_arg], a, TYPE_ID_I64);
        if (a.type == TYPE_ID_NONE) {
            return poison;
        }
    }

    u8 kind = (u8)(IR_TRAILING_ZEROS + callee.ref - NAME_TRAILING_ZEROS);
    if (!is_constant(a)) {
        return emit_value(c, kind, TYPE_ID_I64, value_of(a), 0);
    }

    u32 bits = (u32)c->types.arg0[a.type];
    if (bits > 64) {
        u32 v = value_of(a);
        u32 n = count_bits_128(kind, exact_bits(&c->exacts, v), exact_bits_high(&c->exacts, v));
        return (Operand){ TYPE_ID_I64, exact_int(&c->exacts, n) };
    }
    u64 x = exact_bits(&c->exacts, value_of(a)) & ~0ull >> (64 - bits);
    u32 n = kind == IR_COUNT_ONES     ? (u32)__builtin_popcountll(x)
          : x == 0                    ? bits
          : kind == IR_TRAILING_ZEROS ? (u32)__builtin_ctzll(x)
                                      : (u32)__builtin_clzll(x) - (64 - bits);
    return (Operand){ TYPE_ID_I64, exact_int(&c->exacts, n) };
}

static void
call(Checker *c, u32 file, u32 node)
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
    if (LIKELY(callee.type == OPERAND_FN)) {
        r = call_fn(c, file, node, callee_token, callee.ref, m + 1, nargs);
    } else if (callee.type == TYPE_ID_TYPE) {
        if (nargs == 1) {
            r = cast(c, file, c->stack_tokens[m + 1], c->stack[m + 1], callee.ref);
        } else {
            report(c, file, callee_token, DIAG_ARG_COUNT, 1, nargs);
        }
    } else if (callee.type != TYPE_ID_NONE) {
        r = call_builtin(c, file, node, callee, m + 1, nargs);
    }

    c->sp = m - 1;
    push_operand(c, callee_token, r);
}

static void
push_test(Checker *c, u32 word)
{
    if (c->ntests == c->tests_cap) {
        c->tests_cap *= 2;
        c->tests = resize(c->tests, c->tests_cap, sizeof *c->tests);
    }
    c->tests[c->ntests++] = word;
}

static u32
leaf_test(const Checker *c, u32 at)
{
    const IrCode *code = c->body.code;
    u32 i = c->body.first + at;
    u8 kind = code->kinds[i];
    if (kind == IR_LOAD && code->types[i] == TYPE_ID_BOOL) {
        u32 slot = code->as[i];
        bool present = slot > 0 &&
                       check_is_flag_optional(c, slot_at(c, slot - 1)->type);
        return present ? (slot - 1) << 1 | 1 : NO_TEST;
    }
    if ((kind != IR_EQ && kind != IR_NE) ||
        c->types.kinds[code->types[i]] != TYPE_OPTIONAL) {
        return NO_TEST;
    }

    u32 a = code->as[i];
    u32 b = code->bs[i];
    u32 nil = a & IR_CONST ? a : b;
    u32 load = a & IR_CONST ? b : a;
    if (!(nil & IR_CONST) || (load & IR_CONST) ||
        code->consts[c->body.first_const + (nil & ~IR_CONST)].bits != 0 ||
        code->kinds[c->body.first + load] != IR_LOAD) {
        return NO_TEST;
    }
    return code->as[c->body.first + load] << 1 | (kind == IR_NE);
}

static u32
cond_tests(Checker *c, Operand cond)
{
    if (cond.type != TYPE_ID_BOOL || is_constant(cond)) {
        return 0;
    }
    const IrCode *code = c->body.code;
    u32 at = value_of(cond);
    u32 flip = 0;
    while (code->kinds[c->body.first + at] == IR_NOT) {
        at = code->as[c->body.first + at];
        flip ^= 1;
    }

    u32 n = c->ntests;
    if (n >= c->body.tests + 2 && c->tests[n - 1] == at) {
        u32 count = c->tests[n - 2];
        for (u32 k = n - 2 - count; k < n - 2; k++) {
            c->tests[k] ^= flip;
        }
        return count;
    }
    u32 test = leaf_test(c, at);
    if (test == NO_TEST) {
        return 0;
    }
    push_test(c, test ^ flip);
    push_test(c, 1);
    push_test(c, at);
    return 1;
}

static void
narrow_tests(Checker *c, u32 count, u32 when)
{
    u32 first = c->ntests - 2 - count;
    for (u32 k = first; k < first + count; k++) {
        if ((c->tests[k] & 1) == when) {
            narrow(c, c->tests[k] >> 1);
        }
    }
    c->ntests = first;
}

static NOINLINE u32
narrow_left(Checker *c, u8 kind, Operand a)
{
    u32 count = cond_tests(c, a);
    if (count == 0) {
        return SHORT_VALUE;
    }
    push_fact(c, FACT_REGION);
    narrow_tests(c, count, kind == NODE_AND_LEFT);
    return SHORT_TESTED;
}

static NOINLINE void
narrow_logical(Checker *c, u8 kind, Operand b, Operand r, bool tested)
{
    u32 when = kind == NODE_AND;
    u32 count = cond_tests(c, b);
    u32 first = count ? c->ntests - 2 - count : c->ntests;
    u32 n = first;
    for (u32 k = first; k < first + count; k++) {
        if ((c->tests[k] & 1) == when) {
            c->tests[n++] = c->tests[k];
        }
    }
    c->ntests = n;

    if (tested) {
        u32 k = c->nfacts;
        while (c->facts[--k] != FACT_REGION) {
            u32 slot = c->facts[k];
            if (slot < FACT_PENDING && is_narrowed(c, slot)) {
                push_test(c, slot << 1 | when);
            }
        }
        undo_facts(c, k + 1);
        c->nfacts = k;
    }
    if (c->ntests == first) {
        return;
    }
    push_test(c, c->ntests - first);
    push_test(c, value_of(r));
}

static void
logical_left(Checker *c, u32 file, u32 token, u8 kind)
{
    Operand a = c->stack[c->sp - 1];
    u32 a_token = c->stack_tokens[c->sp - 1];
    u32 mode = SHORT_FOLD;
    if (a.type != TYPE_ID_NONE && a.type != TYPE_ID_BOOL) {
        a = not_bool(c, file, a_token, a);
        c->stack[c->sp - 1] = a;
    }
    if (a.type == TYPE_ID_BOOL && in_body(c)) {
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
            mode = c->body.narrows ? narrow_left(c, kind, a) : SHORT_VALUE;
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
        b = not_bool(c, file, b_token, b);
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
    if (mode == SHORT_FOLD) {
        if (a.type == TYPE_ID_NONE || b.type == TYPE_ID_NONE) {
            return poison;
        }
        assert(is_constant(a) && is_constant(b));
        bool x = exact_sign(&c->exacts, value_of(a)) != 0;
        bool y = exact_sign(&c->exacts, value_of(b)) != 0;
        bool r = kind == NODE_AND ? x && y : x || y;
        return (Operand){ TYPE_ID_BOOL, r ? EXACT_ONE : EXACT_ZERO };
    }

    u32 v = b.type == TYPE_ID_BOOL ? operand_ir(c, b) : ir_const(c, TYPE_ID_BOOL, 0);
    if (kind == NODE_AND) {
        emit(c, IR_ELSE, TYPE_ID_BOOL, v, 0);
        v = ir_const(c, TYPE_ID_BOOL, 0);
    }
    Operand r = emit_value(c, IR_END, TYPE_ID_BOOL, v, 0);
    if (c->body.narrows) {
        narrow_logical(c, kind, b, r, mode == SHORT_TESTED);
    }
    return r;
}

static NOINLINE Operand
unwrap_flagged(Checker *c, u32 file, u32 token, Operand o)
{
    Pair p = pair_of(c, o);
    if (!(p.second & IR_CONST)) {
        emit(c, IR_DROP, 0, emit(c, IR_CHECK_NIL, TYPE_ID_BOOL, p.second, 0), 0);
    } else if (const_bits(c, p.second) == 0) {
        report(c, file, token, DIAG_ALWAYS_NIL, 0, 0);
        return poison;
    }
    return value_half(c, o);
}

static NOINLINE Operand
unwrap(Checker *c, u32 file, u32 token, Operand o)
{
    if (o.type == TYPE_ID_NONE) {
        return poison;
    }
    if (o.type == TYPE_ID_NIL) {
        report(c, file, token, DIAG_NIL_TYPE, 0, 0);
        return poison;
    }
    if (!is_value_type(o.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (is_flagged(c, o)) {
        return unwrap_flagged(c, file, token, o);
    }
    if (!is_nilable(c, o.type)) {
        report(c, file, token, DIAG_NOT_OPTIONAL, o.type, 0);
        return poison;
    }
    if (is_constant(o)) {
        report(c, file, token, DIAG_ALWAYS_NIL, 0, 0);
        return poison;
    }
    if (is_nonnil(o)) {
        return narrowed_value(c, o);
    }
    return emit_value(c, IR_CHECK_NIL, c->types.arg0[o.type], value_of(o), 0);
}

static NOINLINE u32
or_else_flagged(Checker *c, Operand a)
{
    Pair p = pair_of(c, a);
    u32 inner = c->types.arg0[a.type];
    if (!(p.second & IR_CONST)) {
        u32 at = emit(c, IR_IF, inner, p.second, 0);
        emit(c, IR_ELSE, inner, p.first, 0);
        c->stack[c->sp - 1] = (Operand){ OPERAND_STMT, 0 };
        return at;
    }
    if (const_bits(c, p.second) == 0) {
        return SHORT_PASS;
    }
    emit(c, IR_IF, 0, ir_const(c, TYPE_ID_BOOL, 0), 0);
    return SHORT_FOLD;
}

static NOINLINE void
or_else_left(Checker *c, u32 file, u32 node, u32 token)
{
    Operand a = c->stack[c->sp - 1];
    u32 a_token = c->stack_tokens[c->sp - 1];
    u32 mode = SHORT_SKIP;
    if (a.type == TYPE_ID_NIL) {
        report(c, file, a_token, DIAG_NIL_TYPE, 0, 0);
    } else if (a.type != TYPE_ID_NONE && !is_value_type(a.type)) {
        report(c, file, a_token, DIAG_NOT_VALUE, 0, 0);
    } else if (is_flagged(c, a)) {
        mode = or_else_flagged(c, a);
    } else if (a.type != TYPE_ID_NONE && !is_nilable(c, a.type)) {
        report(c, file, token, DIAG_NOT_OPTIONAL, a.type, 0);
    } else if (a.type != TYPE_ID_NONE && is_constant(a)) {
        mode = SHORT_PASS;
    } else if (a.type != TYPE_ID_NONE) {
        u32 slot = add_slot(c, 0, a.type, node);
        store_local(c, slot, a);
        u32 inner = c->types.arg0[a.type];
        u32 test = emit(c, IR_NE, a.type, value_of(load_local(c, slot)), ir_const(c, a.type, 0));
        weigh(c, slot);
        mode = emit(c, IR_IF, inner, test, 0);
        emit(c, IR_ELSE, inner, emit(c, IR_LOAD, inner, slot, 0), 0);
        c->stack[c->sp - 1] = (Operand){ OPERAND_STMT, 0 };
    }
    push(c, token, OPERAND_MARKER + NODE_OR_ELSE_LEFT, mode);
}

static NOINLINE Operand
or_else(Checker *c, u32 file)
{
    u32 b_token = c->stack_tokens[c->sp - 1];
    Operand b = pop(c);
    u32 mode = pop(c).ref;
    Operand a = pop(c);
    if (mode == SHORT_SKIP) {
        return poison;
    }

    bool known = mode == SHORT_PASS || mode == SHORT_FOLD;
    u32 inner = known ? c->types.arg0[a.type]
                      : c->body.code->types[c->body.first + mode];
    u32 optional = optional_type(c, inner);
    bool flagged = check_is_flag_optional(c, optional);
    if (flagged && b.type == optional && is_nonnil(b)) {
        b = value_half(c, b);
    }
    u32 type = b.type == TYPE_ID_NIL || b.type == optional ? optional : inner;
    if (flagged && type == optional && !known) {
        report(c, file, b_token, DIAG_UNSUPPORTED, 0, 0);
        emit(c, IR_END, 0, 0, 0);
        return poison;
    }
    Operand v = convert(c, file, b_token, b, type);
    if (mode == SHORT_PASS) {
        return v;
    }
    if (mode == SHORT_FOLD) {
        if (is_pair(c, v)) {
            drop_pair(c, v);
        } else if (v.type != TYPE_ID_NONE && !is_constant(v)) {
            emit(c, IR_DROP, 0, value_of(v), 0);
        }
        emit(c, IR_END, 0, 0, 0);
        return type == optional ? a : value_half(c, a);
    }
    if (v.type == TYPE_ID_NONE) {
        emit(c, IR_END, 0, 0, 0);
        return poison;
    }
    if (type == optional) {
        assert(c->body.code->kinds[c->body.first + mode + 2] == IR_ELSE);
        c->body.code->types[c->body.first + mode] = optional;
        c->body.code->types[c->body.first + mode + 2] = optional;
    }
    return emit_value(c, IR_END, type, operand_ir(c, v), 0);
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
        if (is_slice(c, type)) {
            report(c, file, token, DIAG_BAD_OPERATOR, type, 0);
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
    store_at(c, type, address, converted);
}

static NOINLINE bool
check_hidden_target(Checker *c, u32 file, u32 token, u32 slot)
{
    if (c->files[file].nodes.kinds[slot_at(c, slot)->node] != NODE_CALL) {
        return true;
    }
    report(c, file, token, DIAG_ASSIGN_RESULT, 0, 0);
    return false;
}

static NOINLINE void
assign_slow(Checker *c, u32 file, u32 token, u8 kind, u32 slot, Operand v, u32 v_token)
{
    u32 type = slot_at(c, slot)->type;
    if (is_slice(c, type)) {
        if (kind != NODE_ASSIGN) {
            report(c, file, token, DIAG_BAD_OPERATOR, type, 0);
            return;
        }
        store_pair(c, slot, convert(c, file, v_token, v, type));
        return;
    }
    if (kind != NODE_ASSIGN) {
        u8 op = assign_ops[kind];
        if (op == NODE_NONE) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return;
        }
        v = binary(c, file, token, op, read_optional(c, slot), v);
        v_token = token;
    }

    Operand converted = convert(c, file, v_token, v, type);
    if (converted.type == TYPE_ID_NONE) {
        return;
    }
    if (is_pair_type(c, type)) {
        store_pair(c, slot, converted);
    } else {
        store_local(c, slot, converted);
    }
    if (is_nonnil(converted)) {
        narrow(c, slot);
    } else {
        drop_narrow(c, slot);
    }
}

static NOINLINE void
assign_lanes(Checker *c, u32 file, u32 node, u32 token, u8 kind, Operand target, Operand v)
{
    u32 type = place_type(c, target);
    u32 vector = lane_vector(c, type);
    u8 op = assign_ops[kind];
    if (!vector) {
        report(c, file, token, place_operator_diag(c, op, type), type, 0);
        return;
    }

    u32 pointer = pointer_type(c, type);
    u32 hidden = add_slot(c, 0, pointer, node);
    store_local(c, hidden, (Operand){ pointer, REF_VALUE | place_address(c, target) });
    Operand old = emit_value(c, IR_LOAD_AT, vector, value_of(load_local(c, hidden)), 0);
    v = convert(c, file, token, binary(c, file, token, op, old, v), vector);
    if (v.type != TYPE_ID_NONE) {
        emit(c, IR_STORE_AT, vector, value_of(load_local(c, hidden)), operand_ir(c, v));
    }
}

static u32
stored_type(const Checker *c, u32 type, Operand v)
{
    u32 vector = is_place(v) ? 0 : lane_vector(c, type);
    return vector ? vector : type;
}

static void
assign_to(Checker *c, u32 file, u32 node, u32 token, u8 kind, Operand target, u32 target_token, Operand v, u32 v_token)
{
    if (target.type == OPERAND_DISCARD) {
        if (kind != NODE_ASSIGN) {
            report(c, file, target_token, DIAG_DISCARD, 0, 0);
        } else if (is_place(v)) {
            if (v.type == OPERAND_ADDRESS) {
                emit(c, IR_DROP, 0, value_of(v), 0);
            }
        } else if (v.type != TYPE_ID_NONE && !is_value_type(v.type)) {
            report(c, file, v_token, DIAG_NOT_VALUE, 0, 0);
        } else if (is_pair(c, v)) {
            drop_pair(c, v);
        } else if (v.type != TYPE_ID_NONE && !is_constant(v)) {
            emit(c, IR_DROP, 0, value_of(v), 0);
        }
        return;
    }
    if (is_place(target)) {
        u32 root = place_root(c, target);
        u8 binding = root < DATA_ROOT  ? binding_kind(c, file, root)
                   : root == DATA_ROOT ? NODE_CONST_INIT
                                       : NODE_NONE;
        if (binding != NODE_NONE) {
            refuse_root(c, file, target_token, target, binding, DIAG_ASSIGN_PARAM);
            return;
        }
        if (root != NO_SLOT && slot_at(c, root)->name == 0 &&
            !check_hidden_target(c, file, target_token, root)) {
            return;
        }
    }
    if (is_place(target) && is_aggregate(c, place_type(c, target))) {
        u32 type = place_type(c, target);
        if (kind != NODE_ASSIGN) {
            assign_lanes(c, file, node, token, kind, target, v);
            return;
        }
        u32 to = stored_type(c, type, v);
        Operand from = convert(c, file, v_token, v, to);
        if (from.type == TYPE_ID_NONE) {
            return;
        }
        if (to != type) {
            emit(c, IR_STORE_AT, to, place_address(c, target), operand_ir(c, from));
        } else {
            copy_place(c, target, from);
        }
        return;
    }
    if (target.type == OPERAND_ADDRESS) {
        assign_at(c, file, node, token, kind, target, v, v_token);
        return;
    }
    if (target.type != OPERAND_LOCAL) {
        if (is_pair(c, target)) {
            report(c, file, target_token, DIAG_UNSUPPORTED, 0, 0);
        } else if (target.type != TYPE_ID_NONE) {
            report(c, file, target_token, DIAG_NOT_ASSIGNABLE, 0, 0);
        }
        return;
    }

    u32 slot = target.ref;
    u32 type = slot_at(c, slot)->type;
    if (has_slow_store(c, type)) {
        assign_slow(c, file, token, kind, slot, v, v_token);
        return;
    }
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
assign(Checker *c, u32 file, u32 node, u32 token, u8 kind)
{
    u32 v_token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    pop(c);
    u32 target_token = c->stack_tokens[c->sp - 1];
    Operand target = pop(c);
    push(c, token, OPERAND_STMT, 0);

    assign_to(c, file, node, token, kind, target, target_token, v, v_token);
}

static NOINLINE u32
or_else_target(Checker *c, u32 file, u32 node, u32 token)
{
    Operand target = c->stack[c->sp - 1];
    u32 target_token = c->stack_tokens[c->sp - 1];
    if (!is_place(target)) {
        if (is_pair(c, target)) {
            report(c, file, target_token, DIAG_UNSUPPORTED, 0, 0);
        } else if (target.type != TYPE_ID_NONE) {
            report(c, file, target_token, DIAG_NOT_ASSIGNABLE, 0, 0);
        }
        return SHORT_SKIP;
    }
    u32 root = place_root(c, target);
    u8 binding = root < DATA_ROOT  ? binding_kind(c, file, root)
               : root == DATA_ROOT ? NODE_CONST_INIT
                                   : NODE_NONE;
    if (binding != NODE_NONE) {
        refuse_root(c, file, target_token, target, binding, DIAG_ASSIGN_PARAM);
        return SHORT_SKIP;
    }
    if (root != NO_SLOT && slot_at(c, root)->name == 0 &&
        !check_hidden_target(c, file, target_token, root)) {
        return SHORT_SKIP;
    }
    u32 type = place_type(c, target);
    if (is_value_optional(c, type) && target.type == OPERAND_LOCAL) {
        u32 present = value_of(load_local(c, target.ref + 1));
        emit(c, IR_IF, 0, emit(c, IR_NOT, TYPE_ID_BOOL, present, 0), 0);
        return SHORT_PASS;
    }
    if (!is_nilable(c, type)) {
        report(c, file, token, DIAG_NOT_OPTIONAL, type, 0);
        return SHORT_SKIP;
    }

    u32 mode = SHORT_PASS;
    Operand old;
    if (target.type == OPERAND_LOCAL) {
        old = load_local(c, target.ref);
    } else {
        u32 pointer = pointer_type(c, type);
        mode = add_slot(c, 0, pointer, node);
        store_local(c, mode, (Operand){ pointer, target.ref });
        old = emit_value(c, IR_LOAD_AT, type, value_of(load_local(c, mode)), 0);
    }
    emit(c, IR_IF, 0, emit(c, IR_EQ, type, value_of(old), ir_const(c, type, 0)), 0);
    return mode;
}

static NOINLINE void
or_else_assign(Checker *c, u32 file, u32 token)
{
    u32 v_token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    u32 mode = pop(c).ref;
    Operand target = pop(c);
    push(c, token, OPERAND_STMT, 0);
    if (mode == SHORT_SKIP) {
        return;
    }

    u32 type = mode == SHORT_PASS ? slot_at(c, target.ref)->type
                                  : c->types.arg0[slot_at(c, mode)->type];
    Operand converted = convert(c, file, v_token, v, type);
    if (converted.type != TYPE_ID_NONE && mode == SHORT_PASS) {
        if (is_pair_type(c, type)) {
            store_pair(c, target.ref, converted);
        } else {
            store_local(c, target.ref, converted);
        }
        if (is_nonnil(converted)) {
            narrow(c, target.ref);
        }
    } else if (converted.type != TYPE_ID_NONE) {
        store_at(c, type, value_of(load_local(c, mode)), converted);
    }
    emit(c, IR_END, 0, 0, 0);
}

static void
expression_statement(Checker *c, u32 file, u32 token)
{
    u32 v_token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    push(c, token, OPERAND_STMT, 0);
    if (v.type != TYPE_ID_NONE && v.type != TYPE_ID_VOID) {
        report(c, file, v_token, v.type == TYPE_ID_TYPE ? DIAG_NOT_VALUE : DIAG_UNUSED, 0, 0);
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

    u8 binding = expose(c, file, target);
    if (binding != NODE_NONE) {
        refuse_root(c, file, token, target, binding, DIAG_ADDRESS_PARAM);
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
    if (check_is_flag_optional(c, type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    if (is_slice(c, type)) {
        slot_at(c, slot + 1)->weight = WEIGHT_ADDRESSED;
    }
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
    u8 kind = c->types.kinds[p.type];
    if (kind != TYPE_POINTER) {
        if (kind == TYPE_OPTIONAL && is_nonnil(p)) {
            p = narrowed_value(c, p);
            kind = c->types.kinds[p.type];
        }
        if (kind != TYPE_POINTER) {
            u8 diag = kind == TYPE_MANY              ? DIAG_MANY_DEREF
                    : is_optional_pointer(c, p.type) ? DIAG_MAYBE_NIL
                                                     : DIAG_NOT_POINTER;
            report(c, file, token, diag, p.type, 0);
            return poison;
        }
    }

    u32 type = c->types.arg0[p.type];
    if (is_compound(c, type) ? !is_storable(c, type) : !is_scalar(c, type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    Operand r = { OPERAND_ADDRESS, p.ref };
    if (place) {
        return r;
    }
    if (!is_aggregate(c, type)) {
        return load_at(c, type, value_of(p));
    }
    u8 after = c->files[file].tokens.kinds[token + 1];
    if (after != TK_LBRACK && after != TK_PERIOD) {
        note_unread(c, r);
    }
    return r;
}

static bool
check_index(Checker *c, u32 file, u32 token, Operand index)
{
    u32 type = is_place(index) ? place_type(c, index) : index.type;
    if (!is_value_type(type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return false;
    }
    if (!is_int(c, type)) {
        report(c, file, token, DIAG_NOT_INDEX, type, 0);
        return false;
    }
    if (check_is_int128(type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return false;
    }
    return true;
}

static Pair
slice_pair(Checker *c, Operand o)
{
    return pair_of(c, is_place(o) ? read_slice(c, o) : o);
}

static NOINLINE void
refuse_index(Checker *c, u32 file, u32 token, u32 type)
{
    u8 diag = is_vector(c, type)           ? DIAG_UNSUPPORTED
            : is_optional_pointer(c, type) ? DIAG_MAYBE_NIL
                                           : DIAG_NOT_INDEXABLE;
    report(c, file, token, diag, type, 0);
}

static NOINLINE Operand
index_place(Checker *c, u32 file, u32 token, Operand base, Operand index, u32 index_token, u8 next)
{
    if (base.type == TYPE_ID_NONE || index.type == TYPE_ID_NONE) {
        return poison;
    }
    if (is_flagged(c, index)) {
        index = unflag(c, file, index_token, index);
        if (index.type == TYPE_ID_NONE) {
            return poison;
        }
    }

    u32 type = is_place(base) ? place_type(c, base) : base.type;
    if (!is_value_type(type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    bool many = c->types.kinds[type] == TYPE_MANY && type != TYPE_ID_CSTRING;
    if (!many && !is_indexable(c, type)) {
        if (c->types.kinds[type] == TYPE_OPTIONAL && is_nonnil(base)) {
            return index_place(c, file, token, narrowed_value(c, base), index, index_token, next);
        }
        refuse_index(c, file, token, type);
        return poison;
    }
    if (type == TYPE_ID_STRING && takes_place[next]) {
        report(c, file, token, DIAG_STRING_READ_ONLY, 0, 0);
        return poison;
    }
    if (!check_index(c, file, index_token, index)) {
        return poison;
    }

    bool slice = is_slice(c, type);
    Pair p = slice ? slice_pair(c, base) : (Pair){ 0, 0 };
    u32 at = value_of(index);
    if (is_constant(index)) {
        bool known = slice ? (p.second & IR_CONST) != 0 : !many;
        u64 n = !known ? 0
              : slice  ? const_bits(c, p.second)
                       : c->types.arg1[type];
        if (!exact_fits(&c->exacts, at, 64, false) ||
            (known && exact_bits(&c->exacts, at) >= n)) {
            report(c, file, index_token, DIAG_INDEX_RANGE, at, type);
            return poison;
        }
        at = ir_const(c, TYPE_ID_I64, exact_bits(&c->exacts, at));
    }

    u32 elem = c->types.arg0[type];
    u32 pointer = pointer_type(c, elem);
    u32 address;
    if (many) {
        assert(!is_place(base));
        address = emit(c, IR_INDEX, pointer, value_of(base), at);
    } else if (!slice) {
        address = emit(c, IR_INDEX, pointer, place_address(c, base), at);
    } else if ((p.second & IR_CONST) && (at & IR_CONST)) {
        address = emit(c, IR_INDEX, pointer, p.first, at);
    } else {
        u32 words[2] = { p.second, at };
        address = emit_extra(c, IR_INDEX_LEN, pointer, p.first, words, 2);
    }
    Operand r = { OPERAND_ADDRESS, REF_VALUE | address };
    if (takes_place[next]) {
        return r;
    }
    if (!is_aggregate(c, elem)) {
        return load_at(c, elem, address);
    }
    if (next != NODE_FIELD) {
        note_unread(c, r);
    }
    return r;
}

static u32
inclusive_end(Checker *c, Operand hi)
{
    u32 type = hi.type;
    u32 v = value_of(hi);
    if (c->types.arg0[type] < 64) {
        type = TYPE_ID_I64;
        v = emit(c, IR_CONVERT, type, v, 0);
    }
    return emit(c, IR_ADD, type, v, ir_const(c, type, 1));
}

static NOINLINE Operand
slice_range(Checker *c, u32 file, u8 kind)
{
    bool has_lo = kind != NODE_SLICE_TO && kind != NODE_SLICE_TO_INCLUSIVE;
    bool has_hi = kind != NODE_SLICE_FROM;
    bool inclusive = kind == NODE_SLICE_INCLUSIVE || kind == NODE_SLICE_TO_INCLUSIVE;

    u32 hi_token = c->stack_tokens[c->sp - 1];
    Operand hi = has_hi ? pop(c) : (Operand){ TYPE_ID_I64, EXACT_ZERO };
    u32 lo_token = c->stack_tokens[c->sp - 1];
    Operand lo = has_lo ? pop(c) : (Operand){ TYPE_ID_I64, EXACT_ZERO };
    u32 token = c->stack_tokens[c->sp - 1];
    Operand base = pop(c);
    if (is_flagged(c, lo)) {
        lo = unflag(c, file, lo_token, lo);
    }
    if (is_flagged(c, hi)) {
        hi = unflag(c, file, hi_token, hi);
    }
    if (base.type == TYPE_ID_NONE || lo.type == TYPE_ID_NONE || hi.type == TYPE_ID_NONE) {
        return poison;
    }

    u32 type = is_place(base) ? place_type(c, base) : base.type;
    if (!is_value_type(type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    bool many = c->types.kinds[type] == TYPE_MANY && type != TYPE_ID_CSTRING;
    if (!many && !is_indexable(c, type) &&
        c->types.kinds[type] == TYPE_OPTIONAL && is_nonnil(base)) {
        base = narrowed_value(c, base);
        type = base.type;
        many = c->types.kinds[type] == TYPE_MANY && type != TYPE_ID_CSTRING;
    }
    if (!many && !is_indexable(c, type)) {
        u8 diag = is_vector(c, type)           ? DIAG_UNSUPPORTED
                : is_optional_pointer(c, type) ? DIAG_MAYBE_NIL
                                               : DIAG_NOT_INDEXABLE;
        report(c, file, token, diag, type, 0);
        return poison;
    }
    if (many && !has_hi) {
        report(c, file, token, DIAG_MANY_END, 0, type);
        return poison;
    }
    if (!check_index(c, file, lo_token, lo) || !check_index(c, file, hi_token, hi)) {
        return poison;
    }
    bool array = is_array(c, type);
    u8 binding = array ? expose(c, file, base) : NODE_NONE;
    if (binding != NODE_NONE) {
        refuse_root(c, file, token, base, binding, DIAG_SLICE_PARAM);
        return poison;
    }

    Exacts *e = &c->exacts;
    if (is_constant(lo) && !exact_fits(e, value_of(lo), 63, false)) {
        report(c, file, lo_token, DIAG_INDEX_RANGE, value_of(lo), type);
        return poison;
    }
    if (is_constant(hi) && !exact_fits(e, value_of(hi), 63, false)) {
        report(c, file, hi_token, DIAG_INDEX_RANGE, value_of(hi), type);
        return poison;
    }

    u32 first, len;
    if (array) {
        first = place_address(c, base);
        len = ir_const(c, TYPE_ID_I64, c->types.arg1[type]);
    } else if (many) {
        assert(!is_place(base));
        first = value_of(base);
        len = ir_const(c, TYPE_ID_I64, INT64_MAX);
    } else {
        Pair p = slice_pair(c, base);
        first = p.first;
        len = p.second;
    }

    bool known = len & IR_CONST;
    u64 n = known ? const_bits(c, len) : 0;
    u64 start = is_constant(lo) ? exact_bits(e, value_of(lo)) : 0;
    u64 end = !has_hi         ? n
            : is_constant(hi) ? exact_bits(e, value_of(hi)) + inclusive
                              : 0;
    if (known && has_hi && is_constant(hi) && end > n) {
        report(c, file, hi_token, DIAG_INDEX_RANGE, value_of(hi), type);
        return poison;
    }
    if (known && is_constant(lo) && start > n) {
        report(c, file, lo_token, DIAG_INDEX_RANGE, value_of(lo), type);
        return poison;
    }
    if (has_hi && is_constant(lo) && is_constant(hi) && start > end) {
        report(c, file, lo_token, DIAG_SLICE_ORDER, value_of(lo), 0);
        return poison;
    }

    u32 result = type == TYPE_ID_STRING ? TYPE_ID_STRING
                                        : slice_type(c, c->types.arg0[type]);
    if (known && is_constant(lo) && is_constant(hi)) {
        u32 ptr = start
                    ? emit(c, IR_INDEX, result, first, ir_const(c, TYPE_ID_I64, start))
                    : first;
        return make_pair(c, result, ptr, ir_const(c, TYPE_ID_I64, end - start));
    }

    u32 lo_ir = is_constant(lo) ? ir_const(c, TYPE_ID_I64, start) : value_of(lo);
    u32 ptr;
    if (has_hi) {
        u32 hi_ir = is_constant(hi) ? ir_const(c, TYPE_ID_I64, end)
                  : inclusive       ? inclusive_end(c, hi)
                                    : value_of(hi);
        u32 words[3] = { len, lo_ir, hi_ir };
        ptr = emit_extra(c, IR_SLICE, result, first, words, 3);
    } else {
        u32 words[2] = { len, lo_ir };
        ptr = emit_extra(c, IR_SLICE_FROM, result, first, words, 2);
    }
    return make_pair(c, result, ptr, emit(c, IR_SECOND, TYPE_ID_I64, 0, 0));
}

static Operand
slice_ptr(Checker *c, Operand o, u32 type)
{
    u32 first;
    if (o.type == OPERAND_LOCAL) {
        first = value_of(load_local(c, o.ref));
    } else {
        Pair p = pair_of(c, is_place(o) ? read_slice(c, o) : o);
        if (!(p.second & IR_CONST)) {
            emit(c, IR_DROP, 0, p.second, 0);
        }
        first = p.first;
    }

    u32 many = intern_type(c, TYPE_MANY, c->types.arg0[type], 0);
    return emit_value(c, IR_INDEX, many, first, ir_const(c, TYPE_ID_I64, 0));
}

static NOINLINE Operand
slice_field(Checker *c, u32 file, u32 token, Operand o)
{
    u32 type = is_place(o) ? place_type(c, o) : o.type;
    u32 name = c->files[file].tokens.ids[token];
    if (name == NAME_PTR) {
        return slice_ptr(c, o, type);
    }
    if (name != NAME_LEN) {
        if (is_pair(c, o)) {
            drop_pair(c, o);
        } else if (o.type == OPERAND_ADDRESS) {
            emit(c, IR_DROP, 0, value_of(o), 0);
        }
        report(c, file, token, DIAG_NO_MEMBER, name, type);
        return poison;
    }
    if (o.type == OPERAND_LOCAL) {
        return load_local(c, o.ref + 1);
    }

    Pair p = pair_of(c, is_place(o) ? read_slice(c, o) : o);
    if (!(p.first & IR_CONST)) {
        emit(c, IR_DROP, 0, p.first, 0);
    }
    if (p.second & IR_CONST) {
        u64 n = const_bits(c, p.second);
        return (Operand){ TYPE_ID_I64, exact_int(&c->exacts, (int64_t)n) };
    }
    return (Operand){ TYPE_ID_I64, REF_VALUE | p.second };
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

u64
check_member_offset(const Checker *c, u32 decl, u32 member)
{
    return check_field_offset(c, field_layout(c, decl, member));
}

static Operand
struct_field(Checker *c, u32 file, u32 token, Operand o, bool place)
{
    u32 type = place_type(c, o);
    u32 name = c->files[file].tokens.ids[token];
    u32 decl = c->types.arg0[type];
    u32 member = find_member(c, decl, name);
    u32 field = member != NO_SLOT ? c->members[member].type : TYPE_ID_NONE;
    if (member == NO_SLOT || !is_storable(c, field)) {
        if (o.type == OPERAND_ADDRESS) {
            emit(c, IR_DROP, 0, value_of(o), 0);
        }
        if (member == NO_SLOT) {
            report(c, file, token, DIAG_NO_MEMBER, name, type);
        } else {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        }
        return poison;
    }

    u32 address = emit(
        c,
        IR_FIELD,
        pointer_type(c, field),
        place_address(c, o),
        field_layout(c, decl, member)
    );
    Operand r = { OPERAND_ADDRESS, REF_VALUE | address };
    if (place) {
        return r;
    }
    u8 after = c->files[file].tokens.kinds[token + 1];
    if (!is_aggregate(c, field)) {
        return load_at(c, field, address);
    }
    if (after != TK_LBRACK && after != TK_PERIOD) {
        note_unread(c, r);
    }
    return r;
}

static NOINLINE Operand
os_args(Checker *c, u32 file, u32 token, bool place)
{
    u32 decl = c->args;
    if (decl == 0) {
        report(c, file, token, DIAG_ARGS_NEED_CODE, 0, 0);
        return poison;
    }
    if (!in_body(c)) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        return poison;
    }

    u32 type = c->decls.types[decl];
    u32 at = emit(c, IR_GLOBAL, pointer_type(c, type), c->decls.values[decl], decl);
    if (place) {
        return (Operand){ OPERAND_ADDRESS, REF_VALUE | at };
    }
    return load_at(c, type, at);
}

static NOINLINE Operand
member_access(Checker *c, u32 file, u32 token, Operand o, bool place)
{
    if (o.type == TYPE_ID_TYPE && c->types.kinds[o.ref] == TYPE_ENUM) {
        return enum_member(c, file, token, o.ref, c->files[file].tokens.ids[token]);
    }
    if (is_value_type(o.type) && c->types.kinds[o.type] == TYPE_POINTER &&
        is_compound(c, c->types.arg0[o.type])) {
        o = (Operand){ OPERAND_ADDRESS, o.ref };
    }

    u32 type = is_place(o) ? place_type(c, o) : TYPE_ID_NONE;
    if (is_array(c, type)) {
        return array_field(c, file, token, o);
    }
    if (is_struct(c, type)) {
        return struct_field(c, file, token, o, place);
    }
    if ((is_place(o) && is_slice(c, type)) || is_pair(c, o)) {
        return slice_field(c, file, token, o);
    }

    u32 t = is_place(o) ? type : is_value_type(o.type) ? o.type : TYPE_ID_NONE;
    if (is_scalar(c, t)) {
        if (o.type == OPERAND_ADDRESS) {
            emit(c, IR_DROP, 0, value_of(o), 0);
        }
        if (is_optional_pointer(c, t) && is_nonnil(o)) {
            return member_access(c, file, token, narrowed_value(c, o), place);
        }
        if (is_optional_pointer(c, t)) {
            report(c, file, token, DIAG_MAYBE_NIL, t, 0);
        } else {
            report(c, file, token, DIAG_NO_MEMBER, c->files[file].tokens.ids[token], t);
        }
    } else if (o.type == OPERAND_PACKAGE && o.ref == NAME_ARGS) {
        return os_args(c, file, token, place);
    } else if (o.type == OPERAND_PACKAGE) {
        return (Operand){ OPERAND_BUILTIN, o.ref };
    } else if (o.type != TYPE_ID_NONE) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
    }
    return poison;
}

static u32
array_literal_type(Checker *c, u32 file, u32 m, u32 base, bool typed)
{
    u32 n = c->sp - m - 1;
    u32 token = c->stack_tokens[base];
    if (typed) {
        u32 elem = type_operand(c, file, token, c->stack[base]);
        if (elem && is_storable(c, elem)) {
            return intern_type(c, TYPE_ARRAY, elem, n);
        }
        if (elem) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        }
        return TYPE_ID_NONE;
    }

    u32 type = expected_type(c, c->sp - m);
    if (type != TYPE_ID_NONE && is_slice(c, type)) {
        type = intern_type(c, TYPE_ARRAY, c->types.arg0[type], n);
    }
    if (type == TYPE_ID_NONE) {
        report(c, file, token, DIAG_LITERAL_TYPE, 0, 0);
    } else if (!is_array(c, type)) {
        report(c, file, token, DIAG_NOT_ARRAY, 0, type);
        type = TYPE_ID_NONE;
    } else if (c->types.arg1[type] != n) {
        report(c, file, token, DIAG_LITERAL_COUNT, n, type);
        type = TYPE_ID_NONE;
    } else if (!is_storable(c, type)) {
        type = TYPE_ID_NONE;
    }
    return type;
}

static u32
base_address(Checker *c, u32 base, bool indirect)
{
    if (indirect) {
        return value_of(load_local(c, base));
    }
    return place_address(c, (Operand){ OPERAND_LOCAL, base });
}

static u32
hold_address(Checker *c, u32 node, u32 type, u32 address)
{
    u32 pointer = pointer_type(c, type);
    u32 slot = add_slot(c, 0, pointer, node);
    store_local(c, slot, (Operand){ pointer, REF_VALUE | address });
    return slot;
}

static void
fill_defaults(Checker *c, u32 node, u32 base, bool indirect, u32 type)
{
    if (is_array(c, type)) {
        u32 elem = c->types.arg0[type];
        u32 index = add_slot(c, 0, TYPE_ID_I64, node);
        store_local(c, index, (Operand){ TYPE_ID_I64, EXACT_ZERO });

        u32 loop = emit(c, IR_LOOP, 0, 0, 0);
        c->body.depth++;
        u32 i = value_of(load_local(c, index));
        u32 n = ir_const(c, TYPE_ID_I64, c->types.arg1[type]);
        emit(c, IR_IF, 0, emit(c, IR_LT, TYPE_ID_I64, i, n), 0);
        u32 at = emit(
            c,
            IR_INDEX,
            pointer_type(c, elem),
            base_address(c, base, indirect),
            value_of(load_local(c, index))
        );
        fill_defaults(c, node, hold_address(c, node, elem, at), true, elem);

        i = value_of(load_local(c, index));
        u32 next = emit(c, IR_ADD_WRAP, TYPE_ID_I64, i, ir_const(c, TYPE_ID_I64, 1));
        weigh(c, index);
        emit(c, IR_STORE, TYPE_ID_I64, index, next);
        emit(c, IR_REPEAT, 0, loop, 0);
        emit(c, IR_END, 0, 0, 0);
        emit(c, IR_END, 0, 0, 0);
        c->body.depth--;
        return;
    }

    u32 decl = c->types.arg0[type];
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    for (u32 k = first; k < end; k++) {
        Member m = c->members[k];
        if (!sets_default(c, m)) {
            continue;
        }
        u32 at = emit(
            c,
            IR_FIELD,
            pointer_type(c, m.type),
            base_address(c, base, indirect),
            field_layout(c, decl, k)
        );
        if (m.value < MEMBER_NONE) {
            emit(c, IR_STORE_AT, m.type, at, operand_ir(c, (Operand){ m.type, m.value }));
        } else {
            fill_defaults(c, node, hold_address(c, node, m.type, at), true, m.type);
        }
    }
}

static u32
zeroless_member(const Checker *c, u32 type)
{
    u32 decl = c->types.arg0[type];
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    for (u32 i = first; i < end; i++) {
        if (c->members[i].value == MEMBER_NONE && !has_zero(c, c->members[i].type)) {
            return i;
        }
    }
    assert(false && "a struct without a zero value has a field without one");
    return first;
}

static bool
can_zero(Checker *c, u32 file, u32 token, u32 type)
{
    switch (c->types.kinds[type]) {
        case TYPE_NONE: return false;
        case TYPE_ENUM:
            if (!has_zero(c, type)) {
                report(c, file, token, DIAG_NO_ZERO_MEMBER, 0, type);
                return false;
            }
            return true;
        case TYPE_POINTER:
        case TYPE_MANY:
        case TYPE_FN:
            report(c, file, token, DIAG_NEVER_NIL, 0, type);
            return false;
        case TYPE_STRUCT:
            if (!has_zero(c, type)) {
                Member m = c->members[zeroless_member(c, type)];
                if (is_tuple(c, type)) {
                    report(c, file, token, DIAG_NO_ZERO_ELEMENT, m.type, type);
                } else {
                    report(c, file, token, DIAG_NO_ZERO_FIELD, m.name, type);
                }
                return false;
            }
            return true;
        default:
            if (is_number(c, type) || type == TYPE_ID_BOOL ||
                is_slice(c, type) || c->types.kinds[type] == TYPE_OPTIONAL) {
                return true;
            }
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return false;
    }
}

static NOINLINE void
declare_zeroed(Checker *c, u32 file, u32 node, u32 token, u32 type_token)
{
    u32 t = local_type(c, file, type_token, type_operand(c, file, type_token, pop(c)));
    u32 elem = t;
    while (is_array(c, elem)) {
        elem = c->types.arg0[elem];
    }
    bool zero = can_zero(c, file, token, elem);

    bool aggregate = is_aggregate(c, t);
    Operand v = !zero     ? poison
              : aggregate ? (Operand){ TYPE_ID_UNDEFINED, 0 }
              : is_pair_type(c, t)
                  ? make_pair(c, t, ir_const(c, t, 0), ir_const(c, second_type(c, t), 0))
                  : (Operand){ t, EXACT_ZERO };
    u32 slot = declare_local(c, file, node, t, v);
    if (!zero || !aggregate) {
        return;
    }
    emit(c, IR_ZERO, t, place_address(c, (Operand){ OPERAND_LOCAL, slot }), 0);
    if (has_defaults(c, elem)) {
        fill_defaults(c, node, slot, false, t);
    }
}

static u32
first_result(const u8 *kinds, u32 list)
{
    u32 i = list - 1;
    u32 depth = 0;
    while (kinds[i] != NODE_TUPLE_START || depth > 0) {
        depth += kinds[i] == NODE_TUPLE_TYPE;
        depth -= kinds[i] == NODE_TUPLE_START;
        i--;
    }
    while (kinds[++i] != NODE_RESULT) {
    }
    return i;
}

static NOINLINE void
declare_results(Checker *c, u32 file, u32 list)
{
    const ParsedFile *f = &c->files[file];
    const u8 *kinds = f->nodes.kinds;
    const u32 *tokens = f->nodes.tokens;

    u32 i = first_result(kinds, list);
    bool single = i == list - 1;

    u32 ret = c->body.ret;
    u32 member = single || ret == TYPE_ID_NONE
                   ? NO_SLOT
                   : c->decls.members[c->types.arg0[ret]];

    for (; i < list; i++) {
        if (kinds[i] != NODE_RESULT) {
            continue;
        }
        u32 t = single ? ret : TYPE_ID_NONE;
        if (member != NO_SLOT) {
            t = c->members[member++].type;
        }
        if (t != TYPE_ID_NONE && !has_zero(c, t)) {
            report(c, file, tokens[i], DIAG_RESULT_ZERO, 0, t);
            t = TYPE_ID_NONE;
        }
        push_type(c, tokens[i - 1], t);
        declare_zeroed(c, file, i, tokens[i], tokens[i - 1]);
        mark_line(c, tokens[i]);
    }
}

static NOINLINE void
named_item(Checker *c, u32 file)
{
    u32 token = c->stack_tokens[c->sp - 1];
    Operand v = pop(c);
    pop(c);
    u32 m = c->sp - 1;
    while (c->stack[--m].type < OPERAND_MARKER) {
    }

    u32 kind = c->stack[m].type - OPERAND_MARKER;
    if ((kind == NODE_LITERAL_START || kind == NODE_ANON_LITERAL_START) &&
        !array_marker(c, m)) {
        Unread u = c->nunread > 0 ? c->unread[c->nunread - 1] : (Unread){ 0 };
        drop_unread(c, c->sp);
        if (u.at == c->sp + 1 && u.place.type == v.type && u.place.ref == v.ref) {
            u.at = c->sp;
            c->unread[c->nunread++] = u;
        }
        push_operand(c, token, v);
        return;
    }
    report(c, file, c->stack_tokens[c->sp - 1], DIAG_UNSUPPORTED, 0, 0);
    c->sp--;
    push_operand(c, token, poison);
}

static u32
literal_struct(Checker *c, u32 file, u32 token, u32 m, bool typed)
{
    u32 type;
    if (typed) {
        type = type_operand(c, file, token, c->stack[m - 1]);
    } else {
        type = expected_type(c, c->sp - m);
        if (type == TYPE_ID_NONE) {
            bool list = c->files[file].tokens.kinds[token] == TK_COMMA;
            report(c, file, token, list ? DIAG_UNSUPPORTED : DIAG_STRUCT_LITERAL_TYPE, 0, 0);
        }
    }
    if (type != TYPE_ID_NONE && !is_struct(c, type)) {
        report(c, file, token, DIAG_NOT_STRUCT, 0, type);
        return TYPE_ID_NONE;
    }
    if (type != TYPE_ID_NONE && !is_storable(c, type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return TYPE_ID_NONE;
    }
    return type;
}

static bool
literal_fields(Checker *c, u32 file, u32 m, u32 type, u32 token, u32 *items)
{
    u32 decl = c->types.arg0[type];
    u32 first = c->decls.members[decl];
    u32 count = c->decls.member_counts[decl];
    u32 given = 0, named = 0;
    bool ok = type != TYPE_ID_NONE;
    for (u32 k = m + 1; ok && k < c->sp; k++) {
        given++;
        if (c->stack[k].type != OPERAND_FIELD) {
            continue;
        }
        named++;
        u32 name = c->stack[k].ref;
        u32 name_token = c->stack_tokens[k];
        if (find_member(c, decl, name) == NO_SLOT) {
            report(c, file, name_token, DIAG_NO_MEMBER, name, type);
            ok = false;
        } else if (c->bound[name]) {
            report(c, file, name_token, DIAG_FIELD_TWICE, name, c->bound[name] - 1);
            ok = false;
        } else {
            c->bound[name] = name_token + 1;
        }
        k++;
    }
    *items = given;
    if (ok && named && named != given) {
        report(c, file, token, DIAG_MIXED_FIELDS, 0, 0);
        return false;
    }
    if (ok && !named && given && given != count) {
        bool list = c->files[file].tokens.kinds[token] == TK_COMMA;
        report(c, file, token, list ? DIAG_VALUE_COUNT : DIAG_FIELD_COUNT, count, given);
        return false;
    }
    if (!ok || given >= count) {
        return ok;
    }

    for (u32 i = first; i < first + count; i++) {
        Member mem = c->members[i];
        if (!c->bound[mem.name] && mem.value == MEMBER_NONE && !has_zero(c, mem.type)) {
            if (is_tuple(c, type)) {
                report(c, file, token, DIAG_NO_ZERO_ELEMENT, mem.type, type);
            } else {
                report(c, file, token, DIAG_MISSING_FIELD, mem.name, 0);
            }
            return false;
        }
    }
    return true;
}

static void
add_reloc(IrRelocs *r, u32 at)
{
    if (r->count == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 64;
        r->offsets = resize(r->offsets, r->cap, sizeof *r->offsets);
    }

    u32 k = r->count++;
    while (k && r->offsets[k - 1] > at) {
        r->offsets[k] = r->offsets[k - 1];
        k--;
    }
    r->offsets[k] = at;
}

static u32
first_reloc(const IrRelocs *r, u64 at)
{
    u32 lo = 0;
    u32 hi = r->count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (r->offsets[mid] < at) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static void
put_data(Checker *c, bool global, u32 at, u32 type, Operand v)
{
    u8 *to = (global ? c->ir.globals : c->ir.data) + at;
    IrRelocs *relocs = global ? &c->ir.global_relocs : &c->ir.data_relocs;
    if (v.type == TYPE_ID_UNTYPED_STRING) {
        u64 offset = c->string_offsets[value_of(v)];
        memcpy(to, &offset, sizeof offset);
        add_reloc(relocs, at);
        if (type == TYPE_ID_STRING) {
            u64 n = c->string_lengths[value_of(v)];
            memcpy(to + 8, &n, sizeof n);
        }
        return;
    }
    if (!is_aggregate(c, type)) {
        put_bits(c, to, type, v);
        return;
    }

    u32 from = value_of(v);
    u64 size = check_type_size(c, type);
    assert(global || from + size <= at);
    memcpy(to, c->ir.data + from, size);

    const IrRelocs *own = &c->ir.data_relocs;
    u32 end = first_reloc(own, from + size);
    for (u32 k = first_reloc(own, from); k < end; k++) {
        add_reloc(relocs, at + (own->offsets[k] - from));
    }
}

static u32
hex_value(const char *p, u32 digits)
{
    u32 v = 0;
    for (u32 k = 0; k < digits; k++) {
        u8 ch = (u8)p[k];
        v = v << 4 | (ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
    }
    return v;
}

static u32
put_utf8(u8 *to, u32 cp)
{
    if (cp < 0x80) {
        to[0] = (u8)cp;
        return 1;
    }
    if (cp < 0x800) {
        to[0] = (u8)(0xC0 | cp >> 6);
        to[1] = (u8)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        to[0] = (u8)(0xE0 | cp >> 12);
        to[1] = (u8)(0x80 | (cp >> 6 & 0x3F));
        to[2] = (u8)(0x80 | (cp & 0x3F));
        return 3;
    }
    to[0] = (u8)(0xF0 | cp >> 18);
    to[1] = (u8)(0x80 | (cp >> 12 & 0x3F));
    to[2] = (u8)(0x80 | (cp >> 6 & 0x3F));
    to[3] = (u8)(0x80 | (cp & 0x3F));
    return 4;
}

static u8
escaped_byte(char e)
{
    switch (e) {
        case '0': return 0;
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        default: return (u8)e;
    }
}

static u32
escape_value(const char *s, u32 *value)
{
    if (s[1] == 'x') {
        *value = hex_value(s + 2, 2);
        return 4;
    }
    if (s[1] != 'u') {
        *value = escaped_byte(s[1]);
        return 2;
    }

    u32 cp = hex_value(s + 2, 4);
    if (cp - 0xD800u >= 0x800) {
        *value = cp;
        return 6;
    }

    bool paired = cp < 0xDC00 && s[6] == '\\' && s[7] == 'u';
    u32 low = paired ? hex_value(s + 8, 4) : 0;
    if (low - 0xDC00u >= 0x400) {
        return 0;
    }
    *value = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    return 12;
}

static NOINLINE Operand
string_literal(Checker *c, u32 file, u32 token)
{
    const ParsedFile *f = &c->files[file];
    u32 start = f->tokens.starts[token];
    const char *s = f->src + start + 1;
    u32 raw = lexer_token_length(f->src, TK_STRING, start) - 2;
    u32 at = reserve_bytes(c, file, token, (u64)raw + 1, 1);
    if (at == NO_SLOT) {
        return poison;
    }

    u8 *to = c->ir.data + at;
    u32 n = 0;
    u32 k = 0;
    for (;;) {
        const char *slash = memchr(s + k, '\\', raw - k);
        u32 run = slash ? (u32)(slash - s) - k : raw - k;
        memcpy(to + n, s + k, run);
        n += run;
        k += run;
        if (k == raw) {
            break;
        }

        u32 cp;
        u32 len = escape_value(s + k, &cp);
        if (len == 0) {
            report(c, file, token, DIAG_SURROGATE, 0, 0);
            c->ir.ndata = at;
            return poison;
        }
        if (s[k + 1] == 'u') {
            n += put_utf8(to + n, cp);
        } else {
            to[n++] = (u8)cp;
        }
        k += len;
    }
    to[n] = 0;
    c->ir.ndata = at + n + 1;

    if (c->nstrings == c->strings_cap) {
        c->strings_cap *= 2;
        c->string_offsets = resize(c->string_offsets, c->strings_cap, sizeof *c->string_offsets);
        c->string_lengths = resize(c->string_lengths, c->strings_cap, sizeof *c->string_lengths);
    }
    c->string_offsets[c->nstrings] = at;
    c->string_lengths[c->nstrings] = n;
    return (Operand){ TYPE_ID_UNTYPED_STRING, c->nstrings++ };
}

static u32
utf8_value(const u8 *s, u32 *cp)
{
    u8 b = s[0];
    u32 n = b < 0xC2 ? 0 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF5 ? 4 : 0;
    if (n == 0) {
        return 0;
    }

    u32 v = b & (0x7Fu >> n);
    for (u32 i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        v = v << 6 | (s[i] & 0x3Fu);
    }

    u32 least = n == 2 ? 0x80 : n == 3 ? 0x800 : 0x10000;
    if (v < least || v > 0x10FFFF || v - 0xD800u < 0x800) {
        return 0;
    }
    *cp = v;
    return n;
}

static NOINLINE Operand
char_literal(Checker *c, u32 file, u32 token)
{
    const ParsedFile *f = &c->files[file];
    u32 start = f->tokens.starts[token];
    const char *s = f->src + start + 1;
    u32 raw = lexer_token_length(f->src, TK_CHAR, start) - 2;

    u32 cp = (u8)s[0];
    u32 n = 1;
    if (s[0] == '\\') {
        n = escape_value(s, &cp);
        if (n == 0) {
            report(c, file, token, DIAG_SURROGATE, 0, 0);
            return poison;
        }
    } else if (cp >= 0x80) {
        n = utf8_value((const u8 *)s, &cp);
        if (n == 0) {
            report(c, file, token, DIAG_CHAR_UTF8, 0, 0);
            return poison;
        }
    }

    if (n != raw) {
        report(c, file, token, DIAG_CHAR_COUNT, 0, 0);
        return poison;
    }
    return (Operand){ TYPE_ID_UNTYPED_INT, exact_int(&c->exacts, cp) };
}

static bool
constant_items(const Checker *c, u32 first, u32 n, u32 elem)
{
    if (!is_number(c, elem)) {
        return false;
    }
    for (u32 k = first; k < first + n; k++) {
        if (!is_constant(c->stack[k])) {
            return false;
        }
    }
    return true;
}

static NOINLINE bool
fill_from_data(Checker *c, u32 file, u32 token, u32 first, u32 n, u32 slot, u32 type)
{
    u32 elem = c->types.arg0[type];
    bool ok = true;
    for (u32 k = first; k < first + n; k++) {
        c->stack[k] = convert(c, file, c->stack_tokens[k], c->stack[k], elem);
        ok = ok && c->stack[k].type != TYPE_ID_NONE;
    }
    u32 at = ok ? reserve_data(c, file, token, type) : NO_SLOT;
    if (at == NO_SLOT) {
        return false;
    }

    u64 size = check_type_size(c, elem);
    for (u32 k = 0; k < n; k++) {
        put_bits(c, c->ir.data + at + k * size, elem, c->stack[first + k]);
    }
    u32 data = emit(c, IR_DATA, pointer_type(c, type), at, 0);
    emit(c, IR_COPY, type, place_address(c, (Operand){ OPERAND_LOCAL, slot }), data);
    return true;
}

static NOINLINE void
array_literal(Checker *c, u32 file, u32 node, u8 kind)
{
    bool typed = kind == NODE_ARRAY_LITERAL;
    u32 m = list_start(c, typed ? NODE_LITERAL_START : NODE_ANON_LITERAL_START);
    u32 n = c->sp - m - 1;
    u32 base = typed ? m - 1 : m;
    u32 token = c->stack_tokens[base];
    u32 type = array_literal_type(c, file, m, base, typed);

    bool ok = type != TYPE_ID_NONE;
    Operand array = { OPERAND_LOCAL, ok ? result_slot(c, file, node, base, type) : 0 };
    u32 elem = c->types.arg0[type];
    if (ok && n && constant_items(c, m + 1, n, elem)) {
        ok = fill_from_data(c, file, token, m + 1, n, array.ref, type);
    } else if (ok) {
        ok = fill_array(c, file, m + 1, n, array.ref, elem);
    }

    c->sp = base;
    drop_unread(c, base);
    push_operand(c, token, ok ? array : poison);
}

static NOINLINE void
struct_literal(Checker *c, u32 file, u32 node, u8 kind)
{
    bool typed = kind == NODE_LITERAL;
    u32 m = list_start(c, typed ? NODE_LITERAL_START : NODE_ANON_LITERAL_START);
    u32 base = typed ? m - 1 : m;
    u32 token = c->stack_tokens[base];
    u32 type = literal_struct(c, file, token, m, typed);
    u32 items = 0;
    bool ok = literal_fields(c, file, m, type, token, &items);

    u32 decl = c->types.arg0[type];
    u32 count = c->decls.member_counts[decl];
    u32 slot = ok ? result_slot(c, file, node, base, type) : 0;
    bool zeroed = false;
    if (ok && items < count) {
        emit(c, IR_ZERO, type, place_address(c, (Operand){ OPERAND_LOCAL, slot }), 0);
        if (c->decls.flags[decl] & DECL_DEFAULTS) {
            fill_defaults(c, node, slot, false, type);
        }
        zeroed = true;
    }

    u32 next = c->decls.members[decl];
    for (u32 k = m + 1; type != TYPE_ID_NONE && k < c->sp; k++) {
        u32 member = next++;
        if (c->stack[k].type == OPERAND_FIELD) {
            u32 name = c->stack[k++].ref;
            c->bound[name] = 0;
            member = find_member(c, decl, name);
        }
        if (!ok || member == NO_SLOT) {
            continue;
        }

        u32 field = c->members[member].type;
        u32 to = stored_type(c, field, c->stack[k]);
        Operand v = convert(c, file, c->stack_tokens[k], c->stack[k], to);
        if (v.type == TYPE_ID_NONE) {
            ok = false;
            continue;
        }
        u32 block = c->nunread && is_place(v) && is_aggregate(c, field)
                      ? early_block(c, k, v)
                      : NO_SLOT;
        if (block != NO_SLOT && !zeroed) {
            fill_block(c, block, slot, IR_FIELD, field_layout(c, decl, member), field, v);
            continue;
        }
        if (block != NO_SLOT) {
            u32 copy = add_slot(c, 0, field, node);
            fill_block(c, block, copy, IR_NOP, 0, field, v);
            v = (Operand){ OPERAND_LOCAL, copy };
        }
        u32 at = emit(
            c,
            IR_FIELD,
            pointer_type(c, field),
            place_address(c, (Operand){ OPERAND_LOCAL, slot }),
            field_layout(c, decl, member)
        );
        store_at(c, to, at, v);
    }

    c->sp = base;
    drop_unread(c, base);
    push_operand(c, token, ok ? (Operand){ OPERAND_LOCAL, slot } : poison);
}

static NOINLINE u32
split_tuple(Checker *c, u32 file, u32 node, u32 token, u32 targets, u32 n)
{
    u32 v_token = c->stack_tokens[c->sp - 1];
    Operand v = c->stack[c->sp - 1];
    u32 type = is_place(v) ? place_type(c, v) : v.type;
    if (type == TYPE_ID_NONE) {
        return NO_SLOT;
    }
    if (!is_value_type(type)) {
        report(c, file, v_token, DIAG_NOT_VALUE, 0, 0);
        return NO_SLOT;
    }
    if (!is_tuple(c, type)) {
        report(c, file, v_token, DIAG_NOT_TUPLE, type, 0);
        return NO_SLOT;
    }
    u32 decl = c->types.arg0[type];
    if (c->decls.member_counts[decl] != n) {
        report(c, file, token, DIAG_VALUE_COUNT, n, c->decls.member_counts[decl]);
        return NO_SLOT;
    }

    if (!is_place(v)) {
        Pair p = pair_of(c, v);
        u32 first = c->sp;
        reserve_stack(c, 2);
        push_operand(c, v_token, (Operand){ c->members[c->decls.members[decl]].type, p.first });
        push_operand(c, v_token, (Operand){ TYPE_ID_BOOL, p.second });
        return first;
    }

    u32 needed = 0;
    for (u32 i = 0; i < n; i++) {
        needed += c->stack[targets + i].type != OPERAND_DISCARD;
    }
    u32 held = NO_SLOT;
    if (v.type == OPERAND_ADDRESS && needed == 0) {
        emit(c, IR_DROP, 0, value_of(v), 0);
    } else if (v.type == OPERAND_ADDRESS && needed > 1) {
        held = hold_address(c, node, type, value_of(v));
    }

    u32 first = c->sp;
    reserve_stack(c, n);
    u32 member = c->decls.members[decl];
    for (u32 i = 0; i < n; i++, member++) {
        Operand e = poison;
        if (c->stack[targets + i].type != OPERAND_DISCARD) {
            u32 base = v.type == OPERAND_LOCAL ? place_address(c, v)
                     : held != NO_SLOT         ? value_of(load_local(c, held))
                                               : value_of(v);
            u32 elem = c->members[member].type;
            u32 at = emit(c, IR_FIELD, pointer_type(c, elem), base, field_layout(c, decl, member));
            e = is_aggregate(c, elem) ? (Operand){ OPERAND_ADDRESS, REF_VALUE | at }
                                      : load_at(c, elem, at);
        }
        push_operand(c, v_token, e);
    }
    return first;
}

static NOINLINE void
settle_values(Checker *c, u32 node, u32 first, bool snapshot)
{
    for (u32 k = first; k < c->sp; k++) {
        Operand v = c->stack[k];
        if (!is_place(v)) {
            continue;
        }
        u32 block = c->nunread ? early_block(c, k, v) : NO_SLOT;
        if (block == NO_SLOT && !(snapshot && k > first && may_change(c, v))) {
            continue;
        }

        u32 type = place_type(c, v);
        Operand copy = { OPERAND_LOCAL, add_slot(c, 0, type, node) };
        if (block != NO_SLOT) {
            fill_block(c, block, copy.ref, IR_NOP, 0, type, v);
        } else {
            copy_place(c, copy, v);
        }
        c->stack[k] = copy;
    }
}

static NOINLINE void
destructure(Checker *c, u32 file, u32 node, u32 token, u8 kind)
{
    u32 t = list_start(c, NODE_TARGETS_START);
    u32 m = t + 1;
    while (c->stack[m].type < OPERAND_MARKER) {
        m++;
    }
    u32 n = m - t - 1;

    u32 first = m + 1;
    if (c->stack[m].type == OPERAND_MARKER + NODE_DESTRUCTURE_VALUE) {
        first = split_tuple(c, file, node, token, t + 1, n);
    } else if (c->sp - first != n) {
        report(c, file, token, DIAG_VALUE_COUNT, n, c->sp - first);
        first = NO_SLOT;
    } else {
        settle_values(c, node, first, kind == NODE_DESTRUCTURE_ASSIGN);
    }

    for (u32 i = 0; i < n; i++) {
        Operand target = c->stack[t + 1 + i];
        u32 target_token = c->stack_tokens[t + 1 + i];
        Operand v = first != NO_SLOT ? c->stack[first + i] : poison;
        u32 v_token = first != NO_SLOT ? c->stack_tokens[first + i] : token;
        if (target.type == OPERAND_TARGET) {
            u32 type = is_place(v) ? place_type(c, v)
                                   : value_type(c, file, v_token, v);
            declare_local(c, file, target.ref, type, type ? convert(c, file, v_token, v, type) : poison);
        } else {
            assign_to(c, file, node, token, NODE_ASSIGN, target, target_token, v, v_token);
        }
    }

    c->sp = t;
    drop_unread(c, t);
    push(c, token, OPERAND_STMT, 0);
}

static u64
global_size(Checker *c, u32 type)
{
    u64 size = check_type_size(c, type);
    return size ? size : 1;
}

static u32
reserve_global(Checker *c, u32 file, u32 token, u32 type)
{
    Ir *ir = &c->ir;
    u64 size = global_size(c, type);
    u32 align = check_type_align(c, type);
    u32 at = (ir->nglobals + align - 1) & ~(align - 1);
    u64 used = (u64)at + c->zeroed + 16;
    if (used > GLOBALS_LIMIT || size > GLOBALS_LIMIT - used) {
        report(c, file, token, DIAG_GLOBALS_TOO_LARGE, 0, 0);
        return NO_SLOT;
    }

    u32 end = at + (u32)size;
    if (end >= ir->globals_cap) {
        ir->globals_cap = end < GLOBALS_LIMIT / 2 ? 2 * end + 64 : GLOBALS_LIMIT;
        ir->globals = resize(ir->globals, ir->globals_cap, 1);
    }
    memset(ir->globals + ir->nglobals, 0, end - ir->nglobals);
    ir->nglobals = end;
    return at;
}

static u32
reserve_zeroed(Checker *c, u32 file, u32 token, u32 type)
{
    u64 size = global_size(c, type);
    u32 align = check_type_align(c, type);
    u32 at = (c->zeroed + align - 1) & ~(align - 1);
    u64 used = (u64)at + c->ir.nglobals + 16;
    if (used > GLOBALS_LIMIT || size > GLOBALS_LIMIT - used) {
        report(c, file, token, DIAG_GLOBALS_TOO_LARGE, 0, 0);
        return NO_SLOT;
    }

    c->zeroed = at + (u32)size;
    c->zeroed_align = align > c->zeroed_align ? align : c->zeroed_align;
    return at;
}

static void
put_defaults(Checker *c, u8 *to, u32 type)
{
    if (is_array(c, type)) {
        u32 elem = c->types.arg0[type];
        u64 size = check_type_size(c, elem);
        for (u64 k = 0; k < c->types.arg1[type]; k++) {
            put_defaults(c, to + k * size, elem);
        }
        return;
    }

    u32 decl = c->types.arg0[type];
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    for (u32 k = first; k < end; k++) {
        Member m = c->members[k];
        if (!sets_default(c, m)) {
            continue;
        }
        u8 *field = to + check_field_offset(c, field_layout(c, decl, k));
        if (m.value < MEMBER_NONE) {
            put_bits(c, field, m.type, (Operand){ m.type, m.value });
        } else {
            put_defaults(c, field, m.type);
        }
    }
}

static Operand
constant_item(Checker *c, u32 file, u32 token, Operand v, u32 type)
{
    if (v.type == TYPE_ID_UNTYPED_STRING &&
        (points_to_bytes(c, type) || type == TYPE_ID_STRING)) {
        return v;
    }
    Operand x = convert(c, file, token, v, type);
    if (x.type != TYPE_ID_NONE && !is_constant(x)) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        return poison;
    }
    return x;
}

static NOINLINE void
constant_array(Checker *c, u32 file, u8 kind)
{
    bool typed = kind == NODE_ARRAY_LITERAL;
    u32 m = list_start(c, typed ? NODE_LITERAL_START : NODE_ANON_LITERAL_START);
    u32 n = c->sp - m - 1;
    u32 base = typed ? m - 1 : m;
    u32 token = c->stack_tokens[base];
    u32 type = array_literal_type(c, file, m, base, typed);

    u32 elem = c->types.arg0[type];
    bool ok = type != TYPE_ID_NONE;
    for (u32 k = 0; type && k < n; k++) {
        Operand x = constant_item(c, file, c->stack_tokens[m + 1 + k], c->stack[m + 1 + k], elem);
        ok = ok && x.type != TYPE_ID_NONE;
        c->stack[m + 1 + k] = x;
    }

    u64 size = check_type_size(c, elem);
    u32 first = n ? value_of(c->stack[m + 1]) : 0;
    bool adopt = ok && n && is_aggregate(c, elem);
    for (u32 k = 1; adopt && k < n; k++) {
        adopt = value_of(c->stack[m + 1 + k]) == first + k * size;
    }

    u32 at = !ok ? NO_SLOT : adopt ? first : reserve_data(c, file, token, type);
    for (u32 k = 0; at != NO_SLOT && !adopt && k < n; k++) {
        put_data(c, false, at + (u32)(k * size), elem, c->stack[m + 1 + k]);
    }

    c->sp = base;
    push_operand(c, token, at != NO_SLOT ? (Operand){ type, at } : poison);
}

static NOINLINE void
constant_struct(Checker *c, u32 file, u8 kind)
{
    bool typed = kind == NODE_LITERAL;
    u32 m = list_start(c, typed ? NODE_LITERAL_START : NODE_ANON_LITERAL_START);
    u32 base = typed ? m - 1 : m;
    u32 token = c->stack_tokens[base];
    u32 type = literal_struct(c, file, token, m, typed);
    u32 items = 0;
    bool ok = literal_fields(c, file, m, type, token, &items);

    u32 at = ok ? reserve_data(c, file, token, type) : NO_SLOT;
    if (at != NO_SLOT) {
        put_defaults(c, c->ir.data + at, type);
    }

    u32 decl = c->types.arg0[type];
    u32 next = c->decls.members[decl];
    for (u32 k = m + 1; type != TYPE_ID_NONE && k < c->sp; k++) {
        u32 member = next++;
        if (c->stack[k].type == OPERAND_FIELD) {
            u32 name = c->stack[k++].ref;
            c->bound[name] = 0;
            member = find_member(c, decl, name);
        }
        if (at == NO_SLOT || member == NO_SLOT) {
            continue;
        }

        u32 field = c->members[member].type;
        Operand v = constant_item(c, file, c->stack_tokens[k], c->stack[k], field);
        if (v.type == TYPE_ID_NONE) {
            at = NO_SLOT;
            continue;
        }
        put_data(c, false, at + (u32)check_field_offset(c, field_layout(c, decl, member)), field, v);
    }

    c->sp = base;
    push_operand(c, token, at != NO_SLOT ? (Operand){ type, at } : poison);
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
        c->breaks_cap *= 2;
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

static NOINLINE void
return_aggregate_peer(Checker *c, u32 file, u32 token, Peers *peers, u8 kind, Operand v)
{
    u32 type = place_type(c, v);
    if (kind != IR_RET) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return;
    }
    if (!is_passed(c, type)) {
        report_unpassed(c, file, token, type);
        return;
    }
    u32 address = place_address(c, v);
    u32 at = emit(c, IR_RET, 0, address, 0);
    add_peer(c, peers, (Break){ { type, REF_VALUE | address }, token, at, 0 });
}

static void
exit_to_peers(Checker *c, u32 file, u32 token, Peers *peers, u8 kind, u32 loop, Operand v)
{
    if (is_place(v) && is_slice(c, place_type(c, v))) {
        v = read_slice(c, v);
    }
    if (v.type == TYPE_ID_UNTYPED_STRING) {
        v = convert(c, file, token, v, TYPE_ID_STRING);
    }
    if (is_place(v) && is_aggregate(c, place_type(c, v))) {
        u32 vector = kind != IR_RET ? lane_vector(c, place_type(c, v)) : 0;
        if (!vector) {
            return_aggregate_peer(c, file, token, peers, kind, v);
            return;
        }
        v = load_lanes(c, NO_SLOT, v, vector);
    }
    if (v.type == TYPE_ID_NONE) {
        return;
    }
    if (!is_value_type(v.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return;
    }
    if (kind == IR_RET && is_vector(c, v.type)) {
        return_aggregate_peer(c, file, token, peers, kind, materialize(c, lane_array(c, v.type), v));
        return;
    }
    if (is_pair(c, v)) {
        if (kind != IR_RET) {
            report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
            return;
        }
        Pair p = pair_of(c, v);
        u32 at = emit(c, IR_RET, 0, p.first, p.second);
        add_peer(c, peers, (Break){ v, token, at, 0 });
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

static Operand
result_value(Checker *c, u32 file, u32 node)
{
    u64 binding = c->bindings[node_name(c, file, node)];
    u32 slot = (u32)binding;
    if ((u32)(binding >> 32) != c->body.owner || slot_at(c, slot)->node != node) {
        return poison;
    }

    u32 type = slot_at(c, slot)->type;
    if (c->types.kinds[type] == TYPE_OPTIONAL) {
        return read_optional(c, slot);
    }
    if (is_slice(c, type)) {
        return read_slice(c, (Operand){ OPERAND_LOCAL, slot });
    }
    if (is_aggregate(c, type)) {
        return (Operand){ OPERAND_LOCAL, slot };
    }
    return load_local(c, slot);
}

static NOINLINE Operand
named_results(Checker *c, u32 file, u32 token, u32 ret)
{
    if (!(c->decls.flags[c->body.decl] & DECL_RESULTS)) {
        if (c->body.decl == c->entry) {
            return (Operand){ TYPE_ID_I32, exact_int(&c->exacts, 0) };
        }
        report(c, file, token, DIAG_RETURN_EMPTY, 0, ret);
        return poison;
    }
    if (ret == TYPE_ID_NONE) {
        return poison;
    }

    const u8 *kinds = c->files[file].nodes.kinds;
    u32 list = c->decls.bodies[c->body.decl] - 2;

    u32 i = first_result(kinds, list);
    if (i == list - 1) {
        return result_value(c, file, i);
    }

    reserve_stack(c, list - i);
    u32 base = c->sp;
    for (; i < list; i++) {
        if (kinds[i] == NODE_RESULT) {
            push_operand(c, token, result_value(c, file, i));
        }
    }

    u32 decl = c->types.arg0[ret];
    u32 member = c->decls.members[decl];
    u32 slot = add_slot(c, 0, ret, list);
    for (u32 k = base; k < c->sp; k++, member++) {
        u32 field = c->members[member].type;
        u32 to = stored_type(c, field, c->stack[k]);
        Operand v = convert(c, file, token, c->stack[k], to);
        if (v.type == TYPE_ID_NONE) {
            continue;
        }
        u32 at = emit(
            c,
            IR_FIELD,
            pointer_type(c, field),
            place_address(c, (Operand){ OPERAND_LOCAL, slot }),
            field_layout(c, decl, member)
        );
        store_at(c, to, at, v);
    }
    c->sp = base;
    return (Operand){ OPERAND_LOCAL, slot };
}

static ALWAYS_INLINE void
emit_return(Checker *c, u32 file, u32 token, Operand v, u32 ret)
{
    v = convert(c, file, token, v, ret);
    if (is_pair(c, v)) {
        Pair p = pair_of(c, v);
        emit(c, IR_RET, ret, p.first, p.second);
    } else if (is_place(v)) {
        emit(c, IR_RET, ret, place_address(c, v), 0);
    } else if (v.type != TYPE_ID_NONE) {
        emit(c, IR_RET, ret, operand_ir(c, v), 0);
    }
}

static NOINLINE void
return_results(Checker *c, u32 file, u32 token, u32 ret)
{
    emit_return(c, file, token, named_results(c, file, token, ret), ret);
}

static ALWAYS_INLINE void
finish_return(Checker *c, u32 file, u32 token, u32 v_token, Operand v, bool has_value)
{
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
            return_results(c, file, token, ret);
        }
        return;
    }
    if (returns_nothing(c, ret)) {
        if (v.type != TYPE_ID_NONE) {
            report(c, file, token, DIAG_RETURN_VALUE, 0, 0);
        }
        return;
    }

    emit_return(c, file, v_token, v, ret);
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
reserve_code(IrCode *code, u32 count, u32 nextra, u32 nslots, u32 nconsts, u32 nlines)
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
    code->lines = grow(
        code->lines,
        &code->lines_cap,
        code->nlines + nlines,
        sizeof *code->lines
    );
}

static void
append_code(IrCode *to, const IrCode *from, u32 first, u32 first_extra, u32 first_slot, u32 first_const, u32 first_line)
{
    u32 count = from->count - first;
    u32 nextra = from->nextra - first_extra;
    u32 nslots = from->nslots - first_slot;
    u32 nconsts = from->nconsts - first_const;
    u32 nlines = from->nlines - first_line;
    reserve_code(to, count, nextra, nslots, nconsts, nlines);

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
    if (nlines) {
        memcpy(
            to->lines + to->nlines,
            from->lines + first_line,
            nlines * sizeof *to->lines
        );
    }
    to->count += count;
    to->nextra += nextra;
    to->nslots += nslots;
    to->nconsts += nconsts;
    to->nlines += nlines;
}

static u32
closing_node(const u8 *kinds, u32 node, u8 start, u8 end, u8 item)
{
    u32 depth = 0;
    for (u32 i = node + 1;; i++) {
        depth += kinds[i] == start;
        if ((kinds[i] == end || kinds[i] == item) && depth-- == 0) {
            return i;
        }
    }
}

static NOINLINE void
drop_assigned(Checker *c, u32 file, u32 node, u8 start, u8 end)
{
    const ParsedFile *f = &c->files[file];
    const u8 *kinds = f->nodes.kinds;
    u32 last = closing_node(kinds, node, start, end, end);
    for (u32 i = node + 1; i < last; i++) {
        if (kinds[i] != NODE_NAME || !takes_place[kinds[i + 1]]) {
            continue;
        }
        u64 binding = c->bindings[f->tokens.ids[f->nodes.tokens[i]]];
        if ((u32)(binding >> 32) == c->body.owner) {
            drop_narrow(c, (u32)binding);
        }
    }
}

static bool
in_defer(const Checker *c, u32 from)
{
    for (u32 k = from; k < c->ndefers; k++) {
        if (c->defers[k].open) {
            return true;
        }
    }
    return false;
}

static NOINLINE Operand
pin(Checker *c, Operand v)
{
    if (!is_place(v) || !may_change(c, v)) {
        return v;
    }
    u32 slot = add_slot(c, 0, place_type(c, v), c->decls.roots[c->body.decl]);
    Operand copy = { OPERAND_LOCAL, slot };
    copy_place(c, copy, v);
    return copy;
}

static u32
moved(const Defer *d, u32 shift, u32 v)
{
    return (v & IR_CONST) || v < d->origin ? v : v + shift;
}

static u32
copy_list(Checker *c, const Defer *d, u32 shift, u32 list, u32 n, u32 lo, u32 hi)
{
    IrCode *code = c->body.code;
    code->extra = grow(
        code->extra,
        &code->extra_cap,
        code->nextra + n,
        sizeof *code->extra
    );
    u32 from = c->body.first_extra + list;
    u32 to = code->nextra;
    for (u32 k = 0; k < n; k++) {
        u32 w = code->extra[from + k];
        code->extra[to + k] = k >= lo && k < hi ? moved(d, shift, w) : w;
    }
    code->nextra = to + n;
    return to - c->body.first_extra;
}

static NOINLINE void
copy_defer(Checker *c, const Defer *d)
{
    IrCode *code = c->body.code;
    const IrCode *from = &c->defer_code;
    u32 shift = code->count - c->body.first - d->origin;
    if (d->count > c->openers_cap) {
        c->openers_cap = d->count * 2;
        c->openers = resize(c->openers, c->openers_cap, 1);
    }
    u8 *openers = c->openers;
    u32 depth = 0;
    for (u32 k = d->first; k < d->first + d->count; k++) {
        u8 kind = from->kinds[k];
        u32 type = from->types[k];
        u32 a = from->as[k];
        u32 b = from->bs[k];
        switch (kind) {
            case IR_LOAD:
            case IR_ADDR: weigh(c, a); break;
            case IR_STORE:
                weigh(c, a);
                b = moved(d, shift, b);
                break;
            case IR_DATA:
            case IR_GLOBAL:
            case IR_C_GLOBAL:
            case IR_CALLBACK:
            case IR_SECOND:
            case IR_NOP:
            case IR_CASE: break;
            case IR_LOAD_AT:
            case IR_FIELD:
            case IR_NEG:
            case IR_NEG_WRAP:
            case IR_NOT:
            case IR_BIT_NOT:
            case IR_TRAILING_ZEROS:
            case IR_LEADING_ZEROS:
            case IR_COUNT_ONES:
            case IR_CONVERT:
            case IR_NARROW:
            case IR_TRUNCATE:
            case IR_BITCAST:
            case IR_CHECK_ALIGN:
            case IR_CHECK_NIL:
            case IR_SPLAT:
            case IR_TOP_BITS:
            case IR_ZERO:
            case IR_DROP:
            case IR_ASSERT:
            case IR_ASSUME: a = moved(d, shift, a); break;
            case IR_INDEX:
            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
            case IR_DIV:
            case IR_REM:
            case IR_ADD_WRAP:
            case IR_SUB_WRAP:
            case IR_MUL_WRAP:
            case IR_SHL:
            case IR_SHR:
            case IR_AND:
            case IR_OR:
            case IR_XOR:
            case IR_EQ:
            case IR_NE:
            case IR_LT:
            case IR_LE:
            case IR_GT:
            case IR_GE:
            case IR_ADD_OVERFLOWS:
            case IR_SUB_OVERFLOWS:
            case IR_MUL_OVERFLOWS:
            case IR_LOOKUP:
            case IR_STORE_AT:
            case IR_COPY:
                a = moved(d, shift, a);
                b = moved(d, shift, b);
                break;
            case IR_INDEX_LEN:
            case IR_SLICE_FROM:
            case IR_STORE_PAIR:
                a = moved(d, shift, a);
                b = copy_list(c, d, shift, b, 2, 0, 2);
                break;
            case IR_SLICE:
                a = moved(d, shift, a);
                b = copy_list(c, d, shift, b, 3, 0, 3);
                break;
            case IR_CALL:
            case IR_CALL_C: {
                u32 n = code->extra[c->body.first_extra + b];
                b = copy_list(c, d, shift, b, n + 1 + is_aggregate(c, type), 1, n + 1);
                break;
            }
            case IR_BLOCK: openers[depth++] = kind; break;
            case IR_LOOP:
                openers[depth++] = kind;
                c->body.depth++;
                break;
            case IR_IF:
            case IR_SWITCH:
                a = moved(d, shift, a);
                openers[depth++] = kind;
                break;
            case IR_ELSE:
                if (type != TYPE_ID_NONE && openers[depth - 1] == IR_IF) {
                    a = moved(d, shift, a);
                }
                break;
            case IR_END:
                depth--;
                c->body.depth -= openers[depth] == IR_LOOP;
                if (type != TYPE_ID_NONE && openers[depth] == IR_IF) {
                    a = moved(d, shift, a);
                }
                break;
            case IR_EXIT:
                a = moved(d, shift, a);
                if (type != TYPE_ID_NONE) {
                    b = moved(d, shift, b);
                }
                break;
            case IR_REPEAT: a = moved(d, shift, a); break;
            default: assert(false);
        }
        emit(c, kind, type, a, b);
    }

    code->lines = grow(
        code->lines,
        &code->lines_cap,
        code->nlines + d->nlines,
        sizeof *code->lines
    );
    for (u32 k = d->first_line; k < d->first_line + d->nlines; k++) {
        IrLine line = from->lines[k];
        code->lines[code->nlines++] = (IrLine){ line.end + shift, line.token };
        c->body.marked = line.end + shift;
    }
}

static NOINLINE void
run_defers(Checker *c, u32 from, u32 token)
{
    u32 file = c->decls.files[c->body.decl];
    mark_line(c, token);
    for (u32 k = c->ndefers; k-- > from;) {
        const Defer *d = &c->defers[k];
        assert(!d->open);
        copy_defer(c, d);
        if (c->nfacts != c->body.facts) {
            drop_assigned(c, file, d->node, NODE_DEFER_START, NODE_DEFER);
        }
        if (!d->ends) {
            c->body.reachable = false;
            return;
        }
    }
}

static NOINLINE COLD void
end_defers(Checker *c, u32 m, u32 token)
{
    u32 k = c->ndefers;
    while (k > c->body.defers && c->defers[k - 1].at > m) {
        k--;
    }
    if (k == c->ndefers) {
        return;
    }
    if (c->body.reachable) {
        run_defers(c, k, token);
    }
    c->ndefers = k;
    c->defer_code.count = k > 0 ? c->defers[k - 1].first + c->defers[k - 1].count : 0;
    c->defer_code.nlines = k > 0
                             ? c->defers[k - 1].first_line + c->defers[k - 1].nlines
                             : 0;
}

static NOINLINE COLD void
leave_body(Checker *c, u32 file, u32 token, u32 v_token, Operand v, bool has_value)
{
    if (in_defer(c, c->body.defers)) {
        report(c, file, token, DIAG_DEFER_EXIT, 0, 0);
        return;
    }
    u32 ret = c->body.ret;
    bool typed = ret != TYPE_ID_INFERRED && !returns_nothing(c, ret);
    if (typed && !has_value) {
        v = named_results(c, file, token, ret);
        v_token = token;
        has_value = true;
    }
    if (typed) {
        v = convert(c, file, v_token, v, ret);
    }
    v = pin(c, v);
    run_defers(c, c->body.defers, token);
    finish_return(c, file, token, v_token, v, has_value);
}

static void
return_value(Checker *c, u32 file, u32 token, bool has_value)
{
    u32 v_token = has_value ? c->stack_tokens[c->sp - 1] : token;
    Operand v = has_value ? pop(c) : poison;
    if (has_value) {
        pop(c);
    }
    push(c, token, OPERAND_STMT, 0);
    c->body.reachable = false;
    if (c->ndefers != c->body.defers) {
        leave_body(c, file, token, v_token, v, has_value);
        return;
    }
    finish_return(c, file, token, v_token, v, has_value);
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
    if (is_place(x) || is_pair(c, x)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return;
    }
    if (x.type != TYPE_ID_NONE) {
        emit(c, IR_EXIT, x.type, target->loop, operand_ir(c, x));
    }
}

static NOINLINE COLD Operand
leave_loops(Checker *c, u32 file, u32 token, const Loop *target, u32 v_token, Operand v)
{
    if (target->value && !target->peer) {
        v = convert(c, file, v_token, v, target->type);
    }
    v = pin(c, v);
    run_defers(c, target->defers, token);
    return v;
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
    if (c->ndefers != target->defers) {
        if (in_defer(c, target->defers)) {
            report(c, file, form ? token - 1 : token, DIAG_DEFER_EXIT, 0, 0);
            return;
        }
        v = leave_loops(c, file, token, target, v_token, v);
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
result_type(Checker *c, u32 file, u32 end, u32 own, u8 item)
{
    const u8 *kinds = c->files[file].nodes.kinds;
    if (kinds[end] == item) {
        u32 m = c->sp - own;
        while (c->stack[--m].type < OPERAND_MARKER) {
        }
        u32 marker = c->stack[m].type - OPERAND_MARKER;
        bool call = marker == NODE_CALL_ARGS && c->stack[m - 1].type == OPERAND_FN;
        bool literal = marker == NODE_LITERAL_START || marker == NODE_ANON_LITERAL_START;
        return call || literal ? expected_type(c, own) : TYPE_ID_NONE;
    }

    u8 next = kinds[end + 1];
    bool direct = c->sp > own &&
                  c->stack[c->sp - own - 1].type == OPERAND_MARKER + NODE_VALUE;
    if (next == NODE_RETURN_VALUE) {
        return c->body.ret == TYPE_ID_INFERRED ? TYPE_ID_NONE : c->body.ret;
    }
    if (next == NODE_BREAK_VALUE) {
        const Loop *to = lookup_label(c, file, c->files[file].nodes.tokens[end + 1]);
        return to && to->value && !to->peer ? to->type : TYPE_ID_NONE;
    }
    if (next == NODE_CASE) {
        const Switch *s = &c->switches[c->nswitches - 1];
        const Loop *to = s->value ? &c->loops[s->loop] : NULL;
        return to && !to->peer ? to->type : TYPE_ID_NONE;
    }
    if (direct && (next == NODE_VAR_TYPE_INIT || next == NODE_ASSIGN ||
                   next == NODE_NAMED_ARG)) {
        return expected_type(c, own);
    }
    if (next == NODE_VALUE_IF_ELSE || next == NODE_VALUE_IF ||
        next == NODE_VALUE_IF_ITEM) {
        u32 m = c->sp - own;
        while (c->stack[--m].type != OPERAND_MARKER + NODE_VALUE_IF_THEN) {
        }
        return c->stack[m - 1].ref;
    }
    return TYPE_ID_NONE;
}

static u32
carried_type(Checker *c, u32 file, u32 end, u32 own, u8 item)
{
    u32 type = result_type(c, file, end, own, item);
    u32 vector = lane_vector(c, type);
    return vector ? vector : type;
}

static void
open_labeled_block(Checker *c, u32 file, u32 node)
{
    Operand label = c->stack[c->sp - 1];
    bool value = label.type == OPERAND_MARKER + NODE_VALUE_LABEL_START;
    u32 expected = TYPE_ID_NONE;
    if (value) {
        u32 end = closing_node(c->files[file].nodes.kinds, node, NODE_VALUE_LABEL_START, NODE_LABELED_VALUE, NODE_LABELED_ITEM);
        expected = carried_type(c, file, end, 1, NODE_LABELED_ITEM);
    }
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
            bool lanes = lane_vector(c, type) != 0;
            Operand x = lanes ? poison : convert(c, file, b.token, b.value, type);
            if (lanes) {
                report(c, file, b.token, DIAG_UNSUPPORTED, 0, 0);
            }
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
    if (target.broken && c->nfacts != target.facts) {
        undo_facts(c, target.facts);
    }
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
        .facts = c->nfacts,
        .value = value,
    };
    if (value) {
        u32 end = closing_node(c->files[file].nodes.kinds, node, NODE_VALUE_SWITCH_START, NODE_VALUE_SWITCH, NODE_SWITCH_ITEM);
        u32 expected = carried_type(c, file, end, 0, NODE_SWITCH_ITEM);
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
        c->switches_cap *= 2;
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
    if (is_flagged(c, v)) {
        v = unflag(c, file, token, v);
    }
    u32 type = value_type(c, file, token, v);
    u8 k = c->types.kinds[type];
    if (type && k != TYPE_INT && k != TYPE_UINTPTR && k != TYPE_ENUM &&
        type != TYPE_ID_BOOL) {
        report(c, file, token, DIAG_SWITCH_TYPE, type, 0);
        type = TYPE_ID_NONE;
    }
    if (check_is_int128(type)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        type = TYPE_ID_NONE;
    }
    v = type ? convert(c, file, token, v, type) : poison;
    s->type = v.type;
    s->before = c->body.reachable;
    s->at = s->type
              ? emit(c, IR_SWITCH, s->type, operand_ir(c, v), 0)
              : emit(c, IR_SWITCH, TYPE_ID_BOOL, ir_const(c, TYPE_ID_BOOL, 0), 0);
    mark_line(c, token);
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
    if (c->nfacts != s->facts) {
        undo_facts(c, s->facts);
    }
    u32 m = list_start(c, NODE_CASE_BODY);
    if (c->ndefers != c->body.defers) {
        end_defers(c, m, token);
    }
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
    return bits_exact(c, type, key ^ order_key(c, type, 0));
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

static NOINLINE u32
narrow_then(Checker *c, Operand cond)
{
    u32 count = cond_tests(c, cond);
    if (count == 0) {
        return 0;
    }

    u32 first = c->ntests - 2 - count;
    u32 pending = 0;
    for (u32 k = first; k < first + count; k++) {
        if (!(c->tests[k] & 1)) {
            push_fact(c, FACT_PENDING | c->tests[k] >> 1);
            pending++;
        }
    }
    narrow_tests(c, count, 1);
    return pending;
}

static void
activate_pending(Checker *c, u32 from, u32 to)
{
    for (u32 k = from; k < to; k++) {
        u32 slot = c->facts[k] & ~FACT_PENDING;
        if (claim_narrow(c, slot)) {
            c->facts[k] = slot;
        }
    }
}

static NOINLINE void
narrow_else(Checker *c, u32 mark, u32 pending)
{
    undo_facts(c, mark + pending);
    activate_pending(c, mark, mark + pending);
}

static NOINLINE void
narrow_body(Checker *c, Operand cond)
{
    u32 count = cond_tests(c, cond);
    if (count > 0) {
        narrow_tests(c, count, 1);
    }
}

static NOINLINE void
settle_if(Checker *c, u32 m, u32 state)
{
    if ((state & (IF_ANY | IF_BEFORE)) != IF_BEFORE) {
        undo_facts(c, c->stack[m + 1].ref);
        return;
    }
    if (state & IF_ELSE) {
        return;
    }

    u32 then = c->sp - 2;
    assert(c->stack[then].type == OPERAND_MARKER + NODE_IF_THEN);
    if (c->body.reachable) {
        undo_facts(c, c->stack[then - 1].ref);
    } else {
        narrow_else(c, c->stack[then - 1].ref, c->stack[then].ref);
    }
}

static void
close_if(Checker *c, u32 token)
{
    u32 m = list_start(c, NODE_IF_START);
    u32 state = c->stack[m].ref;
    bool any = (state & IF_ANY) || c->body.reachable ||
               (!(state & IF_ELSE) && (state & IF_BEFORE));
    if (c->nfacts != c->stack[m + 1].ref) {
        settle_if(c, m, state);
    }
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
    if (c->nfacts != loop.facts) {
        undo_facts(c, loop.facts);
    }
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
    if (is_flagged(c, start)) {
        start = unflag(c, file, token, start);
    }
    if (is_flagged(c, end)) {
        end = unflag(c, file, end_token, end);
    }
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

static u32
add_local(Checker *c, u32 name, u32 type, u32 node)
{
    u32 slot = add_slot(c, name, type, node);
    if (is_slice(c, type)) {
        add_slot(c, name, TYPE_ID_I64, node);
    }
    return slot;
}

static void
take_element(Checker *c, u32 value, u32 elem, u32 at)
{
    if (is_aggregate(c, elem)) {
        u32 to = place_address(c, (Operand){ OPERAND_LOCAL, value });
        emit(c, IR_COPY, elem, to, at);
    } else if (is_slice(c, elem)) {
        store_pair(c, value, load_at(c, elem, at));
    } else {
        store_local(c, value, load_at(c, elem, at));
    }
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
    u32 value = add_local(c, name, elem, node);
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
    take_element(c, value, elem, at);
}

static NOINLINE void
for_slice(Checker *c, u32 file, u32 node, u32 label, Operand range, bool indexed)
{
    u32 type = is_place(range) ? place_type(c, range) : range.type;
    u32 elem = c->types.arg0[type];
    u32 base;
    if (range.type == OPERAND_LOCAL && place_binding(c, file, range) != NODE_NONE) {
        base = range.ref;
    } else {
        Operand v = is_place(range) ? read_slice(c, range) : range;
        base = add_local(c, 0, type, node);
        store_pair(c, base, v);
    }

    u32 index = add_slot(c, 0, TYPE_ID_I64, node);
    store_local(c, index, (Operand){ TYPE_ID_I64, EXACT_ZERO });
    u32 name = node_name(c, file, node);
    u32 value = add_local(c, name, elem, node);
    bind_loop_variable(c, file, node, value);
    if (indexed) {
        slot_at(c, index)->name = node_name(c, file, node + 1);
        slot_at(c, index)->node = node + 1;
        bind_loop_variable(c, file, node + 1, index);
    }

    u32 loop = emit(c, IR_LOOP, 0, 0, 0);
    c->body.depth++;
    u32 i = value_of(load_local(c, index));
    u32 n = value_of(load_local(c, base + 1));
    emit(c, IR_IF, 0, emit(c, IR_LT, TYPE_ID_I64, i, n), 0);
    push_loop(c, (Loop){ loop, emit(c, IR_BLOCK, 0, 0, 0), index, .label = label });
    if (name == NAME_DISCARD) {
        return;
    }

    u32 ptr = value_of(load_local(c, base));
    u32 at = emit(c, IR_INDEX, pointer_type(c, elem), ptr, value_of(load_local(c, index)));
    take_element(c, value, elem, at);
}

static void
for_value(Checker *c, u32 file, u32 node, u32 token, bool inclusive)
{
    if (c->nfacts != c->body.facts) {
        drop_assigned(c, file, node, NODE_FOR_START, NODE_FOR);
    }
    u32 m = list_start(c, NODE_FOR_START);
    u32 label = pending_label(c, m);
    Operand range = pop(c);
    push(c, token, OPERAND_STMT, 0);
    const ParsedFile *f = &c->files[file];
    bool indexed = f->nodes.kinds[node + 1] == NODE_FOR_INDEX;
    if (is_place(range) && is_array(c, place_type(c, range))) {
        for_array(c, file, node, label, range, indexed);
        return;
    }
    if ((is_place(range) && is_slice(c, place_type(c, range))) || is_pair(c, range)) {
        for_slice(c, file, node, label, range, indexed);
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
    if (c->nfacts != loop.facts) {
        undo_facts(c, loop.facts);
    }
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

static NOINLINE void
value_if_then(Checker *c, u32 file, u32 node, u32 token)
{
    u32 cond_token = c->stack_tokens[c->sp - 1];
    Operand cond = c->stack[c->sp - 1];
    u32 end = closing_node(c->files[file].nodes.kinds, node, NODE_VALUE_IF_THEN, NODE_VALUE_IF, NODE_VALUE_IF_ITEM);
    u32 expected = carried_type(c, file, end, 1, NODE_VALUE_IF_ITEM);
    u32 mark = c->nfacts;
    u32 pending = 0;
    u32 mode = SHORT_FOLD;
    if (cond.type != TYPE_ID_NONE && cond.type != TYPE_ID_BOOL) {
        report(c, file, cond_token, is_value_type(cond.type) ? DIAG_NOT_BOOL : DIAG_NOT_VALUE, cond.type, 0);
    } else if (cond.type == TYPE_ID_BOOL && is_constant(cond)) {
        mode = exact_sign(&c->exacts, value_of(cond)) != 0 ? SHORT_SKIP : SHORT_PASS;
        if (mode == SHORT_PASS && in_body(c)) {
            emit(c, IR_IF, 0, ir_const(c, TYPE_ID_BOOL, 0), 0);
        }
    } else if (cond.type == TYPE_ID_BOOL) {
        assert(in_body(c));
        pending = c->body.narrows ? narrow_then(c, cond) : 0;
        mode = emit(c, IR_IF, expected, value_of(cond), 0);
    }
    u32 shared = enclosing_slot(c, file, end, c->sp - 1);

    pop(c);
    push(c, cond_token, OPERAND_STMT, shared);
    push(c, cond_token, OPERAND_STMT, mark);
    push(c, cond_token, OPERAND_STMT, pending);
    push(c, cond_token, OPERAND_STMT, expected);
    push(c, token, OPERAND_MARKER + NODE_VALUE_IF_THEN, mode);
}

static NOINLINE Operand
value_if_branch(Checker *c, u32 file, u32 token, Operand v)
{
    if (v.type == TYPE_ID_NONE) {
        return poison;
    }
    if (is_place(v) && in_body(c) && is_aggregate(c, place_type(c, v))) {
        return v;
    }
    if (is_place(v)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    if (!is_value_type(v.type)) {
        report(c, file, token, DIAG_NOT_VALUE, 0, 0);
        return poison;
    }
    if (is_pair(c, v)) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
        return poison;
    }
    return v;
}

static NOINLINE Operand
hold_value_if(Checker *c, u32 file, u32 node, u32 token, Operand a, u32 expected, u32 mode, u32 shared)
{
    u32 type = expected != TYPE_ID_NONE ? expected : place_type(c, a);
    a = convert(c, file, token, a, type);
    c->body.code->types[c->body.first + mode] = 0;
    if (a.type == TYPE_ID_NONE) {
        return poison;
    }

    Operand held = a;
    if (shared != NO_SLOT && slot_at(c, shared)->type == type) {
        held = (Operand){ OPERAND_LOCAL, shared };
    } else if (a.type != OPERAND_LOCAL || slot_at(c, a.ref)->name != 0) {
        held = (Operand){ OPERAND_LOCAL, add_slot(c, 0, type, node) };
    }
    if (a.type != OPERAND_LOCAL || a.ref != held.ref) {
        copy_place(c, held, a);
    }
    return held;
}

static NOINLINE void
value_if_else(Checker *c, u32 file, u32 node, u32 token)
{
    u32 a_token = c->stack_tokens[c->sp - 1];
    Operand a = value_if_branch(c, file, a_token, pop(c));
    u32 mode = c->stack[c->sp - 1].ref;
    u32 expected = c->stack[c->sp - 2].ref;
    u32 pending = c->stack[c->sp - 3].ref;
    u32 mark = c->stack[c->sp - 4].ref;
    u32 shared = c->stack[c->sp - 5].ref;
    u32 at = 0;
    if (mode == SHORT_SKIP && in_body(c)) {
        emit(c, IR_IF, 0, ir_const(c, TYPE_ID_BOOL, 0), 0);
    } else if (mode == SHORT_PASS && in_body(c)) {
        if (!is_constant(a) && !is_place(a)) {
            emit(c, IR_DROP, 0, value_of(a), 0);
        }
        emit(c, IR_END, 0, 0, 0);
    } else if (mode < SHORT_TESTED) {
        if (is_place(a) && !is_vector(c, expected)) {
            a = hold_value_if(c, file, node, a_token, a, expected, mode, shared);
            at = emit(c, IR_ELSE, 0, 0, 0);
        } else if (expected != TYPE_ID_NONE) {
            if (is_pair_type(c, expected) && a.type != TYPE_ID_NONE) {
                report(c, file, a_token, DIAG_UNSUPPORTED, 0, 0);
                a = poison;
            }
            a = convert(c, file, a_token, a, expected);
            at = emit(c, IR_ELSE, expected, a.type == TYPE_ID_NONE ? 0 : operand_ir(c, a), 0);
        } else if (!is_constant(a)) {
            if (may_widen(c, a)) {
                emit(c, IR_NOP, 0, 0, 0);
            }
            at = emit(c, IR_ELSE, a.type, value_of(a), 0);
        } else {
            at = emit(c, IR_ELSE, 0, 0, 0);
        }
        if (c->nfacts != mark) {
            narrow_else(c, mark, pending);
        }
    }
    push(c, a_token, a.type, a.ref);
    push(c, token, OPERAND_MARKER + NODE_VALUE_IF_ELSE, at);
}

static NOINLINE u32
settle_value_if(Checker *c, u32 file, u32 mode, u32 at, Operand a, u32 a_token, Operand b, u32 b_token)
{
    u32 peer = peer_type(c, a.type, b.type);
    u32 type = is_vector(c, peer) ? peer
             : peer == a.type
                 ? value_type(c, file, a_token, (Operand){ peer, a.ref })
                 : value_type(c, file, b_token, (Operand){ peer, b.ref });
    if (type == TYPE_ID_NONE) {
        return TYPE_ID_NONE;
    }
    if (is_slice(c, type)) {
        report(c, file, a_token, DIAG_UNSUPPORTED, 0, 0);
        return TYPE_ID_NONE;
    }

    IrCode *code = c->body.code;
    u32 to = c->body.first + at;
    if (is_constant(a)) {
        Operand x = convert(c, file, a_token, a, type);
        if (x.type == TYPE_ID_NONE) {
            return TYPE_ID_NONE;
        }
        code->as[to] = operand_ir(c, x);
    } else if (a.type != type) {
        if (!may_widen(c, a) || !widens(c, a.type, type)) {
            report(c, file, a_token, DIAG_MISMATCH, a.type, type);
            return TYPE_ID_NONE;
        }
        code->kinds[to - 1] = IR_CONVERT;
        code->types[to - 1] = type;
        code->as[to - 1] = value_of(a);
        code->as[to] = at - 1;
    }
    code->types[to] = type;
    code->types[c->body.first + mode] = type;
    return type;
}

static NOINLINE Operand
value_if(Checker *c, u32 file)
{
    u32 b_token = c->stack_tokens[c->sp - 1];
    Operand b = value_if_branch(c, file, b_token, pop(c));
    u32 at = pop(c).ref;
    u32 a_token = c->stack_tokens[c->sp - 1];
    Operand a = pop(c);
    u32 mode = pop(c).ref;
    u32 expected = pop(c).ref;
    pop(c);
    u32 mark = pop(c).ref;
    pop(c);
    if (c->nfacts != mark) {
        undo_facts(c, mark);
    }

    if (mode == SHORT_FOLD) {
        return poison;
    }
    if (mode == SHORT_SKIP || mode == SHORT_PASS) {
        if (mode == SHORT_SKIP && in_body(c)) {
            if (!is_constant(b) && !is_place(b)) {
                emit(c, IR_DROP, 0, value_of(b), 0);
            }
            emit(c, IR_END, 0, 0, 0);
        }
        Operand v = mode == SHORT_SKIP ? a : b;
        u32 v_token = mode == SHORT_SKIP ? a_token : b_token;
        return expected == TYPE_ID_NONE || v.type == TYPE_ID_NONE
                 ? v
                 : convert(c, file, v_token, v, expected);
    }

    if (is_place(a)) {
        u32 type = place_type(c, a);
        u32 to = stored_type(c, type, b);
        Operand x = convert(c, file, b_token, b, to);
        if (x.type != TYPE_ID_NONE && to != type) {
            emit(c, IR_STORE_AT, to, place_address(c, a), operand_ir(c, x));
        } else if (x.type != TYPE_ID_NONE &&
                   (x.type != OPERAND_LOCAL || x.ref != a.ref)) {
            copy_place(c, a, x);
        }
        emit(c, IR_END, 0, 0, 0);
        return x.type == TYPE_ID_NONE ? poison : a;
    }
    if (is_place(b) && a.type != TYPE_ID_NONE) {
        u32 want = expected != TYPE_ID_NONE ? expected : a.type;
        if (!is_vector(c, want) || lane_vector(c, place_type(c, b)) != want) {
            report(c, file, b_token, DIAG_MISMATCH, place_type(c, b), want);
            emit(c, IR_END, 0, 0, 0);
            return poison;
        }
        b = load_lanes(c, NO_SLOT, b, want);
    }

    u32 type = expected;
    if (a.type != TYPE_ID_NONE && b.type != TYPE_ID_NONE && type == TYPE_ID_NONE) {
        type = settle_value_if(c, file, mode, at, a, a_token, b, b_token);
    }
    Operand v = type == TYPE_ID_NONE || a.type == TYPE_ID_NONE
                  ? poison
                  : convert(c, file, b_token, b, type);
    if (v.type == TYPE_ID_NONE) {
        emit(c, IR_END, 0, 0, 0);
        return poison;
    }
    return emit_value(c, IR_END, type, operand_ir(c, v), 0);
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

static NOINLINE COLD void
open_defer(Checker *c, u32 node, u32 token)
{
    IrCode *code = c->body.code;
    if (c->ndefers == c->defers_cap) {
        c->defers_cap *= 2;
        c->defers = resize(c->defers, c->defers_cap, sizeof *c->defers);
    }
    c->defers[c->ndefers++] = (Defer){
        .at = c->sp,
        .node = node,
        .origin = code->count - c->body.first,
        .first = c->defer_code.count,
        .first_line = c->defer_code.nlines,
        .facts = c->nfacts,
        .tests = c->ntests,
        .scope = c->nscope,
        .lines = code->nlines,
        .marked = c->body.marked,
        .open = true,
        .reachable = c->body.reachable,
    };
    if (c->nfacts != c->body.facts) {
        hide_facts(c);
    }
    push(c, token, OPERAND_MARKER + NODE_DEFER_START, 0);
}

static NOINLINE COLD void
close_defer(Checker *c, u32 file, u32 node, u32 token, u8 kind)
{
    u32 m = list_start(c, NODE_DEFER_START);
    end_defers(c, m, token);
    Defer *d = &c->defers[c->ndefers - 1];
    assert(d->open && d->at == m);
    unbind_to(c, d->scope);
    if (c->nunread > 0) {
        drop_unread(c, m);
    }
    c->ntests = d->tests;
    if (c->nfacts != d->facts || d->facts != c->body.facts) {
        show_facts(c, d->facts);
    }

    u8 root = c->files[file].nodes.kinds[node - 1];
    bool declares = root == NODE_VAR_INIT || root == NODE_VAR_TYPE ||
                    root == NODE_VAR_TYPE_INIT || root == NODE_DESTRUCTURE_INIT;
    if (kind != NODE_DEFER) {
        report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
    } else if (declares) {
        report(c, file, token, DIAG_DEFER_DECLARATION, 0, 0);
    }

    IrCode *code = c->body.code;
    u32 begin = c->body.first + d->origin;
    bool keep = kind == NODE_DEFER && !declares;
    if (keep) {
        assert(c->defer_code.count == d->first);
        append_code(&c->defer_code, code, begin, code->nextra, code->nslots, code->nconsts, d->lines);
        d->count = code->count - begin;
        d->nlines = code->nlines - d->lines;
        d->open = false;
        d->ends = c->body.reachable;
    }
    code->count = begin;
    code->nlines = d->lines;
    c->body.marked = d->marked;
    c->body.reachable = d->reachable;
    c->ndefers -= !keep;
    c->sp = m;
    push(c, token, OPERAND_STMT, 0);
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
        case NODE_DEFER_START: open_defer(c, node, token); return;
        case NODE_IF_START:
            push(c, token, OPERAND_MARKER + kind, c->body.reachable ? IF_BEFORE : 0);
            return;
        case NODE_IF_THEN: {
            u32 cond_token = c->stack_tokens[c->sp - 1];
            u32 mark = c->nfacts;
            u32 pending = c->body.narrows ? narrow_then(c, c->stack[c->sp - 1]) : 0;
            emit(c, IR_IF, 0, condition(c, file, cond_token, pop(c)), 0);
            mark_line(c, cond_token);
            push(c, cond_token, OPERAND_STMT, mark);
            c->stack[list_start(c, NODE_IF_START)].ref += IF_OPENED;
            push(c, token, OPERAND_MARKER + kind, pending);
            return;
        }
        case NODE_ELSE_IF:
        case NODE_ELSE: {
            u32 m = list_start(c, NODE_IF_START);
            Operand *start = &c->stack[m];
            u32 then = c->sp - 2;
            assert(c->stack[then].type == OPERAND_MARKER + NODE_IF_THEN);
            if (c->body.reachable && !(start->ref & IF_ANY)) {
                c->stack[m + 1].ref = c->stack[then - 1].ref;
            }
            start->ref |= c->body.reachable ? IF_ANY : 0;
            start->ref |= kind == NODE_ELSE ? IF_ELSE : 0;
            c->body.reachable = start->ref & IF_BEFORE;
            emit(c, IR_ELSE, 0, 0, 0);
            if (c->nfacts != c->stack[then - 1].ref) {
                narrow_else(c, c->stack[then - 1].ref, c->stack[then].ref);
            }
            break;
        }
        case NODE_WHILE_START: {
            if (c->nfacts != c->body.facts) {
                drop_assigned(c, file, node, NODE_WHILE_START, NODE_WHILE);
            }
            u32 loop = emit(c, IR_LOOP, 0, 0, 0);
            c->body.depth++;
            push_loop(c, (Loop){ loop, loop, NO_SLOT, .label = pending_label(c, c->sp) });
            break;
        }
        case NODE_VALUE_LABEL_START:
            if (c->nunread != c->unread_done) {
                reserve_copies(c, c->sp);
            }
            c->body.blocks++;
            push(c, token, OPERAND_MARKER + kind, c->files[file].tokens.ids[token]);
            return;
        case NODE_LABEL_START:
            push(c, token, OPERAND_MARKER + kind, c->files[file].tokens.ids[token]);
            return;
        case NODE_VALUE_SWITCH_START:
            if (c->nunread != c->unread_done) {
                reserve_copies(c, c->sp);
            }
            begin_switch(c, file, node, kind, token);
            return;
        case NODE_SWITCH_START:
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
            if (c->body.narrows) {
                narrow_body(c, cond);
            }
            emit(c, IR_IF, 0, condition(c, file, cond_token, cond), 0);
            mark_line(c, cond_token);
            push(c, cond_token, OPERAND_STMT, 0);
            push(c, token, OPERAND_MARKER + kind, forever);
            return;
        }
        case NODE_AND_LEFT:
        case NODE_OR_LEFT:
            if (c->nunread != c->unread_done) {
                reserve_copies(c, c->sp);
            }
            logical_left(c, file, token, kind);
            return;
        case NODE_OR_ELSE_LEFT:
            if (c->nunread != c->unread_done) {
                reserve_copies(c, c->sp);
            }
            or_else_left(c, file, node, token);
            return;
        case NODE_OR_ELSE_TARGET:
            push(c, token, OPERAND_MARKER + NODE_VALUE, or_else_target(c, file, node, token));
            return;
        case NODE_VALUE_IF_THEN:
            if (c->nunread != c->unread_done) {
                reserve_copies(c, c->sp);
            }
            value_if_then(c, file, node, token);
            return;
        case NODE_VALUE_IF_ELSE: value_if_else(c, file, node, token); return;
        default: break;
    }
    push(c, token, OPERAND_MARKER + kind, c->nscratch);
}

static NOINLINE Operand
data_place(Checker *c, u32 decl)
{
    u32 type = c->decls.types[decl];
    u32 at = emit(c, IR_DATA, pointer_type(c, type), c->decls.values[decl], decl);
    return (Operand){ OPERAND_ADDRESS, REF_VALUE | at };
}

static u32
add_callback(Checker *c, u32 decl)
{
    Ir *ir = &c->ir;
    if (ir->ncallbacks == ir->callbacks_cap) {
        ir->callbacks_cap = ir->callbacks_cap ? ir->callbacks_cap * 2 : 16;
        ir->callbacks = resize(ir->callbacks, ir->callbacks_cap, sizeof *ir->callbacks);
    }
    ir->callbacks[ir->ncallbacks] = decl;
    c->decls.values[decl] = ++ir->ncallbacks;
    return ir->ncallbacks;
}

static NOINLINE Operand
callback(Checker *c, u32 file, u32 token, u32 decl, u32 to)
{
    need(c, decl, file, token);
    u32 type = c->decls.types[decl];
    if ((u32)c->types.arg1[type] == TYPE_ID_INFERRED) {
        need_body(c, decl, file, token);
        type = c->decls.types[decl];
    }
    if (type == TYPE_ID_NONE) {
        return poison;
    }
    if (!same_type(c, to, TYPE_FN, c->types.arg0[type], c->types.arg1[type] | FN_C_CALL)) {
        report(c, file, token, DIAG_MISMATCH, type, to);
        return poison;
    }

    u32 k = c->decls.values[decl];
    if (k == 0) {
        k = add_callback(c, decl);
    }
    return emit_value(c, IR_CALLBACK, to, k - 1, decl);
}

static NOINLINE Operand
global_place(Checker *c, u32 file, u32 node, u32 decl)
{
    const ParsedFile *f = &c->files[file];
    u32 token = f->nodes.tokens[node];
    if (!in_body(c)) {
        report(c, file, token, DIAG_NOT_CONSTANT, 0, 0);
        return poison;
    }
    need(c, decl, file, token);
    u32 type = c->decls.types[decl];
    if (type == TYPE_ID_NONE) {
        return poison;
    }

    u32 ptr = pointer_type(c, type);
    u32 at = c->decls.bodies[decl] == NO_BODY
               ? emit(c, IR_C_GLOBAL, ptr, 0, decl)
               : emit(c, IR_GLOBAL, ptr, c->decls.values[decl], decl);
    Operand r = { OPERAND_ADDRESS, REF_VALUE | at };
    u8 next = f->nodes.kinds[node + 1];
    if (takes_place[next]) {
        return r;
    }
    if (!is_aggregate(c, type)) {
        return load_at(c, type, at);
    }
    u8 after = f->tokens.kinds[token + 1];
    if (after != TK_LBRACK && after != TK_PERIOD) {
        note_unread(c, r);
    }
    return r;
}

static const u32 target_names[NTARGETS + 1] = { NAME_LINUX, NAME_ARM64, NAME_X86_64 + 1 };

enum
{
    NPACKAGES = NAME_OS + 1 - NAME_BITS,
};

static const u32 package_members[NPACKAGES + 1] = {
    NAME_TRAILING_ZEROS, NAME_UNALIGNED_LOAD, NAME_LOOKUP,
    NAME_ARGS,           NAME_ASSERT,
};

static u32
builtin_package(u32 id)
{
    u32 k = 0;
    while (id >= package_members[k + 1]) {
        k++;
    }
    return k;
}

static u32
target_type(Checker *c, u32 k)
{
    if (c->target_types[k]) {
        return c->target_types[k];
    }

    Decls *d = &c->decls;
    u32 decl = d->count++;
    u32 type = add_type(c, TYPE_ENUM, decl, TYPE_ID_U8);
    d->names[decl] = NAME_TARGET_OS_TYPE + k;
    d->kinds[decl] = DECL_ENUM;
    d->phases[decl] = PHASE_CHECKED;
    d->types[decl] = type;

    u32 first = c->nmembers;
    u32 n = target_names[k + 1] - target_names[k];
    reserve_members(c, n);
    for (u32 i = 0; i < n; i++) {
        c->members[c->nmembers++] = (Member){
            .name = target_names[k] + i,
            .value = exact_int(&c->exacts, i),
        };
    }
    d->members[decl] = first;
    d->member_counts[decl] = n;
    index_members(c, decl, first);
    enum_runs(c, decl);

    c->target_types[k] = type;
    return type;
}

static Operand
package_member(Checker *c, u32 file, u32 node, u32 package)
{
    const ParsedFile *f = &c->files[file];
    if (f->nodes.kinds[node + 1] != NODE_FIELD) {
        report(c, file, f->nodes.tokens[node], DIAG_PACKAGE_VALUE, 0, 0);
        return poison;
    }

    u32 token = f->nodes.tokens[node + 1];
    u32 id = f->tokens.ids[token];
    u32 first = package_members[package];
    if (id - first >= package_members[package + 1] - first) {
        if (id - NAME_TRAILING_ZEROS < NAME_ASSERT - NAME_TRAILING_ZEROS) {
            report(c, file, token, DIAG_IN_PACKAGE, NAME_BITS + builtin_package(id), 0);
        } else {
            report(c, file, token, DIAG_NO_PACKAGE_MEMBER, NAME_BITS + package, 0);
        }
        return poison;
    }
    if (f->nodes.kinds[node + 2] != NODE_CALL_ARGS && id != NAME_ARGS) {
        report(c, file, token, DIAG_FN_VALUE, 0, 0);
        return poison;
    }
    return (Operand){ OPERAND_PACKAGE, id };
}

static NOINLINE Operand
resolve_builtin(Checker *c, u32 file, u32 node, u32 token, u32 id)
{
    u32 k = id - NAME_TARGET_OS;
    if (k < NTARGETS) {
        c->target_read[k] = true;
        u32 value = c->target[k] - target_names[k];
        return (Operand){ target_type(c, k), exact_int(&c->exacts, value) };
    }
    if (k - NTARGETS < NTARGETS) {
        return (Operand){ TYPE_ID_TYPE, target_type(c, k - NTARGETS) };
    }
    u32 package = id - NAME_BITS;
    if (package < NPACKAGES && c->imports[file * NPACKAGES + package]) {
        return package_member(c, file, node, package);
    }
    if (id - NAME_TRAILING_ZEROS < NAME_ASSERT - NAME_TRAILING_ZEROS) {
        report(c, file, token, DIAG_IN_PACKAGE, NAME_BITS + builtin_package(id), 0);
        return poison;
    }
    if (id != NAME_ASSERT) {
        report(c, file, token, DIAG_UNKNOWN_NAME, 0, 0);
        return poison;
    }
    if (c->files[file].nodes.kinds[node + 1] != NODE_CALL_ARGS) {
        report(c, file, token, DIAG_FN_VALUE, 0, 0);
        return poison;
    }
    return (Operand){ OPERAND_BUILTIN, id };
}

static ALWAYS_INLINE Operand
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
        return resolve_builtin(c, file, node, token, id);
    }

    switch (c->decls.kinds[decl]) {
        case DECL_STRUCT:
        case DECL_ENUM:
        case DECL_UNION: return (Operand){ TYPE_ID_TYPE, c->decls.types[decl] };
        case DECL_CONST:
            need(c, decl, file, token);
            if (in_body(c) && is_aggregate(c, c->decls.types[decl])) {
                return data_place(c, decl);
            }
            return (Operand){ c->decls.types[decl], c->decls.values[decl] };
        case DECL_FN: {
            u32 to = in_body(c) ? expected_at(c, c->sp) : TYPE_ID_NONE;
            if (is_nilable(c, to)) {
                to = c->types.arg0[to];
            }
            if (is_c_fn(c, to)) {
                return callback(c, file, token, decl, to);
            }
            report(c, file, token, DIAG_FN_VALUE, 0, 0);
            return poison;
        }
        case DECL_VAR: return global_place(c, file, node, decl);
        default: assert(false && "unknown declaration kind"); return poison;
    }
}

static NOINLINE void
claim_literal(Checker *c, u32 file, u32 token, u8 kind)
{
    u32 type = TYPE_ID_NONE;
    if (kind == NODE_ANON_LITERAL_START) {
        type = expected_at(c, c->sp);
    } else if (c->stack[c->sp - 1].type == TYPE_ID_TYPE) {
        type = c->stack[c->sp - 1].ref;
    }
    if (type != TYPE_ID_NONE) {
        claim_contained(c, type, file, token);
    }
}

static u32
child_token(const u32 *tokens, u32 begin, u32 i)
{
    return i > begin ? tokens[i - 1] : tokens[i];
}

static void
walk(Checker *c, u32 decl, u32 begin, u32 end)
{
    u32 file = c->decls.files[decl];
    const u8 *kinds = c->files[file].nodes.kinds;
    const u32 *tokens = c->files[file].nodes.tokens;
    const u32 *ids = c->files[file].tokens.ids;
    bool inside = in_body(c);
    u32 base = c->sp;

    for (u32 i = begin; i < end; i++) {
        u8 kind = kinds[i];
        u32 token = tokens[i];
        if (kind >= NODE_FIRST_MARKER) {
            if (kind == NODE_FN_BODY) {
                if (!inside && list_start(c, NODE_FN_START) == base) {
                    c->decls.bodies[c->body.decl] = i;
                    return;
                }
                i = literal_end(kinds, i) - 1;
                continue;
            }
            if (inside) {
                body_marker(c, file, i, kind, token);
            } else if (kind == NODE_AND_LEFT || kind == NODE_OR_LEFT) {
                logical_left(c, file, token, kind);
            } else if (kind == NODE_OR_ELSE_LEFT) {
                or_else_left(c, file, i, token);
            } else if (kind == NODE_VALUE_IF_THEN) {
                value_if_then(c, file, i, token);
            } else if (kind == NODE_VALUE_IF_ELSE) {
                value_if_else(c, file, i, token);
            } else if (kind == NODE_VALUE_LABEL_START) {
                report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
                i = closing_node(kinds, i, kind, NODE_LABELED_VALUE, NODE_LABELED_ITEM);
                push(c, token, TYPE_ID_NONE, 0);
            } else if (kind == NODE_VALUE_SWITCH_START) {
                report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
                i = closing_node(kinds, i, kind, NODE_VALUE_SWITCH, NODE_SWITCH_ITEM);
                push(c, token, TYPE_ID_NONE, 0);
            } else {
                if (kind == NODE_LITERAL_START || kind == NODE_ANON_LITERAL_START) {
                    claim_literal(c, file, token, kind);
                }
                push(c, token, OPERAND_MARKER + kind, c->nscratch);
            }
            continue;
        }

        switch (kind) {
            case NODE_NAME: {
                u32 id = ids[token];
                if (inside && id == NAME_DISCARD) {
                    if (kinds[i + 1] == NODE_VALUE ||
                        kinds[i + 1] == NODE_ASSIGN_TARGET || in_case_labels(c)) {
                        push(c, token, OPERAND_DISCARD, 0);
                    } else {
                        report(c, file, token, DIAG_DISCARD, 0, 0);
                        push_operand(c, token, poison);
                    }
                    break;
                }
                if (inside) {
                    u64 binding = c->bindings[id];
                    if ((u32)(binding >> 32) == c->body.owner) {
                        u32 slot = (u32)binding;
                        if (takes_place[kinds[i + 1]]) {
                            push(c, token, OPERAND_LOCAL, slot);
                        } else {
                            push_operand(c, token, read_local(c, slot, token, kinds[i + 1]));
                        }
                        break;
                    }
                    u32 decl_id = c->package_names[id];
                    if (kinds[i + 1] == NODE_CALL_ARGS && decl_id &&
                        c->decls.kinds[decl_id] == DECL_FN) {
                        need(c, decl_id, file, token);
                        if ((u32)c->types.arg1[c->decls.types[decl_id]] ==
                            TYPE_ID_INFERRED) {
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
                bool place = takes_place[kinds[i + 1]];
                push_operand(c, token, member_access(c, file, token, pop(c), place));
                break;
            }
            case NODE_INDEX:
                if (inside) {
                    u32 index_token = c->stack_tokens[c->sp - 1];
                    Operand index = pop(c);
                    u32 base_token = c->stack_tokens[c->sp - 1];
                    Operand base = pop(c);
                    push_operand(
                        c,
                        base_token,
                        index_place(c, file, base_token, base, index, index_token, kinds[i + 1])
                    );
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_SLICE:
            case NODE_SLICE_INCLUSIVE:
            case NODE_SLICE_FROM:
            case NODE_SLICE_TO:
            case NODE_SLICE_TO_INCLUSIVE:
                if (inside) {
                    u32 base_token = c->stack_tokens[c->sp - node_arity[kind]];
                    push_operand(c, base_token, slice_range(c, file, kind));
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_ARRAY_LITERAL:
            case NODE_ANON_ARRAY_LITERAL:
                if (inside) {
                    array_literal(c, file, i, kind);
                } else {
                    constant_array(c, file, kind);
                }
                break;
            case NODE_LITERAL:
            case NODE_ANON_LITERAL:
                if (inside) {
                    struct_literal(c, file, i, kind);
                } else {
                    constant_struct(c, file, kind);
                }
                break;
            case NODE_ARG_NAME:
                push(c, token, OPERAND_FIELD, ids[token]);
                break;
            case NODE_NAMED_ARG: named_item(c, file); break;
            case NODE_FLOAT:
                push_operand(c, token, number_literal(c, file, i, TYPE_ID_UNTYPED_FLOAT));
                break;
            case NODE_STRING:
                push_operand(c, token, string_literal(c, file, token));
                break;
            case NODE_CHAR:
                push_operand(c, token, char_literal(c, file, token));
                break;
            case NODE_TRUE:
            case NODE_FALSE:
                push(c, token, TYPE_ID_BOOL, kind == NODE_TRUE ? EXACT_ONE : EXACT_ZERO);
                break;
            case NODE_NIL: push(c, token, TYPE_ID_NIL, 0); break;
            case NODE_UNWRAP: {
                u32 base_token = c->stack_tokens[c->sp - 1];
                push_operand(c, base_token, unwrap(c, file, token, pop(c)));
                break;
            }
            case NODE_OR_ELSE: push_operand(c, token, or_else(c, file)); break;
            case NODE_VALUE_IF:
            case NODE_VALUE_IF_ITEM:
                push_operand(c, token, value_if(c, file));
                break;
            case NODE_UNDEFINED: push(c, token, TYPE_ID_UNDEFINED, 0); break;
            case NODE_NEG:
            case NODE_NEG_WRAP:
            case NODE_NOT:
            case NODE_BIT_NOT:
                push_operand(c, token, unary(c, file, token, kind, pop(c)));
                break;
            case NODE_ADDR:
                if (inside) {
                    u32 target_token = c->stack_tokens[c->sp - 1];
                    Operand target = pop(c);
                    push_operand(c, token, address_of(c, file, target_token, target));
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_DEREF:
                if (inside) {
                    bool place = takes_place[kinds[i + 1]];
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
            case NODE_CALL: call(c, file, i); break;
            case NODE_BITCAST: {
                u32 value_token = c->stack_tokens[c->sp - 1];
                Operand v = pop(c);
                u32 type_token = c->stack_tokens[c->sp - 1];
                u32 t = type_operand(c, file, type_token, pop(c));
                push_operand(c, token, bitcast(c, file, token, type_token, t, value_token, v));
                break;
            }
            case NODE_POINTER_TYPE:
            case NODE_POINTER_OPERAND: {
                Operand o = pop(c);
                if (o.type != TYPE_ID_TYPE && kind == NODE_POINTER_OPERAND &&
                    is_value_type(o.type)) {
                    report(c, file, token, DIAG_DEREF, 0, 0);
                    push_operand(c, token, poison);
                    break;
                }
                u32 t = type_operand(c, file, child_token(tokens, begin, i), o);
                push_type(c, token, t ? pointer_type(c, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_OPTIONAL_TYPE: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                push_type(c, token, t ? optional_type(c, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_SLICE_TYPE:
            case NODE_REST_TYPE: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                push_type(c, token, t ? slice_type(c, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_MANY_TYPE: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                push_type(c, token, t ? intern_type(c, TYPE_MANY, t, 0) : TYPE_ID_NONE);
                break;
            }
            case NODE_ARRAY_TYPE: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                bool ok;
                u64 n = array_length(c, file, token, pop(c), &ok);
                push_type(c, token, t && ok ? intern_type(c, TYPE_ARRAY, t, n) : TYPE_ID_NONE);
                break;
            }
            case NODE_ERROR_UNION: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                u32 e = type_operand(c, file, token, pop(c));
                if (e && e != TYPE_ID_ERROR) {
                    report(c, file, token, DIAG_UNSUPPORTED, 0, 0);
                    e = TYPE_ID_NONE;
                }
                push_type(c, token, t && e ? intern_type(c, TYPE_ERROR_UNION, t, e) : TYPE_ID_NONE);
                break;
            }
            case NODE_ERROR_UNION_INFERRED: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                push_type(c, token, t ? intern_type(c, TYPE_ERROR_UNION, t, 0) : TYPE_ID_NONE);
                break;
            }
            case NODE_FN_TYPE: push_type(c, token, close_fn_type(c)); break;
            case NODE_TUPLE_TYPE:
            case NODE_RESULTS:
                push_type(c, token, close_tuple_type(c, file, i, token));
                break;
            case NODE_EXTERN: {
                if (kinds[i - 1] != NODE_FN_TYPE) {
                    unsupported(c, file, i);
                    break;
                }
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                push_type(c, token, t ? c_fn_type(c, file, token, t) : TYPE_ID_NONE);
                break;
            }
            case NODE_MEMBER_TYPE: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
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
                push_member(
                    c,
                    file,
                    i,
                    t,
                    t ? member_default(c, file, token, convert(c, file, token, v, t)) : MEMBER_NONE
                );
                break;
            }
            case NODE_PARAM_TYPE: {
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                push_member(c, file, i, t, MEMBER_NONE);
                if (kinds[i - 1] == NODE_REST_TYPE) {
                    push(c, token, OPERAND_REST, 0);
                }
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
                u32 t = type_operand(c, file, child_token(tokens, begin, i), pop(c));
                u8 k = c->types.kinds[t];
                if (t && k != TYPE_INT && !(kind == NODE_UNION_TAG && k == TYPE_ENUM)) {
                    report(c, file, child_token(tokens, begin, i), DIAG_NOT_INTEGER, 0, 0);
                    t = TYPE_ID_NONE;
                }
                if (check_is_int128(t)) {
                    report(c, file, child_token(tokens, begin, i), DIAG_UNSUPPORTED, 0, 0);
                    t = TYPE_ID_NONE;
                }
                if (t && k == TYPE_ENUM) {
                    need(c, c->types.arg0[t], file, token);
                }
                push(c, token, OPERAND_BACKING, t);
                break;
            }
            case NODE_FN_RETURN:
                push(
                    c,
                    token,
                    OPERAND_RETURN,
                    type_operand(c, file, child_token(tokens, begin, i), pop(c))
                );
                break;
            case NODE_VARIADIC: push(c, token, OPERAND_VARIADIC, 0); break;
            case NODE_STRUCT:
            case NODE_ENUM:
            case NODE_UNION:
            case NODE_CONST_INIT:
            case NODE_CONST_TYPE_INIT:
                if (inside) {
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
                mark_line(c, token);
                push(c, token, OPERAND_STMT, 0);
                break;
            }
            case NODE_TARGET:
                push(c, token, ids[token] == NAME_DISCARD ? OPERAND_DISCARD : OPERAND_TARGET, i);
                break;
            case NODE_ASSIGN_TARGET:
            case NODE_RESULT: break;
            case NODE_DESTRUCTURE_INIT:
            case NODE_DESTRUCTURE_ASSIGN:
                destructure(c, file, i, token, kind);
                mark_line(c, token);
                break;
            case NODE_VAR_TYPE:
                declare_zeroed(c, file, i, token, child_token(tokens, begin, i));
                mark_line(c, token);
                push(c, token, OPERAND_STMT, 0);
                break;
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
                mark_line(c, token);
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
                assign(c, file, i, token, kind);
                mark_line(c, token);
                break;
            case NODE_OR_ELSE_ASSIGN:
                or_else_assign(c, file, token);
                mark_line(c, token);
                break;
            case NODE_EXPR_STMT:
                expression_statement(c, file, token);
                mark_line(c, token);
                break;
            case NODE_RETURN:
            case NODE_RETURN_VALUE:
                return_value(c, file, token, kind == NODE_RETURN_VALUE);
                mark_line(c, token);
                break;
            case NODE_BREAK:
            case NODE_BREAK_LABEL:
            case NODE_BREAK_VALUE:
            case NODE_CONTINUE:
            case NODE_CONTINUE_LABEL:
            case NODE_CONTINUE_VALUE:
                jump(c, file, token, kind);
                mark_line(c, token);
                break;
            case NODE_BLOCK: {
                u32 m = list_start(c, NODE_BLOCK_START);
                if (c->ndefers != c->body.defers) {
                    end_defers(c, m, token);
                }
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
            case NODE_DEFER:
            case NODE_DEFER_CATCH:
            case NODE_DEFER_CATCH_NAMED:
                if (inside) {
                    close_defer(c, file, i, token, kind);
                } else {
                    unsupported(c, file, i);
                }
                break;
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
            case NODE_WHILE:
                close_while(c, token);
                mark_line(c, token);
                break;
            case NODE_RANGE:
            case NODE_RANGE_INCLUSIVE:
                if (inside && kinds[i + 1] == NODE_FOR_VALUE) {
                    for_range(c, file, i, token);
                } else if (inside && in_case_labels(c)) {
                    case_range(c, file, token, kind == NODE_RANGE_INCLUSIVE);
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_FOR_VALUE:
                for_value(c, file, i, token, kinds[i - 1] == NODE_RANGE_INCLUSIVE);
                mark_line(c, token);
                break;
            case NODE_FOR_INDEX:
                if (!inside) {
                    unsupported(c, file, i);
                }
                break;
            case NODE_FOR:
                close_for(c, token);
                mark_line(c, token);
                break;
            case NODE_CASE:
                if (inside) {
                    close_arm(c, file, token);
                    mark_line(c, token);
                } else {
                    c->sp = list_start(c, NODE_CASE_START);
                }
                break;
            case NODE_SWITCH:
            case NODE_VALUE_SWITCH:
            case NODE_SWITCH_ITEM:
                if (inside) {
                    push_operand(c, token, close_switch(c, file, kind));
                } else {
                    unsupported(c, file, i);
                }
                break;
            case NODE_SWITCH_VALUE:
            case NODE_SWITCH_REF:
                if (inside) {
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
    bool external = c->decls.bodies[decl] == NO_BODY;
    bool exported = c->decls.flags[decl] & DECL_EXPORT;
    u8 not_c = external ? DIAG_NOT_C : DIAG_NOT_C_EXPORT;
    u32 m = list_start(c, NODE_FN_START);
    u32 ret = c->decls.flags[decl] & (DECL_PUB | DECL_EXPORT) || external
                ? TYPE_ID_VOID
                : TYPE_ID_INFERRED;
    u32 end = c->sp;
    if (end > m + 1 && c->stack[end - 1].type == OPERAND_RETURN) {
        ret = c->stack[--end].ref;
        if (external || exported) {
            refuse_non_c(c, file, c->stack_tokens[end], ret, not_c);
        }
    }
    bool variadic = end > m + 1 && c->stack[end - 1].type == OPERAND_VARIADIC;
    end -= variadic;
    bool rest = end > m + 1 && c->stack[end - 1].type == OPERAND_REST;

    c->sp = end;
    u32 first = collect_members(c, file, m + 1, DIAG_DUPLICATE_PARAM);
    u32 count = c->nmembers - first;
    u32 list = reserve_list(c, count);
    bool general = is_called_generally(c, ret) || variadic || count + !external > 8;
    for (u32 i = 0; i < count; i++) {
        Member p = c->members[first + i];
        c->lists[list + 1 + i] = p.type;
        general |= is_called_generally(c, p.type);
        if (external || exported) {
            refuse_non_c(c, file, c->files[file].nodes.tokens[p.node], p.type, not_c);
        }
        if (rest && p.value < MEMBER_NONE) {
            report(c, file, c->files[file].nodes.tokens[p.node], DIAG_REST_DEFAULT, 0, 0);
        }
    }

    u64 flags = (external ? FN_C_CALL : 0) | (variadic ? FN_VARIADIC : 0) |
                (rest ? FN_REST : 0);
    c->decls.types[decl] = fn_type(c, list, ret | flags);
    c->decls.flags[decl] |= general ? DECL_GENERAL_CALL : 0;
    c->decls.members[decl] = first;
    c->decls.member_counts[decl] = count;
    c->nscratch = c->stack[m].ref;
    c->sp = m;
    if (exported) {
        add_callback(c, decl);
    }
}

static NOINLINE void
set_global(Checker *c, u32 decl, u32 node)
{
    Decls *d = &c->decls;
    u32 file = d->files[decl];
    const ParsedFile *f = &c->files[file];
    u32 token = f->nodes.tokens[node];
    u8 kind = f->nodes.kinds[node];

    Operand v = { TYPE_ID_UNDEFINED, 0 };
    u32 v_token = token;
    if (kind != NODE_VAR_TYPE) {
        v_token = c->stack_tokens[c->sp - 1];
        v = pop(c);
    }
    u32 t_token = v_token;
    u32 t = TYPE_ID_NONE;
    if (kind == NODE_VAR_INIT) {
        t = value_type(c, file, v_token, v);
    } else {
        if (kind == NODE_VAR_TYPE_INIT) {
            pop(c);
        }
        t_token = c->stack_tokens[c->sp - 1];
        t = type_operand(c, file, t_token, pop(c));
    }

    d->types[decl] = TYPE_ID_NONE;
    if (t == TYPE_ID_NONE) {
        return;
    }
    claim_contained(c, t, file, t_token);
    if (!has_layout(c, t) || holds_128_bit(c, t, d->bodies[decl] == NO_BODY)) {
        report(c, file, t_token, DIAG_UNSUPPORTED, 0, 0);
        return;
    }
    if (d->bodies[decl] == NO_BODY) {
        u32 elem = t;
        while (is_array(c, elem)) {
            elem = c->types.arg0[elem];
        }
        if (is_slice(c, elem) || is_tuple(c, elem)) {
            report(c, file, t_token, DIAG_NOT_C_GLOBAL, 0, elem);
        } else if (c->types.kinds[elem] == TYPE_FN) {
            report(c, file, t_token, DIAG_UNSUPPORTED, 0, 0);
        } else {
            d->types[decl] = t;
        }
        return;
    }

    u32 elem = t;
    while (is_array(c, elem)) {
        elem = c->types.arg0[elem];
    }
    bool given = kind != NODE_VAR_TYPE && v.type != TYPE_ID_UNDEFINED;
    if (!given && kind == NODE_VAR_TYPE && !can_zero(c, file, token, elem)) {
        return;
    }
    Operand x = given ? constant_item(c, file, v_token, v, t) : v;
    if (x.type == TYPE_ID_NONE) {
        return;
    }

    u32 at;
    if (given || (kind == NODE_VAR_TYPE && has_defaults(c, elem))) {
        at = reserve_global(c, file, token, t);
        if (at != NO_SLOT && given) {
            put_data(c, true, at, t, x);
        } else if (at != NO_SLOT) {
            put_defaults(c, c->ir.globals + at, t);
        }
    } else {
        at = reserve_zeroed(c, file, token, t);
        d->flags[decl] |= DECL_ZEROED;
    }
    if (at == NO_SLOT) {
        return;
    }
    d->types[decl] = t;
    d->values[decl] = at;
}

static void
check_decl(Checker *c, u32 decl)
{
    Decls *d = &c->decls;
    if (d->flags[decl] & DECL_TUPLE) {
        finish_tuple(c, decl);
        return;
    }
    u32 first = d->firsts[decl];
    u32 root = d->roots[decl];
    [[maybe_unused]] u32 base = c->sp;
    Body outer = c->body;
    c->body = (Body){ .code = &c->ir.code, .decl = decl };

    d->busy[decl] = 1;
    c->claims[c->nclaims++] = decl;
    reserve_stack(c, root - first + 1);

    bool global = d->kinds[decl] == DECL_VAR;
    bool external = d->bodies[decl] == NO_BODY;
    walk(c, decl, first, root + !(global || external));
    if (d->kinds[decl] == DECL_FN) {
        finish_signature(c, decl);
    } else if (global) {
        set_global(c, decl, root);
    }

    assert(c->sp == base);
    c->nclaims--;
    d->busy[decl] = 0;
    d->phases[decl] = external ? PHASE_CHECKED : PHASE_RESOLVED;
    c->body = outer;
}

static ALWAYS_INLINE u32
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
        .first_line = b->first_line,
        .nlines = code->nlines - b->first_line,
        .calls = b->calls,
        .copies = b->copies,
        .stack_args = b->stack_args,
    };

    if (code != &ir->code) {
        IrCode *deferred = &c->deferred;
        fn.first = deferred->count;
        fn.first_extra = deferred->nextra;
        fn.first_slot = deferred->nslots;
        fn.first_const = deferred->nconsts;
        fn.first_line = deferred->nlines;
        append_code(deferred, code, b->first, b->first_extra, b->first_slot, b->first_const, b->first_line);
    } else if (ir->nfuncs > c->flushed) {
        IrCode *deferred = &c->deferred;
        for (u32 i = c->flushed; i < ir->nfuncs; i++) {
            ir->funcs[i].first += code->count;
            ir->funcs[i].first_extra += code->nextra;
            ir->funcs[i].first_slot += code->nslots;
            ir->funcs[i].first_const += code->nconsts;
            ir->funcs[i].first_line += code->nlines;
        }
        append_code(code, deferred, 0, 0, 0, 0, 0);
        deferred->count = deferred->nextra = deferred->nslots = 0;
        deferred->nconsts = deferred->nlines = 0;
    }

    ir->funcs = grow(ir->funcs, &ir->funcs_cap, ir->nfuncs + 1, sizeof *ir->funcs);
    ir->funcs[ir->nfuncs++] = fn;
    if (code == &ir->code) {
        c->flushed = ir->nfuncs;
    }
    return ir->nfuncs;
}

static void
code_init(IrCode *code)
{
    *code = (IrCode){
        .kinds = malloc(256 * sizeof *code->kinds),
        .types = malloc(256 * sizeof *code->types),
        .as = malloc(256 * sizeof *code->as),
        .bs = malloc(256 * sizeof *code->bs),
        .cap = 256,
        .extra = malloc(64 * sizeof *code->extra),
        .extra_cap = 64,
        .slots = malloc(16 * sizeof *code->slots),
        .slots_cap = 16,
        .consts = malloc(64 * sizeof *code->consts),
        .consts_cap = 64,
        .lines = malloc(64 * sizeof *code->lines),
        .lines_cap = 64,
    };
}

typedef struct
{
    Body body;
    u32 scope;
    u32 pairs;
    u32 unread;
    u32 unread_done;
} Outer;

static ALWAYS_INLINE Outer
open_body(Checker *c, u32 decl, u32 fn)
{
    c->decls.busy[decl] = 1;
    c->claims[c->nclaims++] = decl;

    Outer outer = { .body = c->body };
    IrCode *code = c->nbodies == 0 ? &c->ir.code : &c->builders[c->nbodies];
    if (c->nbodies > c->nbuilders) {
        code_init(code);
        c->nbuilders = c->nbodies;
    } else if (c->nbodies > 0) {
        code->count = code->nextra = code->nslots = code->nconsts = code->nlines = 0;
    }
    c->nbodies++;
    c->body = (Body){
        .code = code,
        .first = code->count,
        .first_extra = code->nextra,
        .first_slot = code->nslots,
        .first_const = code->nconsts,
        .first_line = code->nlines,
        .decl = decl,
        .owner = ++c->owners,
        .ret = (u32)c->types.arg1[fn],
        .loops = c->nloops,
        .breaks = c->nbreaks,
        .defers = c->ndefers,
        .facts = c->nfacts,
        .tests = c->ntests,
        .addressed = c->naddressed,
        .reachable = true,
    };
    outer.scope = c->nscope;
    outer.pairs = c->npairs;
    if (c->nunread > 0) {
        drop_unread(c, c->sp);
        outer.unread = c->nunread;
        outer.unread_done = c->unread_done;
        c->unread_done = outer.unread;
    }
    return outer;
}

static ALWAYS_INLINE void
close_body(Checker *c, u32 decl, const Outer *outer, u32 param_slots)
{
    assert(c->ndefers == c->body.defers);
    unbind_to(c, outer->scope);
    c->nloops = c->body.loops;
    c->nbreaks = c->body.breaks;
    c->nfacts = c->body.facts;
    c->ntests = c->body.tests;
    c->naddressed = c->body.addressed;
    free(c->body.narrowed);
    c->npairs = outer->pairs;
    c->nunread = outer->unread;
    c->unread_done = outer->unread_done;
    c->decls.funcs[decl] = finish_ir(c, decl, param_slots);
    c->body = outer->body;
    c->nbodies--;

    c->nclaims--;
    c->decls.busy[decl] = 0;
    c->decls.phases[decl] = PHASE_CHECKED;
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

    Outer outer = open_body(c, decl, fn);
    IrCode *code = c->body.code;

    const u32 *tokens = c->files[file].nodes.tokens;
    if (!is_passed(c, c->body.ret)) {
        report_unpassed(c, file, tokens[d->roots[decl]], c->body.ret);
        c->body.ret = TYPE_ID_NONE;
    }

    u32 first = d->members[decl];
    u32 nparams = d->member_counts[decl];
    for (u32 i = 0; i < nparams; i++) {
        Member m = c->members[first + i];
        if (!is_passed(c, m.type)) {
            report_unpassed(c, file, tokens[m.node], m.type);
            m.type = TYPE_ID_NONE;
        }
        u32 slot = add_slot(c, m.name, m.type, m.node);
        if (is_pair_type(c, m.type)) {
            add_slot(c, m.name, second_type(c, m.type), m.node);
        }
        if (m.name - (NAME_LAST_KEYWORD + 1) >= NBUILTIN_TYPES) {
            bind(c, file, m.node, slot);
        }
    }
    u32 param_slots = code->nslots - c->body.first_slot;

    reserve_stack(c, end - begin + 1);
    if (d->flags[decl] & DECL_RESULTS) {
        declare_results(c, file, d->bodies[decl] - 2);
    }
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
    mark_line(c, tokens[end - 1]);
    if ((u32)c->types.arg1[fn] == TYPE_ID_INFERRED) {
        u64 flags = c->types.arg1[fn] & ~0xFFFFFFFFull;
        d->types[decl] = intern_type(c, TYPE_FN, c->types.arg0[fn], c->body.ret | flags);
        d->flags[decl] |= is_called_generally(c, c->body.ret) ? DECL_GENERAL_CALL : 0;
    }

    close_body(c, decl, &outer, param_slots);
}

static NOINLINE void
store_args(Checker *c, u32 file, u32 token, u32 argc, u32 argv)
{
    u32 decl = c->args;
    u32 type = c->decls.types[decl];
    u32 at = emit(c, IR_GLOBAL, pointer_type(c, type), c->decls.values[decl], decl);
    u32 ptr = operand_ir(c, load_local(c, argv));
    u32 len = operand_ir(c, convert(c, file, token, load_local(c, argc), TYPE_ID_I64));
    store_at(c, type, at, make_pair(c, type, ptr, len));
}

static NOINLINE void
check_entry(Checker *c)
{
    Decls *d = &c->decls;
    u32 decl = c->entry;
    u32 file = d->files[decl];
    const ParsedFile *f = &c->files[file];
    u32 node = d->roots[decl];
    u32 token = f->nodes.tokens[node];
    u32 base = c->sp;

    Outer outer = open_body(c, decl, d->types[decl]);
    const Member *params = c->members + d->members[decl];
    u32 argc = add_slot(c, params[0].name, params[0].type, node);
    u32 argv = add_slot(c, params[1].name, params[1].type, node);
    if (c->args) {
        store_args(c, file, token, argc, argv);
    }

    reserve_stack(c, 1);
    push(c, token, OPERAND_MARKER + NODE_BLOCK_START, c->nscope);
    u32 end = 0;
    for (u32 k = 0; k < c->nentry_code; k++) {
        u32 s = c->entry_code[k];
        u32 begin = s ? f->nodes.top_ends[s - 1] : 0;
        end = f->nodes.top_ends[s];
        reserve_stack(c, end - begin + 1);
        walk(c, decl, begin, end);
        c->sp = base + 1;
    }
    c->sp = base;

    u32 last = f->nodes.tokens[end - 1];
    if (c->ndefers != c->body.defers) {
        end_defers(c, base, last);
    }
    if (c->body.reachable) {
        Operand zero = { TYPE_ID_I32, exact_int(&c->exacts, 0) };
        emit_return(c, file, last, zero, TYPE_ID_I32);
    }
    mark_line(c, last);
    close_body(c, decl, &outer, 2);
}

static NOINLINE void
declare_import(Checker *c, u32 file, u32 token)
{
    const ParsedFile *f = &c->files[file];
    u32 start = f->tokens.starts[token];
    u32 len = lexer_token_length(f->src, TK_STRING, start);
    const char *path = f->src + start;
    bool in_std = len > 6 && memcmp(path, "\"std/", 5) == 0;
    for (u32 k = 0; in_std && k < NPACKAGES; k++) {
        Name name = names_get(c->names, NAME_BITS + k);
        if (len == name.len + 6 && memcmp(path + 5, name.text, name.len) == 0) {
            u32 *import = &c->imports[file * NPACKAGES + k];
            if (*import) {
                report(c, file, token, DIAG_IMPORT_TWICE, 0, *import);
            } else {
                *import = token;
            }
            return;
        }
    }
    report(c, file, token, in_std ? DIAG_UNKNOWN_PACKAGE : DIAG_UNSUPPORTED, 0, 0);
}

static NOINLINE void
declare_args(Checker *c, u32 file, u32 node)
{
    Decls *d = &c->decls;
    u32 type = slice_type(c, TYPE_ID_CSTRING);
    assert(d->count < d->cap);

    u32 decl = d->count++;
    d->names[decl] = NAME_ARGS;
    d->kinds[decl] = DECL_VAR;
    d->flags[decl] = DECL_ZEROED;
    d->files[decl] = file;
    d->firsts[decl] = node;
    d->roots[decl] = node;
    d->types[decl] = type;
    d->values[decl] = reserve_zeroed(c, file, c->files[file].nodes.tokens[node], type);
    d->phases[decl] = PHASE_CHECKED;
    c->args = decl;
}

static NOINLINE void
declare_entry(Checker *c, u32 file)
{
    Decls *d = &c->decls;
    const ParsedFile *f = &c->files[file];
    u32 s = c->entry_code[0];
    u32 begin = s ? f->nodes.top_ends[s - 1] : 0;
    u32 root = f->nodes.top_ends[s] - 1;
    u32 node = f->nodes.tokens[root] < f->nodes.tokens[begin] ? root : begin;

    u32 argv = intern_type(c, TYPE_MANY, TYPE_ID_CSTRING, 0);
    u32 list = reserve_list(c, 2);
    c->lists[list + 1] = TYPE_ID_I32;
    c->lists[list + 2] = argv;

    reserve_members(c, 2);
    u32 first = c->nmembers;
    c->members[c->nmembers++] = (Member){ NAME_ARGC, TYPE_ID_I32, node, MEMBER_NONE };
    c->members[c->nmembers++] = (Member){ NAME_ARGV, argv, node, MEMBER_NONE };

    assert(d->count < d->cap);
    u32 decl = d->count++;
    d->names[decl] = NAME_MAIN;
    d->kinds[decl] = DECL_FN;
    d->flags[decl] = DECL_EXPORT;
    d->files[decl] = file;
    d->firsts[decl] = node;
    d->roots[decl] = node;
    d->bodies[decl] = node;
    d->types[decl] = fn_type(c, list, TYPE_ID_I32);
    d->members[decl] = first;
    d->member_counts[decl] = 2;
    d->phases[decl] = PHASE_RESOLVED;
    add_callback(c, decl);
    c->entry = decl;

    u32 declared = c->package_names[NAME_MAIN];
    if (declared) {
        u32 at = d->files[declared];
        report(c, at, c->files[at].nodes.tokens[d->roots[declared]], DIAG_MAIN_BESIDE_CODE, 0, 0);
    }
    for (u32 k = 0; k < c->nfiles; k++) {
        if (c->imports[k * NPACKAGES + NAME_OS - NAME_BITS]) {
            declare_args(c, file, node);
            return;
        }
    }
}

static void
declare(Checker *c)
{
    Decls *d = &c->decls;
    u32 entry_file = 0;
    for (u32 f = 0; f < c->nfiles; f++) {
        const ParsedFile *file = &c->files[f];
        const u8 *kinds = file->nodes.kinds;
        const u32 *tokens = file->nodes.tokens;
        if (file->entry) {
            entry_file = f;
        }

        u32 first = 0;
        for (u32 s = 0; s < file->top_count; s++) {
            u32 end = file->nodes.top_ends[s];
            u32 root = end - 1;
            u16 flags = 0;
            bool external = false;
            while (kinds[root] == NODE_PUB || kinds[root] == NODE_EXPORT ||
                   kinds[root] == NODE_EXTERN || kinds[root] == NODE_INLINE ||
                   kinds[root] == NODE_NOINLINE) {
                flags |= kinds[root] == NODE_PUB      ? DECL_PUB
                       : kinds[root] == NODE_EXPORT   ? DECL_EXPORT
                       : kinds[root] == NODE_INLINE   ? DECL_INLINE
                       : kinds[root] == NODE_NOINLINE ? DECL_NOINLINE
                                                      : 0;
                external |= kinds[root] == NODE_EXTERN;
                root--;
            }

            u8 kind = decl_kinds[kinds[root]];
            if (kind == DECL_VAR && file->entry && !external) {
                if (flags) {
                    report(c, f, tokens[root], DIAG_UNSUPPORTED, 0, 0);
                } else {
                    c->entry_code[c->nentry_code++] = s;
                }
                first = end;
                continue;
            }
            if (kind == DECL_NONE) {
                if (kinds[root] == NODE_IMPORT_DECL) {
                    declare_import(c, f, tokens[root]);
                } else if (kinds[root] != NODE_PACKAGE && file->entry) {
                    c->entry_code[c->nentry_code++] = s;
                } else if (kinds[root] != NODE_PACKAGE) {
                    report(c, f, tokens[first], DIAG_CODE_OUTSIDE_ENTRY, 0, 0);
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
            if (external) {
                d->bodies[id] = NO_BODY;
            }
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

    for (u32 i = 0; i < c->nfiles * NPACKAGES; i++) {
        u32 decl = c->package_names[NAME_BITS + i % NPACKAGES];
        if (c->imports[i] && decl) {
            report(c, i / NPACKAGES, c->imports[i], DIAG_IMPORT_CLASH, NAME_BITS + i % NPACKAGES, decl);
        }
    }
    if (c->nentry_code > 0) {
        declare_entry(c, entry_file);
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
check_init(Checker *c, const Names *names, const ParsedFile *files, u32 nfiles)
{
    u32 cap = 1 + NTARGETS;
    for (u32 f = 0; f < nfiles; f++) {
        cap += files[f].top_count;
    }

    *c = (Checker){
        .names = names,
        .files = files,
        .nfiles = nfiles,
        .target = { TARGET_OS_HOST, TARGET_ARCH_HOST },
    };
    c->package_names = calloc(names->count, sizeof *c->package_names);
    c->bound = calloc(names->count, sizeof *c->bound);
    c->bindings = calloc(names->count, sizeof *c->bindings);

    c->decls.names = calloc(cap, sizeof *c->decls.names);
    c->decls.kinds = calloc(cap, sizeof *c->decls.kinds);
    c->decls.flags = calloc(cap, sizeof *c->decls.flags);
    c->decls.phases = calloc(cap, sizeof *c->decls.phases);
    c->decls.busy = calloc(cap, sizeof *c->decls.busy);
    c->decls.files = calloc(cap, sizeof *c->decls.files);
    c->decls.firsts = calloc(cap, sizeof *c->decls.firsts);
    c->decls.roots = calloc(cap, sizeof *c->decls.roots);
    c->decls.types = calloc(cap, sizeof *c->decls.types);
    c->decls.values = calloc(cap, sizeof *c->decls.values);
    c->decls.members = calloc(cap, sizeof *c->decls.members);
    c->decls.member_counts = calloc(cap, sizeof *c->decls.member_counts);
    c->decls.bodies = calloc(cap, sizeof *c->decls.bodies);
    c->decls.funcs = calloc(cap, sizeof *c->decls.funcs);
    c->decls.count = 1;
    c->decls.cap = cap;
    c->imports = calloc((size_t)nfiles * NPACKAGES, sizeof *c->imports);
    c->entry_code = malloc(cap * sizeof *c->entry_code);

    c->types.cap = 256;
    c->types.kinds = malloc(c->types.cap * sizeof *c->types.kinds);
    c->types.arg0 = malloc(c->types.cap * sizeof *c->types.arg0);
    c->types.arg1 = malloc(c->types.cap * sizeof *c->types.arg1);
    c->types.pointers = malloc(c->types.cap * sizeof *c->types.pointers);
    c->types.optionals = malloc(c->types.cap * sizeof *c->types.optionals);
    c->types.slices = malloc(c->types.cap * sizeof *c->types.slices);
    c->types.map_mask = 63;
    c->types.map = calloc(c->types.map_mask + 1, sizeof *c->types.map);
    c->tuple_mask = 15;
    c->tuple_map = calloc(c->tuple_mask + 1, sizeof *c->tuple_map);

    c->lists_cap = 64;
    c->lists = malloc(c->lists_cap * sizeof *c->lists);
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
    c->builders = malloc(CHECK_MAX_DEPTH * sizeof *c->builders);

    c->unread_cap = 16;
    c->unread = malloc(c->unread_cap * sizeof *c->unread);
    c->pairs_cap = 64;
    c->pairs = malloc(c->pairs_cap * sizeof *c->pairs);
    c->strings_cap = 64;
    c->string_offsets = malloc(c->strings_cap * sizeof *c->string_offsets);
    c->string_lengths = malloc(c->strings_cap * sizeof *c->string_lengths);
    c->scope_cap = 64;
    c->scope = malloc(c->scope_cap * sizeof *c->scope);
    c->loops_cap = 16;
    c->loops = malloc(c->loops_cap * sizeof *c->loops);
    c->breaks_cap = 16;
    c->breaks = malloc(c->breaks_cap * sizeof *c->breaks);
    c->defers_cap = 16;
    c->defers = malloc(c->defers_cap * sizeof *c->defers);
    c->openers_cap = 64;
    c->openers = malloc(c->openers_cap);
    c->switches_cap = 16;
    c->switches = malloc(c->switches_cap * sizeof *c->switches);
    c->facts_cap = 64;
    c->facts = malloc(c->facts_cap * sizeof *c->facts);
    c->tests_cap = 64;
    c->tests = malloc(c->tests_cap * sizeof *c->tests);
    c->addressed_cap = 16;
    c->addressed = malloc(c->addressed_cap * sizeof *c->addressed);
    c->ir.funcs_cap = 64;
    c->ir.funcs = malloc(c->ir.funcs_cap * sizeof *c->ir.funcs);
    code_init(&c->ir.code);
    c->body.code = &c->ir.code;
    code_init(&c->deferred);
    code_init(&c->defer_code);
    exacts_init(&c->exacts);

    c->lists[0] = 0;
    c->nlists = 1;
    for (u32 id = 0; id < NFIXED_TYPES; id++) {
        add_type(c, fixed_kinds[id], fixed_arg0[id], fixed_arg1[id]);
    }
}

bool
check_target(Checker *c, u32 k, const char *text, u32 len)
{
    for (u32 id = target_names[k]; id < target_names[k + 1]; id++) {
        Name name = names_get(c->names, id);
        if (name.len == len && memcmp(name.text, text, len) == 0) {
            c->target[k] = id;
            return true;
        }
    }
    return false;
}

static void
place_zeroed(Checker *c)
{
    Decls *d = &c->decls;
    Ir *ir = &c->ir;
    u32 base = (ir->nglobals + c->zeroed_align - 1) & ~(c->zeroed_align - 1);
    assert((u64)base + c->zeroed <= GLOBALS_LIMIT);
    for (u32 decl = 1; decl < d->count; decl++) {
        if (d->kinds[decl] == DECL_VAR && (d->flags[decl] & DECL_ZEROED)) {
            d->values[decl] += base;
        }
    }
    ir->globals_size = base + c->zeroed;
}

void
check_package(Checker *c)
{
    const Decls *d = &c->decls;
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
    c->ir.globals_size = c->ir.nglobals;
    c->ir.nexports = c->ir.ncallbacks;
    if (c->zeroed_align) {
        place_zeroed(c);
    }
    u32 nodes = 0;
    for (u32 f = 0; f < c->nfiles; f++) {
        nodes += c->files[f].count;
    }
    reserve_code(&c->ir.code, nodes, nodes / 4, nodes / 4, nodes / 4, nodes / 8);
    if (c->entry) {
        check_entry(c);
    }
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
        case TYPE_MANY:
            fputs("[*]", out);
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
            fputs(t->arg1[type] & FN_C_CALL ? "extern fn(" : "fn(", out);
            for (u32 i = 1; i <= list[0]; i++) {
                bool rest = i == list[0] && (t->arg1[type] & FN_REST);
                fputs(i > 1 ? ", " : "", out);
                fputs(rest ? ".." : "", out);
                print_type(out, c, rest ? t->arg0[list[i]] : list[i]);
            }
            bool variadic = t->arg1[type] & FN_VARIADIC;
            fputs(variadic ? list[0] ? ", ..) -> " : "..) -> " : ") -> ", out);
            print_type(out, c, (u32)t->arg1[type]);
            return;
        }
        case TYPE_STRUCT:
        case TYPE_ENUM:
        case TYPE_UNION: {
            u32 decl = t->arg0[type];
            if (!(c->decls.flags[decl] & DECL_TUPLE)) {
                print_name(out, c, c->decls.names[decl]);
                return;
            }
            const Member *m = c->members + c->decls.members[decl];
            fputc('(', out);
            for (u32 i = 0; i < c->decls.member_counts[decl]; i++) {
                fputs(i ? ", " : "", out);
                print_type(out, c, m[i].type);
            }
            fputc(')', out);
            return;
        }
        default: assert(false && "not a built type");
    }
}

static void
print_user_type(FILE *out, const Checker *c, u32 type)
{
    if (is_vector(c, type)) {
        fprintf(out, "[%u]", (u32)c->types.arg1[type]);
        type = c->types.arg0[type];
    }
    print_type(out, c, type);
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
    if (bits == 0 && c->types.kinds[type] == TYPE_OPTIONAL) {
        fputs("nil", out);
    } else if (signed_type(c, type)) {
        fprintf(out, "%lld", (long long)bits);
    } else {
        fprintf(out, "%llu", (unsigned long long)bits);
    }
}

static void
print_string(FILE *out, const Checker *c, u32 string)
{
    const u8 *s = c->ir.data + c->string_offsets[string];
    u32 n = c->string_lengths[string];
    fputc('"', out);
    for (u32 k = 0; k < n; k++) {
        u8 ch = s[k];
        switch (ch) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (ch < 0x20 || ch == 0x7F) {
                    fprintf(out, "\\u%04X", ch);
                } else {
                    fputc(ch, out);
                }
                break;
        }
    }
    fputc('"', out);
}

static u32
string_at(const Checker *c, u32 offset)
{
    u32 lo = 0;
    u32 hi = c->nstrings;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (c->string_offsets[mid] < offset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    assert(lo < c->nstrings && c->string_offsets[lo] == offset);
    return lo;
}

static void
print_128(FILE *out, u64 low, u64 high, bool is_signed)
{
    if (is_signed && (int64_t)high < 0) {
        fputc('-', out);
        low = ~low + 1;
        high = ~high + (low == 0);
    }

    u32 limbs[4] = { (u32)low, (u32)(low >> 32), (u32)high, (u32)(high >> 32) };
    u32 groups[5];
    u32 n = 0;
    do {
        u64 rest = 0;
        for (u32 i = 4; i-- > 0;) {
            u64 part = rest << 32 | limbs[i];
            limbs[i] = (u32)(part / 1000000000);
            rest = part % 1000000000;
        }
        groups[n++] = (u32)rest;
    } while (limbs[0] | limbs[1] | limbs[2] | limbs[3]);

    fprintf(out, "%u", groups[n - 1]);
    for (u32 i = n - 1; i-- > 0;) {
        fprintf(out, "%09u", groups[i]);
    }
}

static void
print_data(FILE *out, const Checker *c, bool global, u64 at, u32 type)
{
    if (is_array(c, type)) {
        u32 elem = c->types.arg0[type];
        u64 size = check_type_size(c, elem);
        fputs(".[", out);
        for (u64 k = 0; k < c->types.arg1[type]; k++) {
            fputs(k ? ", " : "", out);
            print_data(out, c, global, at + k * size, elem);
        }
        fputs("]", out);
        return;
    }
    if (is_struct(c, type)) {
        u32 decl = c->types.arg0[type];
        u32 first = c->decls.members[decl];
        u32 count = c->decls.member_counts[decl];
        fputs(count ? ".{ " : ".{}", out);
        for (u32 k = first; k < first + count; k++) {
            fputs(k > first ? ", " : "", out);
            u64 offset = check_field_offset(c, field_layout(c, decl, k));
            print_data(out, c, global, at + offset, c->members[k].type);
        }
        fputs(count ? " }" : "", out);
        return;
    }
    const IrRelocs *relocs = global ? &c->ir.global_relocs : &c->ir.data_relocs;
    if (is_slice(c, type)) {
        u32 k = first_reloc(relocs, at);
        if (k < relocs->count && relocs->offsets[k] == at) {
            u64 offset;
            memcpy(&offset, (global ? c->ir.globals : c->ir.data) + at, sizeof offset);
            print_string(out, c, string_at(c, (u32)offset));
        } else {
            fputs(".[]", out);
        }
        return;
    }

    u64 size = check_type_size(c, type);
    u64 bits = 0;
    if (check_is_int128(type)) {
        u64 words[2];
        memcpy(words, (global ? c->ir.globals : c->ir.data) + at, sizeof words);
        print_128(out, words[0], words[1], type == TYPE_ID_I128);
        return;
    }
    memcpy(&bits, (global ? c->ir.globals : c->ir.data) + at, size);
    u32 k = first_reloc(relocs, at);
    if (k < relocs->count && relocs->offsets[k] == at) {
        print_string(out, c, string_at(c, (u32)bits));
        return;
    }
    if (c->types.kinds[type] == TYPE_FLOAT) {
        print_float(out, bits, c->types.arg0[type]);
        return;
    }
    if (size < 8 && signed_type(c, type)) {
        u32 shift = 64 - 8 * (u32)size;
        bits = (u64)((int64_t)(bits << shift) >> shift);
    }
    print_bits(out, c, type, bits);
}

static void
print_constant(FILE *out, const Checker *c, u32 type, u32 value)
{
    u8 kind = c->types.kinds[type];
    if (type == TYPE_ID_UNTYPED_STRING) {
        print_string(out, c, value);
    } else if (is_aggregate(c, type)) {
        print_data(out, c, false, value, type);
    } else if (type == TYPE_ID_BOOL) {
        fputs(exact_sign(&c->exacts, value) ? "true" : "false", out);
    } else if (kind == TYPE_FLOAT) {
        ExactStatus status;
        u32 width = c->types.arg0[type];
        print_float(out, exact_float(&c->exacts, value, width, &status), width);
    } else if (kind == TYPE_OPTIONAL && exact_sign(&c->exacts, value) == 0) {
        fputs("nil", out);
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
                case 'b':
                    print_name(out, c, NAME_BITS + builtin_package(d.arg));
                    fputc('.', out);
                    print_name(out, c, d.arg);
                    break;
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
                case 'T': print_user_type(out, c, d.arg); break;
                case 'U': print_user_type(out, c, d.arg2); break;
                case '%': fputc('%', out); break;
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
        bool rest = d->kinds[decl] == DECL_FN && i == count - 1 &&
                    (c->types.arg1[d->types[decl]] & FN_REST);
        fputs(i ? ", " : "", out);
        print_name(out, c, m.name);
        if (d->kinds[decl] != DECL_ENUM && (d->kinds[decl] != DECL_UNION || m.type)) {
            fputs(rest ? ": .." : ": ", out);
            print_type(out, c, rest ? c->types.arg0[m.type] : m.type);
        }
        print_value(out, c, m.type, m.value);
    }
    fputs(count ? close : "", out);
}

static void
print_decl(FILE *out, const Checker *c, u32 decl)
{
    const Decls *d = &c->decls;
    fputs(
        d->flags[decl] & DECL_INLINE     ? "@inline "
        : d->flags[decl] & DECL_NOINLINE ? "@noinline "
                                         : "",
        out
    );
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
                if (!is_untyped(type) && type != TYPE_ID_UNTYPED_STRING) {
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
        case DECL_FN: {
            bool variadic = c->types.arg1[type] & FN_VARIADIC;
            fputs(d->bodies[decl] == NO_BODY ? "extern fn " : "fn ", out);
            print_name(out, c, d->names[decl]);
            fputs(d->member_counts[decl] ? "" : variadic ? "(..)" : "()", out);
            print_members(out, c, decl, "(", variadic ? ", ..)" : ")");
            fputs(" -> ", out);
            print_type(out, c, (u32)c->types.arg1[type]);
            break;
        }
        case DECL_VAR:
            fputs(d->bodies[decl] == NO_BODY ? "extern " : "", out);
            print_name(out, c, d->names[decl]);
            if (type == TYPE_ID_NONE) {
                fputs(" = ?", out);
                break;
            }
            fputs(": ", out);
            print_type(out, c, type);
            if (!(d->flags[decl] & DECL_ZEROED) && d->bodies[decl] != NO_BODY) {
                fputs(" = ", out);
                print_data(out, c, true, d->values[decl], type);
            }
            break;
        default: assert(false && "unknown declaration kind");
    }
    fputc('\n', out);
}

void
check_dump(FILE *out, const Checker *c)
{
    for (u32 decl = 1; decl < c->decls.count; decl++) {
        if (!(c->decls.flags[decl] & DECL_TUPLE)) {
            print_decl(out, c, decl);
        }
    }
}

static void
print_operand(FILE *out, const Checker *c, const IrFunc *fn, u32 operand)
{
    if (!(operand & IR_CONST)) {
        fprintf(out, "%%%u", operand);
        return;
    }

    const IrConst *at = &c->ir.code.consts[fn->first_const + (operand & ~IR_CONST)];
    IrConst k = *at;
    if (check_is_int128(k.type)) {
        print_128(out, k.bits, at[1].bits, k.type == TYPE_ID_I128);
        return;
    }
    u32 type = is_vector(c, k.type) ? c->types.arg0[k.type] : k.type;
    if (c->types.kinds[type] == TYPE_FLOAT) {
        print_float(out, k.bits, c->types.arg0[type]);
    } else {
        print_bits(out, c, type, k.bits);
    }
}

static void
print_slot(FILE *out, const Checker *c, const IrFunc *fn, u32 slot)
{
    const IrSlot *slots = c->ir.code.slots + fn->first_slot;
    IrSlot s = slots[slot];
    if (!s.name) {
        fprintf(out, "$%u", slot);
        return;
    }
    print_name(out, c, s.name);
    if (slot > 0 && slots[slot - 1].name == s.name && slots[slot - 1].node == s.node &&
        c->types.kinds[slots[slot - 1].type] == TYPE_SLICE) {
        fputs(".len", out);
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
    if (fn->copies) {
        fprintf(out, "    copies: %u bytes\n", fn->copies);
    }
    if (fn->stack_args) {
        fprintf(out, "    stack arguments: %u bytes\n", fn->stack_args);
    }

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

        bool value = kind < IR_CALL ||
                     (kind <= IR_CALL_C && type != TYPE_ID_VOID &&
                      !is_aggregate(c, type)) ||
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
            case IR_DATA:
            case IR_GLOBAL:
            case IR_C_GLOBAL:
            case IR_CALLBACK:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                if (kind == IR_DATA && b == 0 && c->types.arg0[type] == TYPE_ID_U8) {
                    print_string(out, c, string_at(c, a));
                } else if (kind == IR_DATA && b == 0) {
                    print_data(out, c, false, a, c->types.arg0[type]);
                } else {
                    print_name(out, c, c->decls.names[b]);
                }
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
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                if (is_aggregate(c, type) &&
                    (check_struct_regs(c, type) & REGS_POINTER) && b < DATA_ROOT) {
                    fputs(", ", out);
                    print_slot(out, c, fn, b);
                }
                break;
            case IR_ZERO:
            case IR_NEG:
            case IR_NEG_WRAP:
            case IR_NOT:
            case IR_BIT_NOT:
            case IR_TRAILING_ZEROS:
            case IR_LEADING_ZEROS:
            case IR_COUNT_ONES:
            case IR_CONVERT:
            case IR_NARROW:
            case IR_TRUNCATE:
            case IR_BITCAST:
            case IR_SPLAT:
            case IR_TOP_BITS:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                break;
            case IR_CHECK_ALIGN:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                fprintf(out, ", %u", b);
                break;
            case IR_CHECK_NIL:
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                break;
            case IR_CALL:
            case IR_CALL_C: {
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
                if (is_aggregate(c, type)) {
                    fputs(" -> ", out);
                    print_slot(out, c, fn, args[args[0] + 1]);
                }
                break;
            }
            case IR_INDEX_LEN:
            case IR_SLICE:
            case IR_SLICE_FROM:
            case IR_STORE_PAIR: {
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                const u32 *x = code->extra + fn->first_extra + b;
                u32 n = kind == IR_SLICE ? 3 : 2;
                for (u32 k = 0; k < n; k++) {
                    fputs(", ", out);
                    print_operand(out, c, fn, x[k]);
                }
                break;
            }
            case IR_SECOND:
                fputc(' ', out);
                print_type(out, c, type);
                break;
            case IR_FIELD: {
                u32 decl = c->types.arg0[c->types.arg0[code->types[fn->first + a]]];
                u32 member = c->decls.members[decl] +
                             (b - c->decls.values[decl] - 2) / 2;
                fputc(' ', out);
                print_type(out, c, type);
                fputc(' ', out);
                print_operand(out, c, fn, a);
                fputs(", .", out);
                if (c->decls.flags[decl] & DECL_TUPLE) {
                    fprintf(out, "%u", member - c->decls.members[decl]);
                } else {
                    print_name(out, c, c->members[member].name);
                }
                break;
            }
            case IR_DROP:
            case IR_ASSERT:
            case IR_ASSUME:
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
                if (is_pair_type(c, type)) {
                    fputs(", ", out);
                    print_operand(out, c, fn, b);
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

static void
code_free(IrCode *code)
{
    free(code->kinds);
    free(code->types);
    free(code->as);
    free(code->bs);
    free(code->extra);
    free(code->slots);
    free(code->consts);
    free(code->lines);
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
    free(c->tuple_map);

    for (u32 i = 1; i <= c->nbuilders; i++) {
        code_free(&c->builders[i]);
    }
    free(c->builders);
    code_free(&c->ir.code);
    code_free(&c->deferred);
    code_free(&c->defer_code);
    free(c->ir.funcs);
    free(c->ir.data);
    free(c->ir.data_relocs.offsets);
    free(c->ir.globals);
    free(c->ir.global_relocs.offsets);
    free(c->ir.callbacks);

    exacts_free(&c->exacts);
    free(c->package_names);
    free(c->imports);
    free(c->entry_code);
    free(c->bound);
    free(c->bindings);
    free(c->scope);
    free(c->loops);
    free(c->breaks);
    free(c->defers);
    free(c->openers);
    free(c->cases);
    free(c->switches);
    free(c->facts);
    free(c->tests);
    free(c->addressed);
    free(c->member_keys);
    free(c->member_slots);
    free(c->members);
    free(c->scratch);
    free(c->lists);
    free(c->diags);
    free(c->stack);
    free(c->stack_tokens);
    free(c->unread);
    free(c->pairs);
    free(c->string_offsets);
    free(c->string_lengths);
    free(c->claims);
}
