#include "c_source.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum
{
    NONE = UINT32_MAX,
    BYTES_PER_LINE = 64,
    BLANK_LINES = 8,
};

typedef struct
{
    bool fl;
    bool sign;
    u32 bits;
} Scalar;

typedef struct
{
    u32 *at;
    u32 count;
    u32 cap;
    u32 scanned;
} LineStarts;

typedef struct
{
    FILE *out;
    const Checker *c;
    const char *prefix;
    const IrCode *code;
    const IrFunc *fn;
    u32 *ends;
    u32 *opens;
    u32 *links;
    u32 *stack;
    u32 *last;
    u32 *arms;
    u8 *labels;
    LineStarts *line_starts;
    u32 depth;
    u32 line;
    u32 skip;
    bool open;
} Source;

static const char *const operators[NIR_KINDS] = {
    [IR_ADD] = "+",      [IR_SUB] = "-",      [IR_MUL] = "*",
    [IR_DIV] = "/",      [IR_REM] = "%",      [IR_ADD_WRAP] = "+",
    [IR_SUB_WRAP] = "-", [IR_MUL_WRAP] = "*", [IR_AND] = "&",
    [IR_OR] = "|",       [IR_XOR] = "^",      [IR_EQ] = "==",
    [IR_NE] = "!=",      [IR_LT] = "<",       [IR_LE] = "<=",
    [IR_GT] = ">",       [IR_GE] = ">=",
};

static const char *const builtins[NIR_KINDS] = {
    [IR_ADD] = "add",           [IR_SUB] = "sub",
    [IR_MUL] = "mul",           [IR_ADD_OVERFLOWS] = "add",
    [IR_SUB_OVERFLOWS] = "sub", [IR_MUL_OVERFLOWS] = "mul",
};

