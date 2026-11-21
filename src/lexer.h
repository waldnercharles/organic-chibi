#ifndef CHIBI_LEXER_H
#define CHIBI_LEXER_H

#include "common.h"
#include "names.h"

#include <stdio.h>

// clang-format off
#define CHIBI_TOKENS(OTHER, KW, OP3, OP2, OP1)                                 \
    OTHER(TK_EOF, "EOF")                                                       \
    OTHER(TK_INT, "INT")                                                       \
    OTHER(TK_FLOAT, "FLOAT")                                                   \
    OTHER(TK_STRING, "STRING")                                                 \
    OTHER(TK_IDENT, "IDENT")                                                   \
    KW(TK_IF, "if")                                                            \
    KW(TK_ELSE, "else")                                                        \
    KW(TK_FOR, "for")                                                          \
    KW(TK_WHILE, "while")                                                      \
    KW(TK_SWITCH, "switch")                                                    \
    KW(TK_BREAK, "break")                                                      \
    KW(TK_CONTINUE, "continue")                                                \
    KW(TK_RETURN, "return")                                                    \
    KW(TK_DEFER, "defer")                                                      \
    KW(TK_STRUCT, "struct")                                                    \
    KW(TK_UNION, "union")                                                      \
    KW(TK_ENUM, "enum")                                                        \
    KW(TK_FN, "fn")                                                            \
    KW(TK_PACKAGE, "package")                                                  \
    KW(TK_IMPORT, "import")                                                    \
    KW(TK_PUB, "pub")                                                          \
    KW(TK_TRUE, "true")                                                        \
    KW(TK_FALSE, "false")                                                      \
    KW(TK_NIL, "nil")                                                          \
    KW(TK_IS, "is")                                                            \
    KW(TK_IN, "in")                                                            \
    KW(TK_BITCAST, "bitcast")                                                  \
    KW(TK_AND, "and")                                                          \
    KW(TK_OR, "or")                                                            \
    KW(TK_CONST, "const")                                                      \
    KW(TK_EXPORT, "export")                                                    \
    KW(TK_EXTERN, "extern")                                                    \
    KW(TK_UNDEFINED, "undefined")                                              \
    KW(TK_TRY, "try")                                                          \
    KW(TK_CATCH, "catch")                                                      \
    OP1(TK_LPAREN, '(')                                                        \
    OP1(TK_LBRACK, '[')                                                        \
    OP1(TK_PERIOD, '.')                                                        \
    OP2(TK_DEREF, '.', '*')                                                    \
    OP2(TK_UNWRAP, '!', '!')                                                   \
    OP1(TK_COLON, ':')                                                         \
    OP3(TK_SHLEQ, '<', '<', '=')                                               \
    OP3(TK_SHREQ, '>', '>', '=')                                               \
    OP3(TK_RANGE_INCLUSIVE, '.', '.', '=')                                     \
    OP3(TK_ADD_WRAP_EQ, '+', '%', '=')                                         \
    OP3(TK_SUB_WRAP_EQ, '-', '%', '=')                                         \
    OP3(TK_MUL_WRAP_EQ, '*', '%', '=')                                         \
    OP3(TK_OR_ELSE_EQ, '?', '?', '=')                                          \
    OP2(TK_SHL, '<', '<')                                                      \
    OP2(TK_SHR, '>', '>')                                                      \
    OP2(TK_ANDAND, '&', '&')                                                   \
    OP2(TK_OROR, '|', '|')                                                     \
    OP2(TK_ADDEQ, '+', '=')                                                    \
    OP2(TK_SUBEQ, '-', '=')                                                    \
    OP2(TK_MULEQ, '*', '=')                                                    \
    OP2(TK_ADD_WRAP, '+', '%')                                                 \
    OP2(TK_SUB_WRAP, '-', '%')                                                 \
    OP2(TK_MUL_WRAP, '*', '%')                                                 \
    OP2(TK_DIVEQ, '/', '=')                                                    \
    OP2(TK_MODEQ, '%', '=')                                                    \
    OP2(TK_ANDEQ, '&', '=')                                                    \
    OP2(TK_OREQ, '|', '=')                                                     \
    OP2(TK_XOREQ, '^', '=')                                                    \
    OP2(TK_ARROW, '-', '>')                                                    \
    OP2(TK_FAT_ARROW, '=', '>')                                                \
    OP2(TK_EQEQ, '=', '=')                                                     \
    OP2(TK_NEQ, '!', '=')                                                      \
    OP2(TK_LTE, '<', '=')                                                      \
    OP2(TK_GTE, '>', '=')                                                      \
    OP2(TK_RANGE, '.', '.')                                                    \
    OP2(TK_DCOLON, ':', ':')                                                   \
    OP2(TK_DASSIGN, ':', '=')                                                  \
    OP2(TK_OR_ELSE, '?', '?')                                                  \
    OP2(TK_QUESTION_EQ, '?', '=')                                              \
    OP1(TK_PLUS, '+')                                                          \
    OP1(TK_MINUS, '-')                                                         \
    OP1(TK_STAR, '*')                                                          \
    OP1(TK_SLASH, '/')                                                         \
    OP1(TK_PERCENT, '%')                                                       \
    OP1(TK_EQ, '=')                                                            \
    OP1(TK_NOT, '!')                                                           \
    OP1(TK_AT, '@')                                                            \
    OP1(TK_DOLLAR, '$')                                                        \
    OP1(TK_QUESTION, '?')                                                      \
    OP1(TK_AMP, '&')                                                           \
    OP1(TK_PIPE, '|')                                                          \
    OP1(TK_XOR, '^')                                                           \
    OP1(TK_COMPL, '~')                                                         \
    OP1(TK_LT, '<')                                                            \
    OP1(TK_GT, '>')                                                            \
    OP1(TK_SEMI, ';')                                                          \
    OP1(TK_COMMA, ',')                                                         \
    OP1(TK_LBRACE, '{')                                                        \
    OP1(TK_RBRACE, '}')                                                        \
    OP1(TK_RPAREN, ')')                                                        \
    OP1(TK_RBRACK, ']')                                                        \
    OTHER(TK_OP, "op")
