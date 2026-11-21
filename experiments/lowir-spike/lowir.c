#include "lowir.h"

#include "lowir_interface.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    NONE = UINT32_MAX,
    PREDICT_UNLIKELY = 2,
    CASE_SPAN = 16,
};

typedef struct
{
    bool fl;
    bool sign;
    u32 bits;
} Scalar;

typedef struct
{
    u64 lo;
    u64 hi;
    u32 block;
} Range;

typedef struct
{
    CppgmLowir *m;
    const Checker *c;
    const IrCode *code;
    const char *prefix;
    u32 flags;
    u32 *symbols;
    u32 *c_symbols;
    CppgmType *records;
    CppgmType slice_record;
    u32 trap;
    u32 context;
    u32 data;
    u32 globals;
    CppgmType *params;
    CppgmField *fields;
    LowirValue *args;
    Range *ranges;
    char *name;
    const IrFunc *fn;
    u32 symbol;
    LowirBodyBuffers b;
    LowirBodyCounts n;
    u32 placed;
    u32 temps;
    u32 block;
    u32 spill;
    u32 traps[NTRAPS];
    LowirValue *values;
    u8 *truth;
    u32 *blocks;
    u32 *end_slots;
    u32 *ends;
    u32 *opens;
    u32 *links;
    u32 *stack;
    u32 *last;
    u32 *arms;
    u32 *slots;
    u8 *addressed;
} Lower;

static void
must(const Lower *l, int ok)
{
    if (!ok) {
        fprintf(stderr, "lowir: %s\n", cppgm_lowir_error(l->m));
        exit(1);
    }
}

static u32
intern(Lower *l, int len)
{
    u32 id = cppgm_lowir_symbol(l->m, l->name, (u32)len);
    must(l, id != CPPGM_NO_ID);
    return id;
}

static Name
decl_name(const Lower *l, u32 decl)
{
    return names_get(l->c->names, l->c->decls.names[decl]);
}

static Scalar
scalar_of(const Checker *c, u32 type)
{
    if (c->types.kinds[type] == TYPE_ENUM) {
        type = (u32)c->types.arg1[type];
    }
    switch (c->types.kinds[type]) {
        case TYPE_INT:
            return (Scalar){ false, c->types.arg1[type] != 0, c->types.arg0[type] };
        case TYPE_FLOAT: return (Scalar){ true, true, c->types.arg0[type] };
        case TYPE_BOOL: return (Scalar){ false, false, 8 };
        default: return (Scalar){ false, false, 64 };
    }
}

static bool
is_address(const Checker *c, u32 type)
{
    u8 kind = c->types.kinds[type];
    return kind == TYPE_POINTER || kind == TYPE_MANY || kind == TYPE_SLICE ||
           kind == TYPE_STRUCT || kind == TYPE_FN || kind == TYPE_ARRAY;
}

static CppgmType
scalar_type(const Checker *c, u32 type)
{
    static const CppgmType ints[2][4] = {
        { CPPGM_TYPE_U8, CPPGM_TYPE_U16, CPPGM_TYPE_U32, CPPGM_TYPE_I64 },
        { CPPGM_TYPE_I8, CPPGM_TYPE_I16, CPPGM_TYPE_I32, CPPGM_TYPE_I64 },
    };
    u8 kind = c->types.kinds[type];
    if (is_address(c, type)) {
        return CPPGM_TYPE_PTR;
    }
    if (kind == TYPE_VECTOR) {
        return CPPGM_TYPE_V128;
    }
    if (kind == TYPE_BOOL) {
        return CPPGM_TYPE_I1;
    }
    Scalar s = scalar_of(c, type);
    if (s.fl) {
        return s.bits == 32 ? CPPGM_TYPE_F32 : CPPGM_TYPE_F64;
    }
    return ints[s.sign][__builtin_ctz(s.bits) - 3];
}

static u32
flatten(const Checker *c, u32 type, u64 *count)
{
    while (c->types.kinds[type] == TYPE_ARRAY) {
        *count *= c->types.arg1[type];
        type = c->types.arg0[type];
    }
    return type;
}

static CppgmType record_of(Lower *l, u32 type);

static CppgmType
field_type(Lower *l, u32 type)
{
    u8 kind = l->c->types.kinds[type];
    if (kind == TYPE_STRUCT) {
        return record_of(l, type);
    }
    if (kind == TYPE_SLICE) {
        return l->slice_record;
    }
    if (kind == TYPE_BOOL) {
        return CPPGM_TYPE_U8;
    }
    return scalar_type(l->c, type);
}

static CppgmType
record_of(Lower *l, u32 type)
{
    const Checker *c = l->c;
    if (l->records[type]) {
        return l->records[type];
    }

    u32 first = 0;
    u32 end = 1;
    u32 layout = 0;
    bool fields = c->types.kinds[type] == TYPE_STRUCT;
    if (fields) {
        u32 decl = c->types.arg0[type];
        first = c->decls.members[decl];
        end = first + c->decls.member_counts[decl];
        layout = c->decls.values[decl] + 2 - 2 * first;
    }
    for (u32 i = first; i < end; i++) {
        u64 count = 1;
        u32 t = flatten(c, fields ? c->members[i].type : type, &count);
        if (c->types.kinds[t] == TYPE_STRUCT) {
            record_of(l, t);
        }
    }

    u32 n = 0;
    for (u32 i = first; i < end; i++) {
        u64 count = 1;
        u32 t = flatten(c, fields ? c->members[i].type : type, &count);
        if (count && check_type_size(c, t)) {
            u64 at = fields ? check_field_offset(c, layout + 2 * i) : 0;
            l->fields[n++] = (CppgmField){ at, field_type(l, t), count };
        }
    }

    u64 size = check_type_size(c, type);
    l->records[type] = cppgm_lowir_record(l->m, size ? size : 1, check_type_align(c, type), l->fields, n);
    must(l, l->records[type] != 0);
    return l->records[type];
}

static CppgmType
boundary_type(Lower *l, u32 type)
{
    u8 kind = l->c->types.kinds[type];
    if (type == TYPE_ID_VOID) {
        return CPPGM_TYPE_VOID;
    }
    if (kind == TYPE_STRUCT) {
        return record_of(l, type);
    }
    if (kind == TYPE_SLICE) {
        return l->slice_record;
    }
    return scalar_type(l->c, type);
}

static u32
chibi_signature(Lower *l, const IrFunc *fn)
{
    const Checker *c = l->c;
    const IrSlot *slots = l->code->slots + fn->first_slot;
    u32 n = 0;
    l->params[n++] = CPPGM_TYPE_PTR;
    for (u32 p = 0; p < fn->nparams; p++) {
        u32 t = slots[p].type;
        if (c->types.kinds[t] == TYPE_SLICE) {
            l->params[n++] = CPPGM_TYPE_PTR;
            l->params[n++] = CPPGM_TYPE_I64;
            p++;
        } else {
            l->params[n++] = boundary_type(l, t);
        }
    }

    CppgmType result = boundary_type(l, (u32)c->types.arg1[c->decls.types[fn->decl]]);
    u32 id = cppgm_lowir_signature(l->m, result, l->params, n, CPPGM_ARITY_FIXED);
    must(l, id != CPPGM_NO_ID);
    return id;
}

static u32
c_signature(Lower *l, u32 decl)
{
    const Checker *c = l->c;
    u32 type = c->decls.types[decl];
    const u32 *params = c->lists + c->types.arg0[type];
    for (u32 p = 0; p < params[0]; p++) {
        l->params[p] = boundary_type(l, params[1 + p]);
    }

    CppgmType result = boundary_type(l, (u32)c->types.arg1[type]);
    u32 arity = c->types.arg1[type] & FN_VARIADIC ? CPPGM_ARITY_VARIADIC
                                                  : CPPGM_ARITY_FIXED;
    u32 id = cppgm_lowir_signature(l->m, result, l->params, params[0], arity);
    must(l, id != CPPGM_NO_ID);
    return id;
}

static LowirValue
value_of(u8 kind, u32 index)
{
    return (LowirValue){ kind, 0, 0, index };
}

static LowirValue
nothing(void)
{
    return (LowirValue){ 0 };
}

static LowirValue
temp(u32 id)
{
    return value_of(CPPGM_VALUE_TEMP, id);
}

static LowirValue
slot(u32 id)
{
    return value_of(CPPGM_VALUE_SLOT, id);
}

static LowirValue
symbol(u32 id)
{
    return value_of(CPPGM_VALUE_SYMBOL, id);
}

static LowirValue
block(u32 id)
{
    return value_of(CPPGM_VALUE_BLOCK, id);
}

static void
grow(Lower *l, LowirBodyCounts min)
{
    must(l, cppgm_lowir_body_reserve(l->m, l->symbol, &min, &l->b));
}

static u32
new_block(Lower *l)
{
    if (l->n.blocks == l->b.capacity.blocks) {
        LowirBodyCounts min = l->b.capacity;
        min.blocks *= 2;
        grow(l, min);
    }
    return l->n.blocks++;
}

static u32
new_slot(Lower *l, CppgmType type)
{
    if (l->n.slots == l->b.capacity.slots) {
        LowirBodyCounts min = l->b.capacity;
        min.slots *= 2;
        grow(l, min);
    }
    l->b.slots[l->n.slots] = (LowirSlot){ .type = type };
    return l->n.slots++;
}