void
c_source_prelude(FILE *out)
{
    fputs(
        "#include <stdint.h>\n"
        "\n"
        "#ifdef __clang__\n"
        "#pragma STDC FP_CONTRACT OFF\n"
        "#endif\n"
        "\n"
        "#ifndef CHIBI_FN\n"
        "#define CHIBI_FN static\n"
        "#endif\n"
        "\n"
        "#define CHIBI_STRING(x) #x\n"
        "#define CHIBI_EXPAND(x) CHIBI_STRING(x)\n"
        "#define CHIBI_SYMBOL(name) "
        "__asm__(CHIBI_EXPAND(__USER_LABEL_PREFIX__) name)\n"
        "\n"
        "typedef struct\n"
        "{\n"
        "    uint8_t *ptr;\n"
        "    int64_t len;\n"
        "} chibi_slice;\n"
        "\n"
        "typedef uint8_t chibi_u8x16 __attribute__((vector_size(16)));\n"
        "typedef int8_t chibi_i8x16 __attribute__((vector_size(16)));\n"
        "typedef uint16_t chibi_u16x8 __attribute__((vector_size(16)));\n"
        "typedef int16_t chibi_i16x8 __attribute__((vector_size(16)));\n"
        "typedef uint32_t chibi_u32x4 __attribute__((vector_size(16)));\n"
        "typedef int32_t chibi_i32x4 __attribute__((vector_size(16)));\n"
        "typedef uint64_t chibi_u64x2 __attribute__((vector_size(16)));\n"
        "typedef int64_t chibi_i64x2 __attribute__((vector_size(16)));\n"
        "\n"
        "__attribute__((noreturn, cold)) void chibi_trap(int kind);\n"
        "extern void *chibi_context;\n",
        out
    );
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

static const char *
scalar_name(Scalar t)
{
    static const char *const ints[2][4] = {
        { "uint8_t", "uint16_t", "uint32_t", "uint64_t" },
        { "int8_t", "int16_t", "int32_t", "int64_t" },
    };
    if (t.fl) {
        return t.bits == 32 ? "float" : "double";
    }
    return ints[t.sign][__builtin_ctz(t.bits) - 3];
}

static NOINLINE const char *
vector_name(const Checker *c, u32 type, bool wide)
{
    static const char *const vectors[2][4] = {
        { "chibi_u8x16", "chibi_u16x8", "chibi_u32x4", "chibi_u64x2" },
        { "chibi_i8x16", "chibi_i16x8", "chibi_i32x4", "chibi_i64x2" },
    };
    Scalar lane = scalar_of(c, c->types.arg0[type]);
    return vectors[lane.sign && !wide][__builtin_ctz(lane.bits) - 3];
}

static const char *
wide_name(const Checker *c, u32 type)
{
    if (c->types.kinds[type] == TYPE_VECTOR) {
        return vector_name(c, type, true);
    }
    return scalar_of(c, type).bits <= 32 ? "uint32_t" : "uint64_t";
}

static bool
is_address(const Checker *c, u32 type)
{
    u8 kind = c->types.kinds[type];
    return kind == TYPE_POINTER || kind == TYPE_MANY || kind == TYPE_SLICE ||
           kind == TYPE_STRUCT || kind == TYPE_FN || kind == TYPE_OPTIONAL;
}

static const char *
value_name(const Checker *c, u32 type)
{
    if (c->types.kinds[type] == TYPE_VECTOR) {
        return vector_name(c, type, false);
    }
    return is_address(c, type) ? "uint8_t *" : scalar_name(scalar_of(c, type));
}

static u32
element_of(const Checker *c, u32 type)
{
    while (c->types.kinds[type] == TYPE_ARRAY) {
        type = c->types.arg0[type];
    }
    return type;
}

const char *
c_source_unsupported(const Checker *c)
{
    for (u32 d = 1; d < c->decls.count; d++) {
        u32 type = c->decls.types[d];
        if (c->decls.kinds[d] == DECL_FN && c->decls.bodies[d] == NO_BODY &&
            (c->types.arg1[type] & FN_VARIADIC) && c->lists[c->types.arg0[type]] == 0) {
            return "C11 can't declare an `extern fn` that takes only `..`";
        }
    }
    return NULL;
}

static bool
needs_vector_helpers(const IrCode *code)
{
    for (u32 g = 0; g < code->count; g++) {
        if (code->kinds[g] == IR_LOOKUP || code->kinds[g] == IR_TOP_BITS) {
            return true;
        }
    }
    return false;
}

static void
put_vector_helpers(FILE *out)
{
    fputs(
        "\n"
        "#ifndef CHIBI_VECTORS\n"
        "#define CHIBI_VECTORS\n"
        "\n"
        "#if defined(__aarch64__) && !defined(CHIBI_SIMD_GENERIC)\n"
        "#define CHIBI_NEON 1\n"
        "#include <arm_neon.h>\n"
        "#elif defined(__SSE2__) && !defined(CHIBI_SIMD_GENERIC)\n"
        "#define CHIBI_SSE2 1\n"
        "#include <emmintrin.h>\n"
        "#ifdef __SSSE3__\n"
        "#define CHIBI_SSSE3 1\n"
        "#include <tmmintrin.h>\n"
        "#endif\n"
        "#endif\n"
        "\n"
        "static inline __attribute__((unused)) chibi_u8x16\n"
        "chibi_lookup(chibi_u8x16 t, chibi_u8x16 i)\n"
        "{\n"
        "#if defined(CHIBI_NEON)\n"
        "    return (chibi_u8x16)vqtbl1q_u8((uint8x16_t)t, (uint8x16_t)i);\n"
        "#elif defined(CHIBI_SSSE3)\n"
        "    return (chibi_u8x16)_mm_shuffle_epi8("
        "(__m128i)t, _mm_adds_epu8((__m128i)i, _mm_set1_epi8(0x70)));\n"
        "#else\n"
        "    chibi_u8x16 r;\n"
        "    for (int k = 0; k < 16; k++) {\n"
        "        r[k] = i[k] < 16 ? t[i[k]] : 0;\n"
        "    }\n"
        "    return r;\n"
        "#endif\n"
        "}\n"
        "\n"
        "static inline __attribute__((unused)) uint16_t\n"
        "chibi_top_bits_x16(chibi_u8x16 v)\n"
        "{\n"
        "#if defined(CHIBI_NEON)\n"
        "    uint16x8_t a = "
        "vreinterpretq_u16_u8(vshrq_n_u8((uint8x16_t)v, 7));\n"
        "    uint32x4_t b = vreinterpretq_u32_u16(vsraq_n_u16(a, a, 7));\n"
        "    uint64x2_t c = vreinterpretq_u64_u32(vsraq_n_u32(b, b, 14));\n"
        "    uint8x16_t d = vreinterpretq_u8_u64(vsraq_n_u64(c, c, 28));\n"
        "    return (uint16_t)(vgetq_lane_u8(d, 0) | "
        "vgetq_lane_u8(d, 8) << 8);\n"
        "#elif defined(CHIBI_SSE2)\n"
        "    return (uint16_t)_mm_movemask_epi8((__m128i)v);\n"
        "#else\n"
        "    uint16_t m = 0;\n"
        "    for (int k = 0; k < 16; k++) {\n"
        "        m |= (uint16_t)(v[k] >> 7 << k);\n"
        "    }\n"
        "    return m;\n"
        "#endif\n"
        "}\n"
        "\n"
        "static inline __attribute__((unused)) uint8_t\n"
        "chibi_top_bits_x8(chibi_u16x8 v)\n"
        "{\n"
        "#if defined(CHIBI_NEON)\n"
        "    uint32x4_t a = "
        "vreinterpretq_u32_u16(vshrq_n_u16((uint16x8_t)v, 15));\n"
        "    uint64x2_t b = vreinterpretq_u64_u32(vsraq_n_u32(a, a, 15));\n"
        "    uint8x16_t c = vreinterpretq_u8_u64(vsraq_n_u64(b, b, 30));\n"
        "    return (uint8_t)(vgetq_lane_u8(c, 0) | "
        "vgetq_lane_u8(c, 8) << 4);\n"
        "#elif defined(CHIBI_SSE2)\n"
        "    return (uint8_t)_mm_movemask_epi8(_mm_packs_epi16((__m128i)v, "
        "_mm_setzero_si128()));\n"
        "#else\n"
        "    uint8_t m = 0;\n"
        "    for (int k = 0; k < 8; k++) {\n"
        "        m |= (uint8_t)(v[k] >> 15 << k);\n"
        "    }\n"
        "    return m;\n"
        "#endif\n"
        "}\n"
        "\n"
        "static inline __attribute__((unused)) uint8_t\n"
        "chibi_top_bits_x4(chibi_u32x4 v)\n"
        "{\n"
        "#if defined(CHIBI_NEON)\n"
        "    uint64x2_t a = "
        "vreinterpretq_u64_u32(vshrq_n_u32((uint32x4_t)v, 31));\n"
        "    uint8x16_t b = vreinterpretq_u8_u64(vsraq_n_u64(a, a, 31));\n"
        "    return (uint8_t)(vgetq_lane_u8(b, 0) | "
        "vgetq_lane_u8(b, 8) << 2);\n"
        "#elif defined(CHIBI_SSE2)\n"
        "    return (uint8_t)_mm_movemask_ps(_mm_castsi128_ps((__m128i)v));\n"
        "#else\n"
        "    uint8_t m = 0;\n"
        "    for (int k = 0; k < 4; k++) {\n"
        "        m |= (uint8_t)(v[k] >> 31 << k);\n"
        "    }\n"
        "    return m;\n"
        "#endif\n"
        "}\n"
        "\n"
        "static inline __attribute__((unused)) uint8_t\n"
        "chibi_top_bits_x2(chibi_u64x2 v)\n"
        "{\n"
        "#if defined(CHIBI_NEON)\n"
        "    uint64x2_t a = vshrq_n_u64((uint64x2_t)v, 63);\n"
        "    return (uint8_t)(vgetq_lane_u64(a, 0) | "
        "vgetq_lane_u64(a, 1) << 1);\n"
        "#elif defined(CHIBI_SSE2)\n"
        "    return (uint8_t)_mm_movemask_pd(_mm_castsi128_pd((__m128i)v));\n"
        "#else\n"
        "    return (uint8_t)(v[0] >> 63 | v[1] >> 63 << 1);\n"
        "#endif\n"
        "}\n"
        "\n"
        "#endif\n",
        out
    );
}

static u32
value_type(const Source *s, u32 i)
{
    u32 g = s->fn->first + i;
    u8 kind = s->code->kinds[g];
    u32 type = s->code->types[g];
    if (kind >= IR_EQ && kind <= IR_MUL_OVERFLOWS) {
        return s->c->types.kinds[type] == TYPE_VECTOR ? type : TYPE_ID_BOOL;
    }
    if (kind == IR_TOP_BITS) {
        return s->c->types.arg1[type] == 16 ? TYPE_ID_U16 : TYPE_ID_U8;
    }
    return type;
}

static bool
produces(const Source *s, u32 i)
{
    u32 g = s->fn->first + i;
    u8 kind = s->code->kinds[g];
    u32 type = s->code->types[g];
    if (kind == IR_CALL || kind == IR_CALL_C) {
        return type != TYPE_ID_VOID && s->c->types.kinds[type] != TYPE_STRUCT;
    }
    return kind < IR_CALL || (kind == IR_END && type != TYPE_ID_NONE);
}

static const u32 *
extra(const Source *s, u32 b)
{
    return s->code->extra + s->fn->first_extra + b;
}

static void
put_constant(Source *s, u64 bits, u32 type)
{
    const Checker *c = s->c;
    FILE *out = s->out;
    if (is_address(c, type)) {
        fprintf(out, "(uint8_t *)(uintptr_t)%lluu", (unsigned long long)bits);
        return;
    }
    if (c->types.kinds[type] == TYPE_VECTOR) {
        fprintf(out, "(%s){ ", value_name(c, type));
        for (u64 k = 0; k < c->types.arg1[type]; k++) {
            fputs(k ? ", " : "", out);
            put_constant(s, bits, c->types.arg0[type]);
        }
        fputs(" }", out);
        return;
    }

    Scalar t = scalar_of(c, type);
    if (t.fl) {
        double d;
        if (t.bits == 32) {
            u32 b = (u32)bits;
            float f;
            memcpy(&f, &b, sizeof f);
            d = f;
        } else {
            memcpy(&d, &bits, sizeof d);
        }
        assert(isfinite(d));
        fprintf(out, signbit(d) ? "(%a%s)" : "%a%s", d, t.bits == 32 ? "f" : "");
    } else if (!t.sign) {
        fprintf(out, "%lluu", (unsigned long long)bits);
    } else if ((int64_t)bits == INT64_MIN) {
        fputs("(-9223372036854775807 - 1)", out);
    } else {
        fprintf(out, (int64_t)bits < 0 ? "(%lld)" : "%lld", (long long)bits);
    }
}

static void
put_operand(Source *s, u32 x)
{
    if (x & IR_CONST) {
        IrConst k = s->code->consts[s->fn->first_const + (x & ~IR_CONST)];
        put_constant(s, k.bits, k.type);
        return;
    }
    fprintf(s->out, "v%u", x);
}

static bool
is_length(const Source *s, u32 slot)
{
    const IrSlot *slots = s->code->slots + s->fn->first_slot;
    return slot > 0 && s->c->types.kinds[slots[slot - 1].type] == TYPE_SLICE;
}

static void
put_slot(Source *s, u32 slot, bool object)
{
    const IrSlot *slots = s->code->slots + s->fn->first_slot;
    if (is_length(s, slot)) {
        fprintf(s->out, "s%u.len", slot - 1);
    } else if (!object && s->c->types.kinds[slots[slot].type] == TYPE_SLICE) {
        fprintf(s->out, "s%u.ptr", slot);
    } else {
        fprintf(s->out, "s%u", slot);
    }
}

static void
put_name(Source *s, u32 decl)
{
    Name n = names_get(s->c->names, s->c->decls.names[decl]);
    fprintf(s->out, "%s%.*s", s->prefix, (int)n.len, n.text);
}

static void
put_c_name(Source *s, u32 decl)
{
    Name n = names_get(s->c->names, s->c->decls.names[decl]);
    const char *role = s->c->decls.bodies[decl] == NO_BODY ? "c_" : "adapter_";
    fprintf(s->out, "chibi_%s%s%.*s", s->prefix, role, (int)n.len, n.text);
}

static void
indent(Source *s)
{
    for (u32 d = 0; d < s->depth; d++) {
        fputs("    ", s->out);
    }
}

static void
start_statement(Source *s)
{
    if (s->open) {
        putc(' ', s->out);
    } else {
        indent(s);
    }
}

static void
end_statement(Source *s)
{
    if (s->line) {
        s->open = true;
    } else {
        putc('\n', s->out);
    }
}

static void
put_line(Source *s, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    start_statement(s);
    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            putc(*p, s->out);
            continue;
        }
        switch (*++p) {
            case 'o': put_operand(s, va_arg(ap, u32)); break;
            case 's': put_slot(s, va_arg(ap, u32), false); break;
            case 'S': put_slot(s, va_arg(ap, u32), true); break;
            case 't': fputs(value_name(s->c, va_arg(ap, u32)), s->out); break;
            case 'u': fprintf(s->out, "%u", va_arg(ap, u32)); break;
            case 'U':
                fprintf(s->out, "%lluu", (unsigned long long)va_arg(ap, u64));
                break;
            case 'D': {
                int64_t v = va_arg(ap, int64_t);
                fprintf(s->out, v < 0 ? "(%lld)" : "%lld", (long long)v);
                break;
            }
            case 'F': {
                double v = va_arg(ap, double);
                fprintf(s->out, signbit(v) ? "(%a)" : "%a", v);
                break;
            }
            case 'k': {
                u64 bits = va_arg(ap, u64);
                put_constant(s, bits, va_arg(ap, u32));
                break;
            }
            case 'c': fputs(va_arg(ap, const char *), s->out); break;
            case 'n': put_name(s, va_arg(ap, u32)); break;
            case 'e': put_c_name(s, va_arg(ap, u32)); break;
            case 'T': fprintf(s->out, "chibi_trap(%d)", va_arg(ap, int)); break;
            case 'L': {
                u32 at = va_arg(ap, u32);
                s->labels[at] = 1;
                fprintf(s->out, "L%u", at);
                break;
            }
            case '%': putc('%', s->out); break;
            default: assert(false && "unknown format");
        }
    }
    end_statement(s);
    va_end(ap);
}