// clang-format on

#define CHIBI_TOKEN_NAME(name, ...) name,
typedef enum
{
    CHIBI_TOKENS(CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME)
} TokenKind;

#define CHIBI_SKIP(...)
#define CHIBI_COUNT(...) +1

enum
{
    NKINDS = 0 CHIBI_TOKENS(CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT),
    NKEYWORDS = 0 CHIBI_TOKENS(CHIBI_SKIP, CHIBI_COUNT, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP),
};

// clang-format off
#define CHIBI_BUILTIN_NAMES(TYPE, OTHER)                                       \
    TYPE(NAME_VOID, "void")                                                    \
    TYPE(NAME_BOOL, "bool")                                                    \
    TYPE(NAME_INT, "int")                                                      \
    TYPE(NAME_I8, "i8")                                                        \
    TYPE(NAME_I16, "i16")                                                      \
    TYPE(NAME_I32, "i32")                                                      \
    TYPE(NAME_I64, "i64")                                                      \
    TYPE(NAME_U8, "u8")                                                        \
    TYPE(NAME_U16, "u16")                                                      \
    TYPE(NAME_U32, "u32")                                                      \
    TYPE(NAME_U64, "u64")                                                      \
    TYPE(NAME_F32, "f32")                                                      \
    TYPE(NAME_F64, "f64")                                                      \
    TYPE(NAME_UINTPTR, "uintptr")                                              \
    TYPE(NAME_ERROR, "error")                                                  \
    TYPE(NAME_U8X16, "u8x16")                                                  \
    TYPE(NAME_I8X16, "i8x16")                                                  \
    TYPE(NAME_U16X8, "u16x8")                                                  \
    TYPE(NAME_I16X8, "i16x8")                                                  \
    TYPE(NAME_U32X4, "u32x4")                                                  \
    TYPE(NAME_I32X4, "i32x4")                                                  \
    TYPE(NAME_U64X2, "u64x2")                                                  \
    TYPE(NAME_I64X2, "i64x2")                                                  \
    OTHER(NAME_TAIL, "tail")                                                   \
    OTHER(NAME_DISCARD, "_")                                                   \
    OTHER(NAME_LEN, "len")                                                     \
    OTHER(NAME_PTR, "ptr")                                                     \
    OTHER(NAME_TRAILING_ZEROS, "trailing_zeros")                               \
    OTHER(NAME_LEADING_ZEROS, "leading_zeros")                                 \
    OTHER(NAME_COUNT_ONES, "count_ones")                                       \
    OTHER(NAME_ADD_OVERFLOWS, "add_overflows")                                 \
    OTHER(NAME_SUB_OVERFLOWS, "sub_overflows")                                 \
    OTHER(NAME_MUL_OVERFLOWS, "mul_overflows")                                 \
    OTHER(NAME_UNALIGNED_LOAD, "unaligned_load")                               \
    OTHER(NAME_UNALIGNED_STORE, "unaligned_store")                             \
    OTHER(NAME_LOOKUP, "lookup")                                               \
    OTHER(NAME_TOP_BITS, "top_bits")