static LowirValue
literal(Lower *l, u8 kind, u8 float_kind, u64 low, u64 high)
{
    if (l->n.literals == l->b.capacity.literals) {
        LowirBodyCounts min = l->b.capacity;
        min.literals *= 2;
        grow(l, min);
    }
    l->b.literals[l->n.literals] = (LowirLiteral){ low, high };
    return (LowirValue){ kind, float_kind, 0, l->n.literals++ };
}

static LowirValue
integer(Lower *l, u64 bits)
{
    return literal(l, CPPGM_VALUE_INTEGER, 0, bits, (int64_t)bits < 0 ? UINT64_MAX : 0);
}

static void
push_operand(Lower *l, LowirValue v)
{
    if (l->n.operands == l->b.capacity.operands) {
        LowirBodyCounts min = l->b.capacity;
        min.operands *= 2;
        grow(l, min);
    }
    l->b.operands[l->n.operands++] = v;
}

static void
start(Lower *l, u32 id)
{
    l->b.layout[l->placed++] = id;
    l->block = id;
}

static LowirInstruction *
append(Lower *l, u8 op, u8 sub, CppgmType type)
{
    if (l->block == NONE) {
        start(l, new_block(l));
    }
    if (l->n.instructions == l->b.capacity.instructions) {
        LowirBodyCounts min = l->b.capacity;
        min.instructions *= 2;
        grow(l, min);
    }

    LowirInstruction *in = &l->b.instructions[l->n.instructions++];
    *in = (LowirInstruction){ .op = op,
                              .sub = sub,
                              .result = CPPGM_NO_ID,
                              .next = CPPGM_NO_ID,
                              .location = l->block,
                              .type = type };
    return in;
}

static LowirValue
define(Lower *l, LowirInstruction *in)
{
    in->result = l->temps++;
    return temp(in->result);
}

static LowirValue
put(Lower *l, u8 op, u8 sub, CppgmType type, LowirValue a, LowirValue b, LowirValue c)
{
    LowirInstruction *in = append(l, op, sub, type);
    in->ops[0] = a;
    in->ops[1] = b;
    in->ops[2] = c;
    return define(l, in);
}

static void
effect(Lower *l, u8 op, u8 sub, CppgmType type, LowirValue a, LowirValue b, LowirValue c)
{
    LowirInstruction *in = append(l, op, sub, type);
    in->ops[0] = a;
    in->ops[1] = b;
    in->ops[2] = c;
}

static void
jump(Lower *l, u32 target)
{
    if (l->block != NONE) {
        effect(l, CPPGM_OP_JUMP, 0, CPPGM_TYPE_VOID, block(target), nothing(), nothing());
        l->block = NONE;
    }
}

static void
branch(Lower *l, LowirValue cond, u32 yes, u32 no, u8 predict)
{
    effect(l, CPPGM_OP_BRANCH, predict, CPPGM_TYPE_VOID, cond, block(yes), block(no));
    l->block = NONE;
}

static LowirValue
binary(Lower *l, u8 op, CppgmType type, LowirValue a, LowirValue b)
{
    return put(l, CPPGM_OP_BINARY, op, type, a, b, nothing());
}

static LowirValue
binary_flags(Lower *l, u8 op, CppgmType type, LowirValue a, LowirValue b, u8 flags)
{
    LowirInstruction *in = append(l, CPPGM_OP_BINARY, op, type);
    in->flags = flags;
    in->ops[0] = a;
    in->ops[1] = b;
    return define(l, in);
}

static LowirValue
compare(Lower *l, u8 pred, CppgmType type, LowirValue a, LowirValue b)
{
    return put(l, CPPGM_OP_COMPARE, pred, type, a, b, nothing());
}

static LowirValue
unary(Lower *l, u8 op, CppgmType type, LowirValue a)
{
    return put(l, CPPGM_OP_UNARY, op, type, a, nothing(), nothing());
}

static LowirValue
convert(Lower *l, u8 op, CppgmType to, CppgmType from, LowirValue a)
{
    LowirInstruction *in = append(l, CPPGM_OP_CONVERT, op, to);
    in->ops[0] = a;
    memcpy(&in->immediate, &from, sizeof from);
    return define(l, in);
}

static LowirValue
choose(Lower *l, CppgmType type, LowirValue cond, LowirValue a, LowirValue b)
{
    return put(l, CPPGM_OP_SELECT, 0, type, cond, a, b);
}

static LowirValue
load(Lower *l, CppgmType type, LowirValue address)
{
    return put(l, CPPGM_OP_LOAD, 0, type, address, nothing(), nothing());
}

static void
store(Lower *l, CppgmType type, LowirValue v, LowirValue address)
{
    CppgmType t = type == CPPGM_TYPE_U8  ? CPPGM_TYPE_I8
                : type == CPPGM_TYPE_U16 ? CPPGM_TYPE_I16
                                         : type;
    effect(l, CPPGM_OP_STORE, 0, t, v, address, nothing());
}

static LowirValue
address_of(Lower *l, LowirValue v)
{
    return put(l, CPPGM_OP_ADDR, 0, CPPGM_TYPE_PTR, v, nothing(), nothing());
}

static LowirValue
offset(Lower *l, LowirValue base, u64 bytes)
{
    if (!bytes) {
        return base;
    }
    LowirValue k = integer(l, bytes);
    return put(l, CPPGM_OP_INDEX, 0, CPPGM_TYPE_I8, base, k, nothing());
}

static LowirValue
scale(Lower *l, LowirValue base, LowirValue index, u64 size)
{
    CppgmType type = size == 1 ? CPPGM_TYPE_I8
                   : size == 2 ? CPPGM_TYPE_I16
                   : size == 4 ? CPPGM_TYPE_I32
                   : size == 8 ? CPPGM_TYPE_I64
                               : CPPGM_TYPE_OBJ | size << 16;
    if (!size) {
        return base;
    }
    return put(l, CPPGM_OP_INDEX, 0, type, base, index, nothing());
}

static void
copy_object(Lower *l, LowirValue from, LowirValue to, u64 size, u32 align)
{
    if (size) {
        LowirInstruction *in = append(l, CPPGM_OP_COPY_OBJECT, (u8)__builtin_ctz(align), CPPGM_TYPE_VOID);
        in->ops[0] = from;
        in->ops[1] = to;
        in->immediate = (int64_t)size;
    }
}

static void
zero_object(Lower *l, LowirValue to, u64 size, u32 align)
{
    if (size) {
        LowirInstruction *in = append(l, CPPGM_OP_ZERO_INIT, (u8)__builtin_ctz(align), CPPGM_TYPE_VOID);
        in->ops[0] = to;
        in->immediate = (int64_t)size;
    }
}

static u32
trap_block(Lower *l, u32 kind)
{
    if (l->traps[kind] == NONE) {
        l->traps[kind] = new_block(l);
    }
    return l->traps[kind];
}

static void
check(Lower *l, LowirValue fail, u32 kind)
{
    u32 trap = trap_block(l, kind);
    u32 next = new_block(l);
    branch(l, fail, trap, next, PREDICT_UNLIKELY);
    start(l, next);
}

static LowirValue
call(Lower *l, LowirValue callee, CppgmType type, u32 first, u32 count)
{
    LowirInstruction *in = append(l, CPPGM_OP_CALL, 0, type);
    in->ops[0] = callee;
    in->extra_first = first;
    in->extra_count = count;
    return type == CPPGM_TYPE_VOID ? nothing() : define(l, in);
}

static u32
push_args(Lower *l, u32 n)
{
    u32 first = l->n.operands;
    for (u32 k = 0; k < n; k++) {
        push_operand(l, l->args[k]);
    }
    return first;
}

static void
put_traps(Lower *l)
{
    for (u32 k = TRAP_OVERFLOW; k < NTRAPS; k++) {
        if (l->traps[k] == NONE) {
            continue;
        }
        start(l, l->traps[k]);
        l->args[0] = integer(l, k);
        u32 first = push_args(l, 1);
        call(l, symbol(l->trap), CPPGM_TYPE_VOID, first, 1);
        effect(l, CPPGM_OP_UNREACHABLE, 0, CPPGM_TYPE_VOID, nothing(), nothing(), nothing());
        l->block = NONE;
    }
}

static void
commit(Lower *l)
{
    assert(l->placed == l->n.blocks);
    must(l, cppgm_lowir_body_commit(l->m, l->symbol, &l->n, l->temps));
}

static IrConst
constant_at(const Lower *l, u32 x)
{
    return l->code->consts[l->fn->first_const + (x & ~IR_CONST)];
}

static LowirValue
constant(Lower *l, IrConst k)
{
    const Checker *c = l->c;
    if (!is_address(c, k.type) && c->types.kinds[k.type] != TYPE_VECTOR) {
        Scalar s = scalar_of(c, k.type);
        if (s.fl && s.bits == 32) {
            return literal(l, CPPGM_VALUE_FLOAT, CPPGM_TYPE_F32, (u32)k.bits, 0);
        }
        if (s.fl) {
            return literal(l, CPPGM_VALUE_FLOAT, CPPGM_TYPE_F64, k.bits, 0);
        }
    }
    return integer(l, k.bits);
}

static LowirValue
operand(Lower *l, u32 x)
{
    if (x & IR_CONST) {
        return constant(l, constant_at(l, x));
    }
    if (l->truth[x]) {
        return convert(l, CPPGM_CONVERT_TRUNC, CPPGM_TYPE_I1, CPPGM_TYPE_I64, l->values[x]);
    }
    return l->values[x];
}