static void
put_object_type(Source *s, u32 type)
{
    u8 kind = s->c->types.kinds[type];
    if (kind == TYPE_STRUCT) {
        fputs("struct ", s->out);
        put_name(s, s->c->types.arg0[type]);
    } else if (kind == TYPE_SLICE) {
        fputs("chibi_slice", s->out);
    } else {
        fputs(value_name(s->c, type), s->out);
    }
}

static void
put_declaration(Source *s, u32 type, char tag, u32 id)
{
    const Checker *c = s->c;
    put_object_type(s, element_of(c, type));
    fprintf(s->out, " %c%u", tag, id);
    for (u32 t = type; c->types.kinds[t] == TYPE_ARRAY; t = c->types.arg0[t]) {
        fprintf(s->out, "[%llu]", (unsigned long long)c->types.arg1[t]);
    }
}

static void
put_struct(Source *s, u32 decl, u8 *done)
{
    const Checker *c = s->c;
    done[decl] = 1;
    u32 first = c->decls.members[decl];
    u32 end = first + c->decls.member_counts[decl];
    for (u32 i = first; i < end; i++) {
        u32 t = element_of(c, c->members[i].type);
        if (c->types.kinds[t] == TYPE_STRUCT && !done[c->types.arg0[t]]) {
            put_struct(s, c->types.arg0[t], done);
        }
    }

    u32 type = c->decls.types[decl];
    s->depth = 0;
    put_line(s, "\nstruct %n", decl);
    put_line(s, "{");
    for (u32 i = first; i < end; i++) {
        fputs("    ", s->out);
        put_declaration(s, c->members[i].type, 'f', i - first);
        fputs(";\n", s->out);
    }
    put_line(s, "};");
    put_line(
        s,
        "_Static_assert(sizeof(struct %n) == %U && _Alignof(struct %n) == %u, "
        "\"\");",
        decl,
        check_type_size(c, type),
        decl,
        check_type_align(c, type)
    );
}

static void
put_string_byte(FILE *out, u8 b)
{
    if (b >= 0x20 && b < 0x7F && b != '"' && b != '\\' && b != '?') {
        putc(b, out);
    } else {
        fprintf(out, "\\%03o", b);
    }
}

static void
put_bytes(FILE *out, const u8 *bytes, u32 n)
{
    fputs("\n    \"", out);
    for (u32 i = 0; i < n; i++) {
        if (i && i % BYTES_PER_LINE == 0) {
            fputs("\"\n    \"", out);
        }
        put_string_byte(out, bytes[i]);
    }
    fputc('"', out);
}

