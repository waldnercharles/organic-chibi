#ifndef CHIBI_CHECK_H
#define CHIBI_CHECK_H

#include "exact.h"
#include "ir.h"
#include "names.h"
#include "parser.h"

#include <stdio.h>

typedef struct
{
    const char *path;
    const char *src;
    Tokens tokens;
    Nodes nodes;
    u32 count;
    u32 top_count;
    bool entry;
} ParsedFile;

typedef enum
{
    TYPE_NONE,
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_UINTPTR,
    TYPE_ERROR,
    TYPE_TYPE,
    TYPE_UNTYPED_INT,
    TYPE_UNTYPED_FLOAT,
    TYPE_UNDEFINED,
    TYPE_INFERRED,
    TYPE_POINTER,
    TYPE_OPTIONAL,
    TYPE_SLICE,
    TYPE_ARRAY,
    TYPE_ERROR_UNION,
    TYPE_FN,
    TYPE_STRUCT,
    TYPE_ENUM,
    TYPE_UNION,
} TypeKind;

// clang-format off
#define CHIBI_FIXED_TYPES(X)                                                   \
    X(TYPE_ID_VOID, TYPE_VOID, 0, 0, "void")                                   \
    X(TYPE_ID_BOOL, TYPE_BOOL, 8, 0, "bool")                                   \
    X(TYPE_ID_I8, TYPE_INT, 8, 1, "i8")                                        \
    X(TYPE_ID_I16, TYPE_INT, 16, 1, "i16")                                     \
    X(TYPE_ID_I32, TYPE_INT, 32, 1, "i32")                                     \
    X(TYPE_ID_I64, TYPE_INT, 64, 1, "int")                                     \
    X(TYPE_ID_U8, TYPE_INT, 8, 0, "u8")                                        \
    X(TYPE_ID_U16, TYPE_INT, 16, 0, "u16")                                     \
    X(TYPE_ID_U32, TYPE_INT, 32, 0, "u32")                                     \
    X(TYPE_ID_U64, TYPE_INT, 64, 0, "u64")                                     \
    X(TYPE_ID_F32, TYPE_FLOAT, 32, 0, "f32")                                   \
    X(TYPE_ID_F64, TYPE_FLOAT, 64, 0, "f64")                                   \
    X(TYPE_ID_UINTPTR, TYPE_UINTPTR, 64, 0, "uintptr")                         \
    X(TYPE_ID_ERROR, TYPE_ERROR, 0, 0, "error")                                \
    X(TYPE_ID_TYPE, TYPE_TYPE, 0, 0, "type")                                   \
    X(TYPE_ID_UNTYPED_INT, TYPE_UNTYPED_INT, 0, 0, "untyped integer")          \
    X(TYPE_ID_UNTYPED_FLOAT, TYPE_UNTYPED_FLOAT, 0, 0, "untyped float")        \
    X(TYPE_ID_UNDEFINED, TYPE_UNDEFINED, 0, 0, "undefined")                    \
    X(TYPE_ID_INFERRED, TYPE_INFERRED, 0, 0, "(inferred)")
// clang-format on

#define CHIBI_FIXED_TYPE_ID(id, kind, bits, is_signed, text) id,
enum
{
    TYPE_ID_NONE,
    CHIBI_FIXED_TYPES(CHIBI_FIXED_TYPE_ID) NFIXED_TYPES,
};

typedef enum
{
    DECL_NONE,
    DECL_CONST,
    DECL_STRUCT,
    DECL_ENUM,
    DECL_UNION,
    DECL_FN,
} DeclKind;

enum
{
    DECL_PUB = 1,
    DECL_EXPORT = 2,
    DECL_CYCLE = 4,
};

enum
{
    PHASE_NONE,
    PHASE_DECLARED,
    PHASE_RESOLVED,
    PHASE_CHECKED,
};