static LowirValue
condition(Lower *l, u32 x)
{
    if (x & IR_CONST) {
        return constant(l, constant_at(l, x));
    }
    return l->values[x];
}

static u32
value_type(const Lower *l, u32 i)
{
    u32 g = l->fn->first + i;
    u8 kind = l->code->kinds[g];
    u32 type = l->code->types[g];
    if (kind >= IR_EQ && kind <= IR_MUL_OVERFLOWS) {
        return l->c->types.kinds[type] == TYPE_VECTOR ? type : TYPE_ID_BOOL;
    }
    if (kind == IR_TOP_BITS) {
        return l->c->types.arg1[type] == 16 ? TYPE_ID_U16 : TYPE_ID_U8;
    }
    return type;
}

static u32
operand_type(const Lower *l, u32 x)
{
    return x & IR_CONST ? constant_at(l, x).type : value_type(l, x);
}

static const u32 *
extra(const Lower *l, u32 b)
{
    return l->code->extra + l->fn->first_extra + b;
}

static int64_t
signed_at(u64 bits, u32 width)
{
    return width == 64 ? (int64_t)bits
                       : (int64_t)(bits << (64 - width)) >> (64 - width);
}

static u64
unsigned_at(u64 bits, u32 width)
{
    return width == 64 ? bits : bits & ((1ull << width) - 1);
}

static LowirValue
widen(Lower *l, u32 x)
{
    const Checker *c = l->c;
    u32 t = operand_type(l, x);
    Scalar s = scalar_of(c, t);
    if (x & IR_CONST) {
        u64 bits = constant_at(l, x).bits;
        return integer(l, s.sign ? (u64)signed_at(bits, s.bits) : unsigned_at(bits, s.bits));
    }

    CppgmType from = scalar_type(c, t);
    if (from == CPPGM_TYPE_I64 || from == CPPGM_TYPE_PTR) {
        return l->values[x];
    }
    u8 op = s.sign ? CPPGM_CONVERT_S_EXT : CPPGM_CONVERT_Z_EXT;
    return convert(l, op, CPPGM_TYPE_I64, from, l->values[x]);
}

static LowirValue
change_int(Lower *l, LowirValue v, CppgmType from, Scalar fs, CppgmType to, Scalar ts)
{
    if (from == to) {
        return v;
    }
    if (from == CPPGM_TYPE_PTR || to == CPPGM_TYPE_PTR || ts.bits == fs.bits) {
        return put(l, CPPGM_OP_COPY, 0, to, v, nothing(), nothing());
    }
    if (ts.bits > fs.bits) {
        return convert(l, fs.sign ? CPPGM_CONVERT_S_EXT : CPPGM_CONVERT_Z_EXT, to, from, v);
    }
    return convert(l, CPPGM_CONVERT_TRUNC, to, from, v);
}

static bool
is_length(const Lower *l, u32 k)
{
    const IrSlot *slots = l->code->slots + l->fn->first_slot;
    return k > 0 && l->c->types.kinds[slots[k - 1].type] == TYPE_SLICE;
}

static LowirValue
slot_address(Lower *l, u32 k)
{
    assert(l->slots[k] != NONE);
    return address_of(l, slot(l->slots[k]));
}

static LowirValue
load_slot(Lower *l, u32 k)
{
    const Checker *c = l->c;
    const IrSlot *slots = l->code->slots + l->fn->first_slot;
    if (k < l->fn->nparams && l->slots[k] == NONE) {
        return temp(1 + k);
    }
    if (is_length(l, k) && l->addressed[k - 1]) {
        return load(l, CPPGM_TYPE_I64, offset(l, slot_address(l, k - 1), 8));
    }
    if (is_length(l, k)) {
        return load(l, CPPGM_TYPE_I64, slot(l->slots[k]));
    }
    if (c->types.kinds[slots[k].type] == TYPE_SLICE && l->addressed[k]) {
        return load(l, CPPGM_TYPE_PTR, slot_address(l, k));
    }
    if (c->types.kinds[slots[k].type] == TYPE_SLICE) {
        return load(l, CPPGM_TYPE_PTR, slot(l->slots[k]));
    }
    return load(l, scalar_type(c, slots[k].type), slot(l->slots[k]));
}

static void
store_slot(Lower *l, u32 k, LowirValue v)
{
    const Checker *c = l->c;
    const IrSlot *slots = l->code->slots + l->fn->first_slot;
    if (is_length(l, k) && l->addressed[k - 1]) {
        store(l, CPPGM_TYPE_I64, v, offset(l, slot_address(l, k - 1), 8));
    } else if (is_length(l, k)) {
        store(l, CPPGM_TYPE_I64, v, slot(l->slots[k]));
    } else if (c->types.kinds[slots[k].type] == TYPE_SLICE && l->addressed[k]) {
        store(l, CPPGM_TYPE_PTR, v, slot_address(l, k));
    } else if (c->types.kinds[slots[k].type] == TYPE_SLICE) {
        store(l, CPPGM_TYPE_PTR, v, slot(l->slots[k]));
    } else {
        store(l, scalar_type(c, slots[k].type), v, slot(l->slots[k]));
    }
}

static LowirValue
spill_address(Lower *l)
{
    if (l->spill == NONE) {
        l->spill = new_slot(l, l->slice_record);
    }
    return address_of(l, slot(l->spill));
}

static void
load_at(Lower *l, u32 i, u32 type, u32 a)
{
    u8 kind = l->c->types.kinds[type];
    LowirValue p = operand(l, a);
    if (kind == TYPE_STRUCT) {
        l->values[i] = p;
    } else if (kind == TYPE_SLICE) {
        l->values[i] = load(l, CPPGM_TYPE_PTR, p);
        l->values[i + 1] = load(l, CPPGM_TYPE_I64, offset(l, p, 8));
    } else {
        l->values[i] = load(l, scalar_type(l->c, type), p);
    }
}

static void
index_op(Lower *l, u32 i, u32 type, u32 a, u32 b)
{
    const Checker *c = l->c;
    u64 size = check_type_size(c, c->types.arg0[type]);
    LowirValue base = operand(l, a);
    if (b & IR_CONST) {
        l->values[i] = offset(l, base, constant_at(l, b).bits * size);
        return;
    }

    u32 base_type = value_type(l, a);
    LowirValue k = widen(l, b);
    if (c->types.kinds[base_type] == TYPE_POINTER) {
        u64 n = c->types.arg1[c->types.arg0[base_type]];
        check(l, compare(l, CPPGM_CMP_UGE, CPPGM_TYPE_I64, k, integer(l, n)), TRAP_BOUNDS);
    }
    l->values[i] = scale(l, base, k, size);
}

static void
index_len(Lower *l, u32 i, u32 type, u32 a, const u32 *x)
{
    u64 size = check_type_size(l->c, l->c->types.arg0[type]);
    LowirValue base = operand(l, a);
    LowirValue n = widen(l, x[0]);
    LowirValue k = widen(l, x[1]);
    check(l, compare(l, CPPGM_CMP_UGE, CPPGM_TYPE_I64, k, n), TRAP_BOUNDS);
    l->values[i] = scale(l, base, k, size);
}

static void
slice(Lower *l, u32 i, u8 kind, u32 type, u32 a, const u32 *x)
{
    u64 size = check_type_size(l->c, l->c->types.arg0[type]);
    LowirValue base = operand(l, a);
    LowirValue len = widen(l, x[0]);
    LowirValue lo = widen(l, x[1]);
    LowirValue hi = kind == IR_SLICE ? widen(l, x[2]) : len;

    LowirValue below = compare(l, CPPGM_CMP_LT, CPPGM_TYPE_I64, lo, integer(l, 0));
    LowirValue order = compare(l, CPPGM_CMP_GT, CPPGM_TYPE_I64, lo, hi);
    LowirValue over = compare(l, CPPGM_CMP_GT, CPPGM_TYPE_I64, hi, len);
    LowirValue fail = binary(l, CPPGM_BINARY_OR, CPPGM_TYPE_I64, below, order);
    fail = binary(l, CPPGM_BINARY_OR, CPPGM_TYPE_I64, fail, over);
    check(l, fail, TRAP_BOUNDS);

    l->values[i] = scale(l, base, lo, size);
    l->values[i + 1] = binary(l, CPPGM_BINARY_SUB, CPPGM_TYPE_I64, hi, lo);
}

static const u8 binary_ops[NIR_KINDS] = {
    [IR_ADD] = CPPGM_BINARY_ADD,      [IR_SUB] = CPPGM_BINARY_SUB,
    [IR_MUL] = CPPGM_BINARY_MUL,      [IR_ADD_WRAP] = CPPGM_BINARY_ADD,
    [IR_SUB_WRAP] = CPPGM_BINARY_SUB, [IR_MUL_WRAP] = CPPGM_BINARY_MUL,
    [IR_AND] = CPPGM_BINARY_AND,      [IR_OR] = CPPGM_BINARY_OR,
    [IR_XOR] = CPPGM_BINARY_XOR,
};