static void
put_memory(Source *s, const char *qualifier, const char *what, const u8 *bytes, u32 n, u32 size, IrRelocs relocs)
{
    FILE *out = s->out;
    if (!relocs.count) {
        fprintf(
            out,
            "\nstatic %suint8_t chibi_%s%s[%u] __attribute__((aligned(16)))",
            qualifier,
            s->prefix,
            what,
            size
        );
        if (n) {
            fputs(" =", out);
            put_bytes(out, bytes, n);
        }
        fputs(";\n", out);
        return;
    }

    fprintf(out, "\nstatic %sstruct {\n", qualifier);
    u32 at = 0;
    for (u32 k = 0; k < relocs.count; k++) {
        u32 slot = relocs.offsets[k];
        assert(slot % 8 == 0 && slot >= at);
        if (slot > at) {
            fprintf(out, "    uint8_t b%u[%u];\n", at, slot - at);
        }
        fprintf(out, "    const uint8_t *p%u;\n", slot);
        at = slot + 8;
    }
    if (size > at) {
        fprintf(out, "    uint8_t b%u[%u];\n", at, size - at);
    }

    fprintf(out, "} chibi_%s%s __attribute__((aligned(16))) = {", s->prefix, what);
    at = 0;
    for (u32 k = 0; k < relocs.count; k++) {
        u32 slot = relocs.offsets[k];
        if (slot > at) {
            put_bytes(out, bytes + at, slot - at);
            fputc(',', out);
        }

        u64 offset;
        memcpy(&offset, bytes + slot, sizeof offset);
        fprintf(out, "\n    (const uint8_t *)&chibi_%sdata + %llu,", s->prefix, (unsigned long long)offset);
        at = slot + 8;
    }
    if (size > at) {
        put_bytes(out, bytes + at, n > at ? n - at : 0);
    }
    fputs("\n};\n", out);
}

static void
put_result(Source *s, u32 type)
{
    if (type == TYPE_ID_VOID) {
        fputs("void", s->out);
    } else {
        put_object_type(s, type);
    }
}

static void
put_signature(Source *s, const IrFunc *fn)
{
    const Checker *c = s->c;
    u16 flags = c->decls.flags[fn->decl];
    fputs(
        flags & DECL_INLINE     ? "CHIBI_FN __attribute__((always_inline)) "
        : flags & DECL_NOINLINE ? "CHIBI_FN __attribute__((noinline)) "
                                : "CHIBI_FN ",
        s->out
    );
    put_result(s, (u32)c->types.arg1[c->decls.types[fn->decl]]);
    putc(' ', s->out);
    put_name(s, fn->decl);
    fputs("(void *ctx", s->out);

    const IrSlot *slots = s->code->slots + fn->first_slot;
    for (u32 p = 0; p < fn->nparams;
         p += c->types.kinds[slots[p].type] == TYPE_SLICE ? 2 : 1) {
        fputs(", ", s->out);
        put_declaration(s, slots[p].type, 's', p);
    }
    putc(')', s->out);
}

static void
put_c_signature(Source *s, u32 decl)
{
    const Checker *c = s->c;
    u32 type = c->decls.types[decl];
    const u32 *params = c->lists + c->types.arg0[type];

    put_result(s, (u32)c->types.arg1[type]);
    putc(' ', s->out);
    put_c_name(s, decl);
    putc('(', s->out);
    for (u32 p = 0; p < params[0]; p++) {
        fputs(p ? ", " : "", s->out);
        put_declaration(s, params[1 + p], 's', p);
    }
    if (c->types.arg1[type] & FN_VARIADIC) {
        fputs(", ...", s->out);
    } else if (params[0] == 0) {
        fputs("void", s->out);
    }
    putc(')', s->out);
}

static void
put_symbol(Source *s, u32 decl)
{
    Name n = names_get(s->c->names, s->c->decls.names[decl]);
    fprintf(s->out, " CHIBI_SYMBOL(\"%.*s\");\n", (int)n.len, n.text);
}

static void
put_c_global(Source *s, u32 decl)
{
    const Checker *c = s->c;
    u32 type = c->decls.types[decl];
    fputs("extern ", s->out);
    put_object_type(s, element_of(c, type));
    putc(' ', s->out);
    put_c_name(s, decl);
    for (u32 t = type; c->types.kinds[t] == TYPE_ARRAY; t = c->types.arg0[t]) {
        fprintf(s->out, "[%llu]", (unsigned long long)c->types.arg1[t]);
    }
    put_symbol(s, decl);
}

static u32
line_at(Source *s, u32 file, u32 token)
{
    const char *src = s->c->files[file].src;
    LineStarts *l = &s->line_starts[file];
    u32 pos = s->c->files[file].tokens.starts[token];
    if (pos >= l->scanned) {
        u32 n = 0;
        for (u32 p = l->scanned; p < pos; p++) {
            n += src[p] == '\n';
        }
        if (l->count + n > l->cap) {
            l->cap = (l->count + n) * 2;
            l->at = realloc(l->at, l->cap * sizeof *l->at);
        }
        const char *q = src + l->scanned;
        for (u32 k = 0; k < n; k++) {
            q = (const char *)memchr(q, '\n', (size_t)(src + pos - q)) + 1;
            l->at[l->count++] = (u32)(q - src);
        }
        l->scanned = pos;
        return l->count + 1;
    }

    u32 lo = 0;
    u32 hi = l->count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (l->at[mid] <= pos) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo + 1;
}

static void
put_position(Source *s, u32 decl)
{
    const Checker *c = s->c;
    u32 file = c->decls.files[decl];
    u32 token = c->files[file].nodes.tokens[c->decls.roots[decl]];
    fprintf(s->out, "#line %u \"", line_at(s, file, token));
    for (const char *p = c->files[file].path; *p; p++) {
        put_string_byte(s->out, (u8)*p);
    }
    fputs("\"\n", s->out);
}

static void
put_adapter(Source *s, u32 decl, bool exported)
{
    const Checker *c = s->c;
    u32 type = c->decls.types[decl];

    fputs("\n", s->out);
    put_position(s, decl);
    fputs(exported ? "" : "static ", s->out);
    put_c_signature(s, decl);
    fputs((u32)c->types.arg1[type] == TYPE_ID_VOID ? " { " : " { return ", s->out);
    put_name(s, decl);
    fputs("(chibi_context", s->out);
    for (u32 p = 0; p < c->lists[c->types.arg0[type]]; p++) {
        fprintf(s->out, ", s%u", p);
    }
    fputs("); }\n", s->out);
}

static void
find_ends(Source *s)
{
    const u8 *kinds = s->code->kinds + s->fn->first;
    u32 depth = 0;
    for (u32 i = 0; i < s->fn->count; i++) {
        s->ends[i] = NONE;
        s->links[i] = NONE;
        s->labels[i] = 0;
        u8 kind = kinds[i];
        if (kind == IR_BLOCK || kind == IR_LOOP || kind == IR_IF || kind == IR_SWITCH) {
            s->stack[depth] = i;
            s->last[depth++] = i;
        } else if (kind == IR_ELSE || kind == IR_CASE) {
            s->links[s->last[depth - 1]] = i;
            s->last[depth - 1] = i;
            s->opens[i] = s->stack[depth - 1];
        } else if (kind == IR_END) {
            u32 open = s->stack[--depth];
            s->opens[i] = open;
            for (u32 m = open; m != NONE; m = s->links[m]) {
                s->ends[m] = i;
            }
        }
    }
    assert(depth == 0);
}

