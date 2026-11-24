#include "ast.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define CLANG_KINDS(X)                                                         \
    X(K_TRANSLATION_UNIT, "TranslationUnitDecl")                               \
    X(K_TYPEDEF, "TypedefDecl")                                                \
    X(K_ELABORATED_TYPE, "ElaboratedType")                                     \
    X(K_RECORD_TYPE, "RecordType")                                             \
    X(K_RECORD, "RecordDecl")                                                  \
    X(K_FIELD, "FieldDecl")                                                    \
    X(K_ENUM, "EnumDecl")                                                      \
    X(K_ENUM_CONSTANT, "EnumConstantDecl")                                     \
    X(K_FUNCTION, "FunctionDecl")                                              \
    X(K_PARAM, "ParmVarDecl")                                                  \
    X(K_VAR, "VarDecl")                                                        \
    X(K_STATIC_ASSERT, "StaticAssertDecl")                                     \
    X(K_COMPOUND, "CompoundStmt")                                              \
    X(K_DECL_STMT, "DeclStmt")                                                 \
    X(K_IF, "IfStmt")                                                          \
    X(K_WHILE, "WhileStmt")                                                    \
    X(K_DO, "DoStmt")                                                          \
    X(K_FOR, "ForStmt")                                                        \
    X(K_SWITCH, "SwitchStmt")                                                  \
    X(K_CASE, "CaseStmt")                                                      \
    X(K_DEFAULT, "DefaultStmt")                                                \
    X(K_BREAK, "BreakStmt")                                                    \
    X(K_CONTINUE, "ContinueStmt")                                              \
    X(K_RETURN, "ReturnStmt")                                                  \
    X(K_NULL_STMT, "NullStmt")                                                 \
    X(K_PAREN, "ParenExpr")                                                    \
    X(K_IMPLICIT_CAST, "ImplicitCastExpr")                                     \
    X(K_CAST, "CStyleCastExpr")                                                \
    X(K_DECL_REF, "DeclRefExpr")                                               \
    X(K_MEMBER, "MemberExpr")                                                  \
    X(K_CALL, "CallExpr")                                                      \
    X(K_UNARY, "UnaryOperator")                                                \
    X(K_BINARY, "BinaryOperator")                                              \
    X(K_COMPOUND_ASSIGN, "CompoundAssignOperator")                             \
    X(K_CONDITIONAL, "ConditionalOperator")                                    \
    X(K_SUBSCRIPT, "ArraySubscriptExpr")                                       \
    X(K_INT_LITERAL, "IntegerLiteral")                                         \
    X(K_CHAR_LITERAL, "CharacterLiteral")                                      \
    X(K_FLOAT_LITERAL, "FloatingLiteral")                                      \
    X(K_STRING_LITERAL, "StringLiteral")                                       \
    X(K_BOOL_LITERAL, "CXXBoolLiteralExpr")                                    \
    X(K_TYPE_TRAIT, "UnaryExprOrTypeTraitExpr")                                \
    X(K_CONSTANT, "ConstantExpr")                                              \
    X(K_INIT_LIST, "InitListExpr")                                             \
    X(K_COMPOUND_LITERAL, "CompoundLiteralExpr")                               \
    X(K_IMPLICIT_VALUE_INIT, "ImplicitValueInitExpr")                          \
    X(K_PREDEFINED, "PredefinedExpr")                                          \
    X(K_VA_ARG, "VAArgExpr")                                                   \
    X(K_NOINLINE, "NoInlineAttr")                                              \
    X(K_ALWAYS_INLINE, "AlwaysInlineAttr")                                     \
    X(K_RETURNS_NONNULL, "ReturnsNonNullAttr")

typedef enum
{
    K_NONE,
    K_OTHER,
#define KIND_NAME(name, text) name,
    CLANG_KINDS(KIND_NAME)
#undef KIND_NAME
        NKINDS,
} Kind;

#define CLANG_OPS(X)                                                           \
    X(OP_ADD, "+")                                                             \
    X(OP_SUB, "-")                                                             \
    X(OP_MUL, "*")                                                             \
    X(OP_DIV, "/")                                                             \
    X(OP_MOD, "%")                                                             \
    X(OP_SHL, "<<")                                                            \
    X(OP_SHR, ">>")                                                            \
    X(OP_AND, "&")                                                             \
    X(OP_OR, "|")                                                              \
    X(OP_XOR, "^")                                                             \
    X(OP_EQ, "==")                                                             \
    X(OP_NE, "!=")                                                             \
    X(OP_LT, "<")                                                              \
    X(OP_GT, ">")                                                              \
    X(OP_LE, "<=")                                                             \
    X(OP_GE, ">=")                                                             \
    X(OP_LAND, "&&")                                                           \
    X(OP_LOR, "||")                                                            \
    X(OP_ASSIGN, "=")                                                          \
    X(OP_COMMA, ",")                                                           \
    X(OP_NOT, "!")                                                             \
    X(OP_BIT_NOT, "~")                                                         \
    X(OP_INC, "++")                                                            \
    X(OP_DEC, "--")                                                            \
    X(OP_EXTENSION, "__extension__")                                           \
    X(OP_ADD_ASSIGN, "+=")                                                     \
    X(OP_SUB_ASSIGN, "-=")                                                     \
    X(OP_MUL_ASSIGN, "*=")                                                     \
    X(OP_DIV_ASSIGN, "/=")                                                     \
    X(OP_MOD_ASSIGN, "%=")                                                     \
    X(OP_SHL_ASSIGN, "<<=")                                                    \
    X(OP_SHR_ASSIGN, ">>=")                                                    \
    X(OP_AND_ASSIGN, "&=")                                                     \
    X(OP_OR_ASSIGN, "|=")                                                      \
    X(OP_XOR_ASSIGN, "^=")                                                     \
    X(CAST_LVALUE, "LValueToRValue")                                           \
    X(CAST_NOOP, "NoOp")                                                       \
    X(CAST_INTEGRAL, "IntegralCast")                                           \
    X(CAST_INT_TO_BOOL, "IntegralToBoolean")                                   \
    X(CAST_POINTER_TO_BOOL, "PointerToBoolean")                                \
    X(CAST_BIT, "BitCast")                                                     \
    X(CAST_NULL, "NullToPointer")                                              \
    X(CAST_ARRAY_DECAY, "ArrayToPointerDecay")                                 \
    X(CAST_FUNCTION_DECAY, "FunctionToPointerDecay")                           \
    X(CAST_BUILTIN_DECAY, "BuiltinFnToFnPtr")                                  \
    X(CAST_POINTER_TO_INT, "PointerToIntegral")                                \
    X(CAST_INT_TO_POINTER, "IntegralToPointer")                                \
    X(CAST_TO_VOID, "ToVoid")                                                  \
    X(CAST_INT_TO_FLOAT, "IntegralToFloating")                                 \
    X(CAST_FLOAT_TO_INT, "FloatingToIntegral")                                 \
    X(CAST_FLOAT, "FloatingCast")                                              \
    X(CAST_VECTOR_SPLAT, "VectorSplat")                                        \
    X(WORD_STATIC, "static")                                                   \
    X(WORD_EXTERN, "extern")                                                   \
    X(WORD_SIZEOF, "sizeof")                                                   \
    X(WORD_ALIGNOF, "alignof")                                                 \
    X(WORD_EXPECT, "__builtin_expect")                                         \
    X(WORD_ASSERT_FAIL, "__assert_fail")

typedef enum
{
    OP_NONE,
#define OP_NAME(name, text) name,
    CLANG_OPS(OP_NAME)
#undef OP_NAME
        NOPS,
} Op;

enum
{
    CT_VOID = 1,
    CT_BOOL,
    CT_INT,
    CT_FLOAT,
    CT_POINTER,
    CT_ARRAY,
    CT_FUNCTION,
    CT_RECORD,
    CT_OPAQUE,
    CT_VECTOR,
    CT_VA_LIST,
};

typedef struct
{
    u8 kind;
    u8 bits;
    bool is_signed;
    bool variadic;
    u32 inner;
    u64 count;
    u32 decl;
    u32 first;
    u32 nparams;
} CType;

enum
{
    NULLABLE = 1,
    MANY = 2,
    LEVEL_BITS = 2,
    ALL_NULLABLE = 0x55555555,
};

enum
{
    MARK_EMIT = 1,
    MARK_EXTERN = 2,
    MARK_ASSIGNED = 4,
    MARK_ADDRESSED = 8,
    MARK_ZEROED = 16,
    MARK_DEMANDED = 32,
    MARK_TOLERANT = 64,
    MARK_UNDEFINED = 128,
};

enum
{
    PATH_DEPTH = 8,
    MAX_PATHS = 256,
    SCAN_LIMIT = 128,
    NARROW_MAX = 8,
};

enum
{
    ALLOC_NONE,
    ALLOC_PLAIN,
    ALLOC_ZEROED,
};

typedef struct
{
    u32 fields[PATH_DEPTH];
    u32 len;
} Path;

typedef struct
{
    u32 root;
    Path path;
} Access;

enum
{
    P_LOWEST,
    P_OR,
    P_AND,
    P_COMPARE,
    P_OR_ELSE,
    P_ADD,
    P_MUL,
    P_UNARY,
    P_POSTFIX,
};

enum
{
    LINE_WIDTH = 100,
};

typedef struct
{
    char *text;
    u64 len;
    u64 cap;
} Out;

typedef struct
{
    u32 decl;
    u32 level;
} Site;

typedef struct
{
    Site to;
    u32 from;
    bool call;
} Flow;

typedef struct
{
    u32 type;
    u32 bits;
    u8 prec;
    bool untyped;
    bool nil;
    bool boolean;
} Val;

static Val
value(u32 type, u32 bits, u8 prec)
{
    return (Val){ type, bits, prec, false, false, false };
}

typedef struct
{
    u32 node;
    bool is_switch;
    u32 label;
} Target;

enum
{
    SUBST_OPERAND = UINT32_MAX,
};

typedef struct
{
    u32 name;
    Val val;
} Temp;

typedef struct
{
    u32 file;
    char *text;
} Source;

enum
{
    PACKAGE_BITS,
    PACKAGE_MEM,
    PACKAGE_SIMD,
    NPACKAGES,
};

static const char *const package_names[NPACKAGES] = { "bits", "mem", "simd" };

typedef struct
{
    u32 name;
    Out out;
    bool main;
    u8 packages;
} Group;

typedef struct
{
    Ast a;
    u32 nstrings;
    u8 *kind_of;
    u8 *op_of;
    u8 *kinds;
    u32 *parents;
    u32 *canon;
    u32 *defs;
    u32 *bits;
    u32 *names;
    u8 *marks;
    u8 *narrowed;

    u64 *map_ids;
    u32 *map_nodes;
    u32 map_mask;

    CType *types;
    u32 ntypes;
    u32 types_cap;
    u32 *params;
    u32 nparams;
    u32 params_cap;
    u32 *memo;
    u32 *typedefs;
    u32 *records;
    u32 type_u8;
    u32 type_u64;
    u32 type_i64;
    u32 type_bool;

    Flow *flows;
    u32 nflows;
    u32 flows_cap;

    Path zeroed[MAX_PATHS];
    bool dead[MAX_PATHS];
    u32 nzeroed;
    bool overflow;
    Path kills[MAX_PATHS];
    u32 nkills;
    Path zeroing;

    u8 *main_files;
    u32 *alias;
    u32 *file_groups;
    Group groups[256];
    u32 ngroups;
    u32 group;
    u32 fn;
    u32 depth;
    u32 labels;
    bool uses_clear_cache;
    u8 fn_packages;
    u32 rest;
    u32 *scope;
    u32 nscope;
    u32 scope_cap;
    Target targets[256];
    u32 ntargets;
    u32 *subst;
    Temp *temps;
    u32 ntemps;
    u32 temps_cap;
    u32 *posts;
    u32 nposts;
    u32 posts_cap;
    Source sources[256];
    u32 nsources;
    Out out;
} Tr;

static NORETURN void
die(const Tr *t, u32 node, const char *msg)
{
    fprintf(
        stderr,
        "translate: %s:%u: %s (%s)\n",
        strings_text(&t->a.strings, t->a.files[node]),
        t->a.lines[node],
        msg,
        strings_text(&t->a.strings, t->a.kinds[node])
    );
    exit(1);
}

static void *
grow(void *p, u32 *cap, u32 need, size_t size)
{
    if (need <= *cap) {
        return p;
    }
    while (*cap < need) {
        *cap = *cap ? *cap * 2 : 256;
    }
    return realloc(p, *cap * size);
}

static void
reserve(Out *o, u64 n)
{
    if (o->len + n < o->cap) {
        return;
    }
    while (o->len + n >= o->cap) {
        o->cap = o->cap ? o->cap * 2 : 1 << 16;
    }
    o->text = realloc(o->text, o->cap);
}

static void
put_bytes(Out *o, const char *s, u64 n)
{
    reserve(o, n);
    memcpy(o->text + o->len, s, n);
    o->len += n;
}

static void
put(Out *o, const char *s)
{
    put_bytes(o, s, strlen(s));
}

static void
putf(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    reserve(o, (u64)n + 1);
    va_start(ap, fmt);
    vsnprintf(o->text + o->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    o->len += (u64)n;
}

static void
insert(Out *o, u64 at, const char *s, u64 n)
{
    reserve(o, n);
    memmove(o->text + at + n, o->text + at, o->len - at);
    memcpy(o->text + at, s, n);
    o->len += n;
}

static const char *
text(const Tr *t, u32 sid)
{
    return strings_text(&t->a.strings, sid);
}

static u32
child(const Tr *t, u32 i, u32 k)
{
    u32 c = i + 1;
    for (; k > 0; k--) {
        c = t->a.ends[c];
    }
    assert(c < t->a.ends[i]);
    return c;
}

static u32
nchildren(const Tr *t, u32 i)
{
    u32 n = 0;
    for (u32 c = i + 1; c < t->a.ends[i]; c = t->a.ends[c]) {
        n++;
    }
    return n;
}

static u32
child_of_kind(const Tr *t, u32 i, Kind k)
{
    for (u32 c = i + 1; c < t->a.ends[i]; c = t->a.ends[c]) {
        if (t->kinds[c] == k) {
            return c;
        }
    }
    return 0;
}

static Op
op(const Tr *t, u32 i)
{
    return (Op)t->op_of[t->a.details[i]];
}

static u32
id_slot(const Tr *t, u64 id)
{
    return (u32)((id * 0x9e3779b97f4a7c15ULL) >> 32) & t->map_mask;
}

static u32
node_of(const Tr *t, u64 id)
{
    for (u32 i = id_slot(t, id);; i = (i + 1) & t->map_mask) {
        if (t->map_ids[i] == id) {
            return t->map_nodes[i];
        }
        if (t->map_ids[i] == 0) {
            return 0;
        }
    }
}

static void
map_ids(Tr *t)
{
    u32 n = 1024;
    while (n < t->a.count * 2) {
        n *= 2;
    }
    t->map_ids = calloc(n, sizeof *t->map_ids);
    t->map_nodes = calloc(n, sizeof *t->map_nodes);
    t->map_mask = n - 1;

    for (u32 i = 0; i < t->a.count; i++) {
        u64 id = t->a.ids[i];
        if (id == 0) {
            continue;
        }
        u32 s = id_slot(t, id);
        while (t->map_ids[s] != 0 && t->map_ids[s] != id) {
            s = (s + 1) & t->map_mask;
        }
        t->map_ids[s] = id;
        t->map_nodes[s] = i;
    }
}

static bool
is_ident_byte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool
starts_word(const char *p, const char *w)
{
    size_t n = strlen(w);
    return strncmp(p, w, n) == 0 && !is_ident_byte(p[n]);
}

static bool
word_is(const char *w, u32 n, const char *s)
{
    return strlen(s) == n && memcmp(w, s, n) == 0;
}

static u32
word_length(const char *p)
{
    u32 n = 0;
    while (is_ident_byte(p[n])) {
        n++;
    }
    return n;
}

static bool
is_qualifier(const char *w, u32 n)
{
    return word_is(w, n, "const") || word_is(w, n, "volatile") || word_is(w, n, "restrict") ||
           word_is(w, n, "__restrict") || word_is(w, n, "_Nonnull") || word_is(w, n, "_Nullable") ||
           word_is(w, n, "_Null_unspecified") || word_is(w, n, "_Nullable_result");
}

static u32
intern_type(Tr *t, CType c, const u32 *params)
{
    for (u32 i = 1; i < t->ntypes; i++) {
        const CType *o = &t->types[i];
        if (o->kind == c.kind && o->bits == c.bits && o->is_signed == c.is_signed &&
            o->variadic == c.variadic && o->inner == c.inner &&
            o->count == c.count && o->decl == c.decl && o->nparams == c.nparams &&
            (c.nparams == 0 ||
             memcmp(t->params + o->first, params, c.nparams * sizeof *params) == 0)) {
            return i;
        }
    }

    if (c.nparams > 0) {
        t->params = grow(
            t->params,
            &t->params_cap,
            t->nparams + c.nparams,
            sizeof *t->params
        );
        memcpy(t->params + t->nparams, params, c.nparams * sizeof *params);
        c.first = t->nparams;
        t->nparams += c.nparams;
    }
    if (t->ntypes == 0) {
        t->types = grow(t->types, &t->types_cap, 1, sizeof *t->types);
        t->types[0] = (CType){ 0 };
        t->ntypes = 1;
    }
    t->types = grow(t->types, &t->types_cap, t->ntypes + 1, sizeof *t->types);
    t->types[t->ntypes] = c;
    return t->ntypes++;
}

static u32
simple_type(Tr *t, u8 kind, u8 bits, bool is_signed)
{
    return intern_type(t, (CType){ .kind = kind, .bits = bits, .is_signed = is_signed }, NULL);
}

static u32
pointer_to(Tr *t, u32 inner)
{
    return intern_type(t, (CType){ .kind = CT_POINTER, .inner = inner }, NULL);
}

typedef struct
{
    Tr *t;
    const char *p;
    u32 node;
} Spelling;

static NORETURN void
die_type(const Spelling *s, const char *msg)
{
    fprintf(
        stderr,
        "translate: %s:%u: %s at \"%s\" in \"%s\" (node %u)\n",
        text(s->t, s->t->a.files[s->node]),
        s->t->a.lines[s->node],
        msg,
        s->p,
        text(s->t, s->t->a.types[s->node]),
        s->node
    );
    exit(1);
}

static void
skip_spaces(Spelling *s)
{
    while (*s->p == ' ') {
        s->p++;
    }
}

static void
skip_attribute(Spelling *s)
{
    s->p += strlen("__attribute__");
    int depth = 0;
    do {
        if (*s->p == '(') {
            depth++;
        } else if (*s->p == ')') {
            depth--;
        } else if (*s->p == 0) {
            die_type(s, "unbalanced attribute in a type");
        }
        s->p++;
    } while (depth > 0);
}

static u32
attribute_lanes(Spelling *s)
{
    const char *p = s->p + strlen("__attribute__");
    const char *size = "((__vector_size__(";
    const char *ext = "((ext_vector_type(";
    u32 lanes = 0;
    if (strncmp(p, size, strlen(size)) == 0 || strncmp(p, ext, strlen(ext)) == 0) {
        p += strlen(size);
        while (*p >= '0' && *p <= '9') {
            lanes = lanes * 10 + (u32)(*p - '0');
            p++;
        }
    }
    skip_attribute(s);
    return lanes;
}

static u32 type_at(Spelling *s);
static u32 ctype_of_string(Tr *t, u32 sid, u32 node);

static u32
record_type(Spelling *s, const char *w, u32 n)
{
    Tr *t = s->t;
    u32 sid = strings_find(&t->a.strings, w, n);
    u32 decl = sid < t->nstrings ? t->records[sid] : 0;
    if (decl == 0) {
        return intern_type(t, (CType){ .kind = CT_OPAQUE }, NULL);
    }
    return intern_type(t, (CType){ .kind = CT_RECORD, .decl = decl }, NULL);
}

static u32
specifiers(Spelling *s)
{
    Tr *t = s->t;
    u32 longs = 0;
    bool is_unsigned = false;
    bool is_signed = false;
    bool is_short = false;
    bool is_char = false;
    bool is_int = false;
    u32 base = 0;
    u32 lanes = 0;

    for (;;) {
        skip_spaces(s);
        if (starts_word(s->p, "__attribute__")) {
            lanes = attribute_lanes(s);
            continue;
        }

        const char *w = s->p;
        while (is_ident_byte(*s->p)) {
            s->p++;
        }
        u32 n = (u32)(s->p - w);
        if (n == 0) {
            break;
        }

        if (is_qualifier(w, n)) {
            continue;
        }
        if (word_is(w, n, "unsigned")) {
            is_unsigned = true;
        } else if (word_is(w, n, "signed")) {
            is_signed = true;
        } else if (word_is(w, n, "long")) {
            longs++;
        } else if (word_is(w, n, "short")) {
            is_short = true;
        } else if (word_is(w, n, "int")) {
            is_int = true;
        } else if (word_is(w, n, "char")) {
            is_char = true;
        } else if (word_is(w, n, "void")) {
            base = simple_type(t, CT_VOID, 0, false);
        } else if (word_is(w, n, "_Bool") || word_is(w, n, "bool")) {
            base = simple_type(t, CT_BOOL, 8, false);
        } else if (word_is(w, n, "float")) {
            base = simple_type(t, CT_FLOAT, 32, true);
        } else if (word_is(w, n, "double")) {
            base = simple_type(t, CT_FLOAT, longs ? 128 : 64, true);
        } else if (word_is(w, n, "__int128")) {
            base = simple_type(t, CT_INT, 128, !is_unsigned);
        } else if (word_is(w, n, "typeof") || word_is(w, n, "__typeof__")) {
            skip_spaces(s);
            int depth = 0;
            do {
                depth += *s->p == '(' ? 1 : *s->p == ')' ? -1 : 0;
                s->p++;
            } while (depth > 0);
            base = intern_type(t, (CType){ .kind = CT_OPAQUE }, NULL);
        } else if (word_is(w, n, "__builtin_va_list")) {
            base = intern_type(t, (CType){ .kind = CT_VA_LIST }, NULL);
        } else if (word_is(w, n, "struct") || word_is(w, n, "union")) {
            skip_spaces(s);
            if (*s->p == '(') {
                die_type(s, "an unnamed record type in a type spelling");
            }
            const char *tag = s->p;
            while (is_ident_byte(*s->p)) {
                s->p++;
            }
            base = record_type(s, tag, (u32)(s->p - tag));
        } else if (word_is(w, n, "enum")) {
            skip_spaces(s);
            while (is_ident_byte(*s->p)) {
                s->p++;
            }
            base = simple_type(t, CT_INT, 32, false);
        } else {
            u32 sid = strings_find(&t->a.strings, w, n);
            u32 def = sid < t->nstrings ? t->typedefs[sid] : 0;
            if (def == 0) {
                die_type(s, "an unknown name in a type spelling");
            }
            base = ctype_of_string(t, t->a.types[def], def);
        }
    }

    if (base == 0 && is_char) {
        base = simple_type(t, CT_INT, 8, is_signed);
    } else if (base == 0 && is_short) {
        base = simple_type(t, CT_INT, 16, !is_unsigned);
    } else if (base == 0 && longs > 0) {
        base = simple_type(t, CT_INT, 64, !is_unsigned);
    } else if (base == 0 && (is_int || is_unsigned || is_signed)) {
        base = simple_type(t, CT_INT, 32, !is_unsigned);
    }
    if (base == 0) {
        die_type(s, "a type spelling with no type");
    }
    if (lanes == 0) {
        return base;
    }
    return intern_type(t, (CType){ .kind = CT_VECTOR, .inner = base, .count = lanes }, NULL);
}

static u32
suffixes(Spelling *s, u32 base)
{
    Tr *t = s->t;
    skip_spaces(s);
    if (*s->p == '[') {
        s->p++;
        u64 n = 0;
        while (*s->p >= '0' && *s->p <= '9') {
            n = n * 10 + (u64)(*s->p - '0');
            s->p++;
        }
        if (*s->p != ']') {
            die_type(s, "an array size that isn't a number");
        }
        s->p++;
        u32 inner = suffixes(s, base);
        return intern_type(t, (CType){ .kind = CT_ARRAY, .inner = inner, .count = n }, NULL);
    }
    if (*s->p != '(') {
        return base;
    }

    s->p++;
    u32 params[64];
    u32 n = 0;
    bool variadic = false;
    skip_spaces(s);
    while (*s->p != ')') {
        skip_spaces(s);
        if (strncmp(s->p, "...", 3) == 0) {
            s->p += 3;
            variadic = true;
        } else {
            if (n == 64) {
                die_type(s, "more than 64 parameters");
            }
            params[n++] = type_at(s);
        }
        skip_spaces(s);
        if (*s->p == ',') {
            s->p++;
        } else if (*s->p != ')') {
            die_type(s, "a parameter list that doesn't end");
        }
    }
    s->p++;
    if (n == 1 && t->types[params[0]].kind == CT_VOID) {
        n = 0;
    }
    return intern_type(
        t,
        (CType){ .kind = CT_FUNCTION, .inner = base, .variadic = variadic, .nparams = n },
        params
    );
}

static bool
nested_declarator(const char *p)
{
    p++;
    while (*p == ' ') {
        p++;
    }
    return *p == '*' || *p == '^';
}

static u32
declarator(Spelling *s, u32 base)
{
    for (;;) {
        skip_spaces(s);
        if (*s->p == '*') {
            s->p++;
            base = pointer_to(s->t, base);
        } else if (is_ident_byte(*s->p) && is_qualifier(s->p, word_length(s->p))) {
            s->p += word_length(s->p);
        } else if (starts_word(s->p, "__attribute__")) {
            skip_attribute(s);
        } else {
            break;
        }
    }

    skip_spaces(s);
    if (*s->p != '(' || !nested_declarator(s->p)) {
        return suffixes(s, base);
    }

    const char *inner = s->p + 1;
    int depth = 0;
    do {
        depth += *s->p == '(' ? 1 : *s->p == ')' ? -1 : 0;
        s->p++;
    } while (depth > 0);
    base = suffixes(s, base);

    const char *after = s->p;
    s->p = inner;
    u32 r = declarator(s, base);
    skip_spaces(s);
    if (*s->p != ')') {
        die_type(s, "a nested declarator that doesn't end");
    }
    s->p = after;
    return r;
}

static u32
type_at(Spelling *s)
{
    u32 base = specifiers(s);
    u32 r = declarator(s, base);
    for (;;) {
        skip_spaces(s);
        if (!starts_word(s->p, "__attribute__")) {
            return r;
        }
        skip_attribute(s);
    }
}

static u32
ctype_of_string(Tr *t, u32 sid, u32 node)
{
    if (sid == 0) {
        return 0;
    }
    assert(sid < t->nstrings);
    if (t->memo[sid] != 0) {
        return t->memo[sid];
    }

    Spelling s = { t, text(t, sid), node };
    u32 r = type_at(&s);
    skip_spaces(&s);
    if (*s.p != 0) {
        die(t, node, "a type spelling with text left over");
    }
    t->memo[sid] = r;
    return r;
}

static u32
ctype(Tr *t, u32 node)
{
    u32 sid = t->a.types[node];
    if (sid != 0 && t->a.desugared[node] != 0 && starts_word(text(t, sid), "typeof")) {
        sid = t->a.desugared[node];
    }
    return ctype_of_string(t, sid, node);
}

static bool
is_kind(const Tr *t, u32 type, u8 kind)
{
    return t->types[type].kind == kind;
}

static bool
project_file(const Tr *t, u32 file)
{
    return file != 0 && text(t, file)[0] != '/';
}

static u32
function_body(const Tr *t, u32 fn)
{
    return child_of_kind(t, fn, K_COMPOUND);
}

static u32
param_of(const Tr *t, u32 fn, u32 k)
{
    for (u32 c = fn + 1; c < t->a.ends[fn]; c = t->a.ends[c]) {
        if (t->kinds[c] == K_PARAM) {
            if (k == 0) {
                return c;
            }
            k--;
        }
    }
    return 0;
}

static u32
owner_of(const Tr *t, u32 fn)
{
    u32 c = t->canon[fn];
    return t->defs[c] ? t->defs[c] : c;
}

static u32
init_of(const Tr *t, u32 var)
{
    u32 last = 0;
    for (u32 c = var + 1; c < t->a.ends[var]; c = t->a.ends[c]) {
        u8 k = t->kinds[c];
        if (k != K_OTHER || strstr(text(t, t->a.kinds[c]), "Attr") == NULL) {
            last = c;
        }
    }
    return last;
}

static void
setup_kinds(Tr *t)
{
    static const char *kind_names[] = {
#define KIND_TEXT(name, s) s,
        CLANG_KINDS(KIND_TEXT)
#undef KIND_TEXT
    };
    static const char *op_names[] = {
#define OP_TEXT(name, s) s,
        CLANG_OPS(OP_TEXT)
#undef OP_TEXT
    };

    Strings *s = &t->a.strings;
    u32 ids[NOPS];
    for (u32 k = 0; k < NOPS - 1; k++) {
        ids[k] = strings_intern(s, op_names[k], (u32)strlen(op_names[k]));
    }
    u32 kind_ids[NKINDS];
    for (u32 k = 0; k < NKINDS - 2; k++) {
        kind_ids[k] = strings_intern(s, kind_names[k], (u32)strlen(kind_names[k]));
    }

    t->nstrings = s->count;
    t->kind_of = calloc(t->nstrings, 1);
    t->op_of = calloc(t->nstrings, 1);
    for (u32 i = 1; i < t->nstrings; i++) {
        t->kind_of[i] = K_OTHER;
    }
    for (u32 k = 0; k < NKINDS - 2; k++) {
        t->kind_of[kind_ids[k]] = (u8)(k + 2);
    }
    for (u32 k = 0; k < NOPS - 1; k++) {
        t->op_of[ids[k]] = (u8)(k + 1);
    }

    u32 n = t->a.count;
    t->kinds = malloc(n);
    for (u32 i = 0; i < n; i++) {
        t->kinds[i] = t->kind_of[t->a.kinds[i]];
    }
}

static bool
links_by_name(const Tr *t, u32 d)
{
    u32 storage = t->a.details[d];
    return (t->kinds[d] == K_FUNCTION || t->kinds[d] == K_VAR) &&
           (storage == 0 || strcmp(text(t, storage), "static") != 0);
}

static u64
link_hash(const Tr *t, u32 d, bool by_name)
{
    u64 h = by_name ? 0 : (u64)t->a.files[d] << 32 | t->a.offsets[d];
    h = (h ^ t->a.names[d]) * 0x9e3779b97f4a7c15ULL;
    return (h ^ t->kinds[d]) * 0x9e3779b97f4a7c15ULL;
}

static void
link_decl(Tr *t, u32 *table, u32 mask, u32 d, bool by_name)
{
    for (u32 s = (u32)(link_hash(t, d, by_name) >> 32) & mask;; s = (s + 1) & mask) {
        u32 e = table[s];
        if (e == 0) {
            table[s] = d + 1;
            return;
        }
        e--;
        if (t->kinds[e] == t->kinds[d] && t->a.names[e] == t->a.names[d] &&
            (by_name || (t->a.files[e] == t->a.files[d] &&
                         t->a.offsets[e] == t->a.offsets[d]))) {
            t->alias[d] = e;
            return;
        }
    }
}

static bool
linkable(u8 k)
{
    return k == K_FUNCTION || k == K_VAR || k == K_RECORD || k == K_FIELD ||
           k == K_ENUM || k == K_ENUM_CONSTANT || k == K_TYPEDEF;
}

static void
link_units(Tr *t)
{
    u32 n = t->a.count;
    t->alias = malloc(n * sizeof *t->alias);
    for (u32 i = 0; i < n; i++) {
        t->alias[i] = i;
    }

    u32 count = 0;
    for (u32 root = 0; root < n; root = t->a.ends[root]) {
        for (u32 c = root + 1; c < t->a.ends[root]; c = t->a.ends[c]) {
            count += linkable(t->kinds[c]);
            if (t->kinds[c] == K_RECORD || t->kinds[c] == K_ENUM) {
                count += t->a.ends[c] - c - 1;
            }
        }
    }
    u32 size = 1024;
    while (size < count * 2) {
        size *= 2;
    }
    u32 *by_place = calloc(size, sizeof *by_place);
    u32 *by_name = calloc(size, sizeof *by_name);

    for (u32 root = 0; root < n; root = t->a.ends[root]) {
        for (u32 c = root + 1; c < t->a.ends[root]; c = t->a.ends[c]) {
            if (!linkable(t->kinds[c])) {
                continue;
            }
            bool name = links_by_name(t, c);
            link_decl(t, name ? by_name : by_place, size - 1, c, name);
            if (t->kinds[c] != K_RECORD && t->kinds[c] != K_ENUM) {
                continue;
            }
            for (u32 m = c + 1; m < t->a.ends[c]; m = t->a.ends[m]) {
                if (t->kinds[m] == K_FIELD || t->kinds[m] == K_ENUM_CONSTANT) {
                    link_decl(t, by_place, size - 1, m, false);
                }
            }
        }
    }
    free(by_place);
    free(by_name);

    for (u32 i = 0; i < n; i++) {
        u64 id = t->a.ids[i];
        if (t->alias[i] == i || id == 0) {
            continue;
        }
        u32 s = id_slot(t, id);
        while (t->map_ids[s] != id) {
            s = (s + 1) & t->map_mask;
        }
        t->map_nodes[s] = t->alias[i];
    }
}

static void
setup_decls(Tr *t)
{
    u32 n = t->a.count;
    t->parents = calloc(n, sizeof *t->parents);
    t->canon = malloc(n * sizeof *t->canon);
    t->defs = calloc(n, sizeof *t->defs);
    t->bits = calloc(n, sizeof *t->bits);
    t->names = malloc(n * sizeof *t->names);
    t->marks = calloc(n, 1);
    t->narrowed = calloc(n, 1);
    t->memo = calloc(t->nstrings, sizeof *t->memo);
    t->typedefs = calloc(t->nstrings, sizeof *t->typedefs);
    t->records = calloc(t->nstrings, sizeof *t->records);

    for (u32 i = 0; i < n; i++) {
        for (u32 c = i + 1; c < t->a.ends[i]; c = t->a.ends[c]) {
            t->parents[c] = i;
        }
        t->names[i] = t->a.names[i];
        t->canon[i] = t->alias[i];
        u8 k = t->kinds[i];
        if ((k == K_FUNCTION || k == K_VAR) && t->a.refs[i] != 0) {
            u32 prev = node_of(t, t->a.refs[i]);
            if (prev == 0) {
                die(t, i, "a previous declaration that isn't in the AST");
            }
            t->canon[i] = t->canon[prev];
        }
        if (k == K_FUNCTION && function_body(t, i) != 0 && t->defs[t->canon[i]] == 0) {
            t->defs[t->canon[i]] = i;
        }
    }

    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] != K_FUNCTION) {
            continue;
        }
        u32 owner = owner_of(t, i);
        for (u32 k = 0;; k++) {
            u32 p = param_of(t, i, k);
            if (p == 0) {
                break;
            }
            u32 q = param_of(t, owner, k);
            t->canon[p] = q ? q : p;
        }
    }

    for (u32 i = 0; i < n; i++) {
        u32 name = t->a.names[i];
        if (t->alias[i] != i) {
            continue;
        }
        if (t->kinds[i] == K_RECORD && name != 0) {
            if (t->records[name] == 0 || (t->a.flags[i] & AST_COMPLETE)) {
                t->records[name] = i;
            }
        } else if (t->kinds[i] == K_TYPEDEF) {
            t->typedefs[name] = i;
            for (u32 c = i + 1; c < t->a.ends[i]; c = c + 1) {
                if (t->kinds[c] == K_ELABORATED_TYPE && t->a.refs[c] != 0) {
                    u32 r = node_of(t, t->a.refs[c]);
                    if (r != 0 && t->a.names[r] == 0) {
                        t->records[name] = r;
                        t->names[r] = name;
                    }
                }
            }
        }
    }

    for (u32 i = 0; i < n; i++) {
        const char *spelling = text(t, t->a.types[i]);
        if (t->kinds[i] != K_TYPEDEF || (strncmp(spelling, "struct ", 7) != 0 &&
                                         strncmp(spelling, "union ", 6) != 0)) {
            continue;
        }
        u32 ty = ctype(t, i);
        if (is_kind(t, ty, CT_RECORD)) {
            u32 r = t->types[ty].decl;
            if (t->names[r] == t->a.names[r] && t->a.names[r] != 0) {
                t->names[r] = t->a.names[i];
            }
        }
    }

    t->type_u8 = simple_type(t, CT_INT, 8, false);
    t->type_u64 = simple_type(t, CT_INT, 64, false);
    t->type_i64 = simple_type(t, CT_INT, 64, true);
    t->type_bool = simple_type(t, CT_BOOL, 8, false);
}