static LowirValue
constant_overflow(Lower *l, u8 kind, Scalar s, CppgmType t, LowirValue x, u64 k)
{
    u64 umax = ~0ull >> (64 - s.bits);
    int64_t smax = (int64_t)(umax >> 1);
    int64_t smin = -smax - 1;
    int64_t sk = signed_at(k, s.bits);
    u64 uk = unsigned_at(k, s.bits);
    if (kind == IR_ADD && s.sign && sk > 0) {
        return compare(l, CPPGM_CMP_GT, t, x, integer(l, (u64)(smax - sk)));
    }
    if (kind == IR_ADD && s.sign && sk < 0) {
        return compare(l, CPPGM_CMP_LT, t, x, integer(l, (u64)(smin - sk)));
    }
    if (kind == IR_ADD && !s.sign && uk) {
        return compare(l, CPPGM_CMP_UGT, t, x, integer(l, umax - uk));
    }
    if (kind == IR_SUB && s.sign && sk > 0) {
        return compare(l, CPPGM_CMP_LT, t, x, integer(l, (u64)(smin + sk)));
    }
    if (kind == IR_SUB && s.sign && sk < 0) {
        return compare(l, CPPGM_CMP_GT, t, x, integer(l, (u64)(smax + sk)));
    }
    if (kind == IR_SUB && !s.sign && uk) {
        return compare(l, CPPGM_CMP_ULT, t, x, integer(l, uk));
    }
    if (kind == IR_MUL && s.sign && sk == -1) {
        return compare(l, CPPGM_CMP_EQ, t, x, integer(l, (u64)smin));
    }
    if (kind == IR_MUL && s.sign && (sk < -1 || sk > 1)) {
        int64_t lo = sk > 0 ? smin / sk : smax / sk;
        int64_t hi = sk > 0 ? smax / sk : smin / sk;
        LowirValue d = binary(l, CPPGM_BINARY_SUB, t, x, integer(l, (u64)lo));
        return compare(l, CPPGM_CMP_UGT, t, d, integer(l, (u64)hi - (u64)lo));
    }
    if (kind == IR_MUL && !s.sign && uk > 1) {
        return compare(l, CPPGM_CMP_UGT, t, x, integer(l, umax / uk));
    }
    return nothing();
}

static LowirValue
signs_differ(Lower *l, CppgmType t, LowirValue p, LowirValue q)
{
    LowirValue both = binary(l, CPPGM_BINARY_AND, t, p, q);
    return compare(l, CPPGM_CMP_LT, t, both, integer(l, 0));
}

static LowirValue
wide_overflow(Lower *l, Scalar s, CppgmType t, LowirValue x, LowirValue y, LowirValue *r)
{
    u8 ext = s.sign ? CPPGM_CONVERT_S_EXT : CPPGM_CONVERT_Z_EXT;
    u64 umax = ~0ull >> (64 - s.bits);
    if (s.bits < 64) {
        LowirValue wx = convert(l, ext, CPPGM_TYPE_I64, t, x);
        LowirValue wy = convert(l, ext, CPPGM_TYPE_I64, t, y);
        LowirValue m = binary(l, CPPGM_BINARY_MUL, CPPGM_TYPE_I64, wx, wy);
        LowirValue d = s.sign
                         ? binary(l, CPPGM_BINARY_ADD, CPPGM_TYPE_I64, m, integer(l, (umax >> 1) + 1))
                         : m;
        LowirValue fail = compare(l, CPPGM_CMP_UGT, CPPGM_TYPE_I64, d, integer(l, umax));
        *r = convert(l, CPPGM_CONVERT_TRUNC, t, CPPGM_TYPE_I64, m);
        return fail;
    }

    LowirValue wx = convert(l, ext, CPPGM_TYPE_I128, CPPGM_TYPE_I64, x);
    LowirValue wy = convert(l, ext, CPPGM_TYPE_I128, CPPGM_TYPE_I64, y);
    LowirValue m = binary(l, CPPGM_BINARY_MUL, CPPGM_TYPE_I128, wx, wy);
    *r = convert(l, CPPGM_CONVERT_TRUNC, CPPGM_TYPE_I64, CPPGM_TYPE_I128, m);
    if (s.sign) {
        LowirValue back = convert(l, CPPGM_CONVERT_S_EXT, CPPGM_TYPE_I128, CPPGM_TYPE_I64, *r);
        return compare(l, CPPGM_CMP_NE, CPPGM_TYPE_I128, back, m);
    }
    LowirValue high = binary(l, CPPGM_BINARY_U_SHR, CPPGM_TYPE_I128, m, integer(l, 64));
    return compare(l, CPPGM_CMP_NE, CPPGM_TYPE_I128, high, integer(l, 0));
}

static LowirValue
overflow(Lower *l, u8 kind, u32 type, u32 a, u32 b, LowirValue *x, LowirValue *y, LowirValue *r)
{
    Scalar s = scalar_of(l->c, type);
    CppgmType t = scalar_type(l->c, type);
    *r = nothing();
    if (kind == IR_NEG) {
        u64 smin = ~0ull << (s.bits - 1);
        *x = operand(l, a);
        LowirValue fail = compare(
            l,
            s.sign ? CPPGM_CMP_EQ : CPPGM_CMP_NE,
            t,
            *x,
            integer(l, s.sign ? smin : 0)
        );
        *r = unary(l, CPPGM_UNARY_NEG, t, *x);
        return fail;
    }
    if (kind != IR_SUB && (a & IR_CONST) && !(b & IR_CONST)) {
        u32 swap = a;
        a = b;
        b = swap;
    }

    *x = operand(l, a);
    *y = operand(l, b);
    if (b & IR_CONST) {
        return constant_overflow(l, kind, s, t, *x, constant_at(l, b).bits);
    }
    if (kind == IR_ADD && s.sign) {
        *r = binary(l, CPPGM_BINARY_ADD, t, *x, *y);
        LowirValue p = binary(l, CPPGM_BINARY_XOR, t, *x, *r);
        LowirValue q = binary(l, CPPGM_BINARY_XOR, t, *y, *r);
        return signs_differ(l, t, p, q);
    }
    if (kind == IR_ADD) {
        *r = binary(l, CPPGM_BINARY_ADD, t, *x, *y);
        return compare(l, CPPGM_CMP_ULT, t, *r, *x);
    }
    if (kind == IR_SUB && s.sign) {
        *r = binary(l, CPPGM_BINARY_SUB, t, *x, *y);
        LowirValue p = binary(l, CPPGM_BINARY_XOR, t, *x, *y);
        LowirValue q = binary(l, CPPGM_BINARY_XOR, t, *x, *r);
        return signs_differ(l, t, p, q);
    }
    if (kind == IR_SUB) {
        return compare(l, CPPGM_CMP_ULT, t, *x, *y);
    }
    return wide_overflow(l, s, t, *x, *y, r);
}

static void
arithmetic(Lower *l, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    Scalar s = scalar_of(l->c, type);
    CppgmType t = scalar_type(l->c, type);
    if (s.fl || (l->flags & LOWIR_WRAP)) {
        LowirValue x = operand(l, a);
        if (kind == IR_NEG) {
            l->values[i] = unary(l, CPPGM_UNARY_NEG, t, x);
            return;
        }
        LowirValue y = operand(l, b);
        l->values[i] = binary(l, binary_ops[kind], t, x, y);
        return;
    }

    LowirValue x, y, r;
    LowirValue fail = overflow(l, kind, type, a, b, &x, &y, &r);
    if (fail.kind) {
        check(l, fail, TRAP_OVERFLOW);
    }
    if (!r.kind) {
        u8 flags = s.sign || s.bits < 64 ? CPPGM_FLAG_NO_WRAP : 0;
        r = binary_flags(l, binary_ops[kind], t, x, y, flags);
    }
    l->values[i] = r;
}

static void
overflows(Lower *l, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    static const u8 bases[NIR_KINDS] = {
        [IR_ADD_OVERFLOWS] = IR_ADD,
        [IR_SUB_OVERFLOWS] = IR_SUB,
        [IR_MUL_OVERFLOWS] = IR_MUL,
    };
    LowirValue x, y, r;
    LowirValue fail = overflow(l, bases[kind], type, a, b, &x, &y, &r);
    l->values[i] = fail.kind ? fail : integer(l, 0);
    l->truth[i] = fail.kind != 0;
}

static bool
is_minus_one(const Lower *l, u32 x, Scalar s)
{
    return unsigned_at(constant_at(l, x).bits, s.bits) == unsigned_at(~0ull, s.bits);
}

static void
divide(Lower *l, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    Scalar s = scalar_of(l->c, type);
    CppgmType t = scalar_type(l->c, type);
    LowirValue x = operand(l, a);
    LowirValue y = operand(l, b);
    if (s.fl) {
        l->values[i] = binary(l, CPPGM_BINARY_DIV, t, x, y);
        return;
    }
    if (!(b & IR_CONST)) {
        check(l, compare(l, CPPGM_CMP_EQ, t, y, integer(l, 0)), TRAP_DIVIDE);
    }
    if (!s.sign) {
        u8 op = kind == IR_DIV ? CPPGM_BINARY_U_DIV : CPPGM_BINARY_U_MOD;
        l->values[i] = binary(l, op, t, x, y);
        return;
    }

    u64 smin = ~0ull << (s.bits - 1);
    if (kind == IR_DIV && (b & IR_CONST)) {
        if (is_minus_one(l, b, s)) {
            check(l, compare(l, CPPGM_CMP_EQ, t, x, integer(l, smin)), TRAP_OVERFLOW);
        }
        l->values[i] = binary(l, CPPGM_BINARY_DIV, t, x, y);
    } else if (kind == IR_DIV) {
        LowirValue low = compare(l, CPPGM_CMP_EQ, t, x, integer(l, smin));
        LowirValue minus = compare(l, CPPGM_CMP_EQ, t, y, integer(l, ~0ull));
        check(l, binary(l, CPPGM_BINARY_AND, CPPGM_TYPE_I64, low, minus), TRAP_OVERFLOW);
        l->values[i] = binary(l, CPPGM_BINARY_DIV, t, x, y);
    } else if (b & IR_CONST) {
        l->values[i] = is_minus_one(l, b, s) ? integer(l, 0)
                                             : binary(l, CPPGM_BINARY_MOD, t, x, y);
    } else {
        LowirValue minus = compare(l, CPPGM_CMP_EQ, t, y, integer(l, ~0ull));
        LowirValue safe = choose(l, t, minus, integer(l, 1), y);
        l->values[i] = binary(l, CPPGM_BINARY_MOD, t, x, safe);
    }
}