static void
put_load(Source *s, u32 i, u32 type, u32 a)
{
    u8 kind = s->c->types.kinds[type];
    if (kind == TYPE_STRUCT) {
        put_line(s, "v%u = %o;", i, a);
    } else if (kind == TYPE_SLICE) {
        put_line(s, "__builtin_memcpy(&v%u, %o, 8);", i, a);
        put_line(s, "__builtin_memcpy(&v%u, %o + 8, 8);", i + 1, a);
    } else {
        put_line(s, "__builtin_memcpy(&v%u, %o, sizeof v%u);", i, a, i);
    }
}

static void
put_index(Source *s, u32 i, u32 type, u32 a, u32 b)
{
    const Checker *c = s->c;
    u64 size = check_type_size(c, c->types.arg0[type]);
    if (b & IR_CONST) {
        u64 offset = s->code->consts[s->fn->first_const + (b & ~IR_CONST)].bits * size;
        if (offset) {
            put_line(s, "v%u = %o + %D;", i, a, (int64_t)offset);
        } else {
            put_line(s, "v%u = %o;", i, a);
        }
        return;
    }

    assert(!(a & IR_CONST));
    u32 base = value_type(s, a);
    if (c->types.kinds[base] == TYPE_POINTER) {
        u64 n = c->types.arg1[c->types.arg0[base]];
        put_line(s, "if ((uint64_t)%o >= %U) %T;", b, n, TRAP_BOUNDS);
    }
    put_line(s, "v%u = %o + (int64_t)((uint64_t)%o * %U);", i, a, b, size);
}

static void
put_slice(Source *s, u32 i, u8 kind, u32 type, u32 a, const u32 *x)
{
    u64 size = check_type_size(s->c, s->c->types.arg0[type]);
    put_line(s, "{");
    s->depth++;
    put_line(s, "int64_t lo = %o, hi = %o;", x[1], kind == IR_SLICE ? x[2] : x[0]);
    put_line(s, "if (lo < 0 || lo > hi || hi > %o) %T;", x[0], TRAP_BOUNDS);
    put_line(s, "v%u = lo ? %o + (int64_t)((uint64_t)lo * %U) : %o;", i, a, size, a);
    put_line(s, "v%u = hi - lo;", i + 1);
    s->depth--;
    put_line(s, "}");
}

static void
put_divide(Source *s, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    Scalar t = scalar_of(s->c, type);
    if (t.fl) {
        assert(kind == IR_DIV);
        put_line(s, "v%u = %o / %o;", i, a, b);
        return;
    }
    if (!(b & IR_CONST)) {
        put_line(s, "if (%o == 0) %T;", b, TRAP_DIVIDE);
    }
    if (!t.sign) {
        put_line(s, "v%u = %o %c %o;", i, a, operators[kind], b);
    } else if (kind == IR_DIV) {
        u64 min = ~0ull << (t.bits - 1);
        put_line(s, "if (%o == %k && %o == -1) %T;", a, min, type, b, TRAP_OVERFLOW);
        put_line(s, "v%u = %o / %o;", i, a, b);
    } else {
        put_line(s, "v%u = %o == -1 ? 0 : %o %% %o;", i, b, a, b);
    }
}

static void
put_shift(Source *s, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    const Checker *c = s->c;
    u32 lane = c->types.kinds[type] == TYPE_VECTOR ? c->types.arg0[type] : type;
    if (!(b & IR_CONST)) {
        put_line(s, "if ((uint64_t)%o >= %u) %T;", b, scalar_of(c, lane).bits, TRAP_SHIFT);
    }
    if (kind == IR_SHL) {
        put_line(s, "v%u = (%t)((%c)%o << %o);", i, type, wide_name(c, type), a, b);
    } else {
        put_line(s, "v%u = %o >> %o;", i, a, b);
    }
}

static void
put_count(Source *s, u32 i, u8 kind, u32 type, u32 a)
{
    assert(!(a & IR_CONST));
    u32 bits = scalar_of(s->c, value_type(s, a)).bits;
    const char *u = scalar_name((Scalar){ false, false, bits });
    if (kind == IR_COUNT_ONES) {
        put_line(s, "v%u = (%t)__builtin_popcountll((%c)%o);", i, type, u, a);
    } else if (kind == IR_TRAILING_ZEROS) {
        put_line(s, "v%u = (%t)((%c)%o == 0 ? %u : __builtin_ctzll((%c)%o));", i, type, u, a, bits, u, a);
    } else {
        put_line(
            s,
            "v%u = (%t)((%c)%o == 0 ? %u : __builtin_clzll((%c)%o) - %u);",
            i,
            type,
            u,
            a,
            bits,
            u,
            a,
            64 - bits
        );
    }
}

static void
put_range(Source *s, u32 a, u32 from_type, Scalar from, Scalar to)
{
    u64 from_max = ~0ull >> (64 - from.bits + from.sign);
    u64 to_max = ~0ull >> (64 - to.bits + to.sign);
    u64 to_min = to.sign ? ~0ull << (to.bits - 1) : 0;
    bool low = from.sign && (!to.sign || to.bits < from.bits);
    bool high = to_max < from_max;
    if (low && high) {
        put_line(s, "if (%o < %k || %o > %k) %T;", a, to_min, from_type, a, to_max, from_type, TRAP_RANGE);
    } else if (low) {
        put_line(s, "if (%o < %k) %T;", a, to_min, from_type, TRAP_RANGE);
    } else if (high) {
        put_line(s, "if (%o > %k) %T;", a, to_max, from_type, TRAP_RANGE);
    }
}

static void
put_members(Source *s, u32 i, u32 type, const u32 *runs)
{
    put_line(s, "switch (v%u) {", i);
    for (u32 k = 0; k < runs[0]; k += 4) {
        const u32 *w = runs + 1 + k;
        u64 lo = w[0] | (u64)w[1] << 32;
        u64 hi = w[2] | (u64)w[3] << 32;
        if (lo == hi) {
            put_line(s, "case %k:", lo, type);
        } else {
            put_line(s, "case %k ... %k:", lo, type, hi, type);
        }
    }
    put_line(s, "    break;");
    put_line(s, "default:");
    put_line(s, "    %T;", TRAP_RANGE);
    put_line(s, "}");
}

static void
put_convert(Source *s, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    const Checker *c = s->c;
    assert(!(a & IR_CONST));
    u32 from_type = value_type(s, a);
    Scalar from = scalar_of(c, from_type);
    Scalar to = scalar_of(c, type);
    if (from.fl && !to.fl) {
        double lo = to.sign ? -ldexp(1, (int)to.bits - 1) : 0;
        double hi = ldexp(1, (int)(to.sign ? to.bits - 1 : to.bits));
        put_line(s, "{");
        s->depth++;
        put_line(s, "double d = __builtin_trunc(%o);", a);
        put_line(s, "if (!(d >= %F && d < %F)) %T;", lo, hi, TRAP_RANGE);
        put_line(s, "v%u = (%t)d;", i, type);
        s->depth--;
        put_line(s, "}");
        return;
    }

    if (kind == IR_NARROW && !to.fl) {
        put_range(s, a, from_type, from, to);
    }
    put_line(s, "v%u = (%t)%o;", i, type, a);
    if (kind == IR_NARROW && c->types.kinds[type] == TYPE_ENUM) {
        put_members(s, i, type, extra(s, b));
    }
}

