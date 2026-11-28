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
    TYPE_UNTYPED_STRING,
    TYPE_UNDEFINED,
    TYPE_INFERRED,
    TYPE_ENUM_LITERAL,
    TYPE_POINTER,
    TYPE_OPTIONAL,
    TYPE_ERROR_UNION,
    TYPE_SLICE,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_VECTOR,
    TYPE_FN,
    TYPE_ENUM,
    TYPE_UNION,
    TYPE_MANY,
    TYPE_NIL,
} TypeKind;

// clang-format off
#define CHIBI_FIXED_TYPES(X)                                                   \
    X(TYPE_ID_VOID, TYPE_VOID, 0, 0, "void")                                   \
    X(TYPE_ID_BOOL, TYPE_BOOL, 8, 0, "bool")                                   \
    X(TYPE_ID_I8, TYPE_INT, 8, 1, "i8")                                        \
    X(TYPE_ID_I16, TYPE_INT, 16, 1, "i16")                                     \
    X(TYPE_ID_I32, TYPE_INT, 32, 1, "i32")                                     \
    X(TYPE_ID_I64, TYPE_INT, 64, 1, "int")                                     \
    X(TYPE_ID_I128, TYPE_INT, 128, 1, "i128")                                  \
    X(TYPE_ID_U8, TYPE_INT, 8, 0, "u8")                                        \
    X(TYPE_ID_U16, TYPE_INT, 16, 0, "u16")                                     \
    X(TYPE_ID_U32, TYPE_INT, 32, 0, "u32")                                     \
    X(TYPE_ID_U64, TYPE_INT, 64, 0, "u64")                                     \
    X(TYPE_ID_U128, TYPE_INT, 128, 0, "u128")                                  \
    X(TYPE_ID_F32, TYPE_FLOAT, 32, 0, "f32")                                   \
    X(TYPE_ID_F64, TYPE_FLOAT, 64, 0, "f64")                                   \
    X(TYPE_ID_UINTPTR, TYPE_UINTPTR, 64, 0, "uintptr")                         \
    X(TYPE_ID_ERROR, TYPE_ERROR, 16, 0, "error")                               \
    X(TYPE_ID_TYPE, TYPE_TYPE, 0, 0, "type")                                   \
    X(TYPE_ID_UNTYPED_INT, TYPE_UNTYPED_INT, 0, 0, "untyped integer")          \
    X(TYPE_ID_UNTYPED_FLOAT, TYPE_UNTYPED_FLOAT, 0, 0, "untyped float")        \
    X(TYPE_ID_UNTYPED_STRING, TYPE_UNTYPED_STRING, 0, 0, "untyped string")     \
    X(TYPE_ID_UNDEFINED, TYPE_UNDEFINED, 0, 0, "undefined")                    \
    X(TYPE_ID_INFERRED, TYPE_INFERRED, 0, 0, "(inferred)")                     \
    X(TYPE_ID_ENUM_LITERAL, TYPE_ENUM_LITERAL, 0, 0, "enum literal")           \
    X(TYPE_ID_NIL, TYPE_NIL, 0, 0, "nil")                                      \
    X(TYPE_ID_U8X16, TYPE_VECTOR, TYPE_ID_U8, 16, "u8x16")                     \
    X(TYPE_ID_I8X16, TYPE_VECTOR, TYPE_ID_I8, 16, "i8x16")                     \
    X(TYPE_ID_U16X8, TYPE_VECTOR, TYPE_ID_U16, 8, "u16x8")                     \
    X(TYPE_ID_I16X8, TYPE_VECTOR, TYPE_ID_I16, 8, "i16x8")                     \
    X(TYPE_ID_U32X4, TYPE_VECTOR, TYPE_ID_U32, 4, "u32x4")                     \
    X(TYPE_ID_I32X4, TYPE_VECTOR, TYPE_ID_I32, 4, "i32x4")                     \
    X(TYPE_ID_U64X2, TYPE_VECTOR, TYPE_ID_U64, 2, "u64x2")                     \
    X(TYPE_ID_I64X2, TYPE_VECTOR, TYPE_ID_I64, 2, "i64x2")                     \
    X(TYPE_ID_U8X64, TYPE_VECTOR, TYPE_ID_U8, 64, "u8x64")                     \
    X(TYPE_ID_I8X64, TYPE_VECTOR, TYPE_ID_I8, 64, "i8x64")                     \
    X(TYPE_ID_U16X32, TYPE_VECTOR, TYPE_ID_U16, 32, "u16x32")                  \
    X(TYPE_ID_I16X32, TYPE_VECTOR, TYPE_ID_I16, 32, "i16x32")                  \
    X(TYPE_ID_U32X16, TYPE_VECTOR, TYPE_ID_U32, 16, "u32x16")                  \
    X(TYPE_ID_I32X16, TYPE_VECTOR, TYPE_ID_I32, 16, "i32x16")                  \
    X(TYPE_ID_U64X8, TYPE_VECTOR, TYPE_ID_U64, 8, "u64x8")                     \
    X(TYPE_ID_I64X8, TYPE_VECTOR, TYPE_ID_I64, 8, "i64x8")                     \
    X(TYPE_ID_STRING, TYPE_SLICE, TYPE_ID_U8, 1, "string")                     \
    X(TYPE_ID_CSTRING, TYPE_MANY, TYPE_ID_U8, 1, "cstring")