static void
shift(Lower *l, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    const Checker *c = l->c;
    Scalar s = scalar_of(c, type);
    CppgmType t = scalar_type(c, type);
    LowirValue x = operand(l, a);
    LowirValue y = operand(l, b);
    if (!(b & IR_CONST)) {
        u32 count_type = value_type(l, b);
        CppgmType ct = scalar_type(c, count_type);
        check(l, compare(l, CPPGM_CMP_UGE, ct, y, integer(l, s.bits)), TRAP_SHIFT);
        y = change_int(l, y, ct, scalar_of(c, count_type), t, s);
    }

    u8 op = kind == IR_SHL ? CPPGM_BINARY_SHL
          : s.sign         ? CPPGM_BINARY_SHR
                           : CPPGM_BINARY_U_SHR;
    l->values[i] = binary(l, op, t, x, y);
}

static void
count_bits(Lower *l, u32 i, u8 kind, u32 type, u32 a)
{
    const Checker *c = l->c;
    u32 from = value_type(l, a);
    u32 bits = scalar_of(c, from).bits;
    CppgmType ft = scalar_type(c, from);
    CppgmType w = bits == 64 ? CPPGM_TYPE_I64 : CPPGM_TYPE_U32;
    LowirValue x = l->values[a];
    if (bits < 32) {
        x = convert(l, CPPGM_CONVERT_Z_EXT, w, ft, x);
    } else if (ft != w) {
        x = put(l, CPPGM_OP_COPY, 0, w, x, nothing(), nothing());
    }

    LowirValue r;
    if (kind == IR_COUNT_ONES) {
        r = unary(l, CPPGM_UNARY_COUNT_ONES, w, x);
    } else {
        u8 op = kind == IR_TRAILING_ZEROS ? CPPGM_UNARY_COUNT_TRAILING_ZEROS
                                          : CPPGM_UNARY_COUNT_LEADING_ZEROS;
        LowirValue zero = compare(l, CPPGM_CMP_EQ, w, x, integer(l, 0));
        LowirValue n = unary(l, op, w, x);
        if (kind == IR_LEADING_ZEROS && bits < 32) {
            n = binary(l, CPPGM_BINARY_SUB, w, n, integer(l, 32 - bits));
        }
        r = choose(l, w, zero, integer(l, bits), n);
    }

    Scalar ws = { false, false, bits == 64 ? 64 : 32 };
    l->values[i] = change_int(l, r, w, ws, scalar_type(c, type), scalar_of(c, type));
}

static void
compare_op(Lower *l, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    static const u8 preds[3][6] = {
        { CPPGM_CMP_EQ, CPPGM_CMP_NE, CPPGM_CMP_ULT, CPPGM_CMP_ULE, CPPGM_CMP_UGT, CPPGM_CMP_UGE },
        { CPPGM_CMP_EQ, CPPGM_CMP_NE, CPPGM_CMP_LT, CPPGM_CMP_LE, CPPGM_CMP_GT, CPPGM_CMP_GE },
        { CPPGM_CMP_EQ, CPPGM_CMP_NE, CPPGM_CMP_LT, CPPGM_CMP_LE, CPPGM_CMP_GT, CPPGM_CMP_GE },
    };
    Scalar s = scalar_of(l->c, type);
    LowirValue x = operand(l, a);
    LowirValue y = operand(l, b);
    u32 row = s.fl ? 2 : s.sign ? 1 : 0;
    l->values[i] = compare(l, preds[row][kind - IR_EQ], scalar_type(l->c, type), x, y);
    l->truth[i] = 1;
}

static void
put_ranges(Lower *l, CppgmType type, LowirValue selector, u32 n, u32 otherwise)
{
    for (u32 k = 0; k < n; k++) {
        Range r = l->ranges[k];
        if (r.hi - r.lo < CASE_SPAN) {
            continue;
        }
        LowirValue lo = integer(l, r.lo);
        LowirValue d = binary(l, CPPGM_BINARY_SUB, type, selector, lo);
        LowirValue in = compare(l, CPPGM_CMP_ULE, type, d, integer(l, r.hi - r.lo));
        u32 next = new_block(l);
        branch(l, in, r.block, next, 0);
        start(l, next);
    }

    u32 first = l->n.operands;
    for (u32 k = 0; k < n; k++) {
        Range r = l->ranges[k];
        if (r.hi - r.lo >= CASE_SPAN) {
            continue;
        }
        for (u64 v = r.lo;; v++) {
            push_operand(l, integer(l, v));
            push_operand(l, block(r.block));
            if (v == r.hi) {
                break;
            }
        }
    }
    LowirInstruction *in = append(l, CPPGM_OP_SWITCH, 0, CPPGM_TYPE_VOID);
    in->ops[0] = selector;
    in->ops[1] = block(otherwise);
    in->extra_first = first;
    in->extra_count = l->n.operands - first;
    l->block = NONE;
}

static void
range_check(Lower *l, LowirValue x, CppgmType from_type, Scalar from, Scalar to)
{
    u64 from_max = ~0ull >> (64 - from.bits + from.sign);
    u64 to_max = ~0ull >> (64 - to.bits + to.sign);
    u64 to_min = to.sign ? ~0ull << (to.bits - 1) : 0;
    bool low = from.sign && (!to.sign || to.bits < from.bits);
    bool high = to_max < from_max;

    LowirValue fail = nothing();
    if (low) {
        fail = compare(l, CPPGM_CMP_LT, from_type, x, integer(l, to_min));
    }
    if (high) {
        u8 pred = from.sign ? CPPGM_CMP_GT : CPPGM_CMP_UGT;
        LowirValue over = compare(l, pred, from_type, x, integer(l, to_max));
        fail = low ? binary(l, CPPGM_BINARY_OR, CPPGM_TYPE_I64, fail, over) : over;
    }
    if (fail.kind) {
        check(l, fail, TRAP_RANGE);
    }
}

static bool
run_meets(u64 lo, u64 hi, Scalar from, Scalar to)
{
    if (from.bits == 64) {
        return true;
    }
    if (from.sign) {
        int64_t m = (int64_t)1 << (from.bits - 1);
        return to.sign ? (int64_t)lo < m && (int64_t)hi >= -m : lo < (u64)m;
    }
    u64 top = (1ull << from.bits) - 1;
    return to.sign ? (int64_t)hi >= 0 && (int64_t)lo <= (int64_t)top : lo <= top;
}

static void
members_check(Lower *l, LowirValue v, CppgmType type, const u32 *runs, Scalar from, Scalar to)
{
    u32 ok = new_block(l);
    u32 n = 0;
    for (u32 k = 0; k < runs[0]; k += 4) {
        const u32 *w = runs + 1 + k;
        Range r = { w[0] | (u64)w[1] << 32, w[2] | (u64)w[3] << 32, ok };
        if (run_meets(r.lo, r.hi, from, to)) {
            l->ranges[n++] = r;
        }
    }
    if (n == 0) {
        jump(l, trap_block(l, TRAP_RANGE));
    } else {
        put_ranges(l, type, v, n, trap_block(l, TRAP_RANGE));
    }
    start(l, ok);
}

static LowirValue
float_to_int(Lower *l, LowirValue x, CppgmType from_type, CppgmType to_type, Scalar to)
{
    LowirValue d = x;
    if (from_type == CPPGM_TYPE_F32) {
        d = convert(l, CPPGM_CONVERT_FP_EXT, CPPGM_TYPE_F64, CPPGM_TYPE_F32, x);
    }

    double hi = (double)(1ull << (to.bits - 1)) * (to.sign ? 1.0 : 2.0);
    double lo = to.sign ? -hi - (to.bits == 64 ? 0.0 : 1.0) : -1.0;
    u64 hi_bits;
    u64 lo_bits;
    memcpy(&hi_bits, &hi, sizeof hi);
    memcpy(&lo_bits, &lo, sizeof lo);

    u8 low_pred = to.sign && to.bits == 64 ? CPPGM_CMP_ULT : CPPGM_CMP_ULE;
    LowirValue below = compare(
        l,
        low_pred,
        CPPGM_TYPE_F64,
        d,
        literal(l, CPPGM_VALUE_FLOAT, CPPGM_TYPE_F64, lo_bits, 0)
    );
    LowirValue above = compare(
        l,
        CPPGM_CMP_UGE,
        CPPGM_TYPE_F64,
        d,
        literal(l, CPPGM_VALUE_FLOAT, CPPGM_TYPE_F64, hi_bits, 0)
    );
    check(l, binary(l, CPPGM_BINARY_OR, CPPGM_TYPE_I64, below, above), TRAP_RANGE);

    u8 op = to.sign ? CPPGM_CONVERT_FP_TO_SI : CPPGM_CONVERT_FP_TO_UI;
    return convert(l, op, to_type, CPPGM_TYPE_F64, d);
}