static void
put_bitcast(Source *s, u32 i, u32 type, u32 a)
{
    if (s->c->types.kinds[type] == TYPE_VECTOR) {
        put_line(s, "v%u = (%t)%o;", i, type, a);
        return;
    }
    u32 from = a & IR_CONST
                 ? s->code->consts[s->fn->first_const + (a & ~IR_CONST)].type
                 : value_type(s, a);
    put_line(s, "__builtin_memcpy(&v%u, &(%t){ %o }, sizeof v%u);", i, from, a, i);
}

static void
put_splat(Source *s, u32 i, u32 type, u32 a)
{
    start_statement(s);
    fprintf(s->out, "v%u = (%s){ ", i, value_name(s->c, type));
    for (u64 k = 0; k < s->c->types.arg1[type]; k++) {
        fputs(k ? ", " : "", s->out);
        put_operand(s, a);
    }
    fputs(" };", s->out);
    end_statement(s);
}

static void
put_call(Source *s, u32 i, u8 kind, u32 type, u32 decl, const u32 *list)
{
    const Checker *c = s->c;
    u32 fn = c->decls.types[decl];
    const u32 *params = c->lists + c->types.arg0[fn];
    u32 n = list[0];
    u8 result = c->types.kinds[type];

    bool block = result == TYPE_SLICE;
    for (u32 p = 0; p < params[0]; p++) {
        block |= c->types.kinds[params[1 + p]] == TYPE_STRUCT;
    }
    if (block) {
        put_line(s, "{");
        s->depth++;
    }
    for (u32 p = 0, o = 1; p < params[0]; p++) {
        u32 t = params[1 + p];
        if (c->types.kinds[t] == TYPE_STRUCT) {
            put_line(s, "struct %n a%u;", c->types.arg0[t], o);
            put_line(s, "__builtin_memcpy(&a%u, %o, sizeof a%u);", o, list[o], o);
        }
        o += c->types.kinds[t] == TYPE_SLICE ? 2 : 1;
    }

    start_statement(s);
    if (result == TYPE_SLICE) {
        fputs("chibi_slice r = ", s->out);
    } else if (result == TYPE_STRUCT) {
        fprintf(s->out, "s%u = ", list[n + 1]);
    } else if (type != TYPE_ID_VOID) {
        fprintf(s->out, "v%u = ", i);
    }
    const char *separator = ", ";
    if (kind == IR_CALL_C) {
        put_c_name(s, decl);
        putc('(', s->out);
        separator = "";
    } else {
        put_name(s, decl);
        fputs("(ctx", s->out);
    }
    u32 o = 1;
    for (u32 p = 0; p < params[0]; p++, o++) {
        u8 k = c->types.kinds[params[1 + p]];
        fputs(separator, s->out);
        separator = ", ";
        if (k == TYPE_STRUCT) {
            fprintf(s->out, "a%u", o);
        } else if (k == TYPE_SLICE) {
            fputs("(chibi_slice){ ", s->out);
            put_operand(s, list[o]);
            fputs(", ", s->out);
            put_operand(s, list[++o]);
            fputs(" }", s->out);
        } else {
            put_operand(s, list[o]);
        }
    }
    assert(o == n + 1 || (c->types.arg1[fn] & FN_VARIADIC));
    for (; o <= n; o++) {
        fputs(separator, s->out);
        separator = ", ";
        if (list[o] & IR_CONST) {
            u32 t = s->code->consts[s->fn->first_const + (list[o] & ~IR_CONST)].type;
            fprintf(s->out, "(%s)", value_name(c, t));
        }
        put_operand(s, list[o]);
    }
    fputs(");", s->out);
    end_statement(s);

    if (result == TYPE_SLICE) {
        put_line(s, "v%u = r.ptr;", i);
        put_line(s, "v%u = r.len;", i + 1);
    }
    if (block) {
        s->depth--;
        put_line(s, "}");
    }
}

static void
put_return(Source *s, u32 type, u32 a, u32 b)
{
    u8 kind = s->c->types.kinds[type];
    if (type == TYPE_ID_VOID) {
        put_line(s, "return;");
    } else if (kind == TYPE_STRUCT) {
        put_line(s, "{");
        s->depth++;
        put_line(s, "struct %n r;", s->c->types.arg0[type]);
        put_line(s, "__builtin_memcpy(&r, %o, sizeof r);", a);
        put_line(s, "return r;");
        s->depth--;
        put_line(s, "}");
    } else if (kind == TYPE_SLICE) {
        put_line(s, "return (chibi_slice){ %o, %o };", a, b);
    } else {
        put_line(s, "return %o;", a);
    }
}

static void
put_switch(Source *s, u32 i, u32 type, u32 a, const u32 *x)
{
    u32 narms = 0;
    for (u32 m = i; m != NONE; m = s->links[m]) {
        s->arms[narms++] = m;
    }

    put_line(s, "switch (%o) {", a);
    for (u32 k = 0; k < x[0]; k++) {
        const u32 *w = x + 2 + 5 * k;
        u64 lo = w[0] | (u64)w[1] << 32;
        u64 hi = w[2] | (u64)w[3] << 32;
        assert(w[4] < narms);
        if (lo == hi) {
            put_line(s, "case %k: goto %L;", lo, type, s->arms[w[4]]);
        } else {
            put_line(s, "case %k ... %k: goto %L;", lo, type, hi, type, s->arms[w[4]]);
        }
    }
    if (x[1] == NONE) {
        put_line(s, "default: %T;", TRAP_UNREACHABLE);
    } else {
        put_line(s, "default: goto %L;", s->arms[x[1]]);
    }
    put_line(s, "}");
}

static bool
is_true(const Source *s, u32 x)
{
    return s->code->consts[s->fn->first_const + (x & ~IR_CONST)].bits != 0;
}