// clang-format on

#define CHIBI_FIXED_TYPE_ID(id, kind, arg0, arg1, text) id,
enum
{
    TYPE_ID_NONE,
    CHIBI_FIXED_TYPES(CHIBI_FIXED_TYPE_ID) NFIXED_TYPES,
};

static inline bool
check_is_int128(u32 type)
{
    return type == TYPE_ID_I128 || type == TYPE_ID_U128;
}

typedef enum
{
    DECL_NONE,
    DECL_CONST,
    DECL_STRUCT,
    DECL_ENUM,
    DECL_UNION,
    DECL_FN,
    DECL_VAR,
} DeclKind;

enum
{
    DECL_PUB = 1,
    DECL_EXPORT = 2,
    DECL_CYCLE = 4,
    DECL_GENERAL_CALL = 8,
    DECL_STORED = 16,
    DECL_DEFAULTS = 32,
    DECL_NO_ZERO = 64,
    DECL_ZEROED = 128,
    DECL_INLINE = 256,
    DECL_NOINLINE = 512,
    DECL_TUPLE = 1024,
    DECL_RESULTS = 2048,
    DECL_COLD = 4096,
};

enum
{
    NO_BODY = UINT32_MAX,
};

enum
{
    FN_C_CALL = 1ull << 32,
    FN_VARIADIC = 1ull << 33,
    FN_REST = 1ull << 34,
};

enum
{
    SET_VAR = 1ull << 32,
};

enum
{
    PHASE_NONE,
    PHASE_DECLARED,
    PHASE_RESOLVED,
    PHASE_CHECKED,
};

enum
{
    TARGET_OS,
    TARGET_ARCH,
    NTARGETS,
};

