#ifndef CHIBI_PARSER_H
#define CHIBI_PARSER_H

#include "arena.h"
#include "lexer.h"

#include <stdio.h>

// clang-format off
#define CHIBI_NODES(LEAF, UNARY, BINARY)                                       \
    LEAF(NODE_NAME, TK_IDENT)                                                  \
    LEAF(NODE_INT, TK_INT)                                                     \
    LEAF(NODE_FLOAT, TK_FLOAT)                                                 \
    LEAF(NODE_STRING, TK_STRING)                                               \
    LEAF(NODE_TRUE, TK_TRUE)                                                   \
    LEAF(NODE_FALSE, TK_FALSE)                                                 \
    LEAF(NODE_NIL, TK_NIL)                                                     \
    UNARY(NODE_NEG, TK_MINUS)                                                  \
    UNARY(NODE_NOT, TK_NOT)                                                    \
    UNARY(NODE_BIT_NOT, TK_COMPL)                                              \
    BINARY(NODE_MUL, TK_STAR, PREC_MUL)                                        \
    BINARY(NODE_DIV, TK_SLASH, PREC_MUL)                                       \
    BINARY(NODE_MOD, TK_PERCENT, PREC_MUL)                                     \
    BINARY(NODE_SHL, TK_SHL, PREC_MUL)                                         \
    BINARY(NODE_SHR, TK_SHR, PREC_MUL)                                         \
    BINARY(NODE_BIT_AND, TK_AND, PREC_MUL)                                     \
    BINARY(NODE_ADD, TK_PLUS, PREC_ADD)                                        \
    BINARY(NODE_SUB, TK_MINUS, PREC_ADD)                                       \
    BINARY(NODE_BIT_OR, TK_OR, PREC_ADD)                                       \
    BINARY(NODE_BIT_XOR, TK_XOR, PREC_ADD)                                     \
    BINARY(NODE_EQ, TK_EQEQ, PREC_COMPARE)                                     \
    BINARY(NODE_NE, TK_NEQ, PREC_COMPARE)                                      \
    BINARY(NODE_LT, TK_LT, PREC_COMPARE)                                       \
    BINARY(NODE_LE, TK_LTE, PREC_COMPARE)                                      \
    BINARY(NODE_GT, TK_GT, PREC_COMPARE)                                       \
    BINARY(NODE_GE, TK_GTE, PREC_COMPARE)                                      \
    BINARY(NODE_AND, TK_ANDAND, PREC_AND)                                      \
    BINARY(NODE_OR, TK_OROR, PREC_OR)
// clang-format on

#define CHIBI_NODE_NAME(name, ...) name,
typedef enum
{
    NODE_NONE,
    CHIBI_NODES(CHIBI_NODE_NAME, CHIBI_NODE_NAME, CHIBI_NODE_NAME)
} NodeKind;

enum
{
    NNODES = 1 CHIBI_NODES(CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT),
};

typedef struct
{
    u8 *kinds;
    u32 *tokens;
} Nodes;

#define PARSER_MAX_DEPTH 256

typedef struct
{
    Tokens tokens;
    int pos;
    int end;
    int depth;
    Nodes out;
    int count;
    const char *error;
    u32 error_pos;
} Parser;

int parser_parse(Parser *p, Tokens tokens, int ntokens, Nodes out);

void parser_print_error(const Parser *p, const char *src);

Nodes parser_alloc_nodes(Arena *arena, int ntokens);

void parser_dump(FILE *out, Arena *scratch, const char *src, Tokens tokens, Nodes nodes, int n);

#endif
