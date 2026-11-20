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
    UNARY(NODE_NEG_WRAP, TK_SUB_WRAP)                                          \
    UNARY(NODE_TRY, TK_TRY)                                                    \
    BINARY(NODE_MUL, TK_STAR, PREC_MUL, NODE_NONE)                             \
    BINARY(NODE_MUL_WRAP, TK_MUL_WRAP, PREC_MUL, NODE_NONE)                    \
    BINARY(NODE_DIV, TK_SLASH, PREC_MUL, NODE_NONE)                            \
    BINARY(NODE_MOD, TK_PERCENT, PREC_MUL, NODE_NONE)                          \
    BINARY(NODE_SHL, TK_SHL, PREC_MUL, NODE_NONE)                              \
    BINARY(NODE_SHR, TK_SHR, PREC_MUL, NODE_NONE)                              \
    BINARY(NODE_BIT_AND, TK_AMP, PREC_MUL, NODE_NONE)                          \
    BINARY(NODE_ADD, TK_PLUS, PREC_ADD, NODE_NONE)                             \
    BINARY(NODE_SUB, TK_MINUS, PREC_ADD, NODE_NONE)                            \
    BINARY(NODE_ADD_WRAP, TK_ADD_WRAP, PREC_ADD, NODE_NONE)                    \
    BINARY(NODE_SUB_WRAP, TK_SUB_WRAP, PREC_ADD, NODE_NONE)                    \
    BINARY(NODE_BIT_OR, TK_PIPE, PREC_ADD, NODE_NONE)                          \
    BINARY(NODE_BIT_XOR, TK_XOR, PREC_ADD, NODE_NONE)                          \
    BINARY(NODE_OR_ELSE, TK_OR_ELSE, PREC_OR_ELSE, NODE_OR_ELSE_LEFT)          \
    BINARY(NODE_CATCH, TK_CATCH, PREC_OR_ELSE, NODE_CATCH_LEFT)                \
    BINARY(NODE_EQ, TK_EQEQ, PREC_COMPARE, NODE_NONE)                          \
    BINARY(NODE_NE, TK_NEQ, PREC_COMPARE, NODE_NONE)                           \
    BINARY(NODE_LT, TK_LT, PREC_COMPARE, NODE_NONE)                            \
    BINARY(NODE_LE, TK_LTE, PREC_COMPARE, NODE_NONE)                           \
    BINARY(NODE_GT, TK_GT, PREC_COMPARE, NODE_NONE)                            \
    BINARY(NODE_GE, TK_GTE, PREC_COMPARE, NODE_NONE)                           \
    BINARY(NODE_IS, TK_IS, PREC_COMPARE, NODE_NONE)                            \
    BINARY(NODE_AND, TK_AND, PREC_AND, NODE_AND_LEFT)                          \
    BINARY(NODE_OR, TK_OR, PREC_OR, NODE_OR_LEFT)                              \
    ASSIGN(NODE_ASSIGN, TK_EQ)                                                 \
    ASSIGN(NODE_ADD_ASSIGN, TK_ADDEQ)                                          \
    ASSIGN(NODE_SUB_ASSIGN, TK_SUBEQ)                                          \
    ASSIGN(NODE_MUL_ASSIGN, TK_MULEQ)                                          \
    ASSIGN(NODE_ADD_WRAP_ASSIGN, TK_ADD_WRAP_EQ)                               \
    ASSIGN(NODE_SUB_WRAP_ASSIGN, TK_SUB_WRAP_EQ)                               \
    ASSIGN(NODE_MUL_WRAP_ASSIGN, TK_MUL_WRAP_EQ)                               \
    ASSIGN(NODE_DIV_ASSIGN, TK_DIVEQ)                                          \
    ASSIGN(NODE_MOD_ASSIGN, TK_MODEQ)                                          \
    ASSIGN(NODE_BIT_AND_ASSIGN, TK_ANDEQ)                                      \
    ASSIGN(NODE_BIT_OR_ASSIGN, TK_OREQ)                                        \
    ASSIGN(NODE_BIT_XOR_ASSIGN, TK_XOREQ)                                      \
    ASSIGN(NODE_SHL_ASSIGN, TK_SHLEQ)                                          \
    ASSIGN(NODE_SHR_ASSIGN, TK_SHREQ)                                          \
    ASSIGN(NODE_OR_ELSE_ASSIGN, TK_OR_ELSE_EQ)                                 \
    NODE(NODE_FIELD, 1, "field")                                               \
    NODE(NODE_INDEX, 2, "index")                                               \
    NODE(NODE_INDEX2, 3, "index")                                              \
    NODE(NODE_INDEX3, 4, "index")                                              \
    NODE(NODE_INDEX4, 5, "index")                                              \
    NODE(NODE_SLICE, 3, "slice")                                               \
    NODE(NODE_SLICE_INCLUSIVE, 3, "slice-inclusive")                           \
    NODE(NODE_SLICE_FROM, 2, "slice-from")                                     \
    NODE(NODE_SLICE_TO, 2, "slice-to")                                         \
    NODE(NODE_SLICE_TO_INCLUSIVE, 2, "slice-to-inclusive")                     \
    NODE(NODE_DEREF, 1, "deref")                                               \
    NODE(NODE_UNWRAP, 1, "unwrap")                                             \
    NODE(NODE_BITCAST, 2, "bitcast")                                           \
    NODE(NODE_CATCH_NAME, 0, NULL)                                             \
    NODE(NODE_CATCH_NAMED, 4, "catch")                                         \
    NODE(NODE_ENUM_LITERAL, 0, ".")                                            \
    NODE(NODE_IMPORT, 0, "import")                                             \
    NODE(NODE_POINTER_TYPE, 1, "ptr")                                          \
    NODE(NODE_OPTIONAL_TYPE, 1, "optional")                                    \
    NODE(NODE_ERROR_UNION, 2, "!")                                             \
    NODE(NODE_ERROR_UNION_INFERRED, 1, "!")                                    \
    NODE(NODE_SLICE_TYPE, 1, "slice")                                          \
    NODE(NODE_ARRAY_TYPE, 2, "array")                                          \
    NODE(NODE_ARRAY2_TYPE, 3, "array")                                         \
    NODE(NODE_ARRAY3_TYPE, 4, "array")                                         \
    NODE(NODE_ARRAY4_TYPE, 5, "array")                                         \
    NODE(NODE_EXPR_STMT, 1, NULL)                                              \
    NODE(NODE_VAR_INIT, 1, ":=")                                               \
    NODE(NODE_VAR_TYPE, 1, ":")                                                \
    NODE(NODE_VAR_TYPE_INIT, 3, ":")                                           \
    NODE(NODE_UNDEFINED, 0, NULL)                                              \
    NODE(NODE_PARAM, 0, NULL)                                                  \
    NODE(NODE_PARAM_INIT, 1, ":=")                                             \
    NODE(NODE_PARAM_TYPE, 1, ":")                                              \
    NODE(NODE_PARAM_TYPE_INIT, 3, ":")                                         \
    NODE(NODE_CONST_INIT, 1, "const")                                          \
    NODE(NODE_CONST_TYPE_INIT, 3, "const")                                     \
    NODE(NODE_MEMBER_INIT, 1, ":=")                                            \
    NODE(NODE_MEMBER_TYPE, 1, ":")                                             \
    NODE(NODE_MEMBER_TYPE_INIT, 3, ":")                                        \
    NODE(NODE_ENUM_BACKING, 1, ":")                                            \
    NODE(NODE_ENUM_MEMBER, 0, NULL)                                            \
    NODE(NODE_UNION_TAG, 1, ":")                                               \
    NODE(NODE_EMPTY_MEMBER, 0, NULL)                                           \
    NODE(NODE_ENUM_VALUE, 1, "=")                                              \
    NODE(NODE_FN_RETURN, 1, "->")                                              \
    NODE(NODE_ARG_NAME, 0, NULL)                                               \
    NODE(NODE_NAMED_ARG, 3, "arg")                                             \
    NODE(NODE_WHILE, 4, "while")                                               \
    NODE(NODE_RETURN, 0, NULL)                                                 \
    NODE(NODE_RETURN_VALUE, 2, "return")                                       \
    NODE(NODE_BREAK, 0, NULL)                                                  \
    NODE(NODE_BREAK_LABEL, 0, "break")                                         \
    NODE(NODE_BREAK_VALUE, 1, "break")                                         \
    NODE(NODE_CONTINUE, 0, NULL)                                               \
    NODE(NODE_CONTINUE_LABEL, 0, "continue")                                   \
    NODE(NODE_CONTINUE_VALUE, 1, "continue")                                   \
    NODE(NODE_DEFER, 2, "defer")                                               \
    NODE(NODE_DEFER_CATCH, 2, "defer-catch")                                   \
    NODE(NODE_DEFER_CATCH_NAMED, 3, "defer-catch")                             \
    NODE(NODE_RANGE, 2, "..")                                                  \
    NODE(NODE_RANGE_INCLUSIVE, 2, "..=")                                       \
    NODE(NODE_FOR_VALUE, 0, NULL)                                              \
    NODE(NODE_SWITCH_VALUE, 0, NULL)                                           \
    NODE(NODE_SWITCH_REF, 0, "&")                                              \
    NODE(NODE_FOR_INDEX, 0, NULL)                                              \
    NODE(NODE_PACKAGE, 0, "package")                                           \
    NODE(NODE_IMPORT_DECL, 0, "import")                                        \
    NODE(NODE_PUB, 1, "pub")                                                   \
    NODE(NODE_EXPORT, 1, "export")                                             \
    NODE(NODE_EXTERN, 1, "extern")                                             \
    LIST(NODE_CALL, NODE_CALL_ARGS, 1, "call")                                 \
    LIST(NODE_BLOCK, NODE_BLOCK_START, 0, "block")                             \
    LIST(NODE_IF, NODE_IF_START, 0, "if")                                      \
    LIST(NODE_FN, NODE_FN_START, 0, "fn")                                      \
    LIST(NODE_FN_LITERAL, NODE_FN_START, 0, "fn-literal")                      \
    LIST(NODE_FN_TYPE, NODE_FN_START, 0, "fn-type")                            \
    LIST(NODE_LITERAL, NODE_LITERAL_START, 1, "literal")                       \
    LIST(NODE_ANON_LITERAL, NODE_ANON_LITERAL_START, 0, "literal")             \
    LIST(NODE_ARRAY_LITERAL, NODE_LITERAL_START, 1, "array-literal")           \
    LIST(NODE_ANON_ARRAY_LITERAL, NODE_ANON_LITERAL_START, 0, "array-literal") \
    LIST(NODE_STRUCT, NODE_STRUCT_START, 0, "struct")                          \
    LIST(NODE_ENUM, NODE_ENUM_START, 0, "enum")                                \
    LIST(NODE_UNION, NODE_UNION_START, 0, "union")                             \
    LIST(NODE_ANON_UNION, NODE_UNION_START, 0, "union")                        \
    LIST(NODE_LABELED, NODE_LABEL_START, 0, "label")                           \
    LIST(NODE_LABELED_VALUE, NODE_VALUE_LABEL_START, 0, "label")               \
    LIST(NODE_LABELED_ITEM, NODE_VALUE_LABEL_START, 0, "label")                \
    LIST(NODE_FOR, NODE_FOR_START, 0, "for")                                   \
    LIST(NODE_CASE, NODE_CASE_START, 0, "case")                                \
    LIST(NODE_SWITCH, NODE_SWITCH_START, 0, "switch")                          \
    LIST(NODE_VALUE_SWITCH, NODE_VALUE_SWITCH_START, 0, "switch")              \
    LIST(NODE_SWITCH_ITEM, NODE_VALUE_SWITCH_START, 0, "switch")               \
    MARKER(NODE_CALL_ARGS)                                                     \
    MARKER(NODE_AND_LEFT)                                                      \
    MARKER(NODE_OR_LEFT)                                                       \
    MARKER(NODE_OR_ELSE_LEFT)                                                  \
    MARKER(NODE_CATCH_LEFT)                                                    \
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
    MARKER(NODE_LITERAL_START)                                                 \
    MARKER(NODE_ANON_LITERAL_START)                                            \
    MARKER(NODE_STRUCT_START)                                                  \
    MARKER(NODE_ENUM_START)                                                    \
    MARKER(NODE_UNION_START)                                                   \
    MARKER(NODE_LABEL_START)                                                   \
    MARKER(NODE_VALUE_LABEL_START)                                             \
    MARKER(NODE_FOR_START)                                                     \
    MARKER(NODE_CASE_START)                                                    \
    MARKER(NODE_CASE_BODY)                                                     \
    MARKER(NODE_SWITCH_START)                                                  \
    MARKER(NODE_VALUE_SWITCH_START)                                            \
    MARKER(NODE_VALUE)                                                         \
    MARKER(NODE_RETURN_START)
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
    u32 *top_ends;
} Nodes;

#define PARSER_MAX_DEPTH 256

typedef struct
{
    Tokens tokens;
    int pos;
    int end;
    int depth;
    int brackets;
    bool in_arm;
    Nodes out;
    int count;
    int top_count;
    const char *error;
    u32 error_pos;
} Parser;

int parser_parse(Parser *p, Tokens tokens, int ntokens, Nodes out);

void parser_print_error(const Parser *p, const char *src);

Nodes parser_alloc_nodes(Arena *arena, int ntokens);

void parser_dump(FILE *out, Arena *scratch, const char *src, Tokens tokens, Nodes nodes, int n);

#endif
