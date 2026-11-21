#include "ir_reference.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    NONE = 0xFFFFFFFFu,
    STEP_LIMIT = 50000000,
    TRAP_HANG = 99,
};

typedef struct
{
    bool fl;
    bool sign;
    u32 bits;
} Ty;

static Ty
ty(const Checker *c, u32 type)
{
    u8 kind = c->types.kinds[type];
    if (kind == TYPE_ENUM) {
        return ty(c, (u32)c->types.arg1[type]);
    }
    if (kind == TYPE_BOOL) {
        return (Ty){ false, false, 8 };
    }
    if (kind == TYPE_POINTER || kind == TYPE_MANY || kind == TYPE_SLICE) {
        return (Ty){ false, false, 64 };
    }
    return (Ty){ kind == TYPE_FLOAT,
                 kind == TYPE_INT && c->types.arg1[type],
                 c->types.arg0[type] };
}

static u64
canon(u64 v, Ty t)
{
    if (t.fl || t.bits == 64) {
        return v;
    }
    u64 mask = (1ull << t.bits) - 1;
    v &= mask;
    if (t.sign && (v >> (t.bits - 1)) & 1) {
        v |= ~mask;
    }
    return v;
}

static __int128
wide(u64 v, Ty t)
{
    return t.sign ? (__int128)(int64_t)v : (__int128)v;
}

static bool
fits(__int128 v, Ty t)
{
    if (t.sign) {
        __int128 hi = ((__int128)1 << (t.bits - 1)) - 1;
        return v >= -hi - 1 && v <= hi;
    }
    return v >= 0 && v <= (((__int128)1 << t.bits) - 1);
}

static double
as_double(u64 v, Ty t)
{
    if (t.bits == 32) {
        float f;
        u32 b = (u32)v;
        memcpy(&f, &b, sizeof f);
        return f;
    }
    double d;
    memcpy(&d, &v, sizeof d);
    return d;
}

static u64
from_double(double d, Ty t)
{
    if (t.bits == 32) {
        float f = (float)d;
        u32 b;
        memcpy(&b, &f, sizeof b);
        return b;
    }
    u64 b;
    memcpy(&b, &d, sizeof b);
    return b;
}

static u64
float_op(u8 kind, u64 a, u64 b, Ty t)
{
    if (t.bits == 32) {
        float x = (float)as_double(a, t), y = (float)as_double(b, t), r = 0;
        switch (kind) {
            case IR_ADD: r = x + y; break;
            case IR_SUB: r = x - y; break;
            case IR_MUL: r = x * y; break;
            case IR_DIV: r = x / y; break;
            default: assert(false && "not a float operation");
        }
        return from_double(r, t);
    }
    double x = as_double(a, t), y = as_double(b, t), r = 0;
    switch (kind) {
        case IR_ADD: r = x + y; break;
        case IR_SUB: r = x - y; break;
        case IR_MUL: r = x * y; break;
        case IR_DIV: r = x / y; break;
        default: assert(false && "not a float operation");
    }
    return from_double(r, t);
}

static bool
compare(u8 kind, u64 a, u64 b, Ty t)
{
    if (t.fl) {
        double x = as_double(a, t), y = as_double(b, t);
        switch (kind) {
            case IR_EQ: return x == y;
            case IR_NE: return x != y;
            case IR_LT: return x < y;
            case IR_LE: return x <= y;
            case IR_GT: return x > y;
            default: return x >= y;
        }
    }
    __int128 x = wide(a, t), y = wide(b, t);
    switch (kind) {
        case IR_EQ: return x == y;
        case IR_NE: return x != y;
        case IR_LT: return x < y;
        case IR_LE: return x <= y;
        case IR_GT: return x > y;
        default: return x >= y;
    }
}

static bool
within(u64 x, const u32 *range, Ty t)
{
    u64 lo = range[0] | (u64)range[1] << 32;
    u64 hi = range[2] | (u64)range[3] << 32;
    return wide(x, t) >= wide(lo, t) && wide(x, t) <= wide(hi, t);
}

static bool
is_member(const u32 *runs, Ty t, u64 x)
{
    for (u32 k = 0; k < runs[0]; k += 4) {
        if (within(x, runs + 1 + k, t)) {
            return true;
        }
    }
    return false;
}