static u32
strip(const Tr *t, u32 e)
{
    while (t->kinds[e] == K_PAREN || t->kinds[e] == K_CONSTANT ||
           (t->kinds[e] == K_UNARY && op(t, e) == OP_EXTENSION)) {
        e = e + 1;
    }
    return e;
}

static bool
is_cast(const Tr *t, u32 e)
{
    return t->kinds[e] == K_IMPLICIT_CAST || t->kinds[e] == K_CAST;
}

static u32
strip_noop(const Tr *t, u32 e)
{
    e = strip(t, e);
    while (t->kinds[e] == K_IMPLICIT_CAST && op(t, e) == CAST_NOOP) {
        e = strip(t, e + 1);
    }
    return e;
}

static bool
is_null(const Tr *t, u32 e)
{
    e = strip(t, e);
    while (is_cast(t, e)) {
        Op c = op(t, e);
        if (c == CAST_NULL) {
            return true;
        }
        if (c != CAST_BIT && c != CAST_NOOP) {
            return false;
        }
        e = strip(t, e + 1);
    }
    return t->kinds[e] == K_OTHER &&
           strcmp(text(t, t->a.kinds[e]), "CXXNullPtrLiteralExpr") == 0;
}

static u32
decl_of(const Tr *t, u32 e)
{
    u32 d = node_of(t, t->a.refs[e]);
    if (d == 0) {
        die(t, e, "a reference to a declaration that isn't in the AST");
    }
    return t->canon[d];
}

static u32
callee(const Tr *t, u32 call)
{
    u32 f = strip(t, call + 1);
    while (is_cast(t, f)) {
        f = strip(t, f + 1);
    }
    if (t->kinds[f] != K_DECL_REF) {
        return 0;
    }
    u32 d = decl_of(t, f);
    return t->kinds[d] == K_FUNCTION ? d : 0;
}

static bool
is_pointer(Tr *t, u32 e)
{
    return is_kind(t, ctype(t, e), CT_POINTER);
}

static Site lvalue_site(Tr *t, u32 e);

static Site
value_site(Tr *t, u32 e)
{
    e = strip(t, e);
    if (is_cast(t, e)) {
        Op c = op(t, e);
        if (c == CAST_LVALUE) {
            return lvalue_site(t, e + 1);
        }
        if (c == CAST_NOOP) {
            return value_site(t, e + 1);
        }
        return (Site){ 0, 0 };
    }
    if (t->kinds[e] == K_CALL) {
        u32 f = callee(t, e);
        return (Site){ f, 0 };
    }
    if (t->kinds[e] == K_BINARY && op(t, e) == OP_ASSIGN) {
        return lvalue_site(t, e + 1);
    }
    return (Site){ 0, 0 };
}

static Site
lvalue_site(Tr *t, u32 e)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_DECL_REF: {
            u32 d = decl_of(t, e);
            u8 k = t->kinds[d];
            return k == K_VAR || k == K_PARAM ? (Site){ d, 0 } : (Site){ 0, 0 };
        }
        case K_MEMBER: {
            u32 f = node_of(t, t->a.refs[e]);
            return (Site){ f, 0 };
        }
        case K_UNARY: {
            if (op(t, e) != OP_MUL) {
                return (Site){ 0, 0 };
            }
            Site s = value_site(t, e + 1);
            s.level += s.decl != 0;
            return s;
        }
        case K_SUBSCRIPT: {
            u32 base = strip(t, e + 1);
            if (is_cast(t, base) && op(t, base) == CAST_ARRAY_DECAY) {
                return lvalue_site(t, base + 1);
            }
            Site s = value_site(t, base);
            s.level += s.decl != 0;
            return s;
        }
        default: return (Site){ 0, 0 };
    }
}

static u32
site_bits(const Tr *t, Site s)
{
    return s.decl ? t->bits[s.decl] >> (s.level * LEVEL_BITS) : 0;
}

static bool
set_site(Tr *t, Site s, u32 b)
{
    if (s.decl == 0 || s.level >= 16) {
        return false;
    }
    u32 m = b << (s.level * LEVEL_BITS);
    if ((t->bits[s.decl] & m) == m) {
        return false;
    }
    t->bits[s.decl] |= m;
    return true;
}

static bool
pointer_arith(Tr *t, u32 e)
{
    if (t->kinds[e] != K_BINARY && t->kinds[e] != K_COMPOUND_ASSIGN) {
        return false;
    }
    Op o = op(t, e);
    bool add = o == OP_ADD || o == OP_SUB || o == OP_ADD_ASSIGN || o == OP_SUB_ASSIGN;
    return add && is_pointer(t, e);
}

static bool
nullable(Tr *t, u32 e)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_IMPLICIT_CAST:
        case K_CAST: {
            Op c = op(t, e);
            if (c == CAST_NULL || c == CAST_INT_TO_POINTER) {
                return true;
            }
            if (c == CAST_LVALUE) {
                return site_bits(t, lvalue_site(t, e + 1)) & NULLABLE;
            }
            if (c == CAST_BIT || c == CAST_NOOP) {
                return nullable(t, e + 1);
            }
            return false;
        }
        case K_CALL: {
            u32 f = callee(t, e);
            return f == 0 || (t->bits[f] & NULLABLE);
        }
        case K_BINARY: {
            Op o = op(t, e);
            if (o == OP_ASSIGN || o == OP_COMMA) {
                return nullable(t, child(t, e, 1));
            }
            return false;
        }
        case K_CONDITIONAL:
            return nullable(t, child(t, e, 1)) || nullable(t, child(t, e, 2));
        default: return false;
    }
}

static bool
many_value(Tr *t, u32 e)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_IMPLICIT_CAST:
        case K_CAST: {
            Op c = op(t, e);
            if (c == CAST_ARRAY_DECAY) {
                return true;
            }
            if (c == CAST_LVALUE) {
                return site_bits(t, lvalue_site(t, e + 1)) & MANY;
            }
            if (c == CAST_BIT || c == CAST_NOOP) {
                return many_value(t, e + 1);
            }
            return false;
        }
        case K_CALL: {
            u32 f = callee(t, e);
            return f != 0 && (t->bits[f] & MANY);
        }
        case K_BINARY: return pointer_arith(t, e);
        case K_CONDITIONAL:
            return many_value(t, child(t, e, 1)) || many_value(t, child(t, e, 2));
        case K_STRING_LITERAL: return true;
        default: return false;
    }
}

static void
add_flow(Tr *t, Site to, u32 from, bool call)
{
    if (to.decl == 0) {
        return;
    }
    t->flows = grow(t->flows, &t->flows_cap, t->nflows + 1, sizeof *t->flows);
    t->flows[t->nflows++] = (Flow){ to, from, call };
}

static void zero_record(Tr *t, u32 record);

static void
zero_type(Tr *t, Site s, u32 type)
{
    const CType *c = &t->types[type];
    if (c->kind == CT_POINTER) {
        set_site(t, s, NULLABLE);
    } else if (c->kind == CT_ARRAY) {
        zero_type(t, s, c->inner);
    } else if (c->kind == CT_RECORD) {
        zero_record(t, c->decl);
    }
}

static void
zero_record(Tr *t, u32 record)
{
    if (t->marks[record] & MARK_ZEROED) {
        return;
    }
    t->marks[record] |= MARK_ZEROED;
    for (u32 c = record + 1; c < t->a.ends[record]; c = t->a.ends[c]) {
        if (t->kinds[c] == K_FIELD) {
            zero_type(t, (Site){ c, 0 }, ctype(t, c));
        }
    }
}

static u32
literal_of(const Tr *t, u32 e)
{
    e = strip(t, e);
    if (is_cast(t, e) && (op(t, e) == CAST_LVALUE || op(t, e) == CAST_NOOP) &&
        t->kinds[strip(t, e + 1)] == K_COMPOUND_LITERAL) {
        e = strip(t, e + 1);
    }
    if (t->kinds[e] == K_COMPOUND_LITERAL) {
        e = strip(t, e + 1);
    }
    return e;
}

static u8
allocation(Tr *t, u32 e)
{
    e = strip(t, e);
    while (is_cast(t, e) && (op(t, e) == CAST_BIT || op(t, e) == CAST_NOOP)) {
        e = strip(t, e + 1);
    }
    if (t->kinds[e] != K_CALL) {
        return ALLOC_NONE;
    }
    u32 f = callee(t, e);
    if (f == 0 || t->defs[t->canon[f]] != 0) {
        return ALLOC_NONE;
    }
    const char *name = text(t, t->a.names[f]);
    if (strcmp(name, "calloc") == 0) {
        return ALLOC_ZEROED;
    }
    if (strcmp(name, "malloc") == 0 || strcmp(name, "realloc") == 0) {
        return ALLOC_PLAIN;
    }
    return ALLOC_NONE;
}

static void
flow_init(Tr *t, Site to, u32 e, bool call)
{
    u32 list = literal_of(t, e);
    if (t->kinds[list] == K_IMPLICIT_VALUE_INIT) {
        zero_type(t, to, ctype(t, list));
        return;
    }
    if (t->kinds[list] != K_INIT_LIST) {
        const CType *c = &t->types[ctype(t, e)];
        if (c->kind == CT_POINTER && allocation(t, e) == ALLOC_ZEROED) {
            zero_type(t, (Site){ to.decl, to.level + 1 }, c->inner);
        }
        add_flow(t, to, e, call);
        return;
    }

    CType c = t->types[ctype(t, list)];
    if (c.kind == CT_RECORD) {
        return;
    }
    if (c.kind == CT_ARRAY) {
        for (u32 k = list + 1; k < t->a.ends[list]; k = t->a.ends[k]) {
            flow_init(t, to, k, false);
        }
        return;
    }
    flow_init(t, to, list + 1, false);
}

static bool
has_prefix(const Path *p, const Path *prefix)
{
    return prefix->len <= p->len &&
           memcmp(p->fields, prefix->fields, prefix->len * sizeof *p->fields) == 0;
}

static bool
killed(const Tr *t, const Path *p)
{
    for (u32 k = 0; k < t->nkills; k++) {
        if (has_prefix(p, &t->kills[k])) {
            return true;
        }
    }
    return false;
}

static void
zeroed_paths(Tr *t, u32 type)
{
    const CType *c = &t->types[type];
    if (c->kind == CT_POINTER) {
        if (t->nzeroed == MAX_PATHS) {
            t->overflow = true;
            return;
        }
        t->dead[t->nzeroed] = false;
        t->zeroed[t->nzeroed++] = t->zeroing;
    } else if (c->kind == CT_ARRAY) {
        zeroed_paths(t, c->inner);
    } else if (c->kind == CT_RECORD && t->zeroing.len < PATH_DEPTH) {
        for (u32 f = c->decl + 1; f < t->a.ends[c->decl]; f = t->a.ends[f]) {
            if (t->kinds[f] == K_FIELD) {
                t->zeroing.fields[t->zeroing.len++] = f;
                zeroed_paths(t, ctype(t, f));
                t->zeroing.len--;
            }
        }
    }
}

static bool
all_zero(const Tr *t, u32 list)
{
    for (u32 k = list + 1; k < t->a.ends[list]; k = t->a.ends[k]) {
        if (t->kinds[strip(t, k)] != K_IMPLICIT_VALUE_INIT && !is_null(t, k)) {
            return false;
        }
    }
    return true;
}

static void
zeroed_literal(Tr *t, u32 list)
{
    u32 decl = t->types[ctype(t, list)].decl;
    u32 end = t->a.ends[decl];
    u32 field = decl + 1;
    bool empty = all_zero(t, list);
    for (u32 k = list + 1; k < t->a.ends[list]; k = t->a.ends[k]) {
        while (field < end && t->kinds[field] != K_FIELD) {
            field = t->a.ends[field];
        }
        if (field >= end || t->zeroing.len == PATH_DEPTH) {
            return;
        }
        u32 l = literal_of(t, k);
        t->zeroing.fields[t->zeroing.len++] = field;
        if (empty || t->kinds[strip(t, k)] == K_IMPLICIT_VALUE_INIT) {
            zeroed_paths(t, ctype(t, field));
        } else if (t->kinds[l] == K_INIT_LIST && is_kind(t, ctype(t, l), CT_RECORD)) {
            zeroed_literal(t, l);
        }
        t->zeroing.len--;
        field = t->a.ends[field];
    }
}

static void
zero_path(Tr *t, u32 field, u32 type)
{
    const CType *c = &t->types[type];
    if (c->kind == CT_POINTER) {
        if (killed(t, &t->zeroing)) {
            t->marks[field] |= MARK_UNDEFINED;
        } else {
            set_site(t, (Site){ field, 0 }, NULLABLE);
        }
    } else if (c->kind == CT_ARRAY) {
        zero_path(t, field, c->inner);
    } else if (c->kind == CT_RECORD) {
        if (t->zeroing.len == PATH_DEPTH) {
            zero_record(t, c->decl);
            return;
        }
        for (u32 f = c->decl + 1; f < t->a.ends[c->decl]; f = t->a.ends[f]) {
            if (t->kinds[f] == K_FIELD) {
                t->zeroing.fields[t->zeroing.len++] = f;
                zero_path(t, f, ctype(t, f));
                t->zeroing.len--;
            }
        }
    }
}

static bool
pointer_root(Tr *t, u32 e, Access *a)
{
    e = strip(t, e);
    while (is_cast(t, e) && op(t, e) == CAST_NOOP) {
        e = strip(t, e + 1);
    }
    if (!is_cast(t, e) || op(t, e) != CAST_LVALUE) {
        return false;
    }
    e = strip(t, e + 1);
    if (t->kinds[e] != K_DECL_REF) {
        return false;
    }
    u32 d = decl_of(t, e);
    a->root = d;
    a->path.len = 0;
    return t->kinds[d] == K_VAR || t->kinds[d] == K_PARAM;
}

static bool
access_of(Tr *t, u32 e, Access *a)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_DECL_REF: {
            u32 d = decl_of(t, e);
            a->root = d;
            a->path.len = 0;
            return t->kinds[d] == K_VAR || t->kinds[d] == K_PARAM;
        }
        case K_UNARY: return op(t, e) == OP_MUL && pointer_root(t, e + 1, a);
        case K_MEMBER: {
            bool ok = (t->a.flags[e] & AST_ARROW) ? pointer_root(t, e + 1, a)
                                                  : access_of(t, e + 1, a);
            if (!ok || a->path.len == PATH_DEPTH) {
                return false;
            }
            a->path.fields[a->path.len++] = node_of(t, t->a.refs[e]);
            return true;
        }
        default: return false;
    }
}

static bool
relative(const Access *a, const Access *base, Path *rel)
{
    if (a->root != base->root || !has_prefix(&a->path, &base->path)) {
        return false;
    }
    rel->len = a->path.len - base->path.len;
    memcpy(rel->fields, a->path.fields + base->path.len, rel->len * sizeof *rel->fields);
    return true;
}

static bool
unkilled_under(const Tr *t, const Path *prefix)
{
    for (u32 k = 0; k < t->nzeroed; k++) {
        if (!t->dead[k] && has_prefix(&t->zeroed[k], prefix)) {
            return true;
        }
    }
    return false;
}

static bool
unkilled_field(const Tr *t, u32 field)
{
    for (u32 k = 0; k < t->nzeroed; k++) {
        const Path *p = &t->zeroed[k];
        if (!t->dead[k] && p->fields[p->len - 1] == field) {
            return true;
        }
    }
    return false;
}

static bool
frees(Tr *t, u32 call)
{
    u32 f = callee(t, call);
    return f != 0 && t->defs[t->canon[f]] == 0 &&
           strcmp(text(t, t->a.names[f]), "free") == 0;
}

static bool
reads_unkilled(Tr *t, u32 e, const Access *base)
{
    for (u32 i = e; i < t->a.ends[e];) {
        u8 k = t->kinds[i];
        Access a;
        Path rel;
        if (k == K_TYPE_TRAIT) {
            i = t->a.ends[i];
        } else if (k == K_CALL) {
            if (allocation(t, i) == ALLOC_NONE && !frees(t, i)) {
                return true;
            }
            i = t->a.ends[i + 1];
        } else if (is_cast(t, i) && op(t, i) == CAST_LVALUE &&
                   is_kind(t, ctype(t, i), CT_RECORD) &&
                   t->kinds[strip(t, i + 1)] != K_COMPOUND_LITERAL) {
            return true;
        } else if (k == K_MEMBER && access_of(t, i, &a)) {
            if (relative(&a, base, &rel)) {
                if (unkilled_under(t, &rel)) {
                    return true;
                }
            } else if (a.root != base->root &&
                       unkilled_field(t, a.path.fields[a.path.len - 1])) {
                return true;
            }
            i = t->a.ends[i];
        } else if (k == K_MEMBER) {
            if (unkilled_field(t, node_of(t, t->a.refs[i]))) {
                return true;
            }
            i++;
        } else if (k == K_DECL_REF && decl_of(t, i) == base->root) {
            return true;
        } else {
            i++;
        }
    }
    return false;
}