// clang-format on

#define CHIBI_BUILTIN_NAME_ID(name, text) name,
typedef enum
{
    NAME_LAST_KEYWORD = NKEYWORDS,
    CHIBI_BUILTIN_NAMES(CHIBI_BUILTIN_NAME_ID, CHIBI_BUILTIN_NAME_ID)
} BuiltinName;

enum
{
    NBUILTIN_TYPES = 0 CHIBI_BUILTIN_NAMES(CHIBI_COUNT, CHIBI_SKIP),
    NFIXED_NAMES = NKEYWORDS CHIBI_BUILTIN_NAMES(CHIBI_COUNT, CHIBI_COUNT),
};

static inline bool
lexer_kind_has_id(TokenKind kind)
{
    return kind == TK_IDENT || (kind >= TK_IF && kind < TK_IF + NKEYWORDS);
}

typedef struct
{
    u8 *kinds;
    u32 *starts;
    u32 *ids;
    u64 *line_starts;
} Tokens;

typedef struct
{
    Names names; // NOTE: This doesn't own its strings.
    const char *error;
    u32 error_pos;
} Lexer;

void lexer_init(Lexer *lx, Arena *arena, size_t total_source_bytes);

// Every source must be followed by this many zero bytes.
#define LEXER_SOURCE_PADDING 256
#define LEXER_MAX_SOURCE ((size_t)INT32_MAX - LEXER_SOURCE_PADDING)

// Returns the token count, including TK_EOF. out needs lexer_max_tokens(len)
// tokens. On an error it returns 0 and sets error and error_pos.
int lexer_tokenize(Lexer *lx, const char *src, Tokens out);

void print_error_at(const char *src, u32 pos, const char *msg);

void lexer_print_error(const Lexer *lx, const char *src);

static inline size_t
lexer_max_tokens(size_t len)
{
    return len + 1 + 8;
}

static inline size_t
lexer_line_words(size_t len)
{
    return len / 64 + 1;
}

Tokens lexer_alloc_tokens(Arena *arena, size_t len);

u32 lexer_token_length(const char *src, TokenKind kind, u32 start);

static inline bool
lexer_token_starts_line(Tokens t, int i)
{
    u32 at = t.starts[i];
    return t.line_starts[at / 64] >> (at % 64) & 1;
}

const char *lexer_kind_name(TokenKind k);

void lexer_dump(FILE *out, const char *src, Tokens tokens, int n);

#endif
