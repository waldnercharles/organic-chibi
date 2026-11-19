#ifndef CHIBI_PARSER_H
#define CHIBI_PARSER_H

#include "arena.h"
#include "lexer.h"

#include <stdio.h>

// clang-format off
#define CHIBI_NODES(LEAF, UNARY, BINARY, ASSIGN, NODE, LIST, MARKER)           \
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
    UNARY(NODE_ADDR, TK_AMP)                                                   \
    BINARY(NODE_MUL, TK_STAR, PREC_MUL, NODE_NONE)                             \
    BINARY(NODE_DIV, TK_SLASH, PREC_MUL, NODE_NONE)                            \
    BINARY(NODE_MOD, TK_PERCENT, PREC_MUL, NODE_NONE)                          \
    BINARY(NODE_SHL, TK_SHL, PREC_MUL, NODE_NONE)                              \
    BINARY(NODE_SHR, TK_SHR, PREC_MUL, NODE_NONE)                              \
    BINARY(NODE_BIT_AND, TK_AMP, PREC_MUL, NODE_NONE)                          \
    BINARY(NODE_ADD, TK_PLUS, PREC_ADD, NODE_NONE)                             \
    BINARY(NODE_SUB, TK_MINUS, PREC_ADD, NODE_NONE)                            \
    BINARY(NODE_BIT_OR, TK_PIPE, PREC_ADD, NODE_NONE)                          \
    BINARY(NODE_BIT_XOR, TK_XOR, PREC_ADD, NODE_NONE)                          \
    BINARY(NODE_EQ, TK_EQEQ, PREC_COMPARE, NODE_NONE)                          \
    BINARY(NODE_NE, TK_NEQ, PREC_COMPARE, NODE_NONE)                           \
    BINARY(NODE_LT, TK_LT, PREC_COMPARE, NODE_NONE)                            \
    BINARY(NODE_LE, TK_LTE, PREC_COMPARE, NODE_NONE)                           \
    BINARY(NODE_GT, TK_GT, PREC_COMPARE, NODE_NONE)                            \
    BINARY(NODE_GE, TK_GTE, PREC_COMPARE, NODE_NONE)                           \
    BINARY(NODE_AND, TK_AND, PREC_AND, NODE_AND_LEFT)                          \
    BINARY(NODE_OR, TK_OR, PREC_OR, NODE_OR_LEFT)                              \
    ASSIGN(NODE_ASSIGN, TK_EQ)                                                 \
    ASSIGN(NODE_ADD_ASSIGN, TK_ADDEQ)                                          \
    ASSIGN(NODE_SUB_ASSIGN, TK_SUBEQ)                                          \
    ASSIGN(NODE_MUL_ASSIGN, TK_MULEQ)                                          \
    ASSIGN(NODE_DIV_ASSIGN, TK_DIVEQ)                                          \
    ASSIGN(NODE_MOD_ASSIGN, TK_MODEQ)                                          \
    ASSIGN(NODE_BIT_AND_ASSIGN, TK_ANDEQ)                                      \
    ASSIGN(NODE_BIT_OR_ASSIGN, TK_OREQ)                                        \
    ASSIGN(NODE_BIT_XOR_ASSIGN, TK_XOREQ)                                      \
    ASSIGN(NODE_SHL_ASSIGN, TK_SHLEQ)                                          \
    ASSIGN(NODE_SHR_ASSIGN, TK_SHREQ)                                          \
    NODE(NODE_FIELD, 1, "field")                                               \
    NODE(NODE_INDEX, 2, "index")                                               \
    NODE(NODE_DEREF, 1, "deref")                                               \
    NODE(NODE_EXPR_STMT, 1, NULL)                                              \
    NODE(NODE_VAR_INIT, 1, ":=")                                               \
    NODE(NODE_VAR_TYPE, 1, ":")                                                \
    NODE(NODE_VAR_TYPE_INIT, 3, ":")                                           \
    NODE(NODE_PARAM, 0, NULL)                                                  \
    NODE(NODE_PARAM_INIT, 1, ":=")                                             \
    NODE(NODE_PARAM_TYPE, 1, ":")                                              \
    NODE(NODE_PARAM_TYPE_INIT, 3, ":")                                         \
    NODE(NODE_CONST_INIT, 1, "const")                                          \
    NODE(NODE_CONST_TYPE_INIT, 3, "const")                                     \
    NODE(NODE_FN_RETURN, 1, "->")                                              \
    NODE(NODE_ARG_NAME, 0, NULL)                                               \
    NODE(NODE_NAMED_ARG, 3, "arg")                                             \
    NODE(NODE_WHILE, 4, "while")                                               \
    NODE(NODE_RETURN, 0, NULL)                                                 \
    NODE(NODE_RETURN_VALUE, 1, "return")                                       \
    NODE(NODE_BREAK, 0, NULL)                                                  \
    NODE(NODE_CONTINUE, 0, NULL)                                               \
    NODE(NODE_DEFER, 2, "defer")                                               \
    LIST(NODE_CALL, NODE_CALL_ARGS, 1, "call")                                 \
    LIST(NODE_BLOCK, NODE_BLOCK_START, 0, "block")                             \
    LIST(NODE_IF, NODE_IF_START, 0, "if")                                      \
    LIST(NODE_FN, NODE_FN_START, 0, "fn")                                      \
    LIST(NODE_FN_LITERAL, NODE_FN_START, 0, "fn-literal")                      \
    MARKER(NODE_CALL_ARGS)                                                     \
    MARKER(NODE_AND_LEFT)                                                      \
    MARKER(NODE_OR_LEFT)                                                       \
    MARKER(NODE_BLOCK_START)                                                   \
    MARKER(NODE_IF_START)                                                      \
    MARKER(NODE_IF_THEN)                                                       \
    MARKER(NODE_ELSE_IF)                                                       \
    MARKER(NODE_ELSE)                                                          \
    MARKER(NODE_WHILE_START)                                                   \
    MARKER(NODE_WHILE_BODY)                                                    \
    MARKER(NODE_DEFER_START)                                                   \
    MARKER(NODE_FN_START)                                                      \
    MARKER(NODE_FN_BODY)                                                       \
    MARKER(NODE_VALUE)
// clang-format on

#define CHIBI_NODE_NAME(name, ...) name,
typedef enum
{
    NODE_NONE,
    CHIBI_NODES(CHIBI_NODE_NAME, CHIBI_NODE_NAME, CHIBI_NODE_NAME, CHIBI_NODE_NAME, CHIBI_NODE_NAME, CHIBI_NODE_NAME, CHIBI_NODE_NAME)
} NodeKind;

enum
{
    NNODES = 1 CHIBI_NODES(CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT, CHIBI_COUNT),
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
    int brackets;
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