static void
add_kill(Tr *t, const Path *rel)
{
    if (t->nkills == MAX_PATHS) {
        return;
    }
    t->kills[t->nkills++] = *rel;
    for (u32 k = 0; k < t->nzeroed; k++) {
        if (has_prefix(&t->zeroed[k], rel)) {
            t->dead[k] = true;
        }
    }
}

static u32
zeroing_param(Tr *t, u32 def)
{
    u32 body = function_body(t, def);
    if (body == 0 || t->a.ends[body] == body + 1) {
        return 0;
    }
    u32 s = body + 1;
    if (t->kinds[s] != K_BINARY || op(t, s) != OP_ASSIGN ||
        t->kinds[literal_of(t, child(t, s, 1))] != K_INIT_LIST) {
        return 0;
    }
    u32 l = strip(t, child(t, s, 0));
    Access a;
    if (t->kinds[l] != K_UNARY || !access_of(t, l, &a) || t->kinds[a.root] != K_PARAM) {
        return 0;
    }
    for (u32 k = 0;; k++) {
        u32 p = param_of(t, def, k);
        if (p == 0) {
            return 0;
        }
        if (t->canon[p] == a.root) {
            return k + 1;
        }
    }
}

static bool
kills_by_call(Tr *t, u32 call, const Access *base)
{
    u32 f = callee(t, call);
    if (f != 0 && t->defs[t->canon[f]] == 0) {
        return !reads_unkilled(t, call, base);
    }
    u32 def = f != 0 ? t->defs[t->canon[f]] : 0;
    u32 z = def != 0 ? zeroing_param(t, def) : 0;
    if (z == 0) {
        return false;
    }

    Path rel = { 0 };
    bool found = false;
    u32 k = 0;
    for (u32 a = t->a.ends[call + 1]; a < t->a.ends[call]; a = t->a.ends[a], k++) {
        u32 x = strip(t, a);
        Access acc;
        if (k + 1 == z && t->kinds[x] == K_UNARY && op(t, x) == OP_AND &&
            access_of(t, x + 1, &acc) && relative(&acc, base, &rel) && rel.len != 0) {
            found = true;
        } else if (reads_unkilled(t, a, base)) {
            return false;
        }
    }
    u32 body = function_body(t, def);
    Access none = { 0 };
    if (!found || reads_unkilled(t, child(t, body + 1, 1), &none)) {
        return false;
    }
    add_kill(t, &rel);
    for (u32 s = t->a.ends[body + 1]; s < t->a.ends[body]; s = t->a.ends[s]) {
        if (reads_unkilled(t, s, &none)) {
            return false;
        }
    }
    return true;
}

static void
scan_kills(Tr *t, u32 event, const Access *base)
{
    u32 block = t->parents[event];
    if (t->kinds[block] != K_COMPOUND) {
        return;
    }
    u32 n = 0;
    for (u32 s = t->a.ends[event]; s < t->a.ends[block] && n < SCAN_LIMIT;
         s = t->a.ends[s], n++) {
        u8 k = t->kinds[s];
        Access a;
        Path rel;
        if (k == K_BINARY && op(t, s) == OP_ASSIGN &&
            access_of(t, child(t, s, 0), &a)) {
            if (reads_unkilled(t, child(t, s, 1), base) ||
                (a.root == base->root && has_prefix(&base->path, &a.path))) {
                return;
            }
            if (relative(&a, base, &rel)) {
                add_kill(t, &rel);
            }
        } else if (k == K_CALL) {
            if (!kills_by_call(t, s, base)) {
                return;
            }
        } else if ((k != K_BINARY && k != K_COMPOUND_ASSIGN && k != K_UNARY &&
                    k != K_DECL_STMT && k != K_NULL_STMT) ||
                   reads_unkilled(t, s, base)) {
            return;
        }
    }
}

static u32
literal_event(Tr *t, u32 list, Access *base, Path *prefix, u32 *top)
{
    prefix->len = 0;
    *top = list;
    u32 x = list;
    for (;;) {
        u32 p = t->parents[x];
        u8 k = t->kinds[p];
        if (k == K_PAREN || k == K_COMPOUND_LITERAL ||
            (is_cast(t, p) && (op(t, p) == CAST_NOOP || op(t, p) == CAST_LVALUE))) {
            x = p;
            continue;
        }
        if (k == K_INIT_LIST && is_kind(t, ctype(t, p), CT_RECORD)) {
            if (prefix->len == PATH_DEPTH) {
                return 0;
            }
            u32 field = t->types[ctype(t, p)].decl + 1;
            for (u32 c = p + 1;; c = t->a.ends[c]) {
                while (t->kinds[field] != K_FIELD) {
                    field = t->a.ends[field];
                }
                if (c == x) {
                    break;
                }
                field = t->a.ends[field];
            }
            u32 *fields = prefix->fields;
            memmove(fields + 1, fields, prefix->len * sizeof *fields);
            fields[0] = field;
            prefix->len++;
            *top = p;
            x = p;
            continue;
        }
        if (k == K_BINARY && op(t, p) == OP_ASSIGN && child(t, p, 1) == x &&
            access_of(t, child(t, p, 0), base)) {
            return p;
        }
        if (k == K_VAR && t->a.details[p] == 0 && t->kinds[t->parents[p]] == K_DECL_STMT &&
            nchildren(t, t->parents[p]) == 1) {
            base->root = t->canon[p];
            base->path.len = 0;
            return t->parents[p];
        }
        return 0;
    }
}

static void
zero_literal(Tr *t, u32 list)
{
    Access base;
    Path prefix;
    u32 top;
    u32 event = literal_event(t, list, &base, &prefix, &top);
    t->nzeroed = 0;
    t->nkills = 0;
    t->overflow = false;
    if (event != 0 && prefix.len < PATH_DEPTH) {
        t->zeroing.len = 0;
        zeroed_literal(t, top);
        if (!t->overflow) {
            scan_kills(t, event, &base);
        }
    }

    u32 decl = t->types[ctype(t, list)].decl;
    u32 field = decl + 1;
    bool empty = all_zero(t, list);
    for (u32 k = list + 1; k < t->a.ends[list]; k = t->a.ends[k]) {
        while (t->kinds[field] != K_FIELD) {
            field = t->a.ends[field];
        }
        if (empty || t->kinds[strip(t, k)] == K_IMPLICIT_VALUE_INIT) {
            if (prefix.len == PATH_DEPTH) {
                zero_type(t, (Site){ field, 0 }, ctype(t, field));
            } else {
                t->zeroing = prefix;
                t->zeroing.fields[t->zeroing.len++] = field;
                zero_path(t, field, ctype(t, field));
            }
        }
        field = t->a.ends[field];
    }
}

static void
flow_fields(Tr *t, u32 list)
{
    CType c = t->types[ctype(t, list)];
    if (c.kind != CT_RECORD) {
        return;
    }
    if (strcmp(text(t, t->a.details[c.decl]), "struct") != 0) {
        die(t, list, "an initializer for a union");
    }
    bool empty = all_zero(t, list);
    bool zeroes = false;
    u32 field = c.decl + 1;
    for (u32 k = list + 1; k < t->a.ends[list]; k = t->a.ends[k]) {
        while (field < t->a.ends[c.decl] && t->kinds[field] != K_FIELD) {
            field = t->a.ends[field];
        }
        if (field >= t->a.ends[c.decl]) {
            die(t, list, "an initializer with more values than fields");
        }
        if (empty || t->kinds[strip(t, k)] == K_IMPLICIT_VALUE_INIT) {
            zeroes = true;
        } else {
            flow_init(t, (Site){ field, 0 }, k, false);
        }
        field = t->a.ends[field];
    }
    if (zeroes) {
        zero_literal(t, list);
    }
}

static void
seed_nil(Tr *t, Site s)
{
    set_site(t, s, NULLABLE);
    if (s.decl != 0 && s.level == 0) {
        t->marks[s.decl] |= MARK_TOLERANT;
    }
}

static void
seed_condition(Tr *t, u32 e)
{
    e = strip(t, e);
    if (is_pointer(t, e)) {
        seed_nil(t, value_site(t, e));
    }
}

static bool
local_var(const Tr *t, u32 v)
{
    return t->kinds[v] == K_VAR && t->a.details[v] == 0 && t->parents[v] != 0 &&
           t->kinds[t->parents[v]] == K_DECL_STMT;
}

static u32
write_target(Tr *t, u32 i)
{
    u8 k = t->kinds[i];
    if ((k == K_BINARY && op(t, i) == OP_ASSIGN) || k == K_COMPOUND_ASSIGN ||
        (k == K_UNARY &&
         (op(t, i) == OP_INC || op(t, i) == OP_DEC || op(t, i) == OP_AND))) {
        return strip(t, i + 1);
    }
    return 0;
}

static void
scan_node(Tr *t, u32 i, u32 fn)
{
    u32 target = write_target(t, i);
    if (target != 0 && t->kinds[target] == K_DECL_REF &&
        t->kinds[decl_of(t, target)] == K_VAR) {
        t->marks[decl_of(t, target)] &= (u8)~MARK_DEMANDED;
    }

    switch (t->kinds[i]) {
        case K_VAR: {
            Site s = { i, 0 };
            if (t->a.flags[i] & AST_HAS_INIT) {
                flow_init(t, s, init_of(t, i), local_var(t, i));
            } else if (t->a.details[i] != 0 || t->parents[i] == 0 ||
                       t->kinds[t->parents[i]] == K_TRANSLATION_UNIT) {
                zero_type(t, s, ctype(t, i));
            }
            break;
        }
        case K_BINARY: {
            Op o = op(t, i);
            u32 l = child(t, i, 0);
            u32 r = child(t, i, 1);
            if (o == OP_ASSIGN) {
                flow_init(t, lvalue_site(t, l), r, false);
            } else if ((o == OP_EQ || o == OP_NE) && is_pointer(t, l)) {
                if (is_null(t, r)) {
                    seed_nil(t, value_site(t, l));
                } else if (is_null(t, l)) {
                    seed_nil(t, value_site(t, r));
                }
            } else if ((o == OP_LT || o == OP_GT || o == OP_LE || o == OP_GE) &&
                       is_pointer(t, l)) {
                set_site(t, value_site(t, l), MANY);
                set_site(t, value_site(t, r), MANY);
            } else if (o == OP_SUB && is_pointer(t, l) && is_pointer(t, r)) {
                set_site(t, value_site(t, l), MANY);
                set_site(t, value_site(t, r), MANY);
            } else if (pointer_arith(t, i)) {
                u32 parent = t->parents[i];
                while (t->kinds[parent] == K_PAREN) {
                    parent = t->parents[parent];
                }
                bool header = is_cast(t, parent) && op(t, parent) == CAST_BIT;
                if (!header) {
                    set_site(t, value_site(t, is_pointer(t, l) ? l : r), MANY);
                }
            } else if (o == OP_LAND || o == OP_LOR) {
                seed_condition(t, l);
                seed_condition(t, r);
            }
            break;
        }
        case K_COMPOUND_ASSIGN:
            if (pointer_arith(t, i)) {
                set_site(t, lvalue_site(t, i + 1), MANY);
            }
            break;
        case K_UNARY: {
            Op o = op(t, i);
            if ((o == OP_INC || o == OP_DEC) && is_pointer(t, i + 1)) {
                set_site(t, lvalue_site(t, i + 1), MANY);
            } else if (o == OP_NOT) {
                seed_condition(t, i + 1);
            }
            break;
        }
        case K_SUBSCRIPT: {
            u32 base = strip(t, i + 1);
            if (!(is_cast(t, base) && op(t, base) == CAST_ARRAY_DECAY)) {
                set_site(t, value_site(t, base), MANY);
            }
            break;
        }
        case K_CALL: {
            u32 f = callee(t, i);
            if (f == 0) {
                break;
            }
            u32 owner = owner_of(t, f);
            u32 k = 0;
            for (u32 a = t->a.ends[i + 1]; a < t->a.ends[i]; a = t->a.ends[a], k++) {
                u32 p = param_of(t, owner, k);
                if (p != 0) {
                    flow_init(t, (Site){ t->canon[p], 0 }, a, true);
                }
            }
            break;
        }
        case K_RETURN:
            if (t->a.ends[i] > i + 1 && fn != 0) {
                bool nonnull = t->marks[t->canon[fn]] & MARK_DEMANDED;
                flow_init(t, (Site){ t->canon[fn], 0 }, i + 1, nonnull);
            }
            break;
        case K_IMPLICIT_CAST:
            if (op(t, i) == CAST_POINTER_TO_BOOL) {
                seed_condition(t, i + 1);
            }
            break;
        case K_INIT_LIST: flow_fields(t, i); break;
        case K_IF:
        case K_WHILE:
        case K_CONDITIONAL: seed_condition(t, i + 1); break;
        case K_DO: seed_condition(t, child(t, i, 1)); break;
        case K_FOR: {
            u32 cond = child(t, i, 2);
            if (t->kinds[cond] != K_NONE) {
                seed_condition(t, cond);
            }
            break;
        }
        default: break;
    }
}

static bool
flows_nil(Tr *t, u32 e)
{
    u32 s = strip(t, e);
    while (is_cast(t, s) && (op(t, s) == CAST_BIT || op(t, s) == CAST_NOOP)) {
        s = strip(t, s + 1);
    }
    if (is_cast(t, s) && op(t, s) == CAST_LVALUE && t->narrowed[strip(t, s + 1)]) {
        return false;
    }
    return nullable(t, e);
}

static bool
apply_flow(Tr *t, Flow f)
{
    bool changed = false;
    bool demanded = f.call && f.to.level == 0 && (t->marks[f.to.decl] & MARK_DEMANDED);
    bool tolerant = f.to.level != 0 || (t->marks[f.to.decl] & MARK_TOLERANT);
    if (!demanded && (tolerant || allocation(t, f.from) == ALLOC_NONE) &&
        flows_nil(t, f.from)) {
        changed |= set_site(t, f.to, NULLABLE);
    }
    if (many_value(t, f.from)) {
        changed |= set_site(t, f.to, MANY);
    }

    Site from = value_site(t, f.from);
    if (from.decl == 0) {
        return changed;
    }
    if (site_bits(t, f.to) & MANY) {
        changed |= set_site(t, from, MANY);
    }
    for (u32 level = 1; level < 8; level++) {
        Site a = { f.to.decl, f.to.level + level };
        Site b = { from.decl, from.level + level };
        u32 x = site_bits(t, a) & 3;
        u32 y = site_bits(t, b) & 3;
        changed |= set_site(t, a, y);
        changed |= set_site(t, b, x);
    }
    return changed;
}

static bool
tolerate_site(Tr *t, Site s)
{
    if (s.decl == 0 || s.level != 0 || (t->marks[s.decl] & MARK_TOLERANT)) {
        return false;
    }
    t->marks[s.decl] |= MARK_TOLERANT;
    return true;
}

static bool
tolerate(Tr *t, u32 e)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_IMPLICIT_CAST:
        case K_CAST: {
            Op c = op(t, e);
            if (c == CAST_LVALUE) {
                return tolerate_site(t, lvalue_site(t, e + 1));
            }
            return (c == CAST_BIT || c == CAST_NOOP) && tolerate(t, e + 1);
        }
        case K_CALL: return tolerate_site(t, (Site){ callee(t, e), 0 });
        case K_BINARY: {
            Op o = op(t, e);
            if (o == OP_ASSIGN) {
                return tolerate_site(t, lvalue_site(t, child(t, e, 0)));
            }
            return o == OP_COMMA && tolerate(t, child(t, e, 1));
        }
        case K_CONDITIONAL: {
            bool a = tolerate(t, child(t, e, 1));
            bool b = tolerate(t, child(t, e, 2));
            return a || b;
        }
        default: return false;
    }
}

static void
find_tolerant(Tr *t)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (u32 k = 0; k < t->nflows; k++) {
            Flow f = t->flows[k];
            if (f.to.level != 0 || (t->marks[f.to.decl] & MARK_TOLERANT)) {
                changed |= tolerate(t, f.from);
            }
        }
    }
}

static bool
has_return(const Tr *t, u32 s)
{
    for (u32 i = s; i < t->a.ends[s]; i++) {
        if (t->kinds[i] == K_RETURN) {
            return true;
        }
    }
    return false;
}

static void
demand(Tr *t, u32 fn, u32 e)
{
    switch (t->kinds[e]) {
        case K_BINARY:
            if (op(t, e) == OP_LAND || op(t, e) == OP_LOR) {
                demand(t, fn, e + 1);
                return;
            }
            break;
        case K_CONDITIONAL: demand(t, fn, e + 1); return;
        case K_TYPE_TRAIT: return;
        default: break;
    }

    Site s = { 0, 0 };
    if (t->kinds[e] == K_MEMBER && (t->a.flags[e] & AST_ARROW)) {
        s = value_site(t, e + 1);
    } else if (t->kinds[e] == K_UNARY && op(t, e) == OP_MUL) {
        s = value_site(t, e + 1);
    } else if (t->kinds[e] == K_SUBSCRIPT) {
        u32 base = strip(t, e + 1);
        if (!(is_cast(t, base) && op(t, base) == CAST_ARRAY_DECAY)) {
            s = value_site(t, base);
        }
    }
    if (s.decl != 0 && s.level == 0 &&
        (t->kinds[s.decl] == K_PARAM ||
         (local_var(t, s.decl) && (t->a.flags[s.decl] & AST_HAS_INIT)))) {
        t->marks[s.decl] |= MARK_DEMANDED;
    }
    for (u32 c = e + 1; c < t->a.ends[e]; c = t->a.ends[c]) {
        demand(t, fn, c);
    }
}

static void
demand_params(Tr *t, u32 fn)
{
    u32 body = function_body(t, fn);
    for (u32 s = body + 1; s < t->a.ends[body]; s = t->a.ends[s]) {
        u8 k = t->kinds[s];
        if (k == K_IF || k == K_WHILE || k == K_SWITCH) {
            demand(t, fn, s + 1);
        } else if (k == K_RETURN) {
            if (t->a.ends[s] > s + 1) {
                demand(t, fn, s + 1);
            }
            return;
        } else if (k == K_DECL_STMT || (k != K_FOR && k != K_DO && k != K_COMPOUND)) {
            demand(t, fn, s);
        }
        if (has_return(t, s)) {
            return;
        }
    }
}

static u32
read_local(Tr *t, u32 e)
{
    e = strip(t, e);
    if (!is_cast(t, e) || op(t, e) != CAST_LVALUE || !is_pointer(t, e)) {
        return 0;
    }
    e = strip(t, e + 1);
    if (t->kinds[e] != K_DECL_REF) {
        return 0;
    }
    u32 d = decl_of(t, e);
    bool local = local_var(t, d) && (t->a.flags[d] & AST_HAS_INIT);
    return local || t->kinds[d] == K_PARAM ? d : 0;
}

static u32
not_nil_when(Tr *t, u32 e, bool when, u32 *out, u32 n)
{
    e = strip(t, e);
    if (t->kinds[e] == K_UNARY && op(t, e) == OP_NOT) {
        return not_nil_when(t, e + 1, !when, out, n);
    }
    if (is_cast(t, e) && op(t, e) == CAST_POINTER_TO_BOOL) {
        e = strip(t, e + 1);
    }

    u32 x = 0;
    if (t->kinds[e] == K_BINARY) {
        Op o = op(t, e);
        if ((o == OP_LAND && when) || (o == OP_LOR && !when)) {
            n = not_nil_when(t, child(t, e, 0), when, out, n);
            return not_nil_when(t, child(t, e, 1), when, out, n);
        }
        if ((o == OP_EQ || o == OP_NE) && (o == OP_NE) == when) {
            u32 l = child(t, e, 0);
            u32 r = child(t, e, 1);
            x = is_null(t, r) ? l : is_null(t, l) ? r : 0;
        }
    } else if (when) {
        x = e;
    }
    u32 d = x != 0 ? read_local(t, x) : 0;
    if (d != 0 && n < NARROW_MAX) {
        out[n++] = d;
    }
    return n;
}

static bool
written(Tr *t, u32 fn, u32 d)
{
    for (u32 i = fn; i < t->a.ends[fn]; i++) {
        u32 target = write_target(t, i);
        if (target != 0 && t->kinds[target] == K_DECL_REF && decl_of(t, target) == d) {
            return true;
        }
    }
    return false;
}

static void
mark_narrowed(Tr *t, u32 fn, u32 from, u32 end, const u32 *vars, u32 n)
{
    for (u32 k = 0; k < n; k++) {
        if (written(t, fn, vars[k])) {
            continue;
        }
        for (u32 i = from; i < end; i++) {
            if (t->kinds[i] == K_DECL_REF && decl_of(t, i) == vars[k]) {
                t->narrowed[i] = 1;
            }
        }
    }
}

static bool
terminal(Tr *t, u32 s)
{
    switch (t->kinds[s]) {
        case K_BREAK:
        case K_CONTINUE:
        case K_RETURN: return true;
        case K_COMPOUND: {
            u32 last = 0;
            for (u32 c = s + 1; c < t->a.ends[s]; c = t->a.ends[c]) {
                last = c;
            }
            return last != 0 && terminal(t, last);
        }
        case K_IF:
            return (t->a.flags[s] & AST_HAS_ELSE) &&
                   terminal(t, child(t, s, 1)) && terminal(t, child(t, s, 2));
        default: return false;
    }
}

static u32
narrowing_end(Tr *t, u32 s)
{
    u32 block = t->parents[s];
    u32 end = t->a.ends[s];
    if (t->kinds[block] != K_COMPOUND) {
        return end;
    }
    while (end < t->a.ends[block] && t->kinds[end] != K_CASE &&
           t->kinds[end] != K_DEFAULT) {
        end++;
    }
    return end;
}

static void
narrow_reads(Tr *t, u32 fn)
{
    u32 vars[NARROW_MAX];
    for (u32 i = fn; i < t->a.ends[fn]; i++) {
        if (t->kinds[i] == K_IF) {
            u32 then = child(t, i, 1);
            u32 n = not_nil_when(t, i + 1, true, vars, 0);
            mark_narrowed(t, fn, then, t->a.ends[then], vars, n);

            n = not_nil_when(t, i + 1, false, vars, 0);
            if (t->a.flags[i] & AST_HAS_ELSE) {
                u32 other = child(t, i, 2);
                mark_narrowed(t, fn, other, t->a.ends[other], vars, n);
            } else if (terminal(t, then)) {
                mark_narrowed(t, fn, t->a.ends[i], narrowing_end(t, i), vars, n);
            }
        } else if (t->kinds[i] == K_BINARY &&
                   (op(t, i) == OP_LAND || op(t, i) == OP_LOR)) {
            u32 right = child(t, i, 1);
            u32 n = not_nil_when(t, child(t, i, 0), op(t, i) == OP_LAND, vars, 0);
            mark_narrowed(t, fn, right, t->a.ends[right], vars, n);
        }
    }
}

static void
analyze(Tr *t)
{
    u32 n = t->a.count;
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] != K_FUNCTION || t->canon[i] != i) {
            continue;
        }
        if (t->defs[i] == 0) {
            t->bits[i] |= ALL_NULLABLE;
            for (u32 k = 0;; k++) {
                u32 p = param_of(t, i, k);
                if (p == 0) {
                    break;
                }
                t->bits[p] |= ALL_NULLABLE;
            }
        }
    }

    u32 fn = 0;
    u32 fn_end = 0;
    for (u32 i = 0; i < n; i++) {
        if (i >= fn_end) {
            fn = 0;
        }
        if (t->kinds[i] == K_FUNCTION && function_body(t, i) != 0) {
            if (t->defs[t->canon[i]] != i) {
                i = t->a.ends[i] - 1;
                continue;
            }
            fn = i;
            fn_end = t->a.ends[i];
            if (project_file(t, t->a.files[i])) {
                demand_params(t, i);
                narrow_reads(t, i);
            }
            if (child_of_kind(t, i, K_RETURNS_NONNULL) != 0) {
                t->marks[t->canon[i]] |= MARK_DEMANDED;
            }
        }
        if (!project_file(t, t->a.files[i])) {
            continue;
        }
        scan_node(t, i, fn);
    }

    find_tolerant(t);
    for (bool changed = true; changed;) {
        changed = false;
        for (u32 k = 0; k < t->nflows; k++) {
            changed |= apply_flow(t, t->flows[k]);
        }
    }
}

static void
put_type(Tr *t, Out *o, u32 type, u32 bits)
{
    const CType *c = &t->types[type];
    switch (c->kind) {
        case CT_VOID: put(o, "void"); break;
        case CT_BOOL: put(o, "bool"); break;
        case CT_INT:
            if (c->bits == 64 && c->is_signed) {
                put(o, "int");
            } else {
                putf(o, "%c%u", c->is_signed ? 'i' : 'u', c->bits);
            }
            break;
        case CT_FLOAT: putf(o, "f%u", c->bits); break;
        case CT_VA_LIST: put(o, "int"); break;
        case CT_POINTER: {
            const CType *in = &t->types[c->inner];
            if (in->kind == CT_FUNCTION) {
                put(o, bits & NULLABLE ? "?extern fn(" : "extern fn(");
                for (u32 k = 0; k < in->nparams; k++) {
                    put(o, k == 0 ? "" : ", ");
                    put_type(t, o, t->params[in->first + k], 0);
                }
                put(o, ")");
                if (t->types[in->inner].kind != CT_VOID) {
                    put(o, " -> ");
                    put_type(t, o, in->inner, 0);
                }
                break;
            }
            put(o, bits & NULLABLE ? "?" : "");
            put(o, bits & MANY ? "[*]" : "*");
            if (in->kind == CT_VOID || in->kind == CT_OPAQUE ||
                (in->kind == CT_RECORD && !project_file(t, t->a.files[in->decl]))) {
                put(o, "u8");
            } else {
                put_type(t, o, c->inner, bits >> LEVEL_BITS);
            }
            break;
        }
        case CT_ARRAY:
        case CT_VECTOR:
            putf(o, "[%llu]", (unsigned long long)c->count);
            put_type(t, o, c->inner, bits);
            break;
        case CT_RECORD: put(o, text(t, t->names[c->decl])); break;
        default: put(o, "?"); break;
    }
}

static char *
type_text(Tr *t, u32 type, u32 bits)
{
    Out o = { 0 };
    put_type(t, &o, type, bits);
    put_bytes(&o, "", 1);
    return o.text;
}

static bool
same_chibi_type(Tr *t, u32 a, u32 abits, u32 b, u32 bbits)
{
    char *x = type_text(t, a, abits);
    char *y = type_text(t, b, bbits);
    bool same = strcmp(x, y) == 0;
    free(x);
    free(y);
    return same;
}