enum
{
#if defined(__APPLE__)
    TARGET_OS_HOST = NAME_MACOS,
#elif defined(_WIN32)
    TARGET_OS_HOST = NAME_WINDOWS,
#else
    TARGET_OS_HOST = NAME_LINUX,
#endif
#if defined(__aarch64__)
    TARGET_ARCH_HOST = NAME_ARM64,
#else
    TARGET_ARCH_HOST = NAME_X86_64,
#endif
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
    X(DIAG_PACKAGE_VALUE, "`%t` is a package, so it can't be used here")                            \
    X(DIAG_IN_PACKAGE, "`%t` is in the package `%n`; write `%n.%t` after `import \"std/%n\"`")      \
    X(DIAG_NO_PACKAGE_MEMBER, "the package `%n` has no member `%t`")                                \
    X(DIAG_UNKNOWN_PACKAGE, "no built-in package has the path %t")                                  \
    X(DIAG_IMPORT_TWICE, "this package is already imported at %l")                                  \
    X(DIAG_IMPORT_CLASH, "`%n` is already declared at %d")                                          \
    X(DIAG_NOT_C, "C has no `%U`, so an `extern fn` can't pass or return it")                       \
    X(DIAG_NOT_C_EXPORT, "C has no `%U`, so an `export fn` can't pass or return it")                \
    X(DIAG_NOT_C_GLOBAL, "C has no `%U`, so an `extern` global can't hold it")                      \
    X(DIAG_C_FN_TYPE, "C can't call a chibi `%U`; write `extern %U`")                               \
    X(DIAG_NOT_VARIADIC, "C's `..` takes numbers, `bool`, enums and pointers, not `%U`")            \
    X(DIAG_REST_DEFAULT, "a function with a `..` parameter can't have defaults")                    \
    X(DIAG_TOO_LARGE, "an integer constant must fit in 512 bits")                                   \
    X(DIAG_FLOAT_TOO_LARGE, "a float constant must fit in 4,095 bits as an exact fraction")          \
    X(DIAG_SURROGATE, "`\\uD800` to `\\uDFFF` must come in pairs, as in `\\uD83D\\uDE00`")          \
    X(DIAG_CHAR_COUNT, "a character literal holds one character")                                   \
    X(DIAG_CHAR_UTF8, "a character literal must be valid UTF-8")                                    \
    X(DIAG_DATA_TOO_LARGE, "constants can't take more than 256 MB in all")                          \
    X(DIAG_GLOBALS_TOO_LARGE, "globals can't take more than 2 GB in all")                           \
    X(DIAG_DOES_NOT_FIT, "%v does not fit in `%U`")                                                 \
    X(DIAG_MISMATCH, "`%T` and `%U` don't mix without a conversion")                                \
    X(DIAG_BITCAST_UNTYPED, "`bitcast` needs a typed value, as in `u64(1)`")                        \
    X(DIAG_BITCAST_SLICE, "`bitcast` can't change a slice, because its length would need rescaling") \
    X(DIAG_BITCAST_TYPE, "`bitcast` changes numbers, pointers and 16-byte and 64-byte arrays of integers, not `%U`") \
    X(DIAG_BITCAST_SIZE, "`%T` and `%U` differ in size, so `bitcast` can't change one into the other") \
    X(DIAG_MISALIGNED, "address %v isn't aligned for `%U`")                                         \
    X(DIAG_DIVIDE_BY_ZERO, "division by zero")                                                      \
    X(DIAG_BAD_SHIFT, "shift count %v is out of range")                                             \
    X(DIAG_NEGATIVE_LENGTH, "an array length can't be negative")                                    \
    X(DIAG_TAG_MISMATCH, "`%t` is not a member of the tag enum `%U`")                               \
    X(DIAG_TAG_MISSING, "the union has no member `%n` of the tag enum `%U`")                        \
    X(DIAG_CODE_OUTSIDE_ENTRY, "only the file you run may contain code")                           \
    X(DIAG_MAIN_BESIDE_CODE, "the program starts at the code in the file you run, so it can't declare `main`") \
    X(DIAG_ARGS_NEED_CODE, "`os.args` is set only when the program starts at the code in the file you run") \
    X(DIAG_SHADOW, "`%t` is already a local at %m")                                                 \
    X(DIAG_NOT_VALUE, "expected a value")                                                           \
    X(DIAG_NOT_BOOL, "expected `bool`, found `%T`")                                                 \
    X(DIAG_BAD_OPERATOR, "`%T` doesn't support `%t`")                                               \
    X(DIAG_WRAPPING_ONLY, "`%T` supports only the wrapping `+%%`, `-%%` and `*%%` so far")          \
    X(DIAG_LANE_COMPARE, "`%U` doesn't compare lane by lane with `%t`; use `%b`")                   \
    X(DIAG_NOT_ASSIGNABLE, "only a variable, a field, an element or `p.*` can be assigned to")      \
    X(DIAG_ASSIGN_PARAM, "`%n` is a parameter, so it can't be assigned")                            \
    X(DIAG_ASSIGN_LOOP_VARIABLE, "`%n` is a loop variable, so it can't be assigned")                \
    X(DIAG_ASSIGN_CONSTANT, "`%n` is a constant, so it can't be assigned")                          \
    X(DIAG_ASSIGN_RESULT, "a call's result can't be assigned; copy it to a local to change it")     \
    X(DIAG_NOT_ADDRESSABLE, "only a variable, a field, an element or `p.*` has an address")         \
    X(DIAG_ADDRESS_PARAM, "`%n` is a parameter, so `&` can't take its address")                     \
    X(DIAG_ADDRESS_LOOP_VARIABLE, "`%n` is a loop variable, so `&` can't take its address")         \
    X(DIAG_ADDRESS_CONSTANT, "`%n` is a constant, so `&` can't take its address")                   \
    X(DIAG_NOT_POINTER, "`%T` is not a pointer, so `.*` can't follow it")                           \
    X(DIAG_DEREF, "use `.*` to dereference")                                                        \
    X(DIAG_MANY_DEREF, "`%T` points at many items, so write `p[0]`, not `p.*`")                     \
    X(DIAG_NEVER_NIL, "`%U` is never nil, so this needs a value")                                   \
    X(DIAG_NIL_MISMATCH, "`%U` is never nil")                                                       \
    X(DIAG_NIL_TYPE, "`nil` needs a type here, as in `p: ?*T = nil`")                               \
    X(DIAG_MAYBE_NIL, "`%T` may be nil, so it needs `!!`, `?\?` or a check against `nil` first")    \
    X(DIAG_NOT_OPTIONAL, "`%T` is never nil, so `%t` does nothing")                                 \
    X(DIAG_ALWAYS_NIL, "this is always `nil`, so `!!` always traps")                                \
    X(DIAG_CAN_FAIL, "`%T` can fail, so it needs `try` or `catch` first")                           \
    X(DIAG_CANT_FAIL, "`%T` can't fail, so `%t` does nothing")                                      \
    X(DIAG_TRY_RESULT, "the function returns `%U`, which can't fail, so `try` can't pass an error up") \
    X(DIAG_HANDLER_END, "the handler can reach its end without a value of type `%U`")              \
    X(DIAG_TOO_MANY_ERRORS, "a program can't have more than 65,535 error names")                   \
    X(DIAG_NOT_SET, "expected an error set before `!`, found `%U`")                                \
    X(DIAG_SET_UNION, "`|` joins error sets, not `%U`; a `union` holds one of several types")      \
    X(DIAG_ERROR_TWICE, "`error.%n` is already in this set at %l")                                 \
    X(DIAG_NOT_IN_SET, "`%U` can't hold `error.%n`")                                               \
    X(DIAG_ANY_ERROR, "`%T` can hold any error, and `%U` can't")                                   \
    X(DIAG_NOT_IN_SET_FROM, "`%U` can't hold `error.%n`, which comes from %o")                     \
    X(DIAG_ANY_ERROR_FROM, "`%U` can't hold any error, which comes from %o")                       \
    X(DIAG_NOT_INDEXABLE, "`%T` can't be indexed")                                                  \
    X(DIAG_STRING_READ_ONLY, "a `string` is read-only, so its bytes can't be assigned or pointed to") \
    X(DIAG_NOT_INDEX, "an index must be an integer, found `%T`")                                    \
    X(DIAG_INDEX_RANGE, "index %v is out of range for `%U`")                                        \
    X(DIAG_SLICE_ORDER, "this slice starts at %v, after its end")                                   \
    X(DIAG_MANY_END, "`%U` has no length, so this slice needs an end")                              \
    X(DIAG_NOT_OFFSET, "`p + n` needs an integer `n`, found `%T`")                                  \
    X(DIAG_SLICE_PARAM, "`%n` is a parameter, so a slice can't point into it")                      \
    X(DIAG_SLICE_LOOP_VARIABLE, "`%n` is a loop variable, so a slice can't point into it")          \
    X(DIAG_SLICE_CONSTANT, "`%n` is a constant, so a slice can't point into it")                    \
    X(DIAG_LITERAL_TYPE, "the type of this literal isn't known here; write it, as in `u8.[1, 2]`")  \
    X(DIAG_LITERAL_COUNT, "expected the length of `%U`, found %c elements")                         \
    X(DIAG_NOT_ARRAY, "`%U` is not an array, so an array literal can't build it")                   \
    X(DIAG_STRUCT_LITERAL_TYPE, "the type of this literal isn't known here; write it, as in `Point.{ 1, 2 }`") \
    X(DIAG_NOT_STRUCT, "`%U` is not a struct, so a struct literal can't build it")                  \
    X(DIAG_FIELD_COUNT, "wrong number of fields: expected %c, found %C")                            \
    X(DIAG_TUPLE_SIZE, "a tuple needs two or more types")                                           \
    X(DIAG_NOT_TUPLE, "`%T` is not a tuple, so it can't fill several names")                        \
    X(DIAG_VALUE_COUNT, "wrong number of values: expected %c, found %C")                            \
    X(DIAG_MIXED_FIELDS, "name every field of this literal, or none")                               \
    X(DIAG_FIELD_TWICE, "`%n` is already set at %l")                                                \
    X(DIAG_MISSING_FIELD, "`%n` has no zero value, so this literal needs a value for it")          \
    X(DIAG_NO_ZERO_FIELD, "`%U` has no zero value because of its field `%n`, so this needs a value") \
    X(DIAG_NO_ZERO_ELEMENT, "`%U` has no zero value because of its `%T`, so this needs a value")    \
    X(DIAG_NO_ZERO, "`%U` has no zero value, so this needs a value")                                \
    X(DIAG_UNUSED, "this value is not used; discard it with `_ = ...`")                             \
    X(DIAG_DISCARD, "`_` can only discard a value, as in `_ = f()`")                                \
    X(DIAG_NOT_CALLABLE, "expected a function or a type")                                           \
    X(DIAG_ARG_COUNT, "wrong number of arguments: expected %c, found %C")                           \
    X(DIAG_ARG_COUNT_VARIADIC, "wrong number of arguments: expected at least %c, found %C")         \
    X(DIAG_BUILTIN_INT, "`%b` takes an integer, found `%U`")                                        \
    X(DIAG_BUILTIN_VECTOR, "`%b` takes a 16-byte or 64-byte array of integers, found `%U`")         \
    X(DIAG_BUILTIN_MANY, "`%b` takes a `[*]` pointer, found `%U`")                                  \
    X(DIAG_UNALIGNED_TYPE, "`%b` reads and writes numbers and 16-byte and 64-byte arrays of integers, found `%U`") \
    X(DIAG_LOAD_TYPE, "`mem.unaligned_load` takes a type first, as in `mem.unaligned_load([16]u8, p)`") \
    X(DIAG_SIZE_TYPE, "`%b` takes a type, as in `%b(u32)`")                                         \
    X(DIAG_SIZE_TOO_LARGE, "the size of `%T` doesn't fit in `int`")                                 \
    X(DIAG_STORE_UNTYPED, "`mem.unaligned_store` needs a typed value, as in `u32(0)`")              \
    X(DIAG_LOOKUP_TYPES, "`simd.lookup` takes a `[16]u8` table and `[16]u8` or `[64]u8` indexes, or the same in `i8`, found `%T` and `%U`") \
    X(DIAG_RETURN_VALUE, "this function returns nothing, so `return` takes no value")               \
    X(DIAG_RETURN_EMPTY, "`return` needs a value of type `%U`")                                     \
    X(DIAG_MISSING_RETURN, "the function can reach its end without returning a value of type `%U`") \
    X(DIAG_RESULT_ZERO, "a named result starts at zero, and `%U` has no zero value")                \
    X(DIAG_OUTSIDE_LOOP, "`%t` is only allowed inside a loop")                                      \
    X(DIAG_UNKNOWN_LABEL, "no statement around this is labeled `%t`")                              \
    X(DIAG_CONTINUE_BLOCK, "`%t` labels a block, so `continue` can't go to it")                    \
    X(DIAG_BREAK_VALUE, "`%t` isn't a block used as a value, so `break` can't give it one")       \
    X(DIAG_BREAK_EMPTY, "`%t` is used as a value, so `break %t` needs one")                        \
    X(DIAG_BLOCK_END, "the block can reach its end without a value; end it with `break` and a value") \
    X(DIAG_DEFER_EXIT, "`%t` can't leave a `defer`")                                                \
    X(DIAG_DEFER_DECLARATION, "a `defer` runs its statement later, so the statement can't declare a name") \
    X(DIAG_CATCH_NEVER, "the function returns `%U`, which can't fail, so `defer catch` never runs") \
    X(DIAG_CATCH_IN_DEFER, "no error can leave a `defer`, so a `defer catch` in it never runs")     \
    X(DIAG_NO_MEMBER, "`%U` has no member `%n`")                                                   \
    X(DIAG_NOT_ENUM, "`%U` is not an enum, so `.%n` can't be one of its values")                  \
    X(DIAG_UNKNOWN_ENUM, "the enum of `.%n` isn't known here; write its name, as in `Name.%n`")    \
    X(DIAG_NOT_ENUM_VALUE, "%v is not the value of a member of `%U`")                              \
    X(DIAG_NO_ZERO_MEMBER, "`%U` has no member with the value 0, so this needs a value")           \
    X(DIAG_SWITCH_TYPE, "`switch` needs an integer, an enum or `bool`, found `%T`")               \
    X(DIAG_CASE_TWICE, "this value is already handled at %l")                                     \
    X(DIAG_EMPTY_RANGE, "this range is empty")                                                    \
    X(DIAG_MISSING_CASE, "the switch doesn't handle %w; add an arm for it or a `_` arm")          \
    X(DIAG_MISSING_MEMBER, "the switch doesn't handle `.%n`; add an arm for it or a `_` arm")     \
    X(DIAG_MISSING_ERROR, "the switch doesn't handle `error.%n`; add an arm for it or a `_` arm") \
    X(DIAG_SWITCH_ANY, "`%U` can hold any error, so the switch needs a `_` arm")                  \
    X(DIAG_MISSING_ERROR_FROM, "the switch doesn't handle `error.%n`, which comes from %o; add an arm for it or a `_` arm") \
    X(DIAG_SWITCH_ANY_FROM, "the switch can get any error, which comes from %o, so it needs a `_` arm") \
    X(DIAG_ARM_END, "this arm can reach its end without a value")                                 \
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
    u32 origin_file;
    u32 origin_token;
    u8 kind;
} Diagnostic;