// clang-format off
#define CHIBI_DIAGNOSTICS(X)                                                                        \
    X(DIAG_UNKNOWN_NAME, "unknown name `%t`")                                                       \
    X(DIAG_DUPLICATE, "`%t` is already declared at %d")                                             \
    X(DIAG_DUPLICATE_MEMBER, "`%t` is already a member at %m")                                      \
    X(DIAG_DUPLICATE_PARAM, "`%t` is already a parameter at %m")                                    \
    X(DIAG_RESERVED, "`%t` is a builtin type name")                                                 \
    X(DIAG_CYCLE, "`%t` depends on itself")                                                         \
    X(DIAG_CYCLE_THROUGH, "`%t` depends on itself through `%n`")                                    \
    X(DIAG_TOO_DEEP, "this needs more than 1000 nested declarations")                                \
    X(DIAG_NOT_TYPE, "expected a type")                                                             \
    X(DIAG_NOT_CONSTANT, "expected a constant")                                                     \
    X(DIAG_NOT_INTEGER, "expected an integer type")                                                 \
    X(DIAG_FN_VALUE, "`%t` is a function, so it can't be used here")                                \
    X(DIAG_TOO_LARGE, "an integer constant must fit in 512 bits")                                   \
    X(DIAG_FLOAT_TOO_LARGE, "a float constant must fit in 4,095 bits as an exact fraction")          \
    X(DIAG_DOES_NOT_FIT, "%v does not fit in `%U`")                                                 \
    X(DIAG_MISMATCH, "`%T` and `%U` don't mix without a conversion")                                \
    X(DIAG_DIVIDE_BY_ZERO, "division by zero")                                                      \
    X(DIAG_BAD_SHIFT, "shift count %v is out of range")                                             \
    X(DIAG_NEGATIVE_LENGTH, "an array length can't be negative")                                    \
    X(DIAG_TAG_MISMATCH, "`%t` is not a member of the tag enum `%U`")                               \
    X(DIAG_TAG_MISSING, "the union has no member `%n` of the tag enum `%U`")                        \
    X(DIAG_CODE_OUTSIDE_ENTRY, "only the file you run may contain code")                           \
    X(DIAG_SHADOW, "`%t` is already a local at %m")                                                 \
    X(DIAG_NOT_VALUE, "expected a value")                                                           \
    X(DIAG_NOT_BOOL, "expected `bool`, found `%T`")                                                 \
    X(DIAG_BAD_OPERATOR, "`%T` doesn't support `%t`")                                               \
    X(DIAG_NOT_ASSIGNABLE, "only a local can be assigned to")                                       \
    X(DIAG_ASSIGN_PARAM, "`%t` is a parameter, so it can't be assigned")                            \
    X(DIAG_ASSIGN_LOOP_VARIABLE, "`%t` is a loop variable, so it can't be assigned")                \
    X(DIAG_UNUSED, "this value is not used; discard it with `_ = ...`")                             \
    X(DIAG_DISCARD, "`_` can only discard a value, as in `_ = f()`")                                \
    X(DIAG_NOT_CALLABLE, "expected a function or a type")                                           \
    X(DIAG_ARG_COUNT, "wrong number of arguments: expected %c, found %C")                           \
    X(DIAG_RETURN_VALUE, "this function returns nothing, so `return` takes no value")               \
    X(DIAG_RETURN_EMPTY, "`return` needs a value of type `%U`")                                     \
    X(DIAG_MISSING_RETURN, "the function can reach its end without returning a value of type `%U`") \
    X(DIAG_OUTSIDE_LOOP, "`%t` is only allowed inside a loop")                                      \
    X(DIAG_UNSUPPORTED, "the checker doesn't support this yet")
// clang-format on

#define CHIBI_DIAGNOSTIC_ID(name, text) name,
typedef enum
{
    CHIBI_DIAGNOSTICS(CHIBI_DIAGNOSTIC_ID) NDIAGNOSTICS,
} DiagnosticKind;

typedef struct
{
    u32 file;
    u32 token;
    u32 arg;
    u32 arg2;
    u8 kind;
} Diagnostic;

typedef struct
{
    u32 name;
    u32 type;
    u32 node;
    u32 value;
} Member;

typedef struct
{
    u32 type;
    u32 ref;
} Operand;

typedef struct
{
    u32 name;
    u64 previous;
} Binding;

typedef struct
{
    u32 loop;
    u32 next;
    u32 slot;
    bool inclusive;
    bool broken;
} Loop;

typedef struct
{
    IrCode *code;
    u32 first;
    u32 first_extra;
    u32 first_slot;
    u32 first_const;
    u32 decl;
    u32 owner;
    u32 ret;
    u32 loops;
    bool reachable;
} Body;

typedef struct
{
    u32 *names;
    u8 *kinds;
    u8 *flags;
    u8 *phases;
    u8 *busy;
    u32 *files;
    u32 *firsts;
    u32 *roots;
    u32 *types;
    u32 *values;
    u32 *members;
    u32 *member_counts;
    u32 *bodies;
    u32 *funcs;
    u32 count;
} Decls;

typedef struct
{
    u8 *kinds;
    u32 *arg0;
    u64 *arg1;
    u32 *pointers;
    u32 *optionals;
    u32 *slices;
    u32 count;
    u32 cap;
    u32 *map;
    u32 map_mask;
    u32 map_count;
} Types;

typedef struct
{
    const Names *names;
    const ParsedFile *files;
    u32 nfiles;
    Decls decls;
    Types types;
    u32 *package_names;
    u32 *bound;
    Member *members;
    u32 nmembers;
    u32 members_cap;
    Member *scratch;
    u32 nscratch;
    u32 scratch_cap;
    u32 *lists;
    u32 nlists;
    u32 lists_cap;
    Exacts exacts;
    Diagnostic *diags;
    u32 ndiags;
    u32 diags_cap;
    Operand *stack;
    u32 *stack_tokens;
    u32 sp;
    u32 stack_cap;
    u32 *claims;
    u32 nclaims;
    Ir ir;
    IrCode *builders;
    IrCode deferred;
    u32 flushed;
    u32 nbodies;
    Body body;
    u32 owners;
    u64 *bindings;
    Binding *scope;
    u32 nscope;
    u32 scope_cap;
    Loop *loops;
    u32 nloops;
    u32 loops_cap;
} Checker;

void check_init(Checker *c, const Names *names);

void check_package(Checker *c, const ParsedFile *files, u32 nfiles);

void check_print_diagnostics(FILE *out, const Checker *c);

void check_dump(FILE *out, const Checker *c);

void check_dump_ir(FILE *out, const Checker *c);

void check_free(Checker *c);

#endif