static u32
value_type(const IrCode *ir, u32 g)
{
    u8 kind = ir->kinds[g];
    return kind >= IR_EQ && kind <= IR_GE ? TYPE_ID_BOOL : ir->types[g];
}

void
ir_reference_init(IrReference *r, const Checker *c)
{
    const Ir *ir = &c->ir;
    *r = (IrReference){ .c = c };
    r->ends = malloc((ir->code.count + 1) * sizeof *r->ends);
    r->elses = malloc((ir->code.count + 1) * sizeof *r->elses);
    r->opens = malloc((ir->code.count + 1) * sizeof *r->opens);
    u32 *stack = malloc((ir->code.count + 1) * sizeof *stack);
    u32 *last = malloc((ir->code.count + 1) * sizeof *last);
    for (u32 f = 0; f < ir->nfuncs; f++) {
        const IrFunc *fn = &ir->funcs[f];
        u32 depth = 0;
        for (u32 g = fn->first; g < fn->first + fn->count; g++) {
            r->ends[g] = NONE;
            r->elses[g] = NONE;
            u8 kind = ir->code.kinds[g];
            if (kind == IR_BLOCK || kind == IR_LOOP || kind == IR_IF ||
                kind == IR_SWITCH) {
                last[depth] = g;
                stack[depth++] = g;
            } else if (kind == IR_ELSE || kind == IR_CASE) {
                r->elses[last[depth - 1]] = g;
                last[depth - 1] = g;
            } else if (kind == IR_END) {
                u32 open = stack[--depth];
                r->ends[open] = g;
                r->opens[g] = open;
                for (u32 m = r->elses[open]; m != NONE; m = r->elses[m]) {
                    r->ends[m] = g;
                }
            }
        }
        assert(depth == 0);
    }
    free(stack);
    free(last);

    r->offsets = malloc((ir->code.nslots + 1) * sizeof *r->offsets);
    r->frames = malloc((ir->nfuncs + 1) * sizeof *r->frames);
    for (u32 f = 0; f < ir->nfuncs; f++) {
        const IrFunc *fn = &ir->funcs[f];
        u64 size = 0;
        for (u32 s = 0; s < fn->nslots; s++) {
            u32 type = ir->code.slots[fn->first_slot + s].type;
            u8 kind = c->types.kinds[type];
            u64 bytes = kind == TYPE_ARRAY || kind == TYPE_STRUCT
                          ? check_type_size(c, type)
                          : 8;
            assert(bytes < (1u << 24) && size < (1u << 24));
            r->offsets[fn->first_slot + s] = (u32)size;
            size += (bytes + 7) & ~(u64)7;
        }
        r->frames[f] = (u32)size;
    }

    r->globals = calloc(ir->globals_size + 1, 1);
    if (ir->nglobals) {
        memcpy(r->globals, ir->globals, ir->nglobals);
    }
}

void
ir_reference_free(IrReference *r)
{
    free(r->ends);
    free(r->elses);
    free(r->opens);
    free(r->offsets);
    free(r->frames);
    free(r->globals);
}