enum
{
    MEMBER_NONE = 0xFFFFFFFEu,
    MEMBER_UNDEFINED = 0xFFFFFFFFu,
};

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
    u32 first;
    u32 second;
} Pair;

typedef struct
{
    u32 first;
    u32 last;
    u32 type;
    u32 result;
} Peers;

typedef struct
{
    u32 at;
    Operand place;
    u32 block;
    u32 writes;
} Unread;

typedef struct
{
    u32 loop;
    u32 next;
    u32 slot;
    bool inclusive;
    bool broken;
    bool block;
    bool value;
    bool peer;
    bool dispatch;
    bool before;
    u32 label;
    u32 type;
    u32 marker;
    u32 facts;
    u32 defers;
    Peers peers;
} Loop;

typedef struct
{
    Operand value;
    u32 token;
    u32 exit;
    u32 next;
} Break;

typedef struct
{
    u32 at;
    u32 node;
    u32 origin;
    u32 first;
    u32 count;
    u32 first_line;
    u32 nlines;
    u32 facts;
    u32 tests;
    u32 scope;
    u32 lines;
    u32 marked;
    u32 last;
    u32 name;
    bool open;
    bool reachable;
    bool ends;
    bool catches;
} Defer;

typedef struct
{
    u64 lo;
    u64 hi;
    u32 arm;
    u32 token;
} CaseRange;