static void
convert_op(Lower *l, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    const Checker *c = l->c;
    assert(!(a & IR_CONST));
    u32 from_type = value_type(l, a);
    Scalar from = scalar_of(c, from_type);
    Scalar to = scalar_of(c, type);
    CppgmType ft = scalar_type(c, from_type);
    CppgmType tt = scalar_type(c, type);
    bool from_bool = c->types.kinds[from_type] == TYPE_BOOL;
    LowirValue x = from_bool ? condition(l, a) : operand(l, a);

    if (from.fl && !to.fl) {
        l->values[i] = float_to_int(l, x, ft, tt, to);
        return;
    }
    if (from.fl) {
        u8 op = to.bits > from.bits ? CPPGM_CONVERT_FP_EXT : CPPGM_CONVERT_FP_TRUNC;
        l->values[i] = from.bits == to.bits ? x : convert(l, op, tt, ft, x);
        return;
    }
    if (from_bool) {
        x = choose(l, CPPGM_TYPE_U32, x, integer(l, 1), integer(l, 0));
        ft = CPPGM_TYPE_U32;
        from = (Scalar){ false, false, 32 };
    }
    if (to.fl) {
        u8 op = from.sign ? CPPGM_CONVERT_SI_TO_FP : CPPGM_CONVERT_UI_TO_FP;
        l->values[i] = convert(l, op, tt, ft, x);
        return;
    }

    if (kind == IR_NARROW) {
        range_check(l, x, ft, from, to);
    }
    if (c->types.kinds[type] == TYPE_BOOL) {
        l->values[i] = compare(l, CPPGM_CMP_NE, ft, x, integer(l, 0));
        l->truth[i] = 1;
    } else {
        l->values[i] = change_int(l, x, ft, from, tt, to);
    }
    if (kind == IR_NARROW && c->types.kinds[type] == TYPE_ENUM) {
        members_check(l, l->values[i], tt, extra(l, b), from, to);
    }
}

static LowirValue
variadic(Lower *l, u32 x)
{
    if (!(x & IR_CONST) && l->truth[x]) {
        return l->values[x];
    }
    u32 t = operand_type(l, x);
    if (is_address(l->c, t) || scalar_of(l->c, t).fl) {
        return operand(l, x);
    }
    return widen(l, x);
}

static void
call_op(Lower *l, u32 i, u8 kind, u32 type, u32 decl, const u32 *list)
{
    const Checker *c = l->c;
    u32 fn = c->decls.types[decl];
    const u32 *params = c->lists + c->types.arg0[fn];
    u32 n = list[0];
    u32 nargs = 0;
    if (kind == IR_CALL) {
        l->args[nargs++] = temp(0);
    }

    u32 o = 1;
    for (u32 p = 0; p < params[0]; p++, o++) {
        u32 t = params[1 + p];
        u8 k = c->types.kinds[t];
        if (k == TYPE_STRUCT) {
            LowirValue address = operand(l, list[o]);
            l->args[nargs++] = load(l, record_of(l, t), address);
        } else if (k == TYPE_SLICE) {
            l->args[nargs++] = operand(l, list[o]);
            l->args[nargs++] = operand(l, list[++o]);
        } else {
            l->args[nargs++] = operand(l, list[o]);
        }
    }
    for (; o <= n; o++) {
        l->args[nargs++] = variadic(l, list[o]);
    }

    u32 first = push_args(l, nargs);
    CppgmType result = boundary_type(l, type);
    u32 callee = kind == IR_CALL ? l->symbols[decl] : l->c_symbols[decl];
    LowirValue r = call(l, symbol(callee), result, first, nargs);

    u8 rk = c->types.kinds[type];
    if (rk == TYPE_STRUCT) {
        LowirValue to = slot_address(l, list[n + 1]);
        copy_object(l, r, to, check_type_size(c, type), check_type_align(c, type));
    } else if (rk == TYPE_SLICE) {
        LowirValue p = spill_address(l);
        copy_object(l, r, p, 16, 8);
        l->values[i] = load(l, CPPGM_TYPE_PTR, p);
        l->values[i + 1] = load(l, CPPGM_TYPE_I64, offset(l, p, 8));
    } else if (type != TYPE_ID_VOID) {
        l->values[i] = r;
    }
}

static void
return_op(Lower *l, u32 type, u32 a, u32 b)
{
    const Checker *c = l->c;
    u8 kind = c->types.kinds[type];
    if (type == TYPE_ID_VOID) {
        effect(l, CPPGM_OP_RETURN, 0, CPPGM_TYPE_VOID, nothing(), nothing(), nothing());
    } else if (kind == TYPE_STRUCT) {
        LowirValue address = operand(l, a);
        LowirValue v = load(l, record_of(l, type), address);
        effect(l, CPPGM_OP_RETURN, 0, record_of(l, type), v, nothing(), nothing());
    } else if (kind == TYPE_SLICE) {
        LowirValue x = operand(l, a);
        LowirValue y = operand(l, b);
        LowirValue p = spill_address(l);
        store(l, CPPGM_TYPE_PTR, x, p);
        store(l, CPPGM_TYPE_I64, y, offset(l, p, 8));
        LowirValue v = load(l, l->slice_record, p);
        effect(l, CPPGM_OP_RETURN, 0, l->slice_record, v, nothing(), nothing());
    } else {
        LowirValue v = operand(l, a);
        effect(l, CPPGM_OP_RETURN, 0, scalar_type(c, type), v, nothing(), nothing());
    }
    l->block = NONE;
}

static u32
end_block(Lower *l, u32 e)
{
    if (l->blocks[e] == NONE) {
        l->blocks[e] = new_block(l);
    }
    return l->blocks[e];
}

static void
store_end(Lower *l, u32 e, u32 x)
{
    CppgmType t = scalar_type(l->c, l->code->types[l->fn->first + e]);
    if (l->end_slots[e] == NONE) {
        l->end_slots[e] = new_slot(l, t);
    }
    LowirValue v = operand(l, x);
    store(l, t, v, slot(l->end_slots[e]));
}

static void
switch_op(Lower *l, u32 i, u32 type, u32 a, const u32 *x)
{
    u32 narms = 0;
    for (u32 m = i; m != NONE; m = l->links[m]) {
        l->arms[narms++] = m;
        l->blocks[m] = new_block(l);
    }

    for (u32 k = 0; k < x[0]; k++) {
        const u32 *w = x + 2 + 5 * k;
        assert(w[4] < narms);
        u64 lo = w[0] | (u64)w[1] << 32;
        u64 hi = w[2] | (u64)w[3] << 32;
        l->ranges[k] = (Range){ lo, hi, l->blocks[l->arms[w[4]]] };
    }
    u32 otherwise = x[1] == NONE ? trap_block(l, TRAP_UNREACHABLE)
                                 : l->blocks[l->arms[x[1]]];
    if (type == TYPE_ID_BOOL) {
        LowirValue selector = a & IR_CONST || l->truth[a] ? condition(l, a)
                                                          : convert(
                                                                l,
                                                                CPPGM_CONVERT_Z_EXT,
                                                                CPPGM_TYPE_I64,
                                                                CPPGM_TYPE_I1,
                                                                l->values[a]
                                                            );
        put_ranges(l, CPPGM_TYPE_I64, selector, x[0], otherwise);
    } else {
        put_ranges(l, scalar_type(l->c, type), operand(l, a), x[0], otherwise);
    }
    start(l, l->blocks[i]);
}