static void
line(Tr *t)
{
    put(&t->out, "\n");
    for (u32 i = 0; i < t->depth; i++) {
        put(&t->out, "    ");
    }
}

static void
put_name(Tr *t, u32 decl)
{
    put(&t->out, text(t, t->names[decl]));
}

static void
wrap(Tr *t, u64 at, const char *before, const char *after)
{
    insert(&t->out, at, before, strlen(before));
    put(&t->out, after);
}

static Val
parens(Tr *t, u64 at, Val v, u8 need)
{
    if (v.prec < need) {
        wrap(t, at, "(", ")");
        v.prec = P_POSTFIX;
    }
    return v;
}

static Val
unwrap(Tr *t, u64 at, Val v)
{
    v = parens(t, at, v, P_POSTFIX);
    put(&t->out, "!!");
    v.bits &= ~(u32)NULLABLE;
    return v;
}

static Val
bitcast_to(Tr *t, u64 at, u32 type, u32 bits)
{
    char *ty = type_text(t, type, bits);
    Out head = { 0 };
    putf(&head, "bitcast(%s, ", ty);
    insert(&t->out, at, head.text, head.len);
    put(&t->out, ")");
    free(ty);
    free(head.text);
    return value(type, bits, P_POSTFIX);
}

static Val
adapt(Tr *t, u64 at, Val v, u32 type, u32 bits)
{
    const CType *c = &t->types[type];
    if (c->kind != CT_POINTER || v.nil || v.untyped || !is_kind(t, v.type, CT_POINTER)) {
        return v;
    }
    if ((v.bits & NULLABLE) && !(bits & NULLABLE)) {
        v = unwrap(t, at, v);
    }
    u32 have = v.bits & ~(u32)NULLABLE;
    u32 want = bits & ~(u32)NULLABLE;
    if (same_chibi_type(t, v.type, have, type, want)) {
        v.bits = (v.bits & NULLABLE) | want;
        return v;
    }
    return bitcast_to(t, at, type, bits);
}

static bool
has_effects(const Tr *t, u32 root)
{
    for (u32 i = root; i < t->a.ends[root]; i++) {
        u8 k = t->kinds[i];
        if (k == K_CALL || k == K_COMPOUND_ASSIGN ||
            (k == K_BINARY && op(t, i) == OP_ASSIGN) ||
            (k == K_UNARY && (op(t, i) == OP_INC || op(t, i) == OP_DEC))) {
            return true;
        }
    }
    return false;
}

static bool
is_condition(Tr *t, u32 e)
{
    e = strip(t, e);
    if (t->kinds[e] == K_UNARY) {
        return op(t, e) == OP_NOT;
    }
    if (t->kinds[e] != K_BINARY) {
        return is_kind(t, ctype(t, e), CT_BOOL);
    }
    if (is_kind(t, ctype(t, e), CT_VECTOR)) {
        return false;
    }

    Op o = op(t, e);
    if (o == OP_OR || o == OP_AND) {
        return is_condition(t, child(t, e, 0)) &&
               is_condition(t, child(t, e, 1)) && !has_effects(t, child(t, e, 1));
    }
    return o == OP_LAND || o == OP_LOR || o == OP_EQ || o == OP_NE ||
           o == OP_LT || o == OP_GT || o == OP_LE || o == OP_GE;
}

static Val
bool_to_int(Tr *t, u64 at, u32 type)
{
    insert(&t->out, at, "if ", 3);
    char *ty = type_text(t, type, 0);
    putf(&t->out, " then %s(1) else %s(0)", ty, ty);
    free(ty);
    return value(type, 0, P_LOWEST);
}

static Val ex(Tr *t, u32 e, u32 type, u32 bits);
static u8 cond(Tr *t, u32 e);
static u8 cond_at(Tr *t, u32 e, bool negate);

static Val
operand(Tr *t, u32 e, u8 need)
{
    u64 at = t->out.len;
    Val v = ex(t, e, 0, 0);
    if (v.boolean && is_kind(t, ctype(t, e), CT_INT)) {
        v = bool_to_int(t, at, ctype(t, e));
    }
    return parens(t, at, v, need);
}

static Val
value_as(Tr *t, u32 e, u32 type, u32 bits)
{
    u64 at = t->out.len;
    Val v = ex(t, e, type, bits);
    if (v.boolean && is_kind(t, type, CT_INT)) {
        return bool_to_int(t, at, type);
    }
    return adapt(t, at, v, type, bits);
}

static u32
decl_bits(const Tr *t, u32 decl)
{
    return t->bits[t->canon[decl]];
}

static __int128
parse_value(const char *s)
{
    bool negative = *s == '-';
    s += negative;
    __int128 n = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        n = n * 10 + (*s - '0');
    }
    return negative ? -n : n;
}

static bool
is_enum_constant(const Tr *t, u32 e)
{
    return t->kinds[e] == K_DECL_REF && t->kinds[decl_of(t, e)] == K_ENUM_CONSTANT;
}

static __int128
enum_value(const Tr *t, u32 d)
{
    u32 e = t->parents[d];
    __int128 v = 0;
    for (u32 c = e + 1; c < t->a.ends[e]; c = t->a.ends[c]) {
        if (t->kinds[c] != K_ENUM_CONSTANT) {
            continue;
        }
        if (t->a.ends[c] > c + 1) {
            u32 init = c + 1;
            while (t->kinds[init] == K_IMPLICIT_CAST) {
                init = init + 1;
            }
            if (t->kinds[init] != K_CONSTANT) {
                die(t, c, "an enum constant whose value clang didn't compute");
            }
            v = parse_value(text(t, t->a.details[init]));
        }
        if (c == d) {
            return v;
        }
        v++;
    }
    die(t, d, "an enum constant outside its enum");
}

static __int128
wrap_int(const CType *c, __int128 v)
{
    if (c->kind == CT_BOOL) {
        return v != 0;
    }
    if (c->kind != CT_INT || c->bits >= 128) {
        return v;
    }
    u64 m = c->bits == 64 ? ~0ull : (1ull << c->bits) - 1;
    u64 b = (u64)v & m;
    if (c->is_signed && (b >> (c->bits - 1)) & 1) {
        return (__int128)b - ((__int128)1 << c->bits);
    }
    return b;
}

static bool
fold_binary(Op o, __int128 a, __int128 b, u32 bits, __int128 *r)
{
    unsigned __int128 ua = (unsigned __int128)a;
    switch (o) {
        case OP_ADD: *r = a + b; return true;
        case OP_SUB: *r = a - b; return true;
        case OP_MUL: *r = (__int128)(ua * (unsigned __int128)b); return true;
        case OP_DIV: *r = b != 0 ? a / b : 0; return b != 0;
        case OP_MOD: *r = b != 0 ? a % b : 0; return b != 0;
        case OP_SHL:
            *r = (__int128)(ua << (b & 127));
            return b >= 0 && b < bits;
        case OP_SHR: *r = a >> (b & 127); return b >= 0 && b < bits;
        case OP_AND: *r = a & b; return true;
        case OP_OR: *r = a | b; return true;
        case OP_XOR: *r = a ^ b; return true;
        case OP_EQ: *r = a == b; return true;
        case OP_NE: *r = a != b; return true;
        case OP_LT: *r = a < b; return true;
        case OP_GT: *r = a > b; return true;
        case OP_LE: *r = a <= b; return true;
        case OP_GE: *r = a >= b; return true;
        case OP_LAND: *r = a && b; return true;
        case OP_LOR: *r = a || b; return true;
        default: return false;
    }
}

static bool
constant_value(Tr *t, u32 e, __int128 *v)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_INT_LITERAL:
            *v = parse_value(text(t, t->a.details[e]));
            return true;
        case K_CHAR_LITERAL:
            *v = wrap_int(&t->types[ctype(t, e)], parse_value(text(t, t->a.details[e])));
            return true;
        case K_BOOL_LITERAL:
            *v = strcmp(text(t, t->a.details[e]), "true") == 0;
            return true;
        case K_DECL_REF:
            if (!is_enum_constant(t, e)) {
                return false;
            }
            *v = enum_value(t, decl_of(t, e));
            return true;
        case K_UNARY: {
            __int128 x;
            Op o = op(t, e);
            if ((o != OP_SUB && o != OP_BIT_NOT && o != OP_NOT) ||
                !constant_value(t, e + 1, &x)) {
                return false;
            }
            __int128 r = o == OP_SUB ? -x : o == OP_BIT_NOT ? ~x : !x;
            *v = wrap_int(&t->types[ctype(t, e)], r);
            return true;
        }
        case K_BINARY: {
            __int128 a, b, r;
            u32 l = child(t, e, 0);
            if (!constant_value(t, l, &a) || !constant_value(t, child(t, e, 1), &b)) {
                return false;
            }
            CType c = t->types[ctype(t, e)];
            if (!fold_binary(op(t, e), a, b, t->types[ctype(t, l)].bits, &r)) {
                return false;
            }
            *v = wrap_int(&c, r);
            return true;
        }
        case K_CONDITIONAL: {
            __int128 c;
            return constant_value(t, child(t, e, 0), &c) &&
                   constant_value(t, child(t, e, c ? 1 : 2), v);
        }
        case K_IMPLICIT_CAST:
        case K_CAST: {
            Op c = op(t, e);
            __int128 x;
            if ((c != CAST_INTEGRAL && c != CAST_INT_TO_BOOL && c != CAST_NOOP) ||
                !constant_value(t, e + 1, &x)) {
                return false;
            }
            const CType *ct = &t->types[ctype(t, e)];
            if (ct->kind != CT_INT && ct->kind != CT_BOOL) {
                return false;
            }
            *v = wrap_int(ct, x);
            return true;
        }
        default: return false;
    }
}

static void
put_int(Tr *t, __int128 v)
{
    if (v < 0) {
        put(&t->out, "-");
        v = -v;
    }
    u64 u = (u64)v;
    if (u > 0xffff && ((u & (u + 1)) == 0 || u >= 1ull << 31)) {
        putf(&t->out, "0x%llX", (unsigned long long)u);
    } else {
        putf(&t->out, "%llu", (unsigned long long)u);
    }
}

static void
put_char(Tr *t, __int128 v)
{
    static const char *const escapes[128] = {
        [0] = "\\0",    ['\b'] = "\\b", ['\t'] = "\\t", ['\n'] = "\\n",
        ['\f'] = "\\f", ['\r'] = "\\r", ['\''] = "\\'", ['\\'] = "\\\\",
    };
    if (escapes[v] != NULL) {
        putf(&t->out, "'%s'", escapes[v]);
    } else if (v < 0x20 || v == 0x7F) {
        putf(&t->out, "'\\x%02X'", (unsigned)v);
    } else {
        putf(&t->out, "'%c'", (char)v);
    }
}

static void
put_number(Tr *t, u32 e, __int128 v)
{
    __int128 c;
    if (t->kinds[e] == K_CHAR_LITERAL && constant_value(t, e, &c) && c == v &&
        v >= 0 && v < 128) {
        put_char(t, v);
    } else {
        put_int(t, v);
    }
}