static int
run(IrReference *r, u32 f, const u64 *args, u64 *result)
{
    const Checker *c = r->c;
    const IrCode *ir = &c->ir.code;
    const IrFunc *fn = &c->ir.funcs[f];
    u64 *vals = calloc(fn->count + 1, sizeof *vals);
    u8 *frame = calloc(r->frames[f] + 8, 1);
    const u32 *offsets = r->offsets + fn->first_slot;
    for (u32 p = 0; p < fn->nparams; p++) {
        u32 type = ir->slots[fn->first_slot + p].type;
        if (c->types.kinds[type] == TYPE_STRUCT) {
            memcpy(frame + offsets[p], (const void *)(uintptr_t)args[p], check_type_size(c, type));
        } else {
            memcpy(frame + offsets[p], &args[p], sizeof *args);
        }
    }

#define OPERAND(x)                                                             \
    (((x) & IR_CONST) ? ir->consts[fn->first_const + ((x) & ~IR_CONST)].bits   \
                      : vals[(x)])
#define OPERAND_TYPE(x)                                                        \
    (((x) & IR_CONST) ? ir->consts[fn->first_const + ((x) & ~IR_CONST)].type   \
                      : value_type(ir, fn->first + (x)))

    int trap = TRAP_UNREACHABLE;
    u64 second = 0;
    for (u32 i = 0; i < fn->count;) {
        if (++r->steps > STEP_LIMIT) {
            trap = TRAP_HANG;
            break;
        }
        u32 g = fn->first + i;
        u8 kind = ir->kinds[g];
        u32 type = ir->types[g];
        u32 a = ir->as[g];
        u32 b = ir->bs[g];
        Ty t = ty(c, type);
        u64 x = 0, y = 0;
        if (kind >= IR_ADD && kind <= IR_GE) {
            x = OPERAND(a);
            if (kind < IR_NEG || kind > IR_BIT_NOT) {
                y = OPERAND(b);
            }
        }
        int fail = 0;
        u64 v = 0;
        switch (kind) {
            case IR_LOAD: {
                u64 raw;
                memcpy(&raw, frame + offsets[a], sizeof raw);
                v = canon(raw, t);
                break;
            }
            case IR_STORE: {
                u64 raw = OPERAND(b);
                memcpy(frame + offsets[a], &raw, sizeof raw);
                break;
            }
            case IR_ADDR: v = (u64)(uintptr_t)(frame + offsets[a]); break;
            case IR_DATA: v = (u64)(uintptr_t)(c->ir.data + a); break;
            case IR_GLOBAL: v = (u64)(uintptr_t)(r->globals + a); break;
            case IR_INDEX: {
                u32 base = OPERAND_TYPE(a);
                __int128 k = wide(OPERAND(b), ty(c, OPERAND_TYPE(b)));
                bool checked = !(b & IR_CONST) && c->types.kinds[base] == TYPE_POINTER;
                if (checked &&
                    (k < 0 || k >= (__int128)c->types.arg1[c->types.arg0[base]])) {
                    fail = TRAP_BOUNDS;
                    break;
                }
                v = OPERAND(a) + (u64)k * check_type_size(c, c->types.arg0[type]);
                break;
            }
            case IR_INDEX_LEN: {
                const u32 *x = ir->extra + fn->first_extra + b;
                __int128 n = (int64_t)OPERAND(x[0]);
                __int128 k = wide(OPERAND(x[1]), ty(c, OPERAND_TYPE(x[1])));
                if (k < 0 || k >= n) {
                    fail = TRAP_BOUNDS;
                    break;
                }
                v = OPERAND(a) + (u64)k * check_type_size(c, c->types.arg0[type]);
                break;
            }
            case IR_SLICE:
            case IR_SLICE_FROM: {
                const u32 *x = ir->extra + fn->first_extra + b;
                __int128 n = (int64_t)OPERAND(x[0]);
                __int128 lo = wide(OPERAND(x[1]), ty(c, OPERAND_TYPE(x[1])));
                __int128 hi = kind == IR_SLICE
                                ? wide(OPERAND(x[2]), ty(c, OPERAND_TYPE(x[2])))
                                : n;
                if (lo < 0 || lo > hi || hi > n) {
                    fail = TRAP_BOUNDS;
                    break;
                }
                v = OPERAND(a) + (u64)lo * check_type_size(c, c->types.arg0[type]);
                second = (u64)(hi - lo);
                break;
            }
            case IR_FIELD: v = OPERAND(a) + check_field_offset(c, b); break;
            case IR_SECOND: v = second; break;
            case IR_COPY:
                memmove(
                    (void *)(uintptr_t)OPERAND(a),
                    (const void *)(uintptr_t)OPERAND(b),
                    check_type_size(c, type)
                );
                break;
            case IR_ZERO:
                memset((void *)(uintptr_t)OPERAND(a), 0, check_type_size(c, type));
                break;
            case IR_LOAD_AT: {
                if (c->types.kinds[type] == TYPE_STRUCT) {
                    v = OPERAND(a);
                    break;
                }
                u64 raw = 0;
                const u8 *p = (const u8 *)(uintptr_t)OPERAND(a);
                memcpy(&raw, p, t.bits / 8);
                v = canon(raw, t);
                if (c->types.kinds[type] == TYPE_SLICE) {
                    memcpy(&second, p + 8, sizeof second);
                }
                break;
            }
            case IR_STORE_AT: {
                u64 raw = OPERAND(b);
                memcpy((void *)(uintptr_t)OPERAND(a), &raw, t.bits / 8);
                break;
            }
            case IR_STORE_PAIR: {
                const u32 *x = ir->extra + fn->first_extra + b;
                u64 words[2] = { OPERAND(x[0]), OPERAND(x[1]) };
                memcpy((void *)(uintptr_t)OPERAND(a), words, sizeof words);
                break;
            }
            case IR_ADD:
            case IR_SUB:
            case IR_MUL: {
                if (t.fl) {
                    v = float_op(kind, x, y, t);
                    break;
                }
                __int128 w = 0;
                if (kind == IR_MUL && !t.sign) {
                    unsigned __int128 p = (unsigned __int128)x * y;
                    if (p >> t.bits) {
                        fail = TRAP_OVERFLOW;
                    }
                    v = (u64)p;
                    break;
                }
                if (kind == IR_MUL) {
                    int64_t p;
                    bool over = __builtin_mul_overflow((int64_t)x, (int64_t)y, &p);
                    w = over ? (__int128)1 << 64 : (__int128)p;
                } else {
                    w = kind == IR_ADD ? wide(x, t) + wide(y, t)
                                       : wide(x, t) - wide(y, t);
                }
                if (!fits(w, t)) {
                    fail = TRAP_OVERFLOW;
                }
                v = (u64)w;
                break;
            }
            case IR_ADD_WRAP: v = canon(x + y, t); break;
            case IR_SUB_WRAP: v = canon(x - y, t); break;
            case IR_MUL_WRAP: v = canon(x * y, t); break;
            case IR_DIV:
            case IR_REM: {
                if (t.fl) {
                    v = float_op(kind, x, y, t);
                    break;
                }
                if (y == 0) {
                    fail = TRAP_DIVIDE;
                    break;
                }
                __int128 p = wide(x, t), q = wide(y, t);
                __int128 w = kind == IR_DIV ? p / q : p % q;
                if (!fits(w, t)) {
                    fail = TRAP_OVERFLOW;
                }
                v = canon((u64)w, t);
                break;
            }
            case IR_SHL:
            case IR_SHR:
                if (y >= t.bits) {
                    fail = TRAP_SHIFT;
                    break;
                }
                v = kind == IR_SHL ? canon(x << y, t)
                  : t.sign         ? (u64)((int64_t)x >> y)
                                   : x >> y;
                break;
            case IR_AND: v = x & y; break;
            case IR_OR: v = x | y; break;
            case IR_XOR: v = canon(x ^ y, t); break;
            case IR_NEG:
                if (t.fl) {
                    v = x ^ (t.bits == 32 ? 0x80000000ull : 0x8000000000000000ull);
                } else if (!fits(-wide(x, t), t)) {
                    fail = TRAP_OVERFLOW;
                } else {
                    v = canon(0 - x, t);
                }
                break;
            case IR_NEG_WRAP: v = canon(0 - x, t); break;
            case IR_NOT: v = !x; break;
            case IR_BIT_NOT: v = canon(~x, t); break;
            case IR_EQ:
            case IR_NE:
            case IR_LT:
            case IR_LE:
            case IR_GT:
            case IR_GE: v = compare(kind, x, y, t); break;
            case IR_CONVERT:
            case IR_NARROW:
            case IR_TRUNCATE: {
                u64 s = OPERAND(a);
                Ty from = ty(c, OPERAND_TYPE(a));
                if (kind == IR_TRUNCATE) {
                    v = canon(s, t);
                } else if (from.fl && t.fl) {
                    v = from_double(as_double(s, from), t);
                } else if (t.fl) {
                    double d = from.sign ? (double)(int64_t)s : (double)s;
                    if (t.bits == 32) {
                        float f = from.sign ? (float)(int64_t)s : (float)s;
                        d = f;
                    }
                    v = from_double(d, t);
                } else if (from.fl) {
                    double d = trunc(as_double(s, from));
                    double lo = t.sign ? -ldexp(1, (int)t.bits - 1) : 0;
                    double hi = ldexp(1, (int)(t.sign ? t.bits - 1 : t.bits));
                    if (!(d >= lo && d < hi)) {
                        fail = TRAP_RANGE;
                    } else {
                        v = canon(t.sign ? (u64)(int64_t)d : (u64)d, t);
                    }
                } else if (
                    !fits(wide(s, from), t) ||
                    (c->types.kinds[type] == TYPE_ENUM &&
                     !is_member(ir->extra + fn->first_extra + b, t, canon(s, t)))
                ) {
                    assert(kind == IR_NARROW);
                    fail = TRAP_RANGE;
                } else {
                    v = canon(s, t);
                }
                break;
            }
            case IR_CALL: {
                const u32 *list = ir->extra + fn->first_extra + b;
                u64 *call_args = calloc(list[0] + 1, sizeof *call_args);
                for (u32 k = 0; k < list[0]; k++) {
                    call_args[k] = OPERAND(list[1 + k]);
                }
                u64 out[2] = { 0, 0 };
                bool record = c->types.kinds[type] == TYPE_STRUCT;
                u64 *to = record ? (u64 *)(frame + offsets[list[list[0] + 1]]) : out;
                fail = run(r, c->decls.funcs[a] - 1, call_args, to);
                v = out[0];
                second = out[1];
                free(call_args);
                break;
            }
            case IR_DROP: break;
            case IR_RET:
                if (c->types.kinds[type] == TYPE_STRUCT) {
                    memcpy(result, (const void *)(uintptr_t)OPERAND(a), check_type_size(c, type));
                    trap = 0;
                    goto done;
                }
                result[0] = type == TYPE_ID_VOID ? 0 : OPERAND(a);
                result[1] = c->types.kinds[type] == TYPE_SLICE ? OPERAND(b) : 0;
                trap = 0;
                goto done;
            case IR_BLOCK:
            case IR_LOOP: break;
            case IR_IF:
                if (!OPERAND(a)) {
                    u32 to = r->elses[g] != NONE ? r->elses[g] : r->ends[g];
                    i = to - fn->first + 1;
                    continue;
                }
                break;
            case IR_ELSE:
                if (type != TYPE_ID_NONE) {
                    vals[r->ends[g] - fn->first] = OPERAND(a);
                }
                i = r->ends[g] - fn->first + 1;
                continue;
            case IR_END:
                assert(type == TYPE_ID_NONE || ir->kinds[r->opens[g]] == IR_IF);
                if (type != TYPE_ID_NONE) {
                    v = OPERAND(a);
                }
                break;
            case IR_EXIT:
                if (type != TYPE_ID_NONE) {
                    vals[r->ends[fn->first + a] - fn->first] = OPERAND(b);
                }
                i = r->ends[fn->first + a] - fn->first + 1;
                continue;
            case IR_REPEAT: i = a + 1; continue;
            case IR_NOP: break;
            case IR_SWITCH: {
                const u32 *x = ir->extra + fn->first_extra + b;
                u64 s = OPERAND(a);
                u32 arm = x[1];
                for (u32 k = 0; k < x[0]; k++) {
                    if (within(s, x + 2 + 5 * k, t)) {
                        arm = x[2 + 5 * k + 4];
                        break;
                    }
                }
                if (arm == NONE) {
                    fail = TRAP_UNREACHABLE;
                    break;
                }
                u32 m = g;
                for (u32 k = 0; k < arm; k++) {
                    m = r->elses[m];
                }
                i = m - fn->first + 1;
                continue;
            }
            case IR_CASE: i = r->ends[g] - fn->first + 1; continue;
            default: assert(false && "unknown IR instruction");
        }
        if (fail) {
            trap = fail;
            break;
        }
        vals[i] = v;
        i++;
    }

done:
    free(vals);
    free(frame);
    return trap;
#undef OPERAND
#undef OPERAND_TYPE
}

int
ir_reference_run(IrReference *r, u32 decl, const u64 *args, u64 *result)
{
    const Checker *c = r->c;
    assert(c->types.kinds[c->types.arg1[c->decls.types[decl]]] != TYPE_STRUCT);
    r->steps = 0;
    u64 out[2] = { 0, 0 };
    int trap = run(r, r->c->decls.funcs[decl] - 1, args, out);
    *result = out[0];
    return trap;
}