static void
lower_instruction(Lower *l, u32 i)
{
    const Checker *c = l->c;
    u32 g = l->fn->first + i;
    u8 kind = l->code->kinds[g];
    u32 type = l->code->types[g];
    u32 a = l->code->as[g];
    u32 b = l->code->bs[g];
    switch (kind) {
        case IR_LOAD: l->values[i] = load_slot(l, a); break;
        case IR_LOAD_AT: load_at(l, i, type, a); break;
        case IR_ADDR: l->values[i] = slot_address(l, a); break;
        case IR_DATA:
            l->values[i] = offset(l, address_of(l, symbol(l->data)), a);
            break;
        case IR_GLOBAL:
            l->values[i] = offset(l, address_of(l, symbol(l->globals)), a);
            break;
        case IR_INDEX: index_op(l, i, type, a, b); break;
        case IR_INDEX_LEN: index_len(l, i, type, a, extra(l, b)); break;
        case IR_FIELD: {
            LowirValue base = operand(l, a);
            l->values[i] = offset(l, base, check_field_offset(c, b));
            break;
        }
        case IR_SLICE:
        case IR_SLICE_FROM: slice(l, i, kind, type, a, extra(l, b)); break;
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_NEG: arithmetic(l, i, kind, type, a, b); break;
        case IR_DIV:
        case IR_REM: divide(l, i, kind, type, a, b); break;
        case IR_ADD_WRAP:
        case IR_SUB_WRAP:
        case IR_MUL_WRAP:
        case IR_AND:
        case IR_OR:
        case IR_XOR: {
            LowirValue x = operand(l, a);
            LowirValue y = operand(l, b);
            l->values[i] = binary(l, binary_ops[kind], scalar_type(c, type), x, y);
            break;
        }
        case IR_SHL:
        case IR_SHR: shift(l, i, kind, type, a, b); break;
        case IR_NEG_WRAP: {
            LowirValue x = operand(l, a);
            l->values[i] = unary(l, CPPGM_UNARY_NEG, scalar_type(c, type), x);
            break;
        }
        case IR_NOT: {
            LowirValue x = condition(l, a);
            CppgmType t = !(a & IR_CONST) && l->truth[a] ? CPPGM_TYPE_I64 : CPPGM_TYPE_I1;
            l->values[i] = compare(l, CPPGM_CMP_EQ, t, x, integer(l, 0));
            l->truth[i] = 1;
            break;
        }
        case IR_BIT_NOT: {
            LowirValue x = operand(l, a);
            l->values[i] = unary(l, CPPGM_UNARY_BIT_NOT, scalar_type(c, type), x);
            break;
        }
        case IR_TRAILING_ZEROS:
        case IR_LEADING_ZEROS:
        case IR_COUNT_ONES: count_bits(l, i, kind, type, a); break;
        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE: compare_op(l, i, kind, type, a, b); break;
        case IR_ADD_OVERFLOWS:
        case IR_SUB_OVERFLOWS:
        case IR_MUL_OVERFLOWS: overflows(l, i, kind, type, a, b); break;
        case IR_CONVERT:
        case IR_NARROW:
        case IR_TRUNCATE: convert_op(l, i, kind, type, a, b); break;
        case IR_CALLBACK:
            l->values[i] = address_of(l, symbol(l->c_symbols[b]));
            break;
        case IR_CALL:
        case IR_CALL_C: call_op(l, i, kind, type, a, extra(l, b)); break;
        case IR_STORE: store_slot(l, a, operand(l, b)); break;
        case IR_STORE_AT: {
            LowirValue p = operand(l, a);
            LowirValue v = operand(l, b);
            store(l, scalar_type(c, type), v, p);
            break;
        }
        case IR_STORE_PAIR: {
            const u32 *x = extra(l, b);
            LowirValue p = operand(l, a);
            LowirValue ptr = operand(l, x[0]);
            LowirValue len = operand(l, x[1]);
            store(l, CPPGM_TYPE_PTR, ptr, p);
            store(l, CPPGM_TYPE_I64, len, offset(l, p, 8));
            break;
        }
        case IR_COPY: {
            LowirValue to = operand(l, a);
            LowirValue from = operand(l, b);
            copy_object(l, from, to, check_type_size(c, type), check_type_align(c, type));
            break;
        }
        case IR_ZERO: {
            LowirValue to = operand(l, a);
            zero_object(l, to, check_type_size(c, type), check_type_align(c, type));
            break;
        }
        case IR_RET: return_op(l, type, a, b); break;
        case IR_IF: {
            LowirValue cond = condition(l, a);
            u32 then = new_block(l);
            u32 other = l->links[i] != NONE
                          ? (l->blocks[l->links[i]] = new_block(l))
                          : end_block(l, l->ends[i]);
            if (a & IR_CONST) {
                jump(l, constant_at(l, a).bits ? then : other);
            } else {
                branch(l, cond, then, other, 0);
            }
            start(l, then);
            break;
        }
        case IR_ELSE:
            if (type != TYPE_ID_NONE) {
                store_end(l, l->ends[i], a);
            }
            jump(l, end_block(l, l->ends[i]));
            start(l, l->blocks[i]);
            break;
        case IR_END:
            if (l->code->kinds[l->fn->first + l->opens[i]] == IR_IF &&
                type != TYPE_ID_NONE) {
                store_end(l, i, a);
            }
            jump(l, end_block(l, i));
            start(l, l->blocks[i]);
            if (type != TYPE_ID_NONE) {
                l->values[i] = load(l, scalar_type(c, type), slot(l->end_slots[i]));
            }
            break;
        case IR_EXIT:
            if (type != TYPE_ID_NONE) {
                store_end(l, l->ends[a], b);
            }
            jump(l, end_block(l, l->ends[a]));
            break;
        case IR_REPEAT: jump(l, l->blocks[a]); break;
        case IR_LOOP:
            l->blocks[i] = new_block(l);
            jump(l, l->blocks[i]);
            start(l, l->blocks[i]);
            break;
        case IR_SWITCH: switch_op(l, i, type, a, extra(l, b)); break;
        case IR_CASE:
            jump(l, end_block(l, l->ends[i]));
            start(l, l->blocks[i]);
            break;
        case IR_SECOND:
        case IR_DROP:
        case IR_BLOCK:
        case IR_NOP: break;
        default: assert(false && "unsupported IR instruction");
    }
}

static void
find_ends(Lower *l)
{
    const u8 *kinds = l->code->kinds + l->fn->first;
    u32 depth = 0;
    for (u32 i = 0; i < l->fn->count; i++) {
        l->ends[i] = NONE;
        l->links[i] = NONE;
        l->blocks[i] = NONE;
        l->end_slots[i] = NONE;
        l->truth[i] = 0;
        u8 kind = kinds[i];
        if (kind == IR_BLOCK || kind == IR_LOOP || kind == IR_IF || kind == IR_SWITCH) {
            l->stack[depth] = i;
            l->last[depth++] = i;
        } else if (kind == IR_ELSE || kind == IR_CASE) {
            l->links[l->last[depth - 1]] = i;
            l->last[depth - 1] = i;
        } else if (kind == IR_END) {
            u32 open = l->stack[--depth];
            l->opens[i] = open;
            for (u32 m = open; m != NONE; m = l->links[m]) {
                l->ends[m] = i;
            }
        }
    }
    assert(depth == 0);
}

static void
begin_body(Lower *l, u32 symbol_id, LowirBodyCounts min, u32 temps)
{
    l->symbol = symbol_id;
    must(l, cppgm_lowir_body_reserve(l->m, l->symbol, &min, &l->b));
    l->n = (LowirBodyCounts){ 0 };
    l->placed = 0;
    l->temps = temps;
    l->block = NONE;
    l->spill = NONE;
    for (u32 k = 0; k < NTRAPS; k++) {
        l->traps[k] = NONE;
    }
    start(l, new_block(l));
}

static void
place_slots(Lower *l)
{
    const Checker *c = l->c;
    const IrFunc *fn = l->fn;
    const IrSlot *slots = l->code->slots + fn->first_slot;
    for (u32 k = 0; k < fn->nslots; k++) {
        l->addressed[k] = 0;
    }
    for (u32 i = 0; i < fn->count; i++) {
        if (l->code->kinds[fn->first + i] == IR_ADDR) {
            l->addressed[l->code->as[fn->first + i]] = 1;
        }
    }

    for (u32 k = 0; k < fn->nslots; k++) {
        u32 t = slots[k].type;
        u8 kind = c->types.kinds[t];
        l->slots[k] = NONE;
        if (k < fn->nparams && kind == TYPE_STRUCT) {
            l->slots[k] = new_slot(l, record_of(l, t));
            LowirValue p = slot_address(l, k);
            copy_object(l, temp(1 + k), p, check_type_size(c, t), check_type_align(c, t));
        } else if (k < fn->nparams) {
            continue;
        } else if (is_length(l, k)) {
            l->slots[k] = l->addressed[k - 1] ? NONE : new_slot(l, CPPGM_TYPE_I64);
        } else if (kind == TYPE_SLICE) {
            l->slots[k] = new_slot(l, l->addressed[k] ? l->slice_record : CPPGM_TYPE_PTR);
        } else if (kind == TYPE_STRUCT || kind == TYPE_ARRAY) {
            l->slots[k] = new_slot(l, record_of(l, t));
        } else {
            l->slots[k] = new_slot(l, scalar_type(c, t));
        }
    }
}

static void
lower_function(Lower *l, const IrFunc *fn)
{
    l->fn = fn;
    LowirBodyCounts min = {
        4 * fn->count + 64, 2 * fn->nextra + 64,
        2 * fn->count + 64, fn->nslots + fn->count / 4 + 16,
        fn->count / 2 + 16,
    };
    begin_body(l, l->symbols[fn->decl], min, 1 + fn->nparams);
    find_ends(l);
    place_slots(l);

    for (u32 i = 0; i < fn->count; i++) {
        lower_instruction(l, i);
    }
    if (l->block != NONE) {
        jump(l, trap_block(l, TRAP_UNREACHABLE));
    }
    put_traps(l);
    commit(l);
}

static void
lower_adapter(Lower *l, u32 decl)
{
    const Checker *c = l->c;
    u32 type = c->decls.types[decl];
    const u32 *params = c->lists + c->types.arg0[type];
    LowirBodyCounts min = { 8, params[0] + 8, 8, 8, 8 };
    begin_body(l, l->c_symbols[decl], min, params[0]);

    l->args[0] = load(l, CPPGM_TYPE_PTR, symbol(l->context));
    for (u32 p = 0; p < params[0]; p++) {
        l->args[1 + p] = temp(p);
    }
    u32 first = push_args(l, params[0] + 1);
    CppgmType result = boundary_type(l, (u32)c->types.arg1[type]);
    LowirValue v = call(l, symbol(l->symbols[decl]), result, first, params[0] + 1);
    effect(l, CPPGM_OP_RETURN, 0, result, v, nothing(), nothing());
    l->block = NONE;
    commit(l);
}

