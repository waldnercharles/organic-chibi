#ifndef CHIBI_LEXER_H
#define CHIBI_LEXER_H

#include "common.h"
#include "names.h"

#include <stdio.h>

// clang-format off
#define CHIBI_TOKENS(OTHER, KW, OP2, OP1)                                      \
    OTHER(TK_EOF, "EOF")                                                       \
    OTHER(TK_IDENT, "IDENT")                                                   \
    OTHER(TK_INT, "INT")                                                       \
    OTHER(TK_FLOAT, "FLOAT")                                                   \
    OTHER(TK_STRING, "STRING")                                                 \
    KW(TK_IF, "if")                                                            \
    KW(TK_ELSE, "else")                                                        \
    KW(TK_FOR, "for")                                                          \
    KW(TK_WHILE, "while")                                                      \
    KW(TK_SWITCH, "switch")                                                    \
    KW(TK_CASE, "case")                                                        \
    KW(TK_BREAK, "break")                                                      \
    KW(TK_CONTINUE, "continue")                                                \
    KW(TK_RETURN, "return")                                                    \
    KW(TK_DEFER, "defer")                                                      \
    KW(TK_YIELD, "yield")                                                      \
    KW(TK_GOTO, "goto")                                                        \
    KW(TK_DEFAULT, "default")                                                  \
    KW(TK_TYPE, "type")                                                        \
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
    KW(TK_AS, "as")                                                            \
    KW(TK_SIZEOF, "sizeof")                                                    \
    KW(TK_ALIGNOF, "alignof")                                                  \
    KW(TK_OFFSETOF, "offsetof")                                                \
    KW(TK_TYPEOF, "typeof")                                                    \
    KW(TK_CAST, "cast")                                                        \
    OP2(TK_SHL, '<', '<')                                                      \
    OP2(TK_SHR, '>', '>')                                                      \
    OP2(TK_ANDAND, '&', '&')                                                   \
    OP2(TK_OROR, '|', '|')                                                     \
    OP2(TK_ADDEQ, '+', '=')                                                    \
    OP2(TK_SUBEQ, '-', '=')                                                    \
    OP2(TK_MULEQ, '*', '=')                                                    \
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
    OP2(TK_ELLIPSIS, '.', '.')                                                 \
    OP2(TK_DCOLON, ':', ':')                                                   \
    OP2(TK_DASSIGN, ':', '=')                                                  \
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
    OP1(TK_AND, '&')                                                           \
    OP1(TK_OR, '|')                                                            \
    OP1(TK_XOR, '^')                                                           \
    OP1(TK_COMPL, '~')                                                         \
    OP1(TK_LT, '<')                                                            \
    OP1(TK_GT, '>')                                                            \
    OP1(TK_COLON, ':')                                                         \
    OP1(TK_SEMI, ';')                                                          \
    OP1(TK_PERIOD, '.')                                                        \
    OP1(TK_COMMA, ',')                                                         \
    OP1(TK_LBRACE, '{')                                                        \
    OP1(TK_RBRACE, '}')                                                        \
    OP1(TK_LPAREN, '(')                                                        \
    OP1(TK_RPAREN, ')')                                                        \
    OP1(TK_LBRACK, '[')                                                        \
    OP1(TK_RBRACK, ']')                                                        \
    OTHER(TK_OP, "op")
// clang-format on

#define CHIBI_TOKEN_NAME(name, ...) name,
typedef enum
{
    CHIBI_TOKENS(CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME, CHIBI_TOKEN_NAME)
} TokenKind;

#define CHIBI_SKIP(...)
#define CHIBI_COUNT(...) +1

enum
{
    NKINDS = 0 CHIBI_TOKENS(CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT),
    NKEYWORDS = 0 CHIBI_TOKENS(CHIBI_SKIP, CHIBI_COUNT, CHIBI_SKIP, CHIBI_SKIP),
};

typedef struct
{
    u8 *kinds;
    u32 *starts;
    u32 *ids;
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

void lexer_print_error(const Lexer *lx, const char *src);

static inline size_t
lexer_max_tokens(size_t len)
{
    return len + 1;
}

Tokens lexer_alloc_tokens(Arena *arena, size_t len);

u32 lexer_token_length(const char *src, TokenKind kind, u32 start);

const char *lexer_kind_name(TokenKind k);

void lexer_dump(FILE *out, const char *src, Tokens tokens, int n);

#endif