typedef struct
{
    u32 type;
    u32 decl;
    u32 resolved;
} SetVar;

typedef struct
{
    u32 var;
    u32 from;
    u32 code;
    u32 file;
    u32 token;
    u32 minus;
} SetInput;

typedef struct
{
    u32 file;
    u32 token;
    u32 from;
    u32 to;
    u32 code;
    u32 cases;
    u8 kind;
} SetCheck;

typedef struct
{
    u32 at;
    u32 type;
    u32 token;
    u32 first_case;
    u32 arms;
    u32 default_arm;
    u32 default_token;
    u32 label;
    u32 loop;
    u32 facts;
    u32 node;
    u32 subject;
    u32 declared;
    u32 rest;
    bool value;
    bool before;
    bool reaches;
    bool skipped;
} Switch;

typedef struct
{
    IrCode *code;
    u32 first;
    u32 first_extra;
    u32 first_slot;
    u32 first_const;
    u32 first_line;
    u32 marked;
    u32 decl;
    u32 owner;
    u32 ret;
    Peers returns;
    u32 loops;
    u32 breaks;
    u32 defers;
    u32 depth;
    u32 calls;
    u32 blocks;
    u32 copies;
    u32 stack_args;
    u32 facts;
    u32 tests;
    u32 addressed;
    u32 code_slot;
    u8 *narrowed;
    u32 narrowed_cap;
    bool reachable;
    bool returns_value;
    bool narrows;
    bool scanned;
} Body;