static u32
define_memory(Lower *l, const char *what, const u8 *bytes, u32 n, u32 size, u32 storage)
{
    u32 id = intern(l, sprintf(l->name, "chibi_%s%s", l->prefix, what));
    CppgmDataItem *items = malloc((n / 8 + n % 8 + 1) * sizeof *items);
    u32 k = 0;
    for (u32 o = 0; o + 8 <= n; o += 8) {
        u64 w;
        memcpy(&w, bytes + o, 8);
        items[k++] = (CppgmDataItem){ .type = CPPGM_TYPE_I64, .bits = { w, 0 } };
    }
    for (u32 o = n & ~7u; o < n; o++) {
        items[k++] = (CppgmDataItem){ .type = CPPGM_TYPE_I8, .bits = { bytes[o], 0 } };
    }
    if (size > n) {
        items[k++] = (CppgmDataItem){ .amount = size - n, .kind = CPPGM_DATA_ZERO };
    }

    CppgmType type = CPPGM_TYPE_OBJ | 4ull << 8 | (u64)size << 16;
    must(l, cppgm_lowir_global(l->m, id, type, CPPGM_BINDING_INTERNAL, storage, items, k, CPPGM_GLOBAL_DEFINITION));
    free(items);
    return id;
}

static void
declare_runtime(Lower *l)
{
    CppgmParameter kind = { .type = CPPGM_TYPE_I32 };
    CppgmBoundary noreturn = { .unwind = CPPGM_UNWIND_NO,
                               .returns = CPPGM_RETURN_NORETURN };
    u32 signature = cppgm_lowir_boundary(l->m, CPPGM_TYPE_VOID, &kind, 1, &noreturn);
    must(l, signature != CPPGM_NO_ID);

    l->trap = intern(l, sprintf(l->name, "chibi_trap"));
    must(l, cppgm_lowir_function(l->m, l->trap, signature, CPPGM_BINDING_STRONG, CPPGM_FUNCTION_DECLARATION));
    must(l, cppgm_lowir_symbol_fact(l->m, l->trap, CPPGM_KEY_COLD, 1));

    l->context = intern(l, sprintf(l->name, "chibi_context"));
    must(
        l,
        cppgm_lowir_global(l->m, l->context, CPPGM_TYPE_PTR, CPPGM_BINDING_STRONG, CPPGM_STORAGE_DEFAULT, NULL, 0, CPPGM_GLOBAL_DECLARATION)
    );
}

static void
declare_functions(Lower *l)
{
    const Checker *c = l->c;
    const Ir *ir = &c->ir;
    for (u32 d = 1; d < c->decls.count; d++) {
        if (c->decls.kinds[d] == DECL_FN && c->decls.bodies[d] == NO_BODY) {
            Name n = decl_name(l, d);
            l->c_symbols[d] = intern(l, sprintf(l->name, "%.*s", (int)n.len, n.text));
            u32 signature = c_signature(l, d);
            must(l, cppgm_lowir_function(l->m, l->c_symbols[d], signature, CPPGM_BINDING_STRONG, CPPGM_FUNCTION_DECLARATION));
        }
    }

    u32 binding = l->flags & LOWIR_SHARED ? CPPGM_BINDING_STRONG : CPPGM_BINDING_INTERNAL;
    for (u32 f = 0; f < ir->nfuncs; f++) {
        const IrFunc *fn = &ir->funcs[f];
        Name n = decl_name(l, fn->decl);
        l->symbols[fn->decl] = intern(
            l,
            sprintf(l->name, "%s%.*s", l->prefix, (int)n.len, n.text)
        );
        u32 signature = chibi_signature(l, fn);
        must(l, cppgm_lowir_function(l->m, l->symbols[fn->decl], signature, binding, CPPGM_FUNCTION_DEFINITION));
    }

    for (u32 k = 0; k < ir->ncallbacks; k++) {
        u32 d = ir->callbacks[k];
        if (c->decls.bodies[d] == NO_BODY) {
            continue;
        }
        Name n = decl_name(l, d);
        bool exported = k < ir->nexports;
        int len = exported
                    ? sprintf(l->name, "%.*s", (int)n.len, n.text)
                    : sprintf(l->name, "chibi_%sadapter_%.*s", l->prefix, (int)n.len, n.text);
        l->c_symbols[d] = intern(l, len);
        u32 signature = c_signature(l, d);
        u32 adapter_binding = exported ? CPPGM_BINDING_STRONG : CPPGM_BINDING_INTERNAL;
        must(l, cppgm_lowir_function(l->m, l->c_symbols[d], signature, adapter_binding, CPPGM_FUNCTION_DEFINITION));
    }
}

static u32
max_u32(u32 a, u32 b)
{
    return a > b ? a : b;
}

const char *
lowir_unsupported(const Checker *c)
{
    const IrCode *code = &c->ir.code;
    for (u32 g = 0; g < code->count; g++) {
        u8 kind = code->kinds[g];
        if (kind == IR_SPLAT || kind == IR_LOOKUP || kind == IR_TOP_BITS ||
            c->types.kinds[code->types[g]] == TYPE_VECTOR) {
            return "the LowIR translator doesn't support vectors yet";
        }
    }
    for (u32 s = 0; s < code->nslots; s++) {
        if (c->types.kinds[code->slots[s].type] == TYPE_VECTOR) {
            return "the LowIR translator doesn't support vectors yet";
        }
    }
    return NULL;
}

void
lowir_package(CppgmLowir *m, const Checker *c, const char *prefix, u32 flags)
{
    const Ir *ir = &c->ir;
    const IrCode *code = &ir->code;
    u32 count = 1;
    u32 nslots = 1;
    u32 nargs = 8;
    u32 nranges = 1;
    for (u32 f = 0; f < ir->nfuncs; f++) {
        const IrFunc *fn = &ir->funcs[f];
        count = max_u32(count, fn->count + 1);
        nslots = max_u32(nslots, fn->nslots + 1);
        nargs = max_u32(nargs, fn->nparams + 2);
        for (u32 i = 0; i < fn->count; i++) {
            u32 g = fn->first + i;
            const u32 *x = code->extra + fn->first_extra + code->bs[g];
            u8 kind = code->kinds[g];
            if (kind == IR_CALL || kind == IR_CALL_C) {
                nargs = max_u32(nargs, x[0] + 2);
            } else if (kind == IR_SWITCH) {
                nranges = max_u32(nranges, x[0] + 1);
            } else if (kind == IR_NARROW && c->types.kinds[code->types[g]] == TYPE_ENUM) {
                nranges = max_u32(nranges, x[0] / 4 + 1);
            }
        }
    }
    u32 nfields = 1;
    u32 name_len = 32;
    for (u32 d = 1; d < c->decls.count; d++) {
        name_len = max_u32(name_len, names_get(c->names, c->decls.names[d]).len + 32);
        if (c->decls.kinds[d] == DECL_STRUCT) {
            nfields = max_u32(nfields, c->decls.member_counts[d] + 1);
        }
        if (c->decls.kinds[d] == DECL_FN) {
            nargs = max_u32(nargs, c->lists[c->types.arg0[c->decls.types[d]]] + 2);
        }
    }

    Lower l = { .m = m, .c = c, .code = code, .prefix = prefix, .flags = flags };
    l.symbols = malloc(c->decls.count * sizeof *l.symbols);
    l.c_symbols = malloc(c->decls.count * sizeof *l.c_symbols);
    memset(l.symbols, 0xFF, c->decls.count * sizeof *l.symbols);
    memset(l.c_symbols, 0xFF, c->decls.count * sizeof *l.c_symbols);
    l.records = calloc(c->types.count, sizeof *l.records);
    l.params = malloc(nargs * sizeof *l.params);
    l.args = malloc(nargs * sizeof *l.args);
    l.fields = malloc(nfields * sizeof *l.fields);
    l.ranges = malloc(nranges * sizeof *l.ranges);
    l.name = malloc(name_len + strlen(prefix));
    l.values = malloc(count * sizeof *l.values);
    l.truth = malloc(count);
    u32 *words = malloc(9 * (size_t)count * sizeof *words);
    l.blocks = words;
    l.end_slots = words + count;
    l.ends = words + 2 * count;
    l.opens = words + 3 * count;
    l.links = words + 4 * count;
    l.stack = words + 5 * count;
    l.last = words + 6 * count;
    l.arms = words + 7 * count;
    l.slots = malloc(nslots * sizeof *l.slots);
    l.addressed = malloc(nslots);

    CppgmField halves[2] = { { 0, CPPGM_TYPE_PTR, 1 }, { 8, CPPGM_TYPE_I64, 1 } };
    l.slice_record = cppgm_lowir_record(m, 16, 8, halves, 2);
    must(&l, l.slice_record != 0);
    declare_runtime(&l);
    l.data = ir->ndata
               ? define_memory(&l, "data", ir->data, ir->ndata, ir->ndata, CPPGM_STORAGE_READONLY)
               : NONE;
    l.globals = ir->globals_size
                  ? define_memory(&l, "globals", ir->globals, ir->nglobals, ir->globals_size, CPPGM_STORAGE_DEFAULT)
                  : NONE;
    declare_functions(&l);

    for (u32 f = 0; f < ir->nfuncs; f++) {
        lower_function(&l, &ir->funcs[f]);
    }
    for (u32 k = 0; k < ir->ncallbacks; k++) {
        if (c->decls.bodies[ir->callbacks[k]] != NO_BODY) {
            lower_adapter(&l, ir->callbacks[k]);
        }
    }

    free(l.addressed);
    free(l.slots);
    free(words);
    free(l.truth);
    free(l.values);
    free(l.name);
    free(l.ranges);
    free(l.fields);
    free(l.args);
    free(l.params);
    free(l.records);
    free(l.c_symbols);
    free(l.symbols);
}