static void
put_instruction(Source *s, u32 i)
{
    const Checker *c = s->c;
    u32 g = s->fn->first + i;
    u8 kind = s->code->kinds[g];
    u32 type = s->code->types[g];
    u32 a = s->code->as[g];
    u32 b = s->code->bs[g];
    switch (kind) {
        case IR_LOAD: put_line(s, "v%u = %s;", i, a); break;
        case IR_LOAD_AT: put_load(s, i, type, a); break;
        case IR_ADDR: put_line(s, "v%u = (uint8_t *)&%S;", i, a); break;
        case IR_DATA:
            put_line(s, "v%u = (uint8_t *)&chibi_%cdata + %u;", i, s->prefix, a);
            break;
        case IR_GLOBAL:
            put_line(s, "v%u = (uint8_t *)&chibi_%cglobals + %u;", i, s->prefix, a);
            break;
        case IR_C_GLOBAL: put_line(s, "v%u = (uint8_t *)&%e;", i, b); break;
        case IR_INDEX: put_index(s, i, type, a, b); break;
        case IR_INDEX_LEN: {
            const u32 *x = extra(s, b);
            u64 size = check_type_size(c, c->types.arg0[type]);
            put_line(s, "if ((uint64_t)%o >= (uint64_t)%o) %T;", x[1], x[0], TRAP_BOUNDS);
            put_line(s, "v%u = %o + (int64_t)((uint64_t)%o * %U);", i, a, x[1], size);
            break;
        }
        case IR_FIELD:
            put_line(s, "v%u = %o + %U;", i, a, check_field_offset(c, b));
            break;
        case IR_SLICE:
        case IR_SLICE_FROM: put_slice(s, i, kind, type, a, extra(s, b)); break;
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
            if (scalar_of(c, type).fl) {
                put_line(s, "v%u = %o %c %o;", i, a, operators[kind], b);
            } else {
                put_line(s, "if (__builtin_%c_overflow(%o, %o, &v%u)) %T;", builtins[kind], a, b, i, TRAP_OVERFLOW);
            }
            break;
        case IR_DIV:
        case IR_REM: put_divide(s, i, kind, type, a, b); break;
        case IR_ADD_WRAP:
        case IR_SUB_WRAP:
        case IR_MUL_WRAP: {
            const char *w = wide_name(c, type);
            put_line(s, "v%u = (%t)((%c)%o %c (%c)%o);", i, type, w, a, operators[kind], w, b);
            break;
        }
        case IR_MUL_HIGH:
            put_line(s, "v%u = (uint64_t)(((unsigned __int128)%o * %o) >> 64);", i, a, b);
            break;
        case IR_SHL:
        case IR_SHR: put_shift(s, i, kind, type, a, b); break;
        case IR_AND:
        case IR_OR:
        case IR_XOR:
            put_line(s, "v%u = %o %c %o;", i, a, operators[kind], b);
            break;
        case IR_NEG:
            if (scalar_of(c, type).fl) {
                put_line(s, "v%u = -%o;", i, a);
            } else {
                put_line(s, "if (__builtin_sub_overflow(0, %o, &v%u)) %T;", a, i, TRAP_OVERFLOW);
            }
            break;
        case IR_NEG_WRAP:
            put_line(s, "v%u = (%t)-(%c)%o;", i, type, wide_name(c, type), a);
            break;
        case IR_NOT: put_line(s, "v%u = !%o;", i, a); break;
        case IR_BIT_NOT: put_line(s, "v%u = (%t)~%o;", i, type, a); break;
        case IR_TRAILING_ZEROS:
        case IR_LEADING_ZEROS:
        case IR_COUNT_ONES: put_count(s, i, kind, type, a); break;
        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE:
            if (c->types.kinds[type] == TYPE_VECTOR) {
                put_line(s, "v%u = (%t)(%o %c %o);", i, type, a, operators[kind], b);
            } else {
                put_line(s, "v%u = %o %c %o;", i, a, operators[kind], b);
            }
            break;
        case IR_ADD_OVERFLOWS:
        case IR_SUB_OVERFLOWS:
        case IR_MUL_OVERFLOWS:
            put_line(s, "v%u = __builtin_%c_overflow(%o, %o, &(%t){ 0 });", i, builtins[kind], a, b, type);
            break;
        case IR_CONVERT:
        case IR_NARROW:
        case IR_TRUNCATE: put_convert(s, i, kind, type, a, b); break;
        case IR_BITCAST: put_bitcast(s, i, type, a); break;
        case IR_CHECK_ALIGN:
            put_line(s, "if ((uintptr_t)%o & %uu) %T;", a, b - 1, TRAP_ALIGN);
            put_line(s, "v%u = %o;", i, a);
            break;
        case IR_CHECK_NIL:
            put_line(s, "if (!%o) %T;", a, TRAP_NIL);
            put_line(s, "v%u = %o;", i, a);
            break;
        case IR_SPLAT: put_splat(s, i, type, a); break;
        case IR_LOOKUP:
            put_line(s, "v%u = (%t)chibi_lookup((chibi_u8x16)%o, (chibi_u8x16)%o);", i, type, a, b);
            break;
        case IR_TOP_BITS:
            put_line(
                s,
                "v%u = chibi_top_bits_x%u((%c)%o);",
                i,
                (u32)c->types.arg1[type],
                wide_name(c, type),
                a
            );
            break;
        case IR_CALLBACK: put_line(s, "v%u = (uint8_t *)%e;", i, b); break;
        case IR_CALL:
        case IR_CALL_C: put_call(s, i, kind, type, a, extra(s, b)); break;
        case IR_STORE: put_line(s, "%s = %o;", a, b); break;
        case IR_STORE_AT:
            if (c->types.kinds[type] == TYPE_VECTOR) {
                put_line(s, "__builtin_memcpy(%o, &%o, sizeof(%t));", a, b, type);
            } else {
                put_line(s, "__builtin_memcpy(%o, &(%t){ %o }, sizeof(%t));", a, type, b, type);
            }
            break;
        case IR_STORE_PAIR: {
            const u32 *x = extra(s, b);
            put_line(s, "__builtin_memcpy(%o, &(uint8_t *){ %o }, 8);", a, x[0]);
            put_line(s, "__builtin_memcpy(%o + 8, &(int64_t){ %o }, 8);", a, x[1]);
            break;
        }
        case IR_COPY:
            put_line(s, "__builtin_memmove(%o, %o, %U);", a, b, check_type_size(c, type));
            break;
        case IR_ZERO:
            put_line(s, "__builtin_memset(%o, 0, %U);", a, check_type_size(c, type));
            break;
        case IR_RET: put_return(s, type, a, b); break;
        case IR_IF:
            if (a & IR_CONST) {
                assert(type == TYPE_ID_NONE);
                bool taken = is_true(s, a);
                if (!taken && s->links[i] == NONE) {
                    s->skip = s->ends[i] + 1;
                    break;
                }
                if (!taken) {
                    s->skip = s->links[i];
                }
                put_line(s, "{");
                s->depth++;
                break;
            }
            put_line(s, "if (%o) {", a);
            s->depth++;
            break;
        case IR_ELSE: {
            u32 cond = s->code->as[s->fn->first + s->opens[i]];
            if (cond & IR_CONST) {
                if (is_true(s, cond)) {
                    s->skip = s->ends[i];
                }
                break;
            }
            if (type != TYPE_ID_NONE) {
                put_line(s, "v%u = %o;", s->ends[i], a);
            }
            s->depth--;
            put_line(s, "} else {");
            s->depth++;
            break;
        }
        case IR_END:
            if (s->code->kinds[s->fn->first + s->opens[i]] == IR_IF) {
                if (type != TYPE_ID_NONE) {
                    put_line(s, "v%u = %o;", i, a);
                }
                assert(s->depth > 1);
                s->depth--;
                put_line(s, "}");
            }
            break;
        case IR_EXIT:
            if (type != TYPE_ID_NONE) {
                put_line(s, "v%u = %o;", s->ends[a], b);
            }
            put_line(s, "goto %L;", s->ends[a]);
            break;
        case IR_REPEAT: put_line(s, "goto %L;", a); break;
        case IR_SWITCH: put_switch(s, i, type, a, extra(s, b)); break;
        case IR_CASE: put_line(s, "goto %L;", s->ends[i]); break;
        case IR_ASSERT: put_line(s, "if (!%o) %T;", a, TRAP_ASSERT); break;
        case IR_SECOND:
        case IR_DROP:
        case IR_BLOCK:
        case IR_LOOP:
        case IR_NOP: break;
        default: assert(false && "unsupported IR instruction");
    }
}