typedef struct
{
    u32 *names;
    u8 *kinds;
    u16 *flags;
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
    u32 cap;
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
    bool release_fast;
    bool target_read[NTARGETS];
    u32 target[NTARGETS];
    u32 target_types[NTARGETS];
    Decls decls;
    Types types;
    u32 *package_names;
    u32 *imports;
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
    Unread *unread;
    u32 nunread;
    u32 unread_cap;
    u32 unread_done;
    Pair *pairs;
    u32 npairs;
    u32 pairs_cap;
    u32 *string_offsets;
    u32 *string_lengths;
    u32 nstrings;
    u32 strings_cap;
    u32 *claims;
    u32 nclaims;
    Ir ir;
    IrCode *builders;
    u32 nbuilders;
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
    Break *breaks;
    u32 nbreaks;
    u32 breaks_cap;
    Defer *defers;
    u32 ndefers;
    u32 defers_cap;
    IrCode defer_code;
    u8 *openers;
    u32 openers_cap;
    CaseRange *cases;
    u32 ncases;
    u32 cases_cap;
    Switch *switches;
    u32 nswitches;
    u32 switches_cap;
    u32 *facts;
    u32 nfacts;
    u32 facts_cap;
    u32 *tests;
    u32 ntests;
    u32 tests_cap;
    u32 *addressed;
    u32 naddressed;
    u32 addressed_cap;
    u64 *member_keys;
    u32 *member_slots;
    u32 member_mask;
    u32 member_count;
    u32 *tuple_map;
    u32 tuple_mask;
    u32 tuple_count;
    u32 *set_map;
    u32 set_mask;
    u32 set_count;
    SetVar *set_vars;
    u32 nset_vars;
    u32 set_vars_cap;
    SetInput *set_inputs;
    u32 nset_inputs;
    u32 set_inputs_cap;
    SetCheck *set_checks;
    u32 nset_checks;
    u32 set_checks_cap;
    u32 zeroed;
    u32 zeroed_align;
    u32 *entry_code;
    u32 nentry_code;
    u32 entry;
    u32 args;
    u32 *error_refs;
    u32 *error_names;
    u32 nerrors;
    u32 errors_cap;
} Checker;