static void
put_float(Tr *t, const char *s)
{
    double d = strtod(s, NULL);
    if (d >= 0x1p128 || d != (double)(unsigned __int128)d) {
        put(&t->out, s);
        return;
    }

    char digits[40];
    u32 n = 0;
    unsigned __int128 u = (unsigned __int128)d;
    do {
        digits[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u != 0);
    while (n > 0) {
        n--;
        put_bytes(&t->out, &digits[n], 1);
    }
    put(&t->out, ".0");
}

static Val
int_type_val(u32 type)
{
    return value(type, 0, P_POSTFIX);
}

static bool
constant_leaves(Tr *t, u32 e)
{
    __int128 v;
    if (constant_value(t, e, &v)) {
        return true;
    }
    e = strip(t, e);
    return t->kinds[e] == K_CONDITIONAL && constant_leaves(t, child(t, e, 1)) &&
           constant_leaves(t, child(t, e, 2));
}

static void
put_value(Tr *t, u32 e, __int128 v)
{
    u32 s = strip(t, e);
    while (is_cast(t, s)) {
        s = strip(t, s + 1);
    }
    if (is_enum_constant(t, s) && enum_value(t, decl_of(t, s)) == v) {
        ex(t, s, 0, 0);
    } else {
        put_number(t, s, v);
    }
}

static Val
put_constant(Tr *t, u32 e, u32 type)
{
    __int128 v;
    if (!constant_value(t, e, &v)) {
        return value_as(t, e, type, 0);
    }
    if (is_kind(t, type, CT_BOOL)) {
        put(&t->out, v ? "true" : "false");
        return (Val){ type, 0, P_POSTFIX, false, false, true };
    }
    put_value(t, e, v);
    return (Val){ type, 0, v < 0 ? P_UNARY : P_POSTFIX, true, false, false };
}

static void
put_leaves(Tr *t, u32 e, u32 type, bool typed)
{
    __int128 v;
    if (constant_value(t, e, &v)) {
        char *ty = typed ? type_text(t, type, 0) : NULL;
        putf(&t->out, "%s%s", typed ? ty : "", typed ? "(" : "");
        put_value(t, e, wrap_int(&t->types[type], v));
        put(&t->out, typed ? ")" : "");
        free(ty);
        return;
    }
    e = strip(t, e);
    u32 second = child(t, e, 2);
    put(&t->out, "if ");
    cond(t, child(t, e, 0));
    put(&t->out, " then ");
    put_leaves(t, child(t, e, 1), type, typed);
    put(&t->out, " else ");
    put_leaves(t, second, type, typed && t->kinds[strip(t, second)] == K_CONDITIONAL);
}

static Val
integral_cast(Tr *t, u32 e, bool explicit_cast, u32 want_type)
{
    u32 x = e + 1;
    u32 to = ctype(t, e);
    u32 from = ctype(t, x);
    CType g = t->types[to];
    CType f = t->types[from];

    __int128 value;
    if (constant_value(t, e, &value)) {
        __int128 exact;
        bool same = f.kind == CT_INT && constant_value(t, x, &exact) && exact == value;
        if (!explicit_cast && same) {
            return ex(t, x, 0, 0);
        }
        if (explicit_cast) {
            char *ty = type_text(t, to, 0);
            putf(&t->out, "%s(", ty);
            free(ty);
            if (same && is_enum_constant(t, strip(t, x))) {
                ex(t, x, 0, 0);
            } else {
                put_number(t, strip(t, x), value);
            }
            put(&t->out, ")");
            return int_type_val(to);
        }
        put_int(t, value);
        return (Val){ to, 0, value < 0 ? P_UNARY : P_POSTFIX, true, false, false };
    }

    if (f.kind == CT_BOOL || is_condition(t, x)) {
        u64 at = t->out.len;
        cond(t, x);
        return bool_to_int(t, at, to);
    }
    if (constant_leaves(t, x)) {
        if (!explicit_cast) {
            put_leaves(t, x, to, want_type != to);
            return (Val){ to, 0, P_LOWEST, want_type == to, false, false };
        }
        char *ty = type_text(t, to, 0);
        putf(&t->out, "%s(", ty);
        free(ty);
        put_leaves(t, x, to, false);
        put(&t->out, ")");
        return int_type_val(to);
    }
    if (f.kind != CT_INT || g.kind != CT_INT) {
        die(t, e, "an integral cast between types that aren't integers");
    }

    if (f.bits == g.bits && f.is_signed == g.is_signed) {
        return ex(t, x, 0, 0);
    }

    char *ty = type_text(t, to, 0);
    Val v;
    if (g.bits > f.bits && (f.is_signed == g.is_signed || !f.is_signed)) {
        putf(&t->out, "%s(", ty);
        ex(t, x, 0, 0);
        put(&t->out, ")");
        v = int_type_val(to);
    } else if (g.bits == f.bits) {
        putf(&t->out, "bitcast(%s, ", ty);
        ex(t, x, 0, 0);
        put(&t->out, ")");
        v = int_type_val(to);
    } else if (g.bits > f.bits) {
        u32 wide = simple_type(t, CT_INT, g.bits, true);
        char *w = type_text(t, wide, 0);
        putf(&t->out, "bitcast(%s, %s(", ty, w);
        ex(t, x, 0, 0);
        put(&t->out, "))");
        free(w);
        v = int_type_val(to);
    } else {
        u32 narrow = simple_type(t, CT_INT, g.bits, false);
        char *n = type_text(t, narrow, 0);
        if (g.is_signed) {
            putf(&t->out, "bitcast(%s, ", ty);
        }
        putf(&t->out, "%s(", n);
        if (f.is_signed) {
            u32 u = simple_type(t, CT_INT, f.bits, false);
            char *us = type_text(t, u, 0);
            putf(&t->out, "bitcast(%s, ", us);
            ex(t, x, 0, 0);
            put(&t->out, ")");
            free(us);
        } else {
            u64 inner = t->out.len;
            Val iv = ex(t, x, 0, 0);
            parens(t, inner, iv, P_MUL);
        }
        put(&t->out, " & ");
        put_int(t, g.bits == 64 ? ~0ull : (1ull << g.bits) - 1);
        put(&t->out, g.is_signed ? "))" : ")");
        free(n);
        v = int_type_val(to);
    }
    free(ty);
    return v;
}

static Val
member(Tr *t, u32 e)
{
    u32 base = e + 1;
    if (t->a.flags[e] & AST_ARROW) {
        u64 at = t->out.len;
        Val b = ex(t, base, 0, 0);
        if (b.bits & NULLABLE) {
            b = unwrap(t, at, b);
        }
        b = parens(t, at, b, P_POSTFIX);
        if (b.bits & MANY) {
            put(&t->out, "[0]");
        }
    } else {
        operand(t, base, P_POSTFIX);
    }
    u32 field = node_of(t, t->a.refs[e]);
    putf(&t->out, ".%s", text(t, t->a.names[field]));
    return value(ctype(t, e), t->bits[field], P_POSTFIX);
}

static const char *
overflow_name(const Tr *t, u32 e)
{
    u32 f = t->kinds[e] == K_CALL ? callee(t, e) : 0;
    if (f == 0) {
        return NULL;
    }
    const char *w = text(t, t->a.names[f]);
    return strcmp(w, "__builtin_add_overflow") == 0 ? "add"
         : strcmp(w, "__builtin_sub_overflow") == 0 ? "sub"
         : strcmp(w, "__builtin_mul_overflow") == 0 ? "mul"
                                                    : NULL;
}

static bool
small_lanes(Tr *t, u32 e)
{
    e = strip(t, e);
    if (t->kinds[e] != K_BINARY) {
        return false;
    }
    u32 r = strip(t, child(t, e, 1));
    if (t->kinds[r] == K_IMPLICIT_CAST && op(t, r) == CAST_VECTOR_SPLAT) {
        r = strip(t, r + 1);
    }
    __int128 k;
    if (!constant_value(t, r, &k)) {
        return false;
    }
    return (op(t, e) == OP_AND && k < 16) || (op(t, e) == OP_SHR && k >= 4);
}

static const char *
chibi_builtin(const Tr *t, u32 f)
{
    const char *w = text(t, t->a.names[f]);
    return strcmp(w, "simd_lookup16") == 0 ? "lookup"
         : strcmp(w, "simd_mask16") == 0   ? "top_bits"
                                           : NULL;
}

static const char *
bit_count_name(const char *w)
{
    if (strcmp(w, "__builtin_clzll") == 0 || strcmp(w, "__builtin_clz") == 0) {
        return "leading_zeros";
    }
    if (strcmp(w, "__builtin_ctzll") == 0 || strcmp(w, "__builtin_ctz") == 0) {
        return "trailing_zeros";
    }
    if (strcmp(w, "__builtin_popcountll") == 0 || strcmp(w, "__builtin_popcount") == 0) {
        return "count_ones";
    }
    return NULL;
}

static void
use_package(Tr *t, u32 package)
{
    t->groups[t->group].packages |= (u8)(1u << package);
}

static bool
is_string(const Tr *t, u32 e)
{
    e = strip(t, e);
    while (is_cast(t, e)) {
        e = strip(t, e + 1);
    }
    return t->kinds[e] == K_STRING_LITERAL;
}

static void
rest_arg(Tr *t, u32 a)
{
    u32 type = ctype(t, a);
    const CType *c = &t->types[type];
    __int128 k;
    if (c->kind == CT_INT && constant_value(t, a, &k) && k >= 0) {
        u32 x = strip(t, a);
        while (t->kinds[x] == K_IMPLICIT_CAST) {
            x = strip(t, x + 1);
        }
        if (is_enum_constant(t, x)) {
            put_name(t, decl_of(t, x));
        } else {
            put_number(t, x, k);
        }
        return;
    }
    if (is_string(t, a)) {
        die(t, a, "a string literal as a variadic argument");
    }
    if (is_null(t, a)) {
        put(&t->out, "0");
        return;
    }
    if (c->kind != CT_POINTER && !(c->kind == CT_FLOAT && c->bits == 64) &&
        !(c->kind == CT_INT && c->bits <= 64)) {
        die(t, a, "a variadic argument other than a pointer, `double` or integer up to 64 bits"
        );
    }

    const char *wide = c->kind == CT_FLOAT ? "f64("
                     : c->is_signed        ? "int("
                                           : "u64(";
    u64 at = t->out.len;
    Val v = c->kind == CT_POINTER ? ex(t, a, 0, 0) : value_as(t, a, type, 0);
    if ((c->kind == CT_INT && c->bits < 64) || v.untyped) {
        wrap(t, at, wide, ")");
    }
    if (c->kind != CT_INT || c->is_signed) {
        wrap(t, at, "bitcast(u64, ", ")");
    }
}

static Val
call(Tr *t, u32 e)
{
    u32 f = callee(t, e);
    if (f == 0) {
        die(t, e, "a call through a function pointer");
    }

    const char *w = text(t, t->a.names[f]);
    u32 first_arg = t->a.ends[e + 1];
    if (strcmp(w, "__builtin_expect") == 0) {
        return ex(t, first_arg, 0, 0);
    }
    const char *count = bit_count_name(w);
    if (count != NULL) {
        char *ty = type_text(t, ctype(t, e), 0);
        use_package(t, PACKAGE_BITS);
        putf(&t->out, "%s(bits.%s(", ty, count);
        ex(t, first_arg, 0, 0);
        put(&t->out, "))");
        free(ty);
        return value(ctype(t, e), 0, P_POSTFIX);
    }
    const char *builtin = chibi_builtin(t, f);
    if (builtin != NULL && strcmp(builtin, "lookup") == 0) {
        u32 index = t->a.ends[first_arg];
        use_package(t, PACKAGE_SIMD);
        put(&t->out, "simd.lookup(");
        ex(t, first_arg, 0, 0);
        put(&t->out, ", ");
        if (small_lanes(t, index)) {
            ex(t, index, 0, 0);
        } else {
            operand(t, index, P_MUL);
            put(&t->out, " & 15");
        }
        put(&t->out, ")");
        return value(ctype(t, e), 0, P_POSTFIX);
    }
    if (builtin != NULL && strcmp(builtin, "top_bits") == 0) {
        char *ty = type_text(t, ctype(t, e), 0);
        use_package(t, PACKAGE_SIMD);
        putf(&t->out, "%s(simd.top_bits(", ty);
        ex(t, first_arg, 0, 0);
        put(&t->out, "))");
        free(ty);
        return value(ctype(t, e), 0, P_POSTFIX);
    }
    if (overflow_name(t, e) != NULL) {
        if (t->subst[e] == 0) {
            die(t, e, "an overflow builtin that can't be hoisted");
        }
        Temp temp = t->temps[t->subst[e] - 1];
        put(&t->out, text(t, temp.name));
        return temp.val;
    }
    if (strcmp(w, "__builtin_isfinite") == 0 || strcmp(w, "__builtin_signbit") == 0) {
        die(t, e, "`isfinite` or `signbit` used other than as a condition");
    }

    bool clear_cache = strcmp(w, "__builtin___clear_cache") == 0;
    t->uses_clear_cache |= clear_cache;
    putf(&t->out, "%s(", clear_cache ? "__clear_cache" : text(t, t->names[f]));
    u32 owner = owner_of(t, f);
    bool rest = t->types[ctype(t, f)].variadic && (t->marks[f] & MARK_EMIT);
    u32 k = 0;
    for (u32 a = first_arg; a < t->a.ends[e]; a = t->a.ends[a], k++) {
        if (k > 0) {
            put(&t->out, ", ");
        }
        u32 p = param_of(t, owner, k);
        if (p == 0 && rest) {
            rest_arg(t, a);
            continue;
        }
        if (p == 0) {
            ex(t, a, 0, 0);
            continue;
        }
        value_as(t, a, ctype(t, p), decl_bits(t, p));
    }
    put(&t->out, ")");

    u32 ty = t->types[ctype(t, f)].inner;
    return value(ty, t->bits[f], P_POSTFIX);
}

static bool
points_to_bytes(const Tr *t, u32 type)
{
    const CType *p = &t->types[t->types[type].inner];
    return p->kind == CT_VOID || (p->kind == CT_INT && p->bits == 8 && !p->is_signed);
}

static Val
pointer_cast(Tr *t, u32 e, u32 bits_hint, bool have_hint)
{
    u32 to = ctype(t, e);
    u32 x = e + 1;
    u64 at = t->out.len;
    Val v = ex(t, x, 0, 0);
    if (v.nil) {
        return v;
    }
    if (v.untyped && !points_to_bytes(t, to)) {
        die(t, e, "a string literal cast to a pointer to something other than bytes");
    }
    if (v.untyped) {
        return (Val){ to, v.bits, P_POSTFIX, true, false, false };
    }
    u32 bits = have_hint
                 ? bits_hint
                 : (v.bits & 3) | (t->types[v.type].kind == CT_POINTER ? 0 : NULLABLE);
    if (is_kind(t, v.type, CT_POINTER) && (v.bits & 3) == (bits & 3) &&
        same_chibi_type(t, v.type, v.bits, to, bits)) {
        return v;
    }
    if ((v.bits & NULLABLE) && !(bits & NULLABLE)) {
        v = unwrap(t, at, v);
    }
    return bitcast_to(t, at, to, bits);
}

static char
json_char(const Tr *t, u32 e, const char *s, u32 *k)
{
    char ch = s[*k];
    if (ch != '\\') {
        *k += 1;
        return ch;
    }
    char x = s[*k + 1];
    if (x != '"' && x != '\\') {
        die(t, e, "a string literal escaped in an unexpected way");
    }
    *k += 2;
    return x;
}

static u8
c_escape(const Tr *t, u32 e, const char *s, u32 *k)
{
    char x = json_char(t, e, s, k);
    switch (x) {
        case '\\':
        case '"': return (u8)x;
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'v': return '\v';
        default: break;
    }
    if (x < '0' || x > '3') {
        die(t, e, "a string literal escaped in an unexpected way");
    }
    u32 v = (u32)(x - '0');
    for (u32 n = 0; n < 2; n++) {
        char d = json_char(t, e, s, k);
        if (d < '0' || d > '7') {
            die(t, e, "a string literal escaped in an unexpected way");
        }
        v = v * 8 + (u32)(d - '0');
    }
    return (u8)v;
}

static u32
string_start(const Tr *t, u32 e)
{
    const char *ty = text(t, t->a.types[e]);
    if (strncmp(ty, "const ", 6) == 0) {
        ty += 6;
    }
    if (strncmp(ty, "char[", 5) != 0) {
        die(t, e, "a string literal that isn't made of `char`");
    }

    u32 k = 0;
    if (json_char(t, e, text(t, t->a.details[e]), &k) != '"') {
        die(t, e, "a string literal escaped in an unexpected way");
    }
    return k;
}

static bool
string_byte(const Tr *t, u32 e, u32 *k, u8 *b)
{
    const char *s = text(t, t->a.details[e]);
    char ch = json_char(t, e, s, k);
    if (ch == '"') {
        return false;
    }
    *b = ch == '\\' ? c_escape(t, e, s, k) : (u8)ch;
    return true;
}

static void
put_string(Tr *t, u32 e)
{
    u32 k = string_start(t, e);
    put(&t->out, "\"");
    u8 b;
    while (string_byte(t, e, &k, &b)) {
        switch (b) {
            case '"': put(&t->out, "\\\""); break;
            case '\\': put(&t->out, "\\\\"); break;
            case '\b': put(&t->out, "\\b"); break;
            case '\f': put(&t->out, "\\f"); break;
            case '\n': put(&t->out, "\\n"); break;
            case '\r': put(&t->out, "\\r"); break;
            case '\t': put(&t->out, "\\t"); break;
            default:
                if (b < 0x20 || b >= 0x7F) {
                    putf(&t->out, "\\x%02X", b);
                } else {
                    put_bytes(&t->out, (const char *)&b, 1);
                }
                break;
        }
    }
    put(&t->out, "\"");
}

static Val
vector_cast(Tr *t, u32 e)
{
    u32 to = ctype(t, e);
    u64 at = t->out.len;
    Val v = ex(t, e + 1, 0, 0);
    if (!same_chibi_type(t, v.type, 0, to, 0)) {
        return bitcast_to(t, at, to, 0);
    }
    v.type = to;
    return v;
}

static Val
cast(Tr *t, u32 e, u32 want_type, u32 want_bits)
{
    Op c = op(t, e);
    u32 x = e + 1;
    switch (c) {
        case CAST_LVALUE:
        case CAST_NOOP:
        case CAST_BUILTIN_DECAY: return ex(t, x, want_type, want_bits);
        case CAST_FUNCTION_DECAY:
            if (want_type == 0) {
                die(t, e, "a function's name with nothing to give it a type, as in a comparison"
                );
            }
            return ex(t, x, want_type, want_bits);
        case CAST_NULL:
            put(&t->out, "nil");
            return (Val){ ctype(t, e), NULLABLE, P_POSTFIX, false, true, false };
        case CAST_INTEGRAL:
            return integral_cast(t, e, t->kinds[e] == K_CAST, want_type);
        case CAST_INT_TO_BOOL:
        case CAST_POINTER_TO_BOOL: {
            u8 prec = cond(t, e);
            return (Val){ t->type_bool, 0, prec, false, false, true };
        }
        case CAST_BIT: {
            if (is_kind(t, ctype(t, e), CT_VECTOR)) {
                return vector_cast(t, e);
            }
            bool hint = want_type != 0 && is_kind(t, want_type, CT_POINTER);
            return pointer_cast(t, e, want_bits, hint);
        }
        case CAST_VECTOR_SPLAT: return ex(t, x, 0, 0);
        case CAST_ARRAY_DECAY: {
            u32 a = strip(t, x);
            if (t->kinds[a] == K_STRING_LITERAL) {
                put_string(t, a);
                return (Val){ ctype(t, e), MANY, P_POSTFIX, true, false, false };
            }
            operand(t, x, P_POSTFIX);
            put(&t->out, "[0..].ptr");
            return value(ctype(t, e), MANY, P_POSTFIX);
        }
        case CAST_POINTER_TO_INT: {
            u32 to = ctype(t, e);
            CType g = t->types[to];
            char *ty = type_text(t, to, 0);
            if (g.bits == 64) {
                putf(&t->out, "bitcast(%s, ", ty);
                ex(t, x, 0, 0);
                put(&t->out, ")");
            } else {
                putf(&t->out, "%s(bitcast(u64, ", ty);
                ex(t, x, 0, 0);
                put(&t->out, ") & ");
                put_int(t, (1ull << g.bits) - 1);
                put(&t->out, ")");
            }
            free(ty);
            return int_type_val(to);
        }
        case CAST_INT_TO_POINTER: {
            u32 to = ctype(t, e);
            u32 bits = want_type != 0 && is_kind(t, want_type, CT_POINTER)
                         ? want_bits
                         : NULLABLE;
            char *ty = type_text(t, to, bits);
            putf(&t->out, "bitcast(%s, ", ty);
            CType f = t->types[ctype(t, x)];
            if (f.bits == 64) {
                ex(t, x, 0, 0);
            } else {
                put(&t->out, f.is_signed ? "int(" : "u64(");
                ex(t, x, 0, 0);
                put(&t->out, ")");
            }
            put(&t->out, ")");
            free(ty);
            return value(to, bits, P_POSTFIX);
        }
        case CAST_TO_VOID: die(t, e, "a cast to void inside an expression");
        case CAST_INT_TO_FLOAT:
        case CAST_FLOAT_TO_INT:
        case CAST_FLOAT: {
            u32 to = ctype(t, e);
            char *ty = type_text(t, to, 0);
            putf(&t->out, "%s(", ty);
            ex(t, x, 0, 0);
            put(&t->out, ")");
            free(ty);
            return int_type_val(to);
        }
        default: die(t, e, "an unsupported cast");
    }
}

static const char *
binary_text(Op o, bool wrapping)
{
    switch (o) {
        case OP_ADD: return wrapping ? " +% " : " + ";
        case OP_SUB: return wrapping ? " -% " : " - ";
        case OP_MUL: return wrapping ? " *% " : " * ";
        case OP_DIV: return " / ";
        case OP_MOD: return " % ";
        case OP_SHL: return " << ";
        case OP_SHR: return " >> ";
        case OP_AND: return " & ";
        case OP_OR: return " | ";
        case OP_XOR: return " ^ ";
        case OP_EQ: return " == ";
        case OP_NE: return " != ";
        case OP_LT: return " < ";
        case OP_GT: return " > ";
        case OP_LE: return " <= ";
        case OP_GE: return " >= ";
        case OP_LAND: return " and ";
        case OP_LOR: return " or ";
        default: return NULL;
    }
}

static u8
binary_prec(Op o)
{
    switch (o) {
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_SHL:
        case OP_SHR:
        case OP_AND: return P_MUL;
        case OP_ADD:
        case OP_SUB:
        case OP_OR:
        case OP_XOR: return P_ADD;
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE: return P_COMPARE;
        case OP_LAND: return P_AND;
        case OP_LOR: return P_OR;
        default: return P_LOWEST;
    }
}

static bool
is_unsigned_int(Tr *t, u32 type)
{
    const CType *c = &t->types[type];
    return c->kind == CT_INT && !c->is_signed;
}

static bool
wraps(Tr *t, u32 type)
{
    return is_unsigned_int(t, type) || is_kind(t, type, CT_VECTOR);
}

static void
negated_offset(Tr *t, u32 n)
{
    __int128 k;
    if (constant_value(t, n, &k)) {
        put(&t->out, "-%u64(");
        put_int(t, k);
        put(&t->out, ")");
        return;
    }

    u32 type = ctype(t, n);
    if (!is_unsigned_int(t, type)) {
        put(&t->out, "-");
        operand(t, n, P_UNARY);
    } else if (t->types[type].bits == 64) {
        put(&t->out, "-%");
        operand(t, n, P_UNARY);
    } else {
        put(&t->out, "-%u64(");
        operand(t, n, P_LOWEST);
        put(&t->out, ")");
    }
}

static Val
many_pointer(Tr *t, u32 p)
{
    u64 at = t->out.len;
    Val v = ex(t, p, 0, 0);
    if (v.bits & NULLABLE) {
        v = unwrap(t, at, v);
    }
    if (!(v.bits & MANY)) {
        v = bitcast_to(t, at, v.type, v.bits | MANY);
    }
    return v;
}

static Val
pointer_offset(Tr *t, u32 e, u32 p, u32 n, bool negate)
{
    u64 at = t->out.len;
    Val v = many_pointer(t, p);
    parens(t, at, v, P_ADD);
    put(&t->out, " + ");
    if (negate) {
        negated_offset(t, n);
    } else {
        operand(t, n, P_ADD + 1);
    }
    return value(ctype(t, e), MANY, P_ADD);
}

static Val
pointer_difference(Tr *t, u32 e, u32 l, u32 r)
{
    u32 pointee = t->types[ctype(t, l)].inner;
    bool bytes = is_kind(t, pointee, CT_VOID) ||
                 (is_kind(t, pointee, CT_INT) && t->types[pointee].bits == 8);
    put(&t->out, bytes ? "" : "(");
    put(&t->out, "bitcast(int, ");
    ex(t, l, 0, 0);
    put(&t->out, ") - bitcast(int, ");
    ex(t, r, 0, 0);
    put(&t->out, ")");
    if (!bytes) {
        char *ty = type_text(t, pointee, 0);
        use_package(t, PACKAGE_MEM);
        putf(&t->out, ") / mem.size_of(%s)", ty);
        free(ty);
    }
    return value(ctype(t, e), 0, bytes ? P_ADD : P_MUL);
}

static const char *
lane_compare(Tr *t, u32 e)
{
    if (t->kinds[e] != K_BINARY || !is_kind(t, ctype(t, e), CT_VECTOR)) {
        return NULL;
    }
    switch (op(t, e)) {
        case OP_EQ: return "eq";
        case OP_NE: return "ne";
        case OP_LT: return "lt";
        case OP_GT: return "gt";
        case OP_LE: return "le";
        case OP_GE: return "ge";
        default: return NULL;
    }
}

static Val
binary(Tr *t, u32 e)
{
    Op o = op(t, e);
    u32 l = child(t, e, 0);
    u32 r = child(t, e, 1);
    if (o == OP_ASSIGN || o == OP_COMMA) {
        die(t, e, "an assignment or comma inside an expression");
    }
    const char *lanes = lane_compare(t, e);
    if (lanes != NULL) {
        use_package(t, PACKAGE_SIMD);
        putf(&t->out, "simd.%s(", lanes);
        ex(t, l, 0, 0);
        put(&t->out, ", ");
        ex(t, r, 0, 0);
        put(&t->out, ")");
        return value(ctype(t, l), 0, P_POSTFIX);
    }
    if (is_condition(t, e)) {
        u8 prec = cond(t, e);
        return (Val){ t->type_bool, 0, prec, false, false, true };
    }

    bool lp = is_pointer(t, l);
    bool rp = is_pointer(t, r);
    if (o == OP_SUB && lp && rp) {
        return pointer_difference(t, e, l, r);
    }
    if (lp || rp) {
        return pointer_offset(t, e, lp ? l : r, lp ? r : l, o == OP_SUB);
    }

    u32 type = ctype(t, e);
    u8 prec = binary_prec(o);
    u64 at = t->out.len;
    Val a = operand(t, l, prec);
    u64 a_end = t->out.len;
    put(&t->out, binary_text(o, wraps(t, type)));
    Val b = operand(t, r, prec + 1);
    if ((o == OP_SHL || o == OP_SHR) && a.untyped && !b.untyped) {
        char *ty = type_text(t, type, 0);
        Out head = { 0 };
        putf(&head, "%s(", ty);
        insert(&t->out, a_end, ")", 1);
        insert(&t->out, at, head.text, head.len);
        free(head.text);
        free(ty);
        a.untyped = false;
    }
    return (Val){ type, 0, prec, a.untyped && b.untyped, false, false };
}

static Val
unary(Tr *t, u32 e)
{
    Op o = op(t, e);
    u32 x = e + 1;
    u32 sub = t->subst[e];
    if (sub == SUBST_OPERAND) {
        return ex(t, x, 0, 0);
    }
    if (sub != 0) {
        Temp temp = t->temps[sub - 1];
        put(&t->out, text(t, temp.name));
        temp.val.prec = P_POSTFIX;
        return temp.val;
    }
    switch (o) {
        case OP_SUB: {
            u32 type = ctype(t, e);
            put(&t->out, wraps(t, type) ? "-%" : "-");
            Val v = operand(t, x, P_UNARY);
            return (Val){ type, 0, P_UNARY, v.untyped, false, false };
        }
        case OP_BIT_NOT: {
            u32 type = ctype(t, e);
            __int128 k;
            if (is_unsigned_int(t, type) && constant_value(t, x, &k)) {
                char *ty = type_text(t, type, 0);
                putf(&t->out, "~%s(", ty);
                free(ty);
                ex(t, x, 0, 0);
                put(&t->out, ")");
                return value(type, 0, P_UNARY);
            }
            put(&t->out, "~");
            Val v = operand(t, x, P_UNARY);
            return (Val){ type, 0, P_UNARY, v.untyped, false, false };
        }
        case OP_NOT: {
            u8 prec = cond(t, e);
            return (Val){ t->type_bool, 0, prec, false, false, true };
        }
        case OP_MUL: {
            u64 at = t->out.len;
            Val v = ex(t, x, 0, 0);
            if (v.bits & NULLABLE) {
                v = unwrap(t, at, v);
            }
            v = parens(t, at, v, P_POSTFIX);
            put(&t->out, v.bits & MANY ? "[0]" : ".*");
            return value(ctype(t, e), v.bits >> LEVEL_BITS, P_POSTFIX);
        }
        case OP_AND: {
            put(&t->out, "&");
            operand(t, x, P_UNARY);
            return value(ctype(t, e), 0, P_UNARY);
        }
        case OP_EXTENSION: return ex(t, x, 0, 0);
        default:
            die(t, e, "an unsupported unary operator inside an expression");
    }
}

static void
put_index(Tr *t, u32 index)
{
    put(&t->out, "[");
    u64 at = t->out.len;
    Val v = ex(t, index, 0, 0);
    if (is_kind(t, v.type, CT_BOOL)) {
        insert(&t->out, at, "if ", 3);
        put(&t->out, " then 1 else 0");
    }
    put(&t->out, "]");
}

static Val
subscript(Tr *t, u32 e)
{
    u32 base = strip(t, e + 1);
    u32 index = child(t, e, 1);
    if (is_cast(t, base) && op(t, base) == CAST_ARRAY_DECAY) {
        operand(t, base + 1, P_POSTFIX);
        put_index(t, index);
        Site s = lvalue_site(t, base + 1);
        return (Val){ ctype(t, e), site_bits(t, s), P_POSTFIX,
                      false,       false,           false };
    }

    u64 at = t->out.len;
    Val v = ex(t, base, 0, 0);
    if (v.bits & NULLABLE) {
        v = unwrap(t, at, v);
    }
    if (!(v.bits & MANY)) {
        v = bitcast_to(t, at, v.type, v.bits | MANY);
    }
    parens(t, at, v, P_POSTFIX);
    put_index(t, index);
    return (Val){ ctype(t, e), v.bits >> LEVEL_BITS, P_POSTFIX, false, false,
                  false };
}

static u32
tested_ref(const Tr *t, u32 e)
{
    e = strip(t, e);
    while (is_cast(t, e) && (op(t, e) == CAST_LVALUE || op(t, e) == CAST_NOOP ||
                             op(t, e) == CAST_POINTER_TO_BOOL)) {
        e = strip(t, e + 1);
    }
    return t->kinds[e] == K_DECL_REF ? decl_of(t, e) : 0;
}

static bool
proves_non_nil(const Tr *t, u32 c, u32 decl, bool holds)
{
    c = strip(t, c);
    if (t->kinds[c] == K_UNARY && op(t, c) == OP_NOT) {
        return proves_non_nil(t, c + 1, decl, !holds);
    }
    if (t->kinds[c] != K_BINARY) {
        return holds && tested_ref(t, c) == decl;
    }

    Op o = op(t, c);
    if (o == (holds ? OP_LAND : OP_LOR)) {
        return proves_non_nil(t, child(t, c, 0), decl, holds) ||
               proves_non_nil(t, child(t, c, 1), decl, holds);
    }
    if (o != (holds ? OP_NE : OP_EQ)) {
        return false;
    }
    u32 l = child(t, c, 0);
    u32 r = child(t, c, 1);
    return (tested_ref(t, l) == decl && is_null(t, r)) ||
           (tested_ref(t, r) == decl && is_null(t, l));
}

static Val
conditional_value(Tr *t, u32 e, u32 type, u32 bits, bool typed)
{
    u32 s = strip(t, e);
    if (t->kinds[s] == K_IMPLICIT_CAST && op(t, s) == CAST_INTEGRAL) {
        s = strip(t, s + 1);
    }
    if (typed && t->kinds[s] == K_CONDITIONAL && is_kind(t, type, CT_INT) &&
        constant_leaves(t, s)) {
        put_leaves(t, s, type, true);
        return value(type, 0, P_LOWEST);
    }
    return value_as(t, e, type, bits);
}

static Val
conditional(Tr *t, u32 e, u32 want_type, u32 want_bits)
{
    u32 type = ctype(t, e);
    u32 bits = want_type == type ? want_bits : 0;
    bool pointer = is_kind(t, type, CT_POINTER);
    if (pointer && want_type != type) {
        bits = (nullable(t, e) ? NULLABLE : 0) | (many_value(t, e) ? MANY : 0);
    }
    if (is_kind(t, type, CT_INT) && constant_leaves(t, e)) {
        put_leaves(t, e, type, want_type != type);
        return (Val){ type, 0, P_LOWEST, want_type == type, false, false };
    }

    u32 first = child(t, e, 1);
    u32 second = child(t, e, 2);
    bool swap = pointer && want_type != type && is_string(t, first) &&
                !is_string(t, second);
    u32 then_arm = swap ? second : first;
    u32 else_arm = swap ? first : second;

    put(&t->out, "if ");
    cond_at(t, child(t, e, 0), swap);
    put(&t->out, " then ");
    u64 a_at = t->out.len;
    Val a = conditional_value(t, then_arm, type, bits, want_type != type);
    u64 a_end = t->out.len;
    put(&t->out, " else ");
    Val b = conditional_value(t, else_arm, type, bits, want_type != type);
    __int128 k;
    bool loose = (a.untyped && !constant_value(t, then_arm, &k)) ||
                 (b.untyped && !constant_value(t, else_arm, &k));
    if (pointer && want_type != type && (a.bits & NULLABLE) != (b.bits & NULLABLE)) {
        bool in_then = (a.bits & NULLABLE) != 0;
        u32 arm = in_then ? then_arm : else_arm;
        u32 d = tested_ref(t, arm);
        if (d == 0 || !proves_non_nil(t, child(t, e, 0), d, arm == first)) {
            die(t, e, "a `?:` value mixing pointers that may and can't be nil");
        }
        if (in_then) {
            insert(&t->out, a_end, "!!", 2);
        } else {
            put(&t->out, "!!");
        }
        bits &= ~(u32)NULLABLE;
        a.bits &= ~(u32)NULLABLE;
    }
    if (a.untyped && b.untyped && want_type != type) {
        char *ty = type_text(t, type, bits);
        Out head = { 0 };
        putf(&head, "%s(", ty);
        insert(&t->out, a_end, ")", 1);
        insert(&t->out, a_at, head.text, head.len);
        free(head.text);
        free(ty);
        a.untyped = false;
    }
    if ((pointer && a.nil != b.nil) || loose) {
        a.untyped = true;
        b.untyped = true;
    }
    return (Val){ type,           bits,
                  P_LOWEST,       a.untyped && b.untyped,
                  a.nil && b.nil, a.boolean && b.boolean };
}

static u32
column(const Tr *t)
{
    u64 i = t->out.len;
    while (i > 0 && t->out.text[i - 1] != '\n') {
        i--;
    }
    return (u32)(t->out.len - i);
}

static Val put_init(Tr *t, u32 e, u32 type, u32 bits, bool constant);

static bool
fits(const Tr *t, u64 from)
{
    return column(t) <= LINE_WIDTH &&
           memchr(t->out.text + from, '\n', t->out.len - from) == NULL;
}

static void
separate(Tr *t, bool first, bool multiline)
{
    if (multiline) {
        put(&t->out, first ? "" : ",");
        line(t);
    } else {
        put(&t->out, first ? " " : ", ");
    }
}

static bool
zero_init(Tr *t, u32 e)
{
    __int128 v;
    u32 s = strip(t, e);
    return t->kinds[s] == K_IMPLICIT_VALUE_INIT ||
           (constant_value(t, s, &v) && v == 0) || is_null(t, s);
}

static bool
put_fields(Tr *t, u32 list, u32 type, bool constant, bool multiline)
{
    CType c = t->types[type];
    if (strcmp(text(t, t->a.details[c.decl]), "struct") != 0) {
        die(t, list, "an initializer for a union");
    }

    bool all_zero = true;
    for (u32 k = list + 1; list != 0 && k < t->a.ends[list]; k = t->a.ends[k]) {
        all_zero = all_zero && zero_init(t, k);
    }
    put(&t->out, text(t, t->names[c.decl]));
    if (all_zero) {
        put(&t->out, ".{}");
        return true;
    }

    put(&t->out, ".{");
    t->depth += multiline;
    u32 field = c.decl + 1;
    bool first = true;
    for (u32 k = list + 1; k < t->a.ends[list]; k = t->a.ends[k]) {
        while (t->kinds[field] != K_FIELD) {
            field = t->a.ends[field];
        }
        if (t->kinds[strip(t, k)] != K_IMPLICIT_VALUE_INIT) {
            separate(t, first, multiline);
            u64 at = t->out.len;
            putf(&t->out, ".%s = ", text(t, t->a.names[field]));
            put_init(t, k, ctype(t, field), t->bits[field], constant);
            first = false;
            if (!multiline && !fits(t, at)) {
                return false;
            }
        }
        field = t->a.ends[field];
    }
    if (multiline) {
        put(&t->out, ",");
        t->depth--;
        line(t);
        put(&t->out, "}");
        return true;
    }
    put(&t->out, " }");
    return fits(t, t->out.len);
}

static bool
put_elements(Tr *t, u32 list, u32 type, u32 bits, bool constant, bool multiline)
{
    CType c = t->types[type];
    bool nested = is_kind(t, c.inner, CT_ARRAY) || is_kind(t, c.inner, CT_RECORD);
    u32 filler = 0;
    u32 item = 0;
    if (list != 0) {
        item = list + 1;
        if (item < t->a.ends[list] && (t->a.flags[item] & AST_FILLER)) {
            filler = item;
            item = t->a.ends[item];
        }
    }

    put(&t->out, ".[");
    t->depth += multiline;
    for (u64 k = 0; k < c.count; k++) {
        u32 x = filler;
        if (list != 0 && item < t->a.ends[list]) {
            x = item;
            item = t->a.ends[item];
        } else if (list != 0 && filler == 0) {
            die(t, list, "an array initializer with fewer values than elements");
        }

        u64 at = t->out.len;
        if (multiline && (nested || k == 0)) {
            separate(t, k == 0, true);
        } else if (k > 0) {
            put(&t->out, ", ");
        }
        put_init(t, x, c.inner, bits, constant);
        if (fits(t, at)) {
            continue;
        }
        if (!multiline) {
            return false;
        }
        if (!nested && k > 0) {
            t->out.len = at;
            put(&t->out, ",");
            line(t);
            put_init(t, x, c.inner, bits, constant);
        }
    }
    if (multiline) {
        put(&t->out, ",");
        t->depth--;
        line(t);
    }
    put(&t->out, "]");
    return multiline || fits(t, t->out.len);
}

static void
put_char_array(Tr *t, u32 e, u64 count)
{
    u32 k = string_start(t, e);
    bool more = true;
    put(&t->out, ".[");
    for (u64 i = 0; i < count; i++) {
        u8 b = 0;
        more = more && string_byte(t, e, &k, &b);
        putf(&t->out, i == 0 ? "%u" : ", %u", b);
    }
    put(&t->out, "]");
}

static void
put_lanes(Tr *t, u32 list, u32 type, bool constant)
{
    CType c = t->types[type];
    u32 item = list + 1;

    char *ty = type_text(t, c.inner, 0);
    putf(&t->out, "%s.[", ty);
    free(ty);
    for (u64 k = 0; k < c.count; k++) {
        put(&t->out, k == 0 ? "" : ", ");
        if (item < t->a.ends[list]) {
            put_init(t, item, c.inner, 0, constant);
            item = t->a.ends[item];
        } else {
            put(&t->out, "0");
        }
    }
    put(&t->out, "]");
}

static Val
put_init(Tr *t, u32 e, u32 type, u32 bits, bool constant)
{
    u32 s = e == 0 ? 0 : literal_of(t, e);
    bool zero = s == 0 || t->kinds[s] == K_IMPLICIT_VALUE_INIT;
    u8 kind = t->types[type].kind;
    if (kind == CT_VECTOR && !zero && t->kinds[s] == K_INIT_LIST) {
        put_lanes(t, s, type, constant);
        return value(type, 0, P_POSTFIX);
    }
    if (kind == CT_ARRAY || kind == CT_RECORD) {
        if (!zero && t->kinds[s] == K_STRING_LITERAL) {
            put_char_array(t, s, t->types[type].count);
            return value(type, bits, P_POSTFIX);
        }
        if (!zero && t->kinds[s] != K_INIT_LIST) {
            return value_as(t, e, type, bits);
        }
        u32 list = zero ? 0 : s;
        u64 start = t->out.len;
        u32 labels = t->labels;
        CType c = t->types[type];
        bool rows = kind == CT_ARRAY && c.count > 1 &&
                    (is_kind(t, c.inner, CT_ARRAY) || is_kind(t, c.inner, CT_RECORD));
        bool one_line = !rows &&
                        (kind == CT_ARRAY
                             ? put_elements(t, list, type, bits, constant, false)
                             : put_fields(t, list, type, constant, false));
        if (!one_line) {
            t->out.len = start;
            t->labels = labels;
            if (kind == CT_ARRAY) {
                put_elements(t, list, type, bits, constant, true);
            } else {
                put_fields(t, list, type, constant, true);
            }
        }
        return value(type, bits, P_POSTFIX);
    }

    if (zero) {
        put(&t->out,
            kind == CT_BOOL      ? "false"
            : kind == CT_POINTER ? "nil"
                                 : "0");
        return (Val){ type,
                      bits,
                      P_POSTFIX,
                      kind != CT_BOOL && kind != CT_POINTER,
                      kind == CT_POINTER,
                      kind == CT_BOOL };
    }
    if (t->kinds[s] == K_INIT_LIST) {
        return put_init(t, s + 1, type, bits, constant);
    }
    if (constant) {
        return put_constant(t, e, type);
    }
    return value_as(t, e, type, bits);
}

static bool
global_var(const Tr *t, u32 d)
{
    return t->kinds[d] == K_VAR &&
           (t->kinds[t->parents[d]] == K_TRANSLATION_UNIT || t->a.details[d] != 0);
}

static u32
va_list_decl(const Tr *t, u32 e)
{
    e = strip(t, e);
    while (is_cast(t, e)) {
        e = strip(t, e + 1);
    }
    if (t->kinds[e] != K_DECL_REF || t->kinds[decl_of(t, e)] != K_VAR ||
        global_var(t, decl_of(t, e))) {
        die(t, e, "a `va_list` that isn't a local variable");
    }
    return decl_of(t, e);
}

static Val
va_arg_value(Tr *t, u32 e)
{
    u32 sub = t->subst[e];
    if (sub != 0) {
        Temp temp = t->temps[sub - 1];
        put(&t->out, text(t, temp.name));
        temp.val.prec = P_POSTFIX;
        return temp.val;
    }

    if (t->rest == 0) {
        die(t, e, "a `va_arg` outside a variadic function");
    }
    u32 type = ctype(t, e);
    const CType *c = &t->types[type];
    bool chars = c->kind == CT_POINTER && t->types[c->inner].kind == CT_INT &&
                 t->types[c->inner].bits == 8;
    if (!chars && !(c->kind == CT_FLOAT && c->bits == 64) &&
        !(c->kind == CT_INT && c->bits <= 64)) {
        die(t, e, "a `va_arg` of a type other than `char *`, `double` or integer up to 64 bits"
        );
    }
    u32 bits = chars ? NULLABLE | MANY : 0;
    bool narrow = c->kind == CT_INT && c->bits < 64;
    bool cast = c->kind != CT_INT || c->is_signed;

    if (cast) {
        char *ty = type_text(t, type, bits);
        putf(&t->out, "bitcast(%s, ", ty);
        free(ty);
    }
    if (narrow) {
        putf(&t->out, "u%u(", c->bits);
    }
    putf(&t->out, "%s[", text(t, t->rest));
    put_name(t, va_list_decl(t, e + 1));
    put(&t->out, "]");
    if (narrow) {
        put(&t->out, " & ");
        put_int(t, (1ull << c->bits) - 1);
        put(&t->out, ")");
    }
    if (cast) {
        put(&t->out, ")");
    }
    return value(type, bits, P_POSTFIX);
}

static Val
ex(Tr *t, u32 e, u32 want_type, u32 want_bits)
{
    switch (t->kinds[e]) {
        case K_PAREN:
        case K_CONSTANT: return ex(t, e + 1, want_type, want_bits);
        case K_INT_LITERAL:
        case K_CHAR_LITERAL: {
            __int128 v;
            constant_value(t, e, &v);
            put_number(t, e, v);
            return (Val){ ctype(t, e), 0, P_POSTFIX, true, false, false };
        }
        case K_FLOAT_LITERAL:
            put_float(t, text(t, t->a.details[e]));
            return (Val){ ctype(t, e), 0, P_POSTFIX, true, false, false };
        case K_BOOL_LITERAL:
            put(&t->out, strcmp(text(t, t->a.details[e]), "true") == 0 ? "true" : "false");
            return (Val){ t->type_bool, 0, P_POSTFIX, false, false, true };
        case K_DECL_REF: {
            u32 d = decl_of(t, e);
            if (is_kind(t, ctype(t, e), CT_VA_LIST)) {
                die(t, e, "a `va_list` used other than by `va_start`, `va_arg` and `va_end`");
            }
            put_name(t, d);
            if (t->kinds[d] == K_ENUM_CONSTANT) {
                return (Val){ ctype(t, e), 0, P_POSTFIX, true, false, false };
            }
            u32 bits = t->kinds[d] == K_VAR || t->kinds[d] == K_PARAM ? t->bits[d] : 0;
            return value(ctype(t, e), bits, P_POSTFIX);
        }
        case K_MEMBER: return member(t, e);
        case K_CALL: return call(t, e);
        case K_IMPLICIT_CAST:
        case K_CAST: return cast(t, e, want_type, want_bits);
        case K_BINARY: return binary(t, e);
        case K_UNARY: return unary(t, e);
        case K_SUBSCRIPT: return subscript(t, e);
        case K_CONDITIONAL: return conditional(t, e, want_type, want_bits);
        case K_TYPE_TRAIT: {
            u32 arg = t->a.arg_types[e];
            u32 type = arg != 0 ? ctype_of_string(t, arg, e)
                                : ctype(t, child(t, e, 0));
            const char *w = text(t, t->a.names[e]);
            char *ty = type_text(t, type, 0);
            use_package(t, PACKAGE_MEM);
            putf(&t->out, "mem.%s(%s)", strcmp(w, "sizeof") == 0 ? "size_of" : "align_of", ty);
            free(ty);
            return (Val){ ctype(t, e), 0, P_POSTFIX, true, false, false };
        }
        case K_COMPOUND_LITERAL: return ex(t, e + 1, want_type, want_bits);
        case K_INIT_LIST: return put_init(t, e, ctype(t, e), want_bits, false);
        case K_VA_ARG: return va_arg_value(t, e);
        default: die(t, e, "an unsupported expression");
    }
}

static Op
inverse(Op o)
{
    switch (o) {
        case OP_EQ: return OP_NE;
        case OP_NE: return OP_EQ;
        case OP_LT: return OP_GE;
        case OP_GE: return OP_LT;
        case OP_GT: return OP_LE;
        case OP_LE: return OP_GT;
        default: return OP_NONE;
    }
}


static void
cond_operand(Tr *t, u32 e, u8 need, bool negate)
{
    u64 at = t->out.len;
    u8 prec = cond_at(t, e, negate);
    parens(t, at, (Val){ .prec = prec }, need);
}

static u8
compare(Tr *t, u32 e, Op o)
{
    u32 l = child(t, e, 0);
    u32 r = child(t, e, 1);
    const char *op_text = binary_text(o, false);
    if (!is_pointer(t, l)) {
        operand(t, l, P_COMPARE + 1);
        put(&t->out, op_text);
        operand(t, r, P_COMPARE + 1);
        return P_COMPARE;
    }

    if (o != OP_EQ && o != OP_NE) {
        put(&t->out, "bitcast(u64, ");
        ex(t, l, 0, 0);
        put(&t->out, ")");
        put(&t->out, op_text);
        put(&t->out, "bitcast(u64, ");
        ex(t, r, 0, 0);
        put(&t->out, ")");
        return P_COMPARE;
    }

    bool ln = is_null(t, l);
    bool rn = is_null(t, r);
    if (ln || rn) {
        operand(t, ln ? r : l, P_COMPARE + 1);
        put(&t->out, op_text);
        put(&t->out, "nil");
        return P_COMPARE;
    }

    u64 at = t->out.len;
    Val a = ex(t, l, 0, 0);
    parens(t, at, a, P_COMPARE + 1);
    put(&t->out, op_text);
    u64 bt = t->out.len;
    Val b = ex(t, r, 0, 0);
    if ((a.bits & MANY) != (b.bits & MANY) ||
        !same_chibi_type(t, a.type, a.bits & ~(u32)NULLABLE, b.type, b.bits & ~(u32)NULLABLE)) {
        bitcast_to(t, bt, a.type, (a.bits & MANY) | (b.bits & NULLABLE));
    } else {
        parens(t, bt, b, P_COMPARE + 1);
    }
    return P_COMPARE;
}

static bool
is_f32(Tr *t, u32 e)
{
    const CType *c = &t->types[ctype(t, e)];
    if (c->kind != CT_FLOAT || c->bits > 64) {
        die(t, e, "`isfinite` or `signbit` of a type other than `float` or `double`");
    }
    return c->bits == 32;
}

static u8
finite_test(Tr *t, u32 x, bool negate)
{
    bool f32 = is_f32(t, x);
    const char *mask = f32 ? "0x7F800000" : "0x7FF0000000000000";
    put(&t->out, f32 ? "bitcast(u32, " : "bitcast(u64, ");
    ex(t, x, 0, 0);
    putf(&t->out, ") & %s %s %s", mask, negate ? "==" : "!=", mask);
    return P_COMPARE;
}

static u8
sign_test(Tr *t, u32 x, bool negate)
{
    put(&t->out, is_f32(t, x) ? "bitcast(i32, " : "bitcast(int, ");
    ex(t, x, 0, 0);
    put(&t->out, negate ? ") >= 0" : ") < 0");
    return P_COMPARE;
}

static u8
cond_at(Tr *t, u32 e, bool negate)
{
    e = strip(t, e);
    switch (t->kinds[e]) {
        case K_IMPLICIT_CAST:
        case K_CAST: {
            Op c = op(t, e);
            if (c == CAST_INT_TO_BOOL || c == CAST_POINTER_TO_BOOL ||
                (c == CAST_INTEGRAL && !is_kind(t, ctype(t, e), CT_BOOL))) {
                return cond_at(t, e + 1, negate);
            }
            break;
        }
        case K_CALL: {
            u32 f = callee(t, e);
            const char *w = f != 0 ? text(t, t->a.names[f]) : "";
            if (strcmp(w, "__builtin_expect") == 0) {
                return cond_at(t, t->a.ends[e + 1], negate);
            }
            if (strcmp(w, "__builtin_isfinite") == 0) {
                return finite_test(t, t->a.ends[e + 1], negate);
            }
            if (strcmp(w, "__builtin_signbit") == 0) {
                return sign_test(t, t->a.ends[e + 1], negate);
            }
            break;
        }
        case K_UNARY:
            if (op(t, e) == OP_NOT) {
                return cond_at(t, e + 1, !negate);
            }
            break;
        case K_BINARY: {
            Op o = op(t, e);
            u32 l = child(t, e, 0);
            u32 r = child(t, e, 1);
            if (o == OP_LAND && is_string(t, r)) {
                return cond_at(t, l, negate);
            }
            if ((o == OP_OR || o == OP_AND) && is_condition(t, e)) {
                o = o == OP_OR ? OP_LOR : OP_LAND;
            }
            if (o == OP_LAND || o == OP_LOR) {
                if (negate) {
                    put(&t->out, "!");
                    cond_operand(t, e, P_POSTFIX, false);
                    return P_UNARY;
                }
                u8 prec = binary_prec(o);
                cond_operand(t, l, prec, false);
                put(&t->out, binary_text(o, false));
                cond_operand(t, r, prec + 1, false);
                return prec;
            }
            if (inverse(o) != OP_NONE) {
                return compare(t, e, negate ? inverse(o) : o);
            }
            break;
        }
        default: break;
    }

    u32 type = ctype(t, e);
    const CType *c = &t->types[type];
    if (c->kind == CT_BOOL) {
        if (negate) {
            put(&t->out, "!");
            operand(t, e, P_UNARY);
            return P_UNARY;
        }
        return operand(t, e, P_OR).prec;
    }

    const char *zero = c->kind == CT_POINTER ? "nil"
                     : c->kind == CT_FLOAT   ? "0.0"
                                             : "0";
    if (c->kind != CT_POINTER && c->kind != CT_INT && c->kind != CT_FLOAT) {
        die(t, e, "a condition of an unsupported type");
    }
    operand(t, e, P_COMPARE + 1);
    put(&t->out, negate ? " == " : " != ");
    put(&t->out, zero);
    return P_COMPARE;
}

static u8
cond(Tr *t, u32 e)
{
    return cond_at(t, e, false);
}

static const char *const reserved_names[] = {
    "if",
    "then",
    "else",
    "for",
    "while",
    "switch",
    "break",
    "continue",
    "return",
    "defer",
    "struct",
    "union",
    "enum",
    "fn",
    "package",
    "import",
    "pub",
    "true",
    "false",
    "nil",
    "is",
    "in",
    "bitcast",
    "and",
    "or",
    "const",
    "export",
    "extern",
    "undefined",
    "try",
    "catch",
    "void",
    "bool",
    "int",
    "i8",
    "i16",
    "i32",
    "i64",
    "i128",
    "u8",
    "u16",
    "u32",
    "u64",
    "u128",
    "f32",
    "f64",
    "uintptr",
    "error",
    "string",
    "cstring",
    "_",
};

static bool
reserved(const char *s)
{
    for (u32 i = 0; i < sizeof reserved_names / sizeof reserved_names[0]; i++) {
        if (strcmp(s, reserved_names[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool
in_scope(const Tr *t, u32 name)
{
    for (u32 i = 0; i < t->nscope; i++) {
        if (t->scope[i] == name) {
            return true;
        }
    }
    return false;
}

static u32
intern_text(Tr *t, const char *s)
{
    return strings_intern(&t->a.strings, s, (u32)strlen(s));
}

static bool
shadows_package(const Tr *t, const char *s)
{
    for (u32 k = 0; k < NPACKAGES; k++) {
        if ((t->fn_packages >> k & 1) && strcmp(s, package_names[k]) == 0) {
            return true;
        }
    }
    return false;
}

static u32
declare_local(Tr *t, u32 decl, const char *suffix)
{
    const char *base = text(t, t->a.names[decl]);
    char buf[256];
    snprintf(
        buf,
        sizeof buf,
        "%s%s",
        base,
        suffix[0]                                    ? suffix
        : reserved(base) || shadows_package(t, base) ? "_"
                                                     : ""
    );
    u32 id = intern_text(t, buf);
    for (u32 n = 2; in_scope(t, id); n++) {
        snprintf(buf, sizeof buf, "%s%s%u", base, suffix, n);
        id = intern_text(t, buf);
    }

    t->scope = grow(t->scope, &t->scope_cap, t->nscope + 1, sizeof *t->scope);
    t->scope[t->nscope++] = id;
    t->names[decl] = id;
    return id;
}

static u32
declare_temp(Tr *t, const char *base, const char *suffix)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s%s", base, suffix);
    u32 id = intern_text(t, buf);
    for (u32 n = 2; in_scope(t, id); n++) {
        snprintf(buf, sizeof buf, "%s%s%u", base, suffix, n);
        id = intern_text(t, buf);
    }

    t->scope = grow(t->scope, &t->scope_cap, t->nscope + 1, sizeof *t->scope);
    t->scope[t->nscope++] = id;
    return id;
}

static void
push_target(Tr *t, u32 node, bool is_switch, u32 label)
{
    if (t->ntargets == 256) {
        die(t, node, "loops nested more than 256 deep");
    }
    t->targets[t->ntargets++] = (Target){ node, is_switch, label };
}

static bool
is_loop(const Tr *t, u32 s)
{
    u8 k = t->kinds[s];
    return k == K_FOR || k == K_WHILE || k == K_DO;
}

static bool
has_continue(const Tr *t, u32 s)
{
    for (u32 i = s; i < t->a.ends[s];) {
        if (i != s && is_loop(t, i)) {
            i = t->a.ends[i];
            continue;
        }
        if (t->kinds[i] == K_CONTINUE) {
            return true;
        }
        i++;
    }
    return false;
}

static u32
count_breaks(const Tr *t, u32 s)
{
    u32 n = 0;
    for (u32 i = s + 1; i < t->a.ends[s];) {
        if (is_loop(t, i) || t->kinds[i] == K_SWITCH) {
            i = t->a.ends[i];
            continue;
        }
        n += t->kinds[i] == K_BREAK;
        i++;
    }
    return n;
}

static void stmt(Tr *t, u32 s);
static void braced(Tr *t, u32 s);

static const char *source_text(Tr *t, u32 file);

static bool
blank_between(Tr *t, u32 a, u32 b)
{
    if (t->a.files[a] != t->a.files[b] || (t->a.flags[a] & AST_MACRO) ||
        (t->a.flags[b] & AST_MACRO)) {
        return false;
    }
    const char *src = source_text(t, t->a.files[a]);
    if (src == NULL) {
        return false;
    }
    u32 from = t->a.range_ends[a];
    u32 to = t->a.range_begins[b];
    u32 newlines = 0;
    for (u32 k = from; k < to; k++) {
        newlines += src[k] == '\n';
    }
    return newlines >= 2;
}

static bool
hoisted(const Tr *t, u32 s)
{
    if (t->kinds[s] != K_DECL_STMT) {
        return false;
    }
    for (u32 c = s + 1; c < t->a.ends[s]; c = t->a.ends[c]) {
        u8 k = t->kinds[c];
        if (k != K_ENUM && k != K_RECORD && k != K_TYPEDEF &&
            (k != K_VAR || !global_var(t, c))) {
            return false;
        }
    }
    return true;
}

static void
statements(Tr *t, u32 first, u32 end)
{
    u32 prev = 0;
    for (u32 c = first; c < end; c = t->a.ends[c]) {
        if (hoisted(t, c)) {
            continue;
        }
        if (prev != 0 && blank_between(t, prev, c)) {
            put(&t->out, "\n");
        }
        stmt(t, c);
        prev = c;
    }
}

static void
block(Tr *t, u32 s)
{
    put(&t->out, " ");
    braced(t, s);
}

static void
braced(Tr *t, u32 s)
{
    put(&t->out, "{");
    t->depth++;
    u32 mark = t->nscope;
    if (t->kinds[s] == K_COMPOUND) {
        statements(t, s + 1, t->a.ends[s]);
    } else {
        stmt(t, s);
    }
    t->nscope = mark;
    t->depth--;
    line(t);
    put(&t->out, "}");
}

static bool
same_val_type(Tr *t, Val v, u32 type, u32 bits)
{
    if (v.untyped || v.nil) {
        return false;
    }
    if (v.boolean) {
        return is_kind(t, type, CT_BOOL);
    }
    return same_chibi_type(t, v.type, v.bits, type, bits);
}

static void
increment(Tr *t, u32 e)
{
    Op o = op(t, e);
    u32 x = e + 1;
    u32 type = ctype(t, x);
    Val v = ex(t, x, 0, 0);
    if (is_kind(t, type, CT_POINTER)) {
        const char *step = o == OP_INC ? "1" : "-%u64(1)";
        if (!(v.bits & NULLABLE)) {
            putf(&t->out, " += %s", step);
            return;
        }
        if (has_effects(t, x)) {
            die(t, e, "`++` or `--` on a pointer that may be nil, with side effects");
        }
        put(&t->out, " = ");
        u64 at = t->out.len;
        unwrap(t, at, ex(t, x, 0, 0));
        putf(&t->out, " + %s", step);
        return;
    }
    bool wrapping = is_unsigned_int(t, type);
    if (o == OP_INC) {
        put(&t->out, wrapping ? " +%= 1" : " += 1");
    } else {
        put(&t->out, wrapping ? " -%= 1" : " -= 1");
    }
}

static const char *
last_name(Tr *t, u32 e)
{
    for (;;) {
        e = strip_noop(t, e);
        switch (t->kinds[e]) {
            case K_DECL_REF: return text(t, t->names[decl_of(t, e)]);
            case K_MEMBER: return text(t, t->a.names[node_of(t, t->a.refs[e])]);
            case K_SUBSCRIPT: e = e + 1; break;
            case K_UNARY:
                if (op(t, e) != OP_MUL) {
                    return "value";
                }
                e = e + 1;
                break;
            case K_IMPLICIT_CAST:
                if (op(t, e) != CAST_LVALUE && op(t, e) != CAST_ARRAY_DECAY) {
                    return "value";
                }
                e = e + 1;
                break;
            default: return "value";
        }
    }
}

static bool
has_call(const Tr *t, u32 root)
{
    for (u32 i = root; i < t->a.ends[root]; i++) {
        if (t->kinds[i] == K_CALL) {
            return true;
        }
    }
    return false;
}

static u32
use_of(const Tr *t, u32 *e)
{
    u32 p = t->parents[*e];
    while (t->kinds[p] == K_PAREN) {
        *e = p;
        p = t->parents[p];
    }
    return p;
}

static bool
private_local(const Tr *t, u32 e)
{
    e = strip(t, e);
    if (t->kinds[e] != K_DECL_REF) {
        return false;
    }
    u32 d = decl_of(t, e);
    if ((t->kinds[d] != K_VAR && t->kinds[d] != K_PARAM) || global_var(t, d)) {
        return false;
    }

    u32 body = function_body(t, t->fn);
    for (u32 i = body; i < t->a.ends[body]; i++) {
        if (t->kinds[i] != K_DECL_REF || decl_of(t, i) != d) {
            continue;
        }
        u32 at = i;
        u32 p = use_of(t, &at);
        while (t->kinds[p] == K_MEMBER && !(t->a.flags[p] & AST_ARROW)) {
            at = p;
            p = use_of(t, &at);
        }
        if (t->kinds[p] == K_UNARY && op(t, p) == OP_AND) {
            u32 arg = p;
            if (overflow_name(t, use_of(t, &arg)) == NULL) {
                return false;
            }
        } else if (is_cast(t, p) && op(t, p) == CAST_ARRAY_DECAY) {
            return false;
        }
    }
    return true;
}

static bool
value_used(const Tr *t, u32 e, u32 root, bool used)
{
    for (u32 at = e; at != root;) {
        u32 p = t->parents[at];
        bool through = t->kinds[p] == K_PAREN ||
                       (t->kinds[p] == K_CAST && op(t, p) == CAST_TO_VOID);
        if (!through) {
            return true;
        }
        at = p;
    }
    return used;
}

static void
refuse_branch(const Tr *t, u32 e, u32 root, const char *what)
{
    char msg[128];
    for (u32 at = e; at != root;) {
        u32 p = t->parents[at];
        bool right = at != p + 1;
        if (t->kinds[p] == K_TYPE_TRAIT) {
            snprintf(msg, sizeof msg, "%s inside `sizeof`", what);
            die(t, e, msg);
        }
        if (t->kinds[p] == K_BINARY && op(t, p) == OP_COMMA) {
            snprintf(msg, sizeof msg, "%s used as a value inside a comma expression", what);
            die(t, e, msg);
        }
        if ((t->kinds[p] == K_BINARY &&
             (op(t, p) == OP_LAND || op(t, p) == OP_LOR) && right) ||
            (t->kinds[p] == K_CONDITIONAL && right)) {
            snprintf(msg, sizeof msg, "%s used as a value where it may not run", what);
            die(t, e, msg);
        }
        at = p;
    }
}

static void
put_overflow(Tr *t, u32 e, u32 flag)
{
    u32 a = t->a.ends[e + 1];
    u32 b = t->a.ends[a];
    u32 r = strip(t, t->a.ends[b]);
    if (t->kinds[r] != K_UNARY || op(t, r) != OP_AND || has_effects(t, a) ||
        has_effects(t, b) || has_effects(t, r + 1)) {
        die(t, e, "an overflow builtin whose operands have side effects");
    }
    if (!private_local(t, r + 1)) {
        die(t, e, "an overflow builtin whose result isn't a private local");
    }
    u32 type = ctype(t, r + 1);
    if (ctype(t, a) != type || ctype(t, b) != type) {
        die(t, e, "an overflow builtin whose operands and result differ in type");
    }

    use_package(t, PACKAGE_BITS);
    put_name(t, decl_of(t, strip(t, r + 1)));
    putf(&t->out, ", %s = bits.%s_overflow(", text(t, flag), overflow_name(t, e));
    ex(t, a, 0, 0);
    put(&t->out, ", ");
    ex(t, b, 0, 0);
    put(&t->out, ")");
}

static void
hoist_overflow(Tr *t, u32 e, u32 root)
{
    refuse_branch(t, e, root, "an overflow builtin");
    u32 name = declare_temp(t, "overflowed", "");
    putf(&t->out, "%s: bool = undefined", text(t, name));
    line(t);
    put_overflow(t, e, name);
    line(t);

    Val flag = { t->type_bool, 0, P_POSTFIX, false, false, true };
    t->temps = grow(t->temps, &t->temps_cap, t->ntemps + 1, sizeof *t->temps);
    t->temps[t->ntemps++] = (Temp){ name, flag };
    t->subst[e] = t->ntemps;
}

static void
advance_list(Tr *t, u32 e)
{
    put_name(t, va_list_decl(t, e + 1));
    put(&t->out, " += 1");
}

static void
hoist_va_arg(Tr *t, u32 e, u32 root, bool after)
{
    refuse_branch(t, e, root, "`va_arg`");
    if (after) {
        t->posts = grow(t->posts, &t->posts_cap, t->nposts + 1, sizeof *t->posts);
        t->posts[t->nposts++] = e;
        return;
    }

    u32 name = declare_temp(t, "arg", "");
    putf(&t->out, "%s := ", text(t, name));
    Val v = va_arg_value(t, e);
    line(t);
    advance_list(t, e);
    line(t);
    t->temps = grow(t->temps, &t->temps_cap, t->ntemps + 1, sizeof *t->temps);
    t->temps[t->ntemps++] = (Temp){ name, v };
    t->subst[e] = t->ntemps;
}

static u32
hoist_increments(Tr *t, u32 root, bool after, bool used)
{
    u32 posts = t->nposts;
    bool calls = has_call(t, root);
    u32 lists = 0;
    for (u32 i = root; i < t->a.ends[root]; i++) {
        if (overflow_name(t, i) != NULL) {
            hoist_overflow(t, i, root);
            continue;
        }
        if (t->kinds[i] == K_VA_ARG) {
            if (lists++ > 0) {
                die(t, i, "two `va_arg`s in one statement");
            }
            hoist_va_arg(t, i, root, after);
            continue;
        }
        if (t->kinds[i] != K_UNARY || (op(t, i) != OP_INC && op(t, i) != OP_DEC) ||
            !value_used(t, i, root, used)) {
            continue;
        }
        refuse_branch(t, i, root, "`++` or `--`");
        if (has_effects(t, i + 1)) {
            die(t, i, "`++` or `--` used as a value on an operand with side effects");
        }

        if (!(t->a.flags[i] & AST_POSTFIX)) {
            increment(t, i);
            line(t);
            t->subst[i] = SUBST_OPERAND;
            continue;
        }
        if (after && (!calls || private_local(t, i + 1))) {
            t->posts = grow(t->posts, &t->posts_cap, t->nposts + 1, sizeof *t->posts);
            t->posts[t->nposts++] = i;
            t->subst[i] = SUBST_OPERAND;
            continue;
        }

        u32 name = declare_temp(t, last_name(t, i + 1), "_old");
        putf(&t->out, "%s := ", text(t, name));
        Val v = ex(t, i + 1, 0, 0);
        line(t);
        increment(t, i);
        line(t);
        t->temps = grow(t->temps, &t->temps_cap, t->ntemps + 1, sizeof *t->temps);
        t->temps[t->ntemps++] = (Temp){ name, v };
        t->subst[i] = t->ntemps;
    }
    return posts;
}

static void
finish_posts(Tr *t, u32 posts)
{
    for (u32 k = posts; k < t->nposts; k++) {
        line(t);
        if (t->kinds[t->posts[k]] == K_VA_ARG) {
            advance_list(t, t->posts[k]);
        } else {
            increment(t, t->posts[k]);
        }
    }
    t->nposts = posts;
}

static bool
needs_hoist(const Tr *t, u32 root)
{
    for (u32 i = root; i < t->a.ends[root]; i++) {
        if ((t->kinds[i] == K_UNARY && (op(t, i) == OP_INC || op(t, i) == OP_DEC) &&
             value_used(t, i, root, true)) ||
            overflow_name(t, i) != NULL || t->kinds[i] == K_VA_ARG) {
            return true;
        }
    }
    return false;
}

static void
put_var_decl(Tr *t, u32 v, u32 init)
{
    u32 type = ctype(t, v);
    u32 bits = t->bits[v];
    u32 name = declare_local(t, v, "");
    char *ty = type_text(t, type, bits);
    if (init == 0) {
        putf(&t->out, "%s: %s = undefined", text(t, name), ty);
        free(ty);
        return;
    }

    u64 at = t->out.len;
    Val val = value_as(t, init, type, bits);
    Out head = { 0 };
    if (same_val_type(t, val, type, bits) && !is_kind(t, type, CT_ARRAY)) {
        putf(&head, "%s := ", text(t, name));
    } else {
        putf(&head, "%s: %s = ", text(t, name), ty);
    }
    insert(&t->out, at, head.text, head.len);
    free(head.text);
    free(ty);
}

static void
branch_assign(Tr *t, u32 v, u32 value)
{
    put(&t->out, " {");
    t->depth++;
    u32 mark = t->nscope;
    line(t);
    u32 posts = hoist_increments(t, value, true, true);
    put_name(t, v);
    put(&t->out, " = ");
    value_as(t, value, ctype(t, v), t->bits[v]);
    finish_posts(t, posts);
    t->nscope = mark;
    t->depth--;
    line(t);
    put(&t->out, "}");
}

static void
var_decl(Tr *t, u32 v)
{
    if (t->a.details[v] != 0) {
        die(t, v, "a static or extern local");
    }

    line(t);
    u32 init = (t->a.flags[v] & AST_HAS_INIT) ? init_of(t, v) : 0;
    u32 x = init != 0 ? strip(t, init) : 0;
    if (x != 0 && t->kinds[x] == K_CONDITIONAL && !needs_hoist(t, child(t, x, 0)) &&
        (needs_hoist(t, child(t, x, 1)) || needs_hoist(t, child(t, x, 2)))) {
        put_var_decl(t, v, 0);
        line(t);
        put(&t->out, "if ");
        cond(t, child(t, x, 0));
        branch_assign(t, v, child(t, x, 1));
        put(&t->out, " else");
        branch_assign(t, v, child(t, x, 2));
        return;
    }
    if (x != 0 && overflow_name(t, x) != NULL) {
        assert(is_kind(t, ctype(t, v), CT_BOOL));
        put_var_decl(t, v, 0);
        line(t);
        put_overflow(t, x, t->names[v]);
        return;
    }
    u32 posts = init != 0 ? hoist_increments(t, init, true, true) : t->nposts;
    put_var_decl(t, v, init);
    finish_posts(t, posts);
}

static bool
is_assert_function(Tr *t, u32 f)
{
    const char *w = text(t, t->a.names[f]);
    return strcmp(w, "__assert_fail") == 0 || strcmp(w, "__assert_rtn") == 0;
}

static bool
calls(Tr *t, u32 e, const char *name)
{
    e = strip(t, e);
    if (t->kinds[e] != K_CALL) {
        return false;
    }
    u32 f = callee(t, e);
    return f != 0 && strcmp(text(t, t->a.names[f]), name) == 0;
}

static u32
strip_casts(Tr *t, u32 e)
{
    e = strip(t, e);
    while (is_cast(t, e)) {
        e = strip(t, e + 1);
    }
    return e;
}

static u32
asserted(Tr *t, u32 e)
{
    if (t->kinds[e] != K_CONDITIONAL) {
        return 0;
    }
    if (calls(t, child(t, e, 2), "__assert_fail")) {
        return child(t, e, 0);
    }
    if (!calls(t, child(t, e, 1), "__assert_rtn")) {
        return 0;
    }
    u32 expect = strip_casts(t, child(t, e, 0));
    assert(calls(t, expect, "__builtin_expect"));
    u32 not = strip_casts(t, t->a.ends[expect + 1]);
    assert(t->kinds[not] == K_UNARY && op(t, not) == OP_NOT);
    return not + 1;
}

static void
put_assert(Tr *t, u32 e)
{
    e = strip(t, e);
    if (t->kinds[e] == K_BINARY && op(t, e) == OP_LAND &&
        !is_string(t, child(t, e, 1))) {
        put_assert(t, child(t, e, 0));
        line(t);
        put_assert(t, child(t, e, 1));
        return;
    }
    put(&t->out, "assert(");
    cond_at(t, e, false);
    put(&t->out, ")");
}

static void
assign(Tr *t, u32 lhs, u32 rhs)
{
    u32 r = strip_noop(t, rhs);
    if (t->kinds[r] == K_BINARY && op(t, r) == OP_ASSIGN) {
        assign(t, child(t, r, 0), child(t, r, 1));
        line(t);
        Val l = ex(t, lhs, 0, 0);
        put(&t->out, " = ");
        value_as(t, child(t, r, 0), l.type, l.bits);
        return;
    }

    Val l = ex(t, lhs, 0, 0);
    put(&t->out, " = ");
    value_as(t, rhs, l.type, l.bits);
}

static const char *
assign_text(Op o, bool wrapping)
{
    switch (o) {
        case OP_ADD_ASSIGN: return wrapping ? " +%= " : " += ";
        case OP_SUB_ASSIGN: return wrapping ? " -%= " : " -= ";
        case OP_MUL_ASSIGN: return wrapping ? " *%= " : " *= ";
        case OP_DIV_ASSIGN: return " /= ";
        case OP_MOD_ASSIGN: return " %= ";
        case OP_SHL_ASSIGN: return " <<= ";
        case OP_SHR_ASSIGN: return " >>= ";
        case OP_AND_ASSIGN: return " &= ";
        case OP_OR_ASSIGN: return " |= ";
        case OP_XOR_ASSIGN: return " ^= ";
        default: return NULL;
    }
}

static void
compound_assign(Tr *t, u32 e)
{
    Op o = op(t, e);
    u32 l = child(t, e, 0);
    u32 r = child(t, e, 1);
    u32 type = ctype(t, l);
    if (is_kind(t, type, CT_POINTER)) {
        Val v = ex(t, l, 0, 0);
        if (v.bits & NULLABLE) {
            if (has_effects(t, l)) {
                die(t, e, "pointer arithmetic on a pointer that may be nil, with side effects"
                );
            }
            put(&t->out, " = ");
            u64 at = t->out.len;
            unwrap(t, at, ex(t, l, 0, 0));
            put(&t->out, " + ");
        } else {
            put(&t->out, " += ");
        }
        if (o == OP_SUB_ASSIGN) {
            negated_offset(t, r);
        } else {
            operand(t, r, P_LOWEST);
        }
        return;
    }

    if (is_kind(t, type, CT_BOOL) &&
        (o == OP_OR_ASSIGN || o == OP_AND_ASSIGN) && !has_effects(t, l)) {
        ex(t, l, 0, 0);
        put(&t->out, " = ");
        u64 at = t->out.len;
        if (cond(t, r) < (o == OP_OR_ASSIGN ? P_OR : P_AND)) {
            wrap(t, at, "(", ")");
        }
        put(&t->out, o == OP_OR_ASSIGN ? " or " : " and ");
        ex(t, l, 0, 0);
        return;
    }

    u32 lhs_type = ctype_of_string(t, t->a.arg_types[e], e);
    if (lhs_type == type) {
        ex(t, l, 0, 0);
        put(&t->out, assign_text(o, wraps(t, type)));
        operand(t, r, P_LOWEST);
        return;
    }

    bool bitwise = o == OP_OR_ASSIGN || o == OP_AND_ASSIGN || o == OP_XOR_ASSIGN;
    u32 x = strip(t, r);
    bool widened = is_cast(t, x) && op(t, x) == CAST_INTEGRAL && ctype(t, x + 1) == type;
    if (!is_kind(t, type, CT_INT) || !bitwise || (!widened && !constant_leaves(t, r))) {
        die(t, e, "a compound assignment that promotes its target");
    }
    ex(t, l, 0, 0);
    put(&t->out, assign_text(o, false));
    if (widened) {
        operand(t, x + 1, P_LOWEST);
    } else {
        put_leaves(t, r, type, true);
    }
}

enum
{
    ACCESS_NONE,
    ACCESS_LOAD,
    ACCESS_STORE,
};

static u32
scalar_bytes(const Tr *t, u32 type)
{
    const CType *c = &t->types[type];
    if (c->kind == CT_INT) {
        return c->bits / 8;
    }
    if (c->kind == CT_VECTOR) {
        return (u32)c->count * t->types[c->inner].bits / 8;
    }
    return 0;
}

static u32
void_operand(const Tr *t, u32 e)
{
    e = strip(t, e);
    while (t->kinds[e] == K_IMPLICIT_CAST &&
           (op(t, e) == CAST_BIT || op(t, e) == CAST_NOOP)) {
        e = strip(t, e + 1);
    }
    return e;
}

static u32
addressed_name(Tr *t, u32 e)
{
    e = void_operand(t, e);
    if (t->kinds[e] != K_UNARY || op(t, e) != OP_AND) {
        return 0;
    }
    u32 x = strip(t, e + 1);
    return t->kinds[x] == K_DECL_REF ? x : 0;
}

static bool
size_matches(Tr *t, u32 n, u32 type)
{
    u32 bytes = scalar_bytes(t, type);
    n = strip(t, n);
    if (bytes == 0) {
        return false;
    }
    if (t->kinds[n] == K_TYPE_TRAIT) {
        u32 arg = t->a.arg_types[n];
        u32 of = arg != 0 ? ctype_of_string(t, arg, n) : ctype(t, child(t, n, 0));
        return strcmp(text(t, t->a.names[n]), "sizeof") == 0 &&
               scalar_bytes(t, of) == bytes;
    }
    __int128 v;
    return constant_value(t, n, &v) && v == bytes;
}

static u8
memory_access(Tr *t, u32 call, u32 *var)
{
    u32 f = callee(t, call);
    if (f == 0 || strcmp(text(t, t->a.names[f]), "memcpy") != 0) {
        return ACCESS_NONE;
    }
    u32 to = t->a.ends[call + 1];
    u32 from = t->a.ends[to];
    u32 size = t->a.ends[from];

    u32 into = addressed_name(t, to);
    if (into != 0 && size_matches(t, size, ctype(t, into))) {
        *var = into;
        return ACCESS_LOAD;
    }
    u32 out = addressed_name(t, from);
    if (out != 0 && size_matches(t, size, ctype(t, out))) {
        *var = out;
        return ACCESS_STORE;
    }
    return ACCESS_NONE;
}

static void
put_memory_access(Tr *t, u32 call, u8 access, u32 var)
{
    u32 to = t->a.ends[call + 1];
    u32 from = t->a.ends[to];
    use_package(t, PACKAGE_MEM);
    if (access == ACCESS_LOAD) {
        ex(t, var, 0, 0);
        char *ty = type_text(t, ctype(t, var), 0);
        putf(&t->out, " = mem.unaligned_load(%s, ", ty);
        free(ty);
        many_pointer(t, void_operand(t, from));
    } else {
        put(&t->out, "mem.unaligned_store(");
        many_pointer(t, void_operand(t, to));
        put(&t->out, ", ");
        ex(t, var, 0, 0);
    }
    put(&t->out, ")");
}

static void
put_expr_stmt(Tr *t, u32 e)
{
    switch (t->kinds[e]) {
        case K_BINARY:
            if (op(t, e) == OP_ASSIGN) {
                assign(t, child(t, e, 0), child(t, e, 1));
                return;
            }
            break;
        case K_COMPOUND_ASSIGN: compound_assign(t, e); return;
        case K_UNARY:
            if (op(t, e) == OP_INC || op(t, e) == OP_DEC) {
                increment(t, e);
                return;
            }
            break;
        case K_CALL: {
            u32 var = 0;
            u8 access = memory_access(t, e, &var);
            if (access != ACCESS_NONE) {
                put_memory_access(t, e, access, var);
                return;
            }
            u32 f = callee(t, e);
            if (f != 0 && strcmp(text(t, t->a.names[f]), "__builtin_va_start") == 0) {
                put_name(t, va_list_decl(t, t->a.ends[e + 1]));
                put(&t->out, " = 0");
                return;
            }
            u32 result = f ? t->types[ctype(t, f)].inner : 0;
            if (result != 0 && is_kind(t, result, CT_VOID)) {
                ex(t, e, 0, 0);
                return;
            }
            break;
        }
        case K_CAST:
            if (op(t, e) == CAST_TO_VOID) {
                put(&t->out, "_ = ");
                ex(t, e + 1, 0, 0);
                return;
            }
            break;
        case K_CONDITIONAL:
            if (asserted(t, e) != 0) {
                put_assert(t, asserted(t, e));
                return;
            }
            break;
        default: break;
    }
    put(&t->out, "_ = ");
    ex(t, e, 0, 0);
}

static void
expr_stmt(Tr *t, u32 e)
{
    e = strip(t, e);
    if (t->kinds[e] == K_BINARY && op(t, e) == OP_COMMA) {
        expr_stmt(t, child(t, e, 0));
        line(t);
        expr_stmt(t, child(t, e, 1));
        return;
    }
    u32 posts = hoist_increments(t, e, true, false);
    put_expr_stmt(t, e);
    finish_posts(t, posts);
}

static void
if_stmt(Tr *t, u32 s)
{
    u32 c = strip(t, s + 1);
    if (!(t->a.flags[s] & AST_HAS_ELSE) && t->kinds[c] == K_BINARY &&
        op(t, c) == OP_LAND && needs_hoist(t, child(t, c, 1))) {
        hoist_increments(t, child(t, c, 0), false, true);
        put(&t->out, "if ");
        cond(t, child(t, c, 0));
        put(&t->out, " {");
        t->depth++;
        u32 mark = t->nscope;
        line(t);
        hoist_increments(t, child(t, c, 1), false, true);
        put(&t->out, "if ");
        cond(t, child(t, c, 1));
        block(t, child(t, s, 1));
        t->nscope = mark;
        t->depth--;
        line(t);
        put(&t->out, "}");
        return;
    }

    hoist_increments(t, s + 1, false, true);
    put(&t->out, "if ");
    cond(t, s + 1);
    block(t, child(t, s, 1));
    if (!(t->a.flags[s] & AST_HAS_ELSE)) {
        return;
    }
    u32 e = child(t, s, 2);
    put(&t->out, " else ");
    if (t->kinds[e] == K_IF && !needs_hoist(t, e + 1)) {
        if_stmt(t, e);
    } else {
        braced(t, e);
    }
}

static void
loop_body(Tr *t, u32 loop, u32 body, u32 first, u32 step)
{
    bool label = step != 0 && has_continue(t, body);
    u32 id = t->labels++;
    push_target(t, loop, false, label ? id : UINT32_MAX);

    put(&t->out, " {");
    t->depth++;
    u32 mark = t->nscope;
    if (first != 0) {
        line(t);
        increment(t, first);
    }
    if (label) {
        line(t);
        putf(&t->out, "next%u:", id);
        block(t, body);
    } else if (t->kinds[body] == K_COMPOUND) {
        statements(t, body + 1, t->a.ends[body]);
    } else {
        stmt(t, body);
    }
    t->nscope = mark;
    if (step != 0) {
        line(t);
        expr_stmt(t, step);
    }
    t->depth--;
    line(t);
    put(&t->out, "}");
    t->ntargets--;
}

static bool
writes(Tr *t, u32 root, u32 decl)
{
    for (u32 i = root; i < t->a.ends[root]; i++) {
        u32 target = write_target(t, i);
        if (target != 0 && t->kinds[target] == K_DECL_REF && decl_of(t, target) == decl) {
            return true;
        }
    }
    return false;
}

static bool
for_range(Tr *t, u32 s)
{
    u32 init = child(t, s, 0);
    u32 c = child(t, s, 2);
    u32 inc = child(t, s, 3);
    u32 body = child(t, s, 4);
    if (t->kinds[init] != K_DECL_STMT || nchildren(t, init) != 1 ||
        t->kinds[c] != K_BINARY || op(t, c) != OP_LT ||
        t->kinds[inc] != K_UNARY || op(t, inc) != OP_INC) {
        return false;
    }
    u32 v = init + 1;
    if (t->kinds[v] != K_VAR || !(t->a.flags[v] & AST_HAS_INIT) ||
        !is_kind(t, ctype(t, v), CT_INT)) {
        return false;
    }

    u32 l = strip(t, child(t, c, 0));
    if (!(is_cast(t, l) && op(t, l) == CAST_LVALUE)) {
        return false;
    }
    l = strip(t, l + 1);
    u32 x = strip(t, inc + 1);
    if (t->kinds[l] != K_DECL_REF || decl_of(t, l) != v ||
        t->kinds[x] != K_DECL_REF || decl_of(t, x) != v || writes(t, body, v)) {
        return false;
    }

    u32 bound = strip(t, child(t, c, 1));
    __int128 k;
    bool constant = constant_value(t, bound, &k);
    if (!constant) {
        u32 b = bound;
        if (!(is_cast(t, b) && op(t, b) == CAST_LVALUE)) {
            return false;
        }
        b = strip(t, b + 1);
        if (t->kinds[b] != K_DECL_REF) {
            return false;
        }
        u32 d = decl_of(t, b);
        if ((t->kinds[d] != K_VAR && t->kinds[d] != K_PARAM) || writes(t, body, d) ||
            !same_chibi_type(t, ctype(t, b), 0, ctype(t, v), 0)) {
            return false;
        }
    }

    u32 mark = t->nscope;
    u32 name = declare_local(t, v, "");
    putf(&t->out, "for %s in ", text(t, name));
    u32 start = init_of(t, v);
    u64 at = t->out.len;
    Val a = ex(t, start, 0, 0);
    if (constant && a.untyped &&
        !(is_kind(t, ctype(t, v), CT_INT) && t->types[ctype(t, v)].bits == 64 &&
          t->types[ctype(t, v)].is_signed)) {
        char *ty = type_text(t, ctype(t, v), 0);
        Out head = { 0 };
        putf(&head, "%s(", ty);
        insert(&t->out, at, head.text, head.len);
        put(&t->out, ")");
        free(head.text);
        free(ty);
    }
    put(&t->out, "..");
    ex(t, bound, 0, 0);
    loop_body(t, s, body, 0, 0);
    t->nscope = mark;
    return true;
}

static bool
for_countdown(Tr *t, u32 s)
{
    u32 init = child(t, s, 0);
    u32 c = child(t, s, 2);
    u32 inc = child(t, s, 3);
    u32 body = child(t, s, 4);
    if (t->kinds[init] != K_DECL_STMT || nchildren(t, init) != 1 ||
        t->kinds[c] != K_BINARY || op(t, c) != OP_GT || t->kinds[inc] != K_NONE) {
        return false;
    }
    u32 v = init + 1;
    u32 dec = strip(t, child(t, c, 0));
    if (is_cast(t, dec) && op(t, dec) == CAST_INTEGRAL) {
        dec = strip(t, dec + 1);
    }
    if (t->kinds[v] != K_VAR || !is_kind(t, ctype(t, v), CT_INT) ||
        t->kinds[dec] != K_UNARY || op(t, dec) != OP_DEC ||
        !(t->a.flags[dec] & AST_POSTFIX)) {
        return false;
    }
    u32 x = strip(t, dec + 1);
    u32 bound = strip(t, child(t, c, 1));
    __int128 k;
    if (t->kinds[x] != K_DECL_REF || decl_of(t, x) != v ||
        (constant_value(t, bound, &k) ? k < 0 : needs_hoist(t, bound))) {
        return false;
    }

    u32 mark = t->nscope;
    put(&t->out, "{");
    t->depth++;
    stmt(t, init);
    line(t);
    put(&t->out, "while ");
    put_name(t, v);
    put(&t->out, " > ");
    ex(t, bound, 0, 0);
    loop_body(t, s, body, dec, 0);
    t->depth--;
    line(t);
    put(&t->out, "}");
    t->nscope = mark;
    return true;
}

static void
for_stmt(Tr *t, u32 s)
{
    if (for_range(t, s) || for_countdown(t, s)) {
        return;
    }

    u32 init = child(t, s, 0);
    u32 c = child(t, s, 2);
    u32 inc = child(t, s, 3);
    u32 body = child(t, s, 4);
    if (t->kinds[c] != K_NONE && needs_hoist(t, c)) {
        die(t, c, "`++`, `--`, `va_arg` or an overflow builtin used as a value in a `for` condition"
        );
    }
    bool scoped = t->kinds[init] == K_DECL_STMT;
    u32 mark = t->nscope;
    if (scoped) {
        put(&t->out, "{");
        t->depth++;
        stmt(t, init);
        line(t);
    } else if (t->kinds[init] != K_NONE) {
        expr_stmt(t, init);
        line(t);
    }
    put(&t->out, "while ");
    if (t->kinds[c] == K_NONE) {
        put(&t->out, "true");
    } else {
        cond(t, c);
    }
    loop_body(t, s, body, 0, t->kinds[inc] == K_NONE ? 0 : inc);
    if (scoped) {
        t->depth--;
        line(t);
        put(&t->out, "}");
    }
    t->nscope = mark;
}

static void
hoisted_while(Tr *t, u32 s)
{
    u32 c = s + 1;
    u32 body = child(t, s, 1);
    push_target(t, s, false, UINT32_MAX);
    u32 x = strip(t, c);
    put(&t->out, "while ");
    if (t->kinds[x] == K_BINARY && op(t, x) == OP_LAND &&
        !needs_hoist(t, child(t, x, 0))) {
        cond(t, child(t, x, 0));
        c = child(t, x, 1);
    } else {
        put(&t->out, "true");
    }
    put(&t->out, " {");
    t->depth++;
    u32 mark = t->nscope;
    line(t);
    hoist_increments(t, c, false, true);
    put(&t->out, "if ");
    cond_at(t, c, true);
    put(&t->out, " {");
    t->depth++;
    line(t);
    put(&t->out, "break");
    t->depth--;
    line(t);
    put(&t->out, "}");
    if (t->kinds[body] == K_COMPOUND) {
        statements(t, body + 1, t->a.ends[body]);
    } else {
        stmt(t, body);
    }
    t->nscope = mark;
    t->depth--;
    line(t);
    put(&t->out, "}");
    t->ntargets--;
}

static void
do_stmt(Tr *t, u32 s)
{
    u32 body = child(t, s, 0);
    u32 c = child(t, s, 1);
    bool label = has_continue(t, body);
    u32 id = t->labels++;
    push_target(t, s, false, label ? id : UINT32_MAX);
    put(&t->out, "while true {");
    t->depth++;
    u32 mark = t->nscope;
    if (label) {
        line(t);
        putf(&t->out, "next%u:", id);
        block(t, body);
    } else if (t->kinds[body] == K_COMPOUND) {
        statements(t, body + 1, t->a.ends[body]);
    } else {
        stmt(t, body);
    }
    t->nscope = mark;
    line(t);
    hoist_increments(t, c, false, true);
    put(&t->out, "if ");
    cond_at(t, c, true);
    put(&t->out, " {");
    t->depth++;
    line(t);
    put(&t->out, "break");
    t->depth--;
    line(t);
    put(&t->out, "}");
    t->depth--;
    line(t);
    put(&t->out, "}");
    t->ntargets--;
}

static void
case_value(Tr *t, u32 c)
{
    u32 v = child(t, c, 0);
    ex(t, v, 0, 0);
}

static void
switch_stmt(Tr *t, u32 s)
{
    u32 value = child(t, s, 0);
    u32 body = child(t, s, 1);
    if (t->kinds[body] != K_COMPOUND) {
        die(t, s, "a switch whose body isn't a block");
    }

    u32 total = count_breaks(t, body);
    u32 final = 0;
    bool has_default = false;
    u32 first = body + 1;
    for (u32 c = first; c < t->a.ends[body]; c = t->a.ends[c]) {
        u32 next = t->a.ends[c];
        bool arm_end = next >= t->a.ends[body] || t->kinds[next] == K_CASE ||
                       t->kinds[next] == K_DEFAULT;
        u32 last = c;
        while (t->kinds[last] == K_CASE || t->kinds[last] == K_DEFAULT) {
            has_default |= t->kinds[last] == K_DEFAULT;
            last = child(t, last, t->kinds[last] == K_CASE ? 1 : 0);
        }
        if (arm_end && t->kinds[last] == K_BREAK) {
            final++;
        }
    }

    hoist_increments(t, value, false, true);
    u32 id = t->labels++;
    bool label = total != final;
    if (label) {
        putf(&t->out, "sw%u: ", id);
    }
    push_target(t, s, true, label ? id : UINT32_MAX);
    put(&t->out, "switch ");
    operand(t, value, P_LOWEST);
    put(&t->out, " {");
    t->depth++;

    for (u32 c = first; c < t->a.ends[body];) {
        if (t->kinds[c] != K_CASE && t->kinds[c] != K_DEFAULT) {
            die(t, c, "a statement before the first case");
        }
        line(t);
        u32 last = c;
        bool first_label = true;
        bool is_default = false;
        while (t->kinds[last] == K_CASE || t->kinds[last] == K_DEFAULT) {
            if (t->kinds[last] == K_DEFAULT) {
                is_default = true;
                last = child(t, last, 0);
                continue;
            }
            put(&t->out, first_label ? "" : ", ");
            case_value(t, last);
            first_label = false;
            last = child(t, last, 1);
        }
        if (is_default) {
            put(&t->out, first_label ? "_" : ", _");
        }
        put(&t->out, " => {");
        t->depth++;
        u32 mark = t->nscope;

        u32 stmts[4096];
        u32 n = 0;
        stmts[n++] = last;
        u32 next = t->a.ends[c];
        while (next < t->a.ends[body] && t->kinds[next] != K_CASE &&
               t->kinds[next] != K_DEFAULT) {
            if (n == 4096) {
                die(t, next, "a switch arm with more than 4096 statements");
            }
            stmts[n++] = next;
            next = t->a.ends[next];
        }
        if (t->kinds[stmts[n - 1]] == K_BREAK) {
            n--;
        } else if (next < t->a.ends[body] && !terminal(t, stmts[n - 1])) {
            die(t, stmts[n - 1], "a switch arm that falls through");
        }
        for (u32 k = 0; k < n; k++) {
            if (t->kinds[stmts[k]] != K_NULL_STMT) {
                stmt(t, stmts[k]);
            }
        }
        t->nscope = mark;
        t->depth--;
        if (n > 0) {
            line(t);
        }
        put(&t->out, "}");
        c = next;
    }
    if (!has_default) {
        line(t);
        put(&t->out, "_ => {}");
    }
    t->depth--;
    line(t);
    put(&t->out, "}");
    t->ntargets--;
}

static void
return_stmt(Tr *t, u32 s)
{
    if (t->a.ends[s] == s + 1) {
        put(&t->out, "return");
        return;
    }
    hoist_increments(t, s + 1, false, true);
    u32 fn = t->canon[t->fn];
    u32 type = t->types[ctype(t, fn)].inner;
    put(&t->out, "return ");
    value_as(t, s + 1, type, t->bits[fn]);
}

static void
stmt(Tr *t, u32 s)
{
    switch (t->kinds[s]) {
        case K_NULL_STMT: return;
        case K_CALL: {
            u32 f = callee(t, s);
            if (f != 0 && strcmp(text(t, t->a.names[f]), "__builtin_va_end") == 0) {
                return;
            }
            break;
        }
        case K_DECL_STMT:
            for (u32 c = s + 1; c < t->a.ends[s]; c = t->a.ends[c]) {
                if (t->kinds[c] == K_STATIC_ASSERT ||
                    (t->kinds[c] == K_VAR && global_var(t, c))) {
                    continue;
                }
                if (t->kinds[c] == K_VAR) {
                    var_decl(t, c);
                } else if (t->kinds[c] != K_RECORD && t->kinds[c] != K_TYPEDEF) {
                    die(t, c, "an unsupported local declaration");
                }
            }
            return;
        default: break;
    }

    line(t);
    switch (t->kinds[s]) {
        case K_COMPOUND: braced(t, s); return;
        case K_IF: if_stmt(t, s); return;
        case K_WHILE:
            if (needs_hoist(t, s + 1)) {
                hoisted_while(t, s);
                return;
            }
            put(&t->out, "while ");
            cond(t, s + 1);
            loop_body(t, s, child(t, s, 1), 0, 0);
            return;
        case K_DO: do_stmt(t, s); return;
        case K_FOR: for_stmt(t, s); return;
        case K_SWITCH: switch_stmt(t, s); return;
        case K_RETURN: return_stmt(t, s); return;
        case K_BREAK: {
            Target g = t->targets[t->ntargets - 1];
            if (g.is_switch) {
                if (g.label == UINT32_MAX) {
                    die(t, s, "a break out of a switch with no label");
                }
                putf(&t->out, "break sw%u", g.label);
            } else {
                put(&t->out, "break");
            }
            return;
        }
        case K_CONTINUE: {
            u32 k = t->ntargets;
            while (k > 0 && t->targets[k - 1].is_switch) {
                k--;
            }
            if (k == 0) {
                die(t, s, "a continue outside a loop");
            }
            Target g = t->targets[k - 1];
            if (g.label != UINT32_MAX) {
                putf(&t->out, "break next%u", g.label);
            } else {
                put(&t->out, "continue");
            }
            return;
        }
        default: expr_stmt(t, s); return;
    }
}

static const char *
source_text(Tr *t, u32 file)
{
    for (u32 i = 0; i < t->nsources; i++) {
        if (t->sources[i].file == file) {
            return t->sources[i].text;
        }
    }
    if (t->nsources == 256) {
        return NULL;
    }

    char *data = NULL;
    FILE *f = fopen(text(t, file), "rb");
    if (f != NULL) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc((size_t)size + 1);
        if (fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        } else {
            data[size] = 0;
        }
        fclose(f);
    }
    t->sources[t->nsources++] = (Source){ file, data };
    return data;
}

static void
mark_type(Tr *t, u32 node, u32 type)
{
    if (type == 0) {
        return;
    }
    CType c = t->types[type];
    switch (c.kind) {
        case CT_POINTER:
            if (t->types[c.inner].kind == CT_FUNCTION && t->types[c.inner].variadic) {
                die(t, node, "a pointer to a variadic function");
            }
            mark_type(t, node, c.inner);
            break;
        case CT_ARRAY: mark_type(t, node, c.inner); break;
        case CT_FUNCTION:
            mark_type(t, node, c.inner);
            for (u32 k = 0; k < c.nparams; k++) {
                mark_type(t, node, t->params[c.first + k]);
            }
            break;
        case CT_RECORD:
            if (!project_file(t, t->a.files[c.decl]) || (t->marks[c.decl] & MARK_EMIT)) {
                break;
            }
            t->marks[c.decl] |= MARK_EMIT;
            for (u32 f = c.decl + 1; f < t->a.ends[c.decl]; f = t->a.ends[f]) {
                if (t->kinds[f] == K_FIELD) {
                    mark_type(t, f, ctype(t, f));
                }
            }
            break;
        default: break;
    }
}

static bool
has_type_text(const Tr *t, u32 sid)
{
    return sid != 0 && text(t, sid)[0] != '<';
}

static bool
builtin_function(const Tr *t, u32 f)
{
    return strncmp(text(t, t->a.names[f]), "__builtin_", 10) == 0;
}

static bool
replaced_call(Tr *t, u32 ref)
{
    u32 call = t->parents[t->parents[ref]];
    u32 var = 0;
    return t->kinds[call] == K_CALL && t->kinds[t->parents[call]] == K_COMPOUND &&
           memory_access(t, call, &var) != ACCESS_NONE;
}

static bool
address_escapes(const Tr *t, u32 e)
{
    for (;;) {
        u32 p = use_of(t, &e);
        if (t->kinds[p] == K_MEMBER && !(t->a.flags[p] & AST_ARROW)) {
            e = p;
            continue;
        }
        if (!is_cast(t, p)) {
            return t->kinds[p] != K_TYPE_TRAIT;
        }
        Op c = op(t, p);
        if (c == CAST_LVALUE) {
            return false;
        }
        if (c == CAST_NOOP) {
            e = p;
            continue;
        }
        if (c != CAST_ARRAY_DECAY) {
            return true;
        }
        u32 s = use_of(t, &p);
        if (t->kinds[s] != K_SUBSCRIPT || p != s + 1) {
            return true;
        }
        e = s;
    }
}

static void
mark_uses(Tr *t, u32 root, u32 *work, u32 *nwork)
{
    for (u32 i = root; i < t->a.ends[root]; i++) {
        if (t->kinds[i] == K_STATIC_ASSERT) {
            i = t->a.ends[i] - 1;
            continue;
        }
        bool decay = is_cast(t, i) && (op(t, i) == CAST_FUNCTION_DECAY ||
                                       op(t, i) == CAST_BUILTIN_DECAY);
        if (has_type_text(t, t->a.types[i]) && t->kinds[i] != K_DECL_REF && !decay) {
            mark_type(t, i, ctype(t, i));
        }
        if (t->a.arg_types[i] != 0) {
            mark_type(t, i, ctype_of_string(t, t->a.arg_types[i], i));
        }
        if (t->kinds[i] != K_DECL_REF) {
            continue;
        }

        u32 d = decl_of(t, i);
        if (t->kinds[d] == K_ENUM_CONSTANT) {
            t->marks[d] |= MARK_EMIT;
            continue;
        }
        if (global_var(t, d)) {
            if (address_escapes(t, i)) {
                t->marks[d] |= MARK_ADDRESSED;
            }
            if (!(t->marks[d] & MARK_EMIT)) {
                t->marks[d] |= MARK_EMIT;
                work[(*nwork)++] = d;
            }
            continue;
        }
        if (t->kinds[d] != K_FUNCTION || builtin_function(t, d) ||
            chibi_builtin(t, d) != NULL || replaced_call(t, i) ||
            (t->marks[d] & (MARK_EMIT | MARK_EXTERN))) {
            continue;
        }
        u32 def = t->defs[d];
        if (is_assert_function(t, d)) {
            t->marks[d] |= MARK_EXTERN;
        } else if (def != 0 && project_file(t, t->a.files[def])) {
            t->marks[d] |= MARK_EMIT;
            work[(*nwork)++] = def;
        } else {
            t->marks[d] |= MARK_EXTERN;
            mark_type(t, i, ctype(t, d));
        }
    }
}

static u8
packages_used(Tr *t, u32 fn)
{
    u8 used = 0;
    for (u32 i = fn; i < t->a.ends[fn]; i++) {
        u32 f = t->kinds[i] == K_CALL ? callee(t, i) : 0;
        const char *w = f != 0 ? text(t, t->a.names[f]) : "";
        const char *builtin = f != 0 ? chibi_builtin(t, f) : NULL;
        if (bit_count_name(w) != NULL || overflow_name(t, i) != NULL) {
            used |= 1u << PACKAGE_BITS;
        } else if (builtin != NULL || lane_compare(t, i) != NULL) {
            used |= 1u << PACKAGE_SIMD;
        } else if (strcmp(w, "memcpy") == 0 || t->kinds[i] == K_TYPE_TRAIT ||
                   (t->kinds[i] == K_BINARY && op(t, i) == OP_SUB &&
                    is_pointer(t, child(t, i, 0)))) {
            used |= 1u << PACKAGE_MEM;
        }
    }
    return used;
}

static void
mark_assigned_params(Tr *t, u32 fn)
{
    for (u32 i = fn; i < t->a.ends[fn]; i++) {
        u32 target = write_target(t, i);
        while (target != 0 && t->kinds[target] == K_MEMBER &&
               !(t->a.flags[target] & AST_ARROW)) {
            target = strip(t, target + 1);
        }
        if (target != 0 && t->kinds[target] == K_DECL_REF) {
            u32 d = decl_of(t, target);
            if (t->kinds[d] == K_PARAM) {
                t->marks[d] |= MARK_ASSIGNED;
            }
        }
    }
}

static void
put_params(Tr *t, u32 fn, bool locals)
{
    put(&t->out, "(");
    u32 k = 0;
    for (;; k++) {
        u32 p = param_of(t, fn, k);
        if (p == 0) {
            break;
        }
        u32 c = t->canon[p];
        put(&t->out, k > 0 ? ", " : "");
        if (locals) {
            u32 name = declare_local(t, c, (t->marks[c] & MARK_ASSIGNED) ? "_in" : "");
            put(&t->out, text(t, name));
        } else if (t->a.names[p] != 0 && !reserved(text(t, t->a.names[p]))) {
            put(&t->out, text(t, t->a.names[p]));
        } else {
            putf(&t->out, "p%u", k);
        }
        char *ty = type_text(t, ctype(t, p), t->bits[c]);
        putf(&t->out, ": %s", ty);
        free(ty);
    }
    if (t->types[ctype(t, fn)].variadic && locals) {
        t->rest = declare_temp(t, "args", "");
        putf(&t->out, "%s%s: ..u64", k > 0 ? ", " : "", text(t, t->rest));
    } else if (t->types[ctype(t, fn)].variadic) {
        put(&t->out, k > 0 ? ", .." : "..");
    }
    put(&t->out, ")");

    u32 c = t->canon[fn];
    u32 result = t->types[ctype(t, fn)].inner;
    if (!is_kind(t, result, CT_VOID)) {
        char *ty = type_text(t, result, t->bits[c]);
        putf(&t->out, " -> %s", ty);
        free(ty);
    }
}

static void
emit_extern(Tr *t, u32 f)
{
    if (is_assert_function(t, f)) {
        return;
    }
    putf(&t->out, "extern fn %s", text(t, t->a.names[f]));
    put_params(t, f, false);
    put(&t->out, "\n");
}

static void
emit_record(Tr *t, u32 r)
{
    if (strcmp(text(t, t->a.details[r]), "struct") != 0) {
        die(t, r, "a union");
    }
    putf(&t->out, "\nstruct %s {", text(t, t->names[r]));
    for (u32 f = r + 1; f < t->a.ends[r]; f = t->a.ends[f]) {
        if (t->kinds[f] == K_RECORD) {
            die(t, f, "a nested record");
        }
        if (t->kinds[f] != K_FIELD) {
            continue;
        }
        char *ty = type_text(t, ctype(t, f), t->bits[f]);
        putf(&t->out, "\n    %s: %s", text(t, t->a.names[f]), ty);
        if ((t->marks[f] & MARK_UNDEFINED) && !(t->bits[f] & NULLABLE)) {
            put(&t->out, " = undefined");
        }
        put(&t->out, ",");
        free(ty);
    }
    put(&t->out, "\n}\n");
}

static void
emit_enum_constants(Tr *t, u32 e)
{
    bool first = true;
    for (u32 c = e + 1; c < t->a.ends[e]; c = t->a.ends[c]) {
        if (t->kinds[c] != K_ENUM_CONSTANT || !(t->marks[c] & MARK_EMIT)) {
            continue;
        }
        put(&t->out, first ? "\n" : "");
        first = false;
        putf(&t->out, "const %s := ", text(t, t->names[c]));
        put_int(t, enum_value(t, c));
        put(&t->out, "\n");
    }
}

static bool
const_object(const Tr *t, u32 v)
{
    const char *s = text(t, t->a.types[v]);
    const char *end = strchr(s, '[');
    end = end ? end : s + strlen(s);
    const char *from = s;
    for (const char *p = s; p < end; p++) {
        if (*p == '*') {
            from = p + 1;
        }
    }
    for (const char *p = from; p < end; p++) {
        if ((p == s || !is_ident_byte(p[-1])) && starts_word(p, "const")) {
            return true;
        }
    }
    return false;
}

static bool
is_extern_var(const Tr *t, u32 v)
{
    return t->a.details[v] != 0 && strcmp(text(t, t->a.details[v]), "extern") == 0;
}

static void
emit_extern_global(Tr *t, u32 v)
{
    char *ty = type_text(t, ctype(t, v), t->bits[v]);
    putf(&t->out, "extern %s: %s\n", text(t, t->names[v]), ty);
    free(ty);
}

static void
emit_global(Tr *t, u32 v)
{
    u32 type = ctype(t, v);
    u32 bits = t->bits[v];
    bool constant = const_object(t, v) && !(t->marks[v] & MARK_ADDRESSED);
    u32 init = (t->a.flags[v] & AST_HAS_INIT) ? init_of(t, v) : 0;
    putf(&t->out, "\n%s%s", constant ? "const " : "", text(t, t->names[v]));
    if (is_kind(t, type, CT_RECORD) && (init != 0 || constant)) {
        put(&t->out, " := ");
        put_init(t, init, type, bits, true);
        put(&t->out, "\n");
        return;
    }

    char *ty = type_text(t, type, bits);
    putf(&t->out, ": %s", ty);
    free(ty);
    if (init != 0 || constant) {
        put(&t->out, " = ");
        put_init(t, init, type, bits, true);
    }
    put(&t->out, "\n");
}

static bool
calls_later(const Tr *t, u32 def)
{
    for (u32 i = def; i < t->a.ends[def]; i++) {
        u32 f = t->kinds[i] == K_CALL ? callee(t, i) : 0;
        if (f != 0 && t->defs[f] >= def) {
            return true;
        }
    }
    return false;
}

static void
emit_function(Tr *t, u32 def)
{
    u32 c = t->canon[def];
    bool exported = t->a.details[c] == 0 ||
                    strcmp(text(t, t->a.details[c]), "static") != 0;
    if (t->types[ctype(t, def)].variadic && exported) {
        die(t, def, "an exported variadic function");
    }
    t->fn = def;
    t->rest = 0;
    t->nscope = 0;
    t->depth = 0;
    t->ntargets = 0;
    t->fn_packages = packages_used(t, def);
    mark_assigned_params(t, def);

    const char *mark = child_of_kind(t, def, K_ALWAYS_INLINE) ? "@inline "
                     : child_of_kind(t, def, K_NOINLINE)      ? "@noinline "
                                                              : "";
    putf(&t->out, "\n%s%sfn %s", mark, exported ? "export " : "", text(t, t->names[c]));
    put_params(t, def, true);
    if (is_kind(t, t->types[ctype(t, def)].inner, CT_VOID) && calls_later(t, def)) {
        put(&t->out, " -> void");
    }
    put(&t->out, " {");
    t->depth = 1;
    for (u32 k = 0;; k++) {
        u32 p = param_of(t, def, k);
        if (p == 0) {
            break;
        }
        u32 pc = t->canon[p];
        if (t->marks[pc] & MARK_ASSIGNED) {
            u32 in = t->names[pc];
            u32 name = declare_local(t, pc, "");
            line(t);
            putf(&t->out, "%s := %s", text(t, name), text(t, in));
        }
    }
    u32 body = function_body(t, def);
    statements(t, body + 1, t->a.ends[body]);
    t->depth = 0;
    put(&t->out, "\n}\n");
}

static bool
top_level(const Tr *t, u32 i)
{
    u8 k = t->kinds[i];
    return (t->marks[i] & (MARK_EMIT | MARK_EXTERN)) &&
           (k == K_FUNCTION || k == K_VAR || k == K_RECORD || k == K_ENUM_CONSTANT);
}

static bool
static_local(const Tr *t, u32 v)
{
    return t->kinds[v] == K_VAR && (t->marks[v] & MARK_EMIT) &&
           t->kinds[t->parents[v]] == K_DECL_STMT;
}

static u32 *
top_names(Tr *t, u32 extra, u32 *mask)
{
    u32 size = 1024;
    while (size < (t->a.strings.count + extra) * 2) {
        size *= 2;
    }
    *mask = size - 1;
    return calloc(size, sizeof(u32) * 2);
}

static u32
claim_name(const Tr *t, u32 *table, u32 mask, u32 d)
{
    u32 name = t->names[d];
    for (u32 s = (u32)((name * 0x9e3779b97f4a7c15ULL) >> 32) & mask;;
         s = (s + 1) & mask) {
        if (table[2 * s + 1] == 0) {
            table[2 * s] = name;
            table[2 * s + 1] = d + 1;
            return d;
        }
        if (table[2 * s] == name) {
            return table[2 * s + 1] - 1;
        }
    }
}

static void
name_static_locals(Tr *t)
{
    u32 n = t->a.count;
    u32 locals = 0;
    for (u32 v = 0; v < n; v++) {
        locals += static_local(t, v);
    }
    u32 mask;
    u32 *table = top_names(t, locals, &mask);
    for (u32 i = 0; i < n; i++) {
        if (top_level(t, i) && !static_local(t, i)) {
            claim_name(t, table, mask, i);
        }
    }

    for (u32 v = 0; v < n; v++) {
        if (!static_local(t, v)) {
            continue;
        }
        u32 fn = t->parents[v];
        while (t->kinds[fn] != K_FUNCTION) {
            fn = t->parents[fn];
        }

        const char *f = text(t, t->a.names[fn]);
        const char *base = text(t, t->a.names[v]);
        char buf[256];
        snprintf(buf, sizeof buf, "%s_%s", f, base);
        t->names[v] = intern_text(t, buf);
        for (u32 k = 2; claim_name(t, table, mask, v) != v; k++) {
            snprintf(buf, sizeof buf, "%s_%s%u", f, base, k);
            t->names[v] = intern_text(t, buf);
        }
    }
    free(table);
}

static void
name_functions(Tr *t)
{
    for (u32 f = 0; f < t->a.count; f++) {
        if (t->kinds[f] != K_FUNCTION || t->canon[f] != f ||
            !(t->marks[f] & MARK_EMIT) || !reserved(text(t, t->a.names[f]))) {
            continue;
        }
        if (t->a.details[f] == 0 || strcmp(text(t, t->a.details[f]), "static") != 0) {
            die(t, f, "an exported function whose name chibi reserves");
        }
        char buf[256];
        snprintf(buf, sizeof buf, "%s_", text(t, t->a.names[f]));
        t->names[f] = intern_text(t, buf);
    }
}

static u32
group_of_file(Tr *t, u32 file)
{
    if (!project_file(t, file)) {
        return 0;
    }
    if (t->file_groups[file] != 0) {
        return t->file_groups[file];
    }

    const char *path = text(t, file);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    u32 name = strings_intern(&t->a.strings, base, dot ? (u32)(dot - base) : (u32)strlen(base));
    u32 g = 1;
    while (g < t->ngroups && t->groups[g].name != name) {
        g++;
    }
    if (g == t->ngroups) {
        if (name == t->groups[0].name || t->ngroups == 256) {
            fprintf(stderr, "translate: %s: no chibi file can hold it\n", path);
            exit(1);
        }
        t->groups[t->ngroups++] = (Group){ .name = name };
    }
    t->file_groups[file] = g;
    return g;
}

static u32
group_of(Tr *t, u32 d)
{
    u32 home = d;
    if (t->kinds[d] == K_FUNCTION) {
        home = t->defs[t->canon[d]] ? t->defs[t->canon[d]] : d;
    } else if (t->kinds[d] == K_VAR && t->kinds[t->parents[d]] == K_DECL_STMT) {
        while (t->kinds[home] != K_FUNCTION) {
            home = t->parents[home];
        }
    }
    return group_of_file(t, t->a.files[home]);
}

static void
switch_group(Tr *t, u32 g)
{
    t->groups[t->group].out = t->out;
    t->out = t->groups[g].out;
    t->group = g;
}

static bool
internal(const Tr *t, u32 d)
{
    u32 storage = t->a.details[d];
    return (t->kinds[d] == K_FUNCTION || t->kinds[d] == K_VAR) &&
           storage != 0 && strcmp(text(t, storage), "static") == 0;
}

static void
name_clashes(Tr *t)
{
    u32 n = t->a.count;
    u32 mask;
    u32 *table = top_names(t, 0, &mask);
    u8 *clash = calloc(n, 1);
    for (u32 i = 0; i < n; i++) {
        if (!top_level(t, i)) {
            continue;
        }
        u32 first = claim_name(t, table, mask, i);
        if (first != i) {
            clash[first] = 1;
            clash[i] = 1;
        }
    }
    free(table);

    for (u32 i = 0; i < n; i++) {
        if (!clash[i]) {
            continue;
        }
        if (!internal(t, i)) {
            if (t->kinds[i] == K_FUNCTION || t->kinds[i] == K_VAR) {
                continue;
            }
            die(t, i, "a name that another file of the program also declares");
        }
        char buf[256];
        snprintf(
            buf,
            sizeof buf,
            "%s_%s",
            text(t, t->groups[group_of(t, i)].name),
            text(t, t->names[i])
        );
        t->names[i] = intern_text(t, buf);
    }
    free(clash);

    table = top_names(t, 0, &mask);
    for (u32 i = 0; i < n; i++) {
        u32 first = top_level(t, i) ? claim_name(t, table, mask, i) : i;
        if (first != i) {
            die(t, internal(t, i) ? i : first, "a renamed declaration whose new name is taken"
            );
        }
    }
    free(table);
}

static void
emit_program(Tr *t)
{
    u32 n = t->a.count;
    u32 *work = malloc(n * sizeof *work);
    u32 nwork = 0;
    t->subst = calloc(n, sizeof *t->subst);
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_FUNCTION && function_body(t, i) != 0 &&
            t->main_files[t->a.files[i]] && t->defs[t->canon[i]] == i) {
            t->marks[t->canon[i]] |= MARK_EMIT;
            work[nwork++] = i;
        }
    }
    for (u32 k = 0; k < nwork; k++) {
        u32 def = work[k];
        mark_type(t, def, ctype(t, def));
        mark_uses(t, def, work, &nwork);
    }
    name_static_locals(t);
    name_functions(t);
    name_clashes(t);

    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_ENUM && t->alias[i] == i) {
            switch_group(t, group_of(t, i));
            emit_enum_constants(t, i);
        }
    }
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_RECORD && (t->marks[i] & MARK_EMIT)) {
            switch_group(t, group_of(t, i));
            emit_record(t, i);
        }
    }
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_VAR && t->canon[i] == i &&
            (t->marks[i] & MARK_EMIT) && !is_extern_var(t, i)) {
            switch_group(t, group_of(t, i));
            emit_global(t, i);
        }
    }
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_FUNCTION && t->defs[t->canon[i]] == i &&
            (t->marks[t->canon[i]] & MARK_EMIT)) {
            switch_group(t, group_of(t, i));
            emit_function(t, i);
        }
    }

    switch_group(t, 0);
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_FUNCTION && t->canon[i] == i &&
            (t->marks[i] & MARK_EXTERN)) {
            emit_extern(t, i);
        }
    }
    if (t->uses_clear_cache) {
        put(&t->out, "extern fn __clear_cache(begin: ?[*]u8, end: ?[*]u8)\n");
    }
    for (u32 i = 0; i < n; i++) {
        if (t->kinds[i] == K_VAR && t->canon[i] == i &&
            (t->marks[i] & MARK_EMIT) && is_extern_var(t, i)) {
            emit_extern_global(t, i);
        }
    }
    t->groups[0].out = t->out;
    free(work);
}