static void
go_to_line(Source *s, u32 line)
{
    if (line == s->line) {
        return;
    }
    if (s->open) {
        putc('\n', s->out);
        s->open = false;
        s->line++;
    }

    if (s->line && line - s->line <= BLANK_LINES) {
        for (; s->line < line; s->line++) {
            putc('\n', s->out);
        }
    } else {
        fprintf(s->out, "#line %u\n", line);
        s->line = line;
    }
}

static void
put_function(Source *s, const IrFunc *fn)
{
    s->fn = fn;
    s->skip = 0;
    find_ends(s);
    fputs("\n", s->out);
    put_position(s, fn->decl);
    put_signature(s, fn);
    fputs("\n{\n", s->out);
    s->depth = 1;

    const IrSlot *slots = s->code->slots + fn->first_slot;
    for (u32 k = fn->nparams; k < fn->nslots; k++) {
        if (!is_length(s, k)) {
            indent(s);
            put_declaration(s, slots[k].type, 's', k);
            fputs(";\n", s->out);
        }
    }
    for (u32 i = 0; i < fn->count; i++) {
        if (produces(s, i)) {
            put_line(s, "%t v%u;", value_type(s, i), i);
        }
    }

    u32 file = s->c->decls.files[fn->decl];
    const IrLine *lines = s->code->lines + fn->first_line;
    u32 i = 0;
    for (u32 k = 0; k < fn->nlines; k++) {
        if (lines[k].end <= s->skip) {
            i = lines[k].end;
            continue;
        }
        go_to_line(s, line_at(s, file, lines[k].token));
        for (; i < lines[k].end; i++) {
            if (i < s->skip) {
                continue;
            }
            put_instruction(s, i);
            if (s->labels[i] || s->code->kinds[fn->first + i] == IR_LOOP) {
                put_line(s, "L%u:;", i);
            }
        }
    }
    assert(i == fn->count);
    put_line(s, "%T;", TRAP_UNREACHABLE);
    fputs(s->open ? "\n}\n" : "}\n", s->out);
    s->line = 0;
    s->open = false;
}

static void
put_target_guards(FILE *out, const Checker *c)
{
    static const char *const macros[] = { "__linux__",
                                          "__APPLE__",
                                          "_WIN32",
                                          "__aarch64__",
                                          "__x86_64__" };
    for (u32 k = 0; k < NTARGETS; k++) {
        if (c->target_read[k]) {
            Name name = names_get(c->names, NAME_TARGET_OS + k);
            Name value = names_get(c->names, c->target[k]);
            fprintf(out, "\n#ifndef %s\n", macros[c->target[k] - NAME_LINUX]);
            fprintf(
                out,
                "#error \"chibi wrote this C for %.*s %.*s\"\n",
                (int)name.len,
                name.text,
                (int)value.len,
                value.text
            );
            fputs("#endif\n", out);
        }
    }
}

const char *
c_source_package(FILE *out, const Checker *c, const char *prefix)
{
    assert(strncmp(prefix, "chibi_", strlen(prefix) < 6 ? strlen(prefix) : 6) != 0);
    const char *error = c_source_unsupported(c);
    if (error) {
        return error;
    }

    const Ir *ir = &c->ir;
    u32 cap = 1;
    for (u32 f = 0; f < ir->nfuncs; f++) {
        cap = ir->funcs[f].count + 1 > cap ? ir->funcs[f].count + 1 : cap;
    }
    u32 *words = malloc(6 * (size_t)cap * sizeof *words);
    u8 *labels = malloc(cap);
    u8 *done = calloc(c->decls.count, 1);
    LineStarts *line_starts = calloc(c->nfiles, sizeof *line_starts);
    Source s = { .out = out,
                 .c = c,
                 .prefix = prefix,
                 .code = &ir->code,
                 .ends = words,
                 .opens = words + cap,
                 .links = words + 2 * cap,
                 .stack = words + 3 * cap,
                 .last = words + 4 * cap,
                 .arms = words + 5 * cap,
                 .labels = labels,
                 .line_starts = line_starts };

    put_target_guards(out, c);
    if (needs_vector_helpers(&ir->code)) {
        put_vector_helpers(out);
    }
    for (u32 d = 1; d < c->decls.count; d++) {
        if (c->decls.kinds[d] == DECL_STRUCT &&
            (c->decls.flags[d] & DECL_STORED) && !done[d]) {
            put_struct(&s, d, done);
        }
    }
    if (ir->ndata) {
        put_memory(&s, "const ", "data", ir->data, ir->ndata, ir->ndata, ir->data_relocs);
    }
    if (ir->globals_size) {
        put_memory(&s, "", "globals", ir->globals, ir->nglobals, ir->globals_size, ir->global_relocs);
    }

    fputs("\n", out);
    for (u32 f = 0; f < ir->nfuncs; f++) {
        put_signature(&s, &ir->funcs[f]);
        fputs(";\n", out);
    }
    for (u32 d = 1; d < c->decls.count; d++) {
        if (c->decls.kinds[d] == DECL_FN && c->decls.bodies[d] == NO_BODY) {
            put_c_signature(&s, d);
            put_symbol(&s, d);
        } else if (c->decls.kinds[d] == DECL_VAR && c->decls.bodies[d] == NO_BODY &&
                   c->decls.types[d] != TYPE_ID_NONE) {
            put_c_global(&s, d);
        }
    }
    for (u32 k = 0; k < ir->nexports; k++) {
        put_c_signature(&s, ir->callbacks[k]);
        put_symbol(&s, ir->callbacks[k]);
    }
    for (u32 k = 0; k < ir->ncallbacks; k++) {
        if (c->decls.bodies[ir->callbacks[k]] != NO_BODY) {
            put_adapter(&s, ir->callbacks[k], k < ir->nexports);
        }
    }
    for (u32 f = 0; f < ir->nfuncs; f++) {
        put_function(&s, &ir->funcs[f]);
    }

    for (u32 f = 0; f < c->nfiles; f++) {
        free(line_starts[f].at);
    }
    free(line_starts);
    free(done);
    free(labels);
    free(words);
    return NULL;
}

void
c_source_runtime(FILE *out)
{
    static const char *const texts[NTRAPS] = {
        [TRAP_OVERFLOW] = "integer overflow",
        [TRAP_DIVIDE] = "division by zero",
        [TRAP_SHIFT] = "shift count out of range",
        [TRAP_RANGE] = "value out of range",
        [TRAP_UNREACHABLE] = "unreachable code reached",
        [TRAP_BOUNDS] = "index out of bounds",
        [TRAP_ALIGN] = "misaligned pointer",
        [TRAP_NIL] = "unexpected nil",
        [TRAP_ASSERT] = "assertion failed",
    };

    fputs(
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "\n"
        "void *chibi_context;\n"
        "\n"
        "void\n"
        "chibi_trap(int kind)\n"
        "{\n"
        "    static const char *const texts[] = {\n",
        out
    );
    for (u32 k = TRAP_OVERFLOW; k < NTRAPS; k++) {
        fprintf(out, "        [%u] = \"%s\",\n", k, texts[k]);
    }
    fputs(
        "    };\n"
        "    fflush(NULL);\n"
        "    fprintf(stderr, \"trap: %s\\n\", texts[kind]);\n"
        "    abort();\n"
        "}\n",
        out
    );
}