static inline bool
check_is_flag_optional(const Checker *c, u32 type)
{
    return type >= NFIXED_TYPES && c->types.kinds[type] == TYPE_OPTIONAL &&
           !c->types.arg1[type];
}

static inline bool
check_is_error_pair(const Checker *c, u32 type)
{
    return type >= NFIXED_TYPES && c->types.kinds[type] == TYPE_ERROR_UNION &&
           c->types.arg0[type] != TYPE_ID_VOID;
}

static inline u32
check_error_union_first(const Checker *c, u32 type)
{
    u32 value = (u32)c->types.arg0[type];
    return value == TYPE_ID_VOID ? TYPE_ID_ERROR : value;
}

static inline u32
check_top_bits_type(const Checker *c, u32 vector)
{
    u64 lanes = c->types.arg1[vector];
    return lanes <= 8  ? TYPE_ID_U8
         : lanes == 16 ? TYPE_ID_U16
         : lanes == 32 ? TYPE_ID_U32
                       : TYPE_ID_U64;
}

void check_init(Checker *c, const Names *names, const ParsedFile *files, u32 nfiles);

bool check_target(Checker *c, u32 k, const char *text, u32 len);

void check_package(Checker *c);

void check_print_diagnostics(FILE *out, const Checker *c);

void check_dump(FILE *out, const Checker *c);

void check_dump_ir(FILE *out, const Checker *c);

u64 check_type_size(const Checker *c, u32 type);

u32 check_type_align(const Checker *c, u32 type);

u64 check_field_offset(const Checker *c, u32 field);

u64 check_member_offset(const Checker *c, u32 decl, u32 member);

typedef struct
{
    u64 *newlines;
    u32 *before;
    u32 count;
    u32 cap;
    u32 total;
} LineStarts;

u32 check_line(const Checker *c, LineStarts *l, u32 file, u32 token);

void check_free_lines(LineStarts *l);

enum
{
    REGS_COUNT = 7,
    REGS_FLOAT = 8,
    REGS_DOUBLE = 16,
    REGS_POINTER = 32,
};

u32 check_struct_regs(const Checker *c, u32 type);

enum
{
    FIRST_INT_ARG = 1,
    PLACE_APPLE = 1,
    PLACE_VARIADIC = 2,
    PLACE_STACK = 1u << 31,
};

typedef struct
{
    u32 ints;
    u32 floats;
    u32 stack;
} ArgCursor;

u32 check_place_arg(ArgCursor *at, u32 regs, u32 size, u32 align, u32 how);

void check_free(Checker *c);

#endif