static bool
write_group(const Tr *t, const char *dir, const Group *g)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.chibi", dir, text(t, g->name));
    const char *start = g->out.text;
    u64 len = g->out.len;
    while (len != 0 && *start == '\n') {
        start++;
        len--;
    }

    char imports[128];
    u32 n = 0;
    for (u32 k = 0; k < NPACKAGES; k++) {
        if (g->packages >> k & 1) {
            const char *name = package_names[k];
            n += (u32)snprintf(imports + n, sizeof imports - n, "import \"std/%s\"\n", name);
        }
    }
    if (n != 0) {
        imports[n++] = '\n';
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL || fwrite(imports, 1, n, f) != n ||
        fwrite(start, 1, len, f) != len || fclose(f) != 0) {
        fprintf(stderr, "translate: %s: cannot write\n", path);
        return false;
    }
    return true;
}

int
main(int argc, char **argv)
{
    if (argc < 4 || argc % 2 != 0) {
        fprintf(stderr, "usage: translate OUTDIR AST.json FILE.c [AST.json FILE.c]...\n");
        return 2;
    }

    static Tr t;
    for (int k = 2; k < argc; k += 2) {
        if (!ast_load(&t.a, argv[k], stderr)) {
            return 1;
        }
    }
    setup_kinds(&t);
    map_ids(&t);
    link_units(&t);
    setup_decls(&t);

    t.main_files = calloc(t.nstrings, 1);
    t.file_groups = calloc(t.nstrings, sizeof *t.file_groups);
    t.groups[0] = (Group){ .name = intern_text(&t, "extern") };
    t.ngroups = 1;
    for (int k = 3; k < argc; k += 2) {
        u32 file = strings_find(&t.a.strings, argv[k], (u32)strlen(argv[k]));
        if (file == STRINGS_MISSING || file >= t.nstrings) {
            fprintf(stderr, "translate: %s isn't a file in the ASTs\n", argv[k]);
            return 1;
        }
        t.main_files[file] = 1;
        t.groups[group_of_file(&t, file)].main = true;
    }
    analyze(&t);
    emit_program(&t);

    for (u32 g = 0; g < t.ngroups; g++) {
        if ((g == 0 || t.groups[g].main || t.groups[g].out.len != 0) &&
            !write_group(&t, argv[1], &t.groups[g])) {
            return 1;
        }
    }
    return 0;
}
