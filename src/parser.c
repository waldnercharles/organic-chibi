#include "parser.h"

#include <assert.h>

enum
{
    PREC_NONE,
    PREC_OR,
    PREC_AND,
    PREC_COMPARE,
    PREC_OR_ELSE,
    PREC_ADD,
    PREC_MUL,
};

#define PREFIX_NODE(name, token) [token] = name,
static const u8 prefix_nodes[NKINDS] = {
    CHIBI_NODES(PREFIX_NODE, PREFIX_NODE, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
};

typedef struct
{
    u8 prec;
    u8 node;
    u8 marker;
} BinaryOp;

#define BINARY_OP(name, token, prec, marker) [token] = { prec, name, marker },
static const BinaryOp binary_ops[NKINDS] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, BINARY_OP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
};

#define ASSIGN_NODE(name, token) [token] = name,
static const u8 assign_nodes[NKINDS] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, ASSIGN_NODE, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
};

#define ARITY_0(name, ...) [name] = 0,
#define ARITY_1(name, ...) [name] = 1,
#define ARITY_2(name, ...) [name] = 2,
#define ARITY_3(name, ...) [name] = 3,
#define BINARY_ARITY(name, token, prec, marker)                                \
    [name] = 2 + (marker != NODE_NONE),
#define NODE_ARITY(name, arity, label) [name] = arity,
#define LIST_ARITY(name, start, before, label) [name] = before,
static const u8 node_arity[NNODES] = {
    CHIBI_NODES(ARITY_0, ARITY_1, BINARY_ARITY, ARITY_3, NODE_ARITY, LIST_ARITY, ARITY_0)
};

enum
{
    MAX_DIMS = 4,
};

static_assert(NODE_INDEX4 == NODE_INDEX + MAX_DIMS - 1, "index nodes must be consecutive");

static const u8 array_nodes[MAX_DIMS + 1] = {
    NODE_NONE,        NODE_ARRAY_TYPE,  NODE_ARRAY2_TYPE,
    NODE_ARRAY3_TYPE, NODE_ARRAY4_TYPE,
};

static void
fail(Parser *p, const char *msg)
{
    if (p->error == NULL) {
        TokenKind kind = p->tokens.kinds[p->pos];
        p->error = kind == TK_ANDAND ? "use `and` instead of `&&`"
                 : kind == TK_OROR   ? "use `or` instead of `||`"
                                     : msg;
        p->error_pos = p->tokens.starts[p->pos];
    }
    p->pos = p->end;
}

static void
emit(Parser *p, NodeKind kind, int token)
{
    int count = p->count;
    u8 *kinds = p->out.kinds;
    p->out.tokens[count] = (u32)token;
    p->count = count + 1;
    kinds[count] = (u8)kind;
}

static void
expect(Parser *p, TokenKind kind, const char *msg)
{
    if (p->tokens.kinds[p->pos] != kind) {
        fail(p, msg);
        return;
    }
    p->pos++;
}

static bool
descend(Parser *p)
{
    if (p->depth == PARSER_MAX_DEPTH) {
        fail(p, "nested too deeply");
        return false;
    }
    p->depth++;
    return true;
}

static bool
starts_line(Parser *p)
{
    return lexer_token_starts_line(p->tokens, p->pos);
}

static void parse_binary(Parser *p, int min_prec);
static void parse_block(Parser *p);
static void parse_switch(Parser *p, bool value);

static ALWAYS_INLINE void
parse_items(Parser *p, TokenKind close)
{
    while (p->tokens.kinds[p->pos] != close) {
        int at = p->pos;
        const u8 *kinds = p->tokens.kinds;
        if (kinds[at] == TK_PERIOD && lexer_kind_has_id(kinds[at + 1]) &&
            kinds[at + 2] == TK_EQ) {
            emit(p, NODE_ARG_NAME, at + 1);
            emit(p, NODE_VALUE, at + 2);
            p->pos += 3;
            parse_binary(p, PREC_OR);
            emit(p, NODE_NAMED_ARG, at);
        } else {
            parse_binary(p, PREC_OR);
            u8 *last = &p->out.kinds[p->count - 1];
            if (*last == NODE_LABELED_VALUE) {
                *last = NODE_LABELED_ITEM;
            } else if (*last == NODE_VALUE_SWITCH) {
                *last = NODE_SWITCH_ITEM;
            } else if (*last == NODE_VALUE_IF) {
                *last = NODE_VALUE_IF_ITEM;
            }
        }

        if (p->tokens.kinds[p->pos] != TK_COMMA) {
            return;
        }
        p->pos++;
    }
}

static void
parse_literal(Parser *p, NodeKind node, NodeKind start, TokenKind close)
{
    if (!descend(p)) {
        return;
    }

    p->pos++;
    emit(p, start, p->pos++);
    p->brackets++;
    parse_items(p, close);
    p->brackets--;
    p->depth--;

    if (p->tokens.kinds[p->pos] != close) {
        fail(p, close == TK_RBRACE ? "expected `,` or `}`" : "expected `,` or `]`");
        return;
    }
    emit(p, node, p->pos++);
}

static int
parse_indices(Parser *p)
{
    int n = 0;
    for (;;) {
        parse_binary(p, PREC_OR);
        n++;

        if (p->tokens.kinds[p->pos] != TK_COMMA) {
            return n;
        }
        if (n == MAX_DIMS) {
            fail(p, "at most 4 dimensions");
            return n;
        }
        p->pos++;
    }
}

static const NodeKind slice_nodes[2][2] = {
    { NODE_SLICE_TO, NODE_SLICE_TO_INCLUSIVE },
    { NODE_SLICE, NODE_SLICE_INCLUSIVE },
};

static NOINLINE void
parse_slice(Parser *p, int at, bool has_start)
{
    bool inclusive = p->tokens.kinds[p->pos++] == TK_RANGE_INCLUSIVE;
    if (has_start && !inclusive && p->tokens.kinds[p->pos] == TK_RBRACK) {
        p->pos++;
        emit(p, NODE_SLICE_FROM, at);
        return;
    }

    parse_binary(p, PREC_OR);
    expect(p, TK_RBRACK, "expected `]`");
    emit(p, slice_nodes[has_start][inclusive], at);
}

static_assert(TK_COLON == TK_LPAREN + 5, "the six postfix tokens must be consecutive kinds");

static void
parse_postfix(Parser *p)
{
    for (;;) {
        int at = p->pos;
        TokenKind kind = p->tokens.kinds[at];
        if (kind < TK_LPAREN || kind > TK_COLON) {
            return;
        }

        switch (kind) {
            case TK_PERIOD:
                if (p->in_arm && p->brackets == 0 && starts_line(p)) {
                    return;
                }
                if (p->tokens.kinds[at + 1] == TK_LBRACE) {
                    parse_literal(p, NODE_LITERAL, NODE_LITERAL_START, TK_RBRACE);
                    break;
                }
                if (p->tokens.kinds[at + 1] == TK_LBRACK) {
                    parse_literal(p, NODE_ARRAY_LITERAL, NODE_LITERAL_START, TK_RBRACK);
                    break;
                }

                p->pos++;
                if (!lexer_kind_has_id(p->tokens.kinds[p->pos])) {
                    fail(p, "expected a field name after `.`");
                    return;
                }
                emit(p, NODE_FIELD, p->pos++);
                break;

            case TK_DEREF:
                if (p->in_arm && p->brackets == 0 && starts_line(p)) {
                    return;
                }
                emit(p, NODE_DEREF, p->pos++);
                break;

            case TK_UNWRAP:
                if (p->brackets == 0 && starts_line(p)) {
                    return;
                }
                emit(p, NODE_UNWRAP, p->pos++);
                break;

            case TK_COLON: {
                int last = p->count - 1;
                if (p->tokens.kinds[at + 1] != TK_LBRACE ||
                    p->out.kinds[last] != NODE_NAME ||
                    p->out.tokens[last] != (u32)(at - 1)) {
                    return;
                }
                p->out.kinds[last] = NODE_VALUE_LABEL_START;
                p->pos++;
                parse_block(p);
                emit(p, NODE_LABELED_VALUE, at - 1);
                break;
            }

            case TK_LBRACK:
                if (p->brackets == 0 && starts_line(p)) {
                    return;
                }
                if (!descend(p)) {
                    return;
                }
                p->pos++;
                p->brackets++;
                TokenKind first = p->tokens.kinds[p->pos];
                if (first == TK_RANGE || first == TK_RANGE_INCLUSIVE) {
                    parse_slice(p, at, false);
                } else {
                    int n = parse_indices(p);
                    TokenKind kind = p->tokens.kinds[p->pos];
                    if (kind == TK_RBRACK) {
                        p->pos++;
                        emit(p, NODE_INDEX - 1 + n, at);
                    } else if (n == 1 && (kind == TK_RANGE || kind == TK_RANGE_INCLUSIVE)) {
                        parse_slice(p, at, true);
                    } else {
                        fail(p, "expected `]`");
                    }
                }
                p->brackets--;
                p->depth--;
                break;

            case TK_LPAREN:
                if (p->brackets == 0 && starts_line(p)) {
                    return;
                }
                if (!descend(p)) {
                    return;
                }
                emit(p, NODE_CALL_ARGS, p->pos++);
                p->brackets++;
                parse_items(p, TK_RPAREN);
                p->brackets--;
                p->depth--;
                if (p->tokens.kinds[p->pos] != TK_RPAREN) {
                    fail(p, "expected `,` or `)`");
                    return;
                }
                emit(p, NODE_CALL, p->pos++);
                break;

            default: return;
        }
    }
}

static void parse_function(Parser *p, bool named);
static void parse_composite_type(Parser *p);
static void parse_type(Parser *p);

static NOINLINE void
parse_bitcast(Parser *p)
{
    int at = p->pos++;
    if (p->tokens.kinds[p->pos] != TK_LPAREN) {
        fail(p, "expected `(`");
        return;
    }
    if (!descend(p)) {
        return;
    }

    p->pos++;
    p->brackets++;
    parse_type(p);
    expect(p, TK_COMMA, "expected `,`");
    parse_binary(p, PREC_OR);
    expect(p, TK_RPAREN, "expected `)`");
    p->brackets--;
    p->depth--;

    emit(p, NODE_BITCAST, at);
    parse_postfix(p);
}

static void
parse_operand(Parser *p)
{
    int at = p->pos;
    TokenKind kind = p->tokens.kinds[at];
    NodeKind node = prefix_nodes[kind];

    if (node != NODE_NONE && node_arity[node] == 0) {
        p->pos++;
        emit(p, node, at);
        parse_postfix(p);
        return;
    }

    if (kind == TK_PERIOD) {
        TokenKind next = p->tokens.kinds[at + 1];
        if (next == TK_LBRACE) {
            parse_literal(p, NODE_ANON_LITERAL, NODE_ANON_LITERAL_START, TK_RBRACE);
            parse_postfix(p);
            return;
        }
        if (next == TK_LBRACK) {
            parse_literal(p, NODE_ANON_ARRAY_LITERAL, NODE_ANON_LITERAL_START, TK_RBRACK);
            parse_postfix(p);
            return;
        }
        if (lexer_kind_has_id(next)) {
            p->pos += 2;
            emit(p, NODE_ENUM_LITERAL, at + 1);
            parse_postfix(p);
            return;
        }
    }

    if (kind == TK_STAR) {
        if (!descend(p)) {
            return;
        }
        p->pos++;
        parse_type(p);
        TokenKind next = p->tokens.kinds[p->pos + 1];
        bool literal = p->tokens.kinds[p->pos] == TK_PERIOD &&
                       (next == TK_LBRACE || next == TK_LBRACK);
        emit(p, literal ? NODE_POINTER_TYPE : NODE_POINTER_OPERAND, at);
        p->depth--;
        parse_postfix(p);
        return;
    }

    if (kind == TK_LBRACK) {
        parse_composite_type(p);
        if (p->tokens.kinds[p->pos] == TK_LBRACE) {
            fail(p, "expected `.{` or `.[` after a type");
            return;
        }
        parse_postfix(p);
        return;
    }

    if (kind == TK_QUESTION) {
        parse_composite_type(p);
        parse_postfix(p);
        return;
    }

    if (kind == TK_FN) {
        parse_function(p, false);
        return;
    }

    if (kind == TK_IMPORT) {
        if (p->tokens.kinds[at + 1] != TK_STRING) {
            p->pos++;
            fail(p, "expected a path in quotes");
            return;
        }
        p->pos += 2;
        emit(p, NODE_IMPORT, at + 1);
        parse_postfix(p);
        return;
    }

    if (node == NODE_NONE && kind != TK_LPAREN) {
        if (kind == TK_BITCAST) {
            parse_bitcast(p);
            return;
        }
        if (kind == TK_SWITCH) {
            parse_switch(p, true);
            return;
        }

        const char *msg = kind == TK_UNDEFINED
                            ? "use `undefined` only as `name: Type = undefined`"
                        : kind == TK_UNWRAP
                            ? "use `!!` only after a value on the same line"
                        : kind == TK_IF ? "put parentheses around an `if` "
                                          "expression inside another expression"
                                        : "expected an expression";
        fail(p, msg);
        return;
    }

    if (!descend(p)) {
        return;
    }

    p->pos++;
    if (kind == TK_LPAREN) {
        p->brackets++;
        parse_binary(p, PREC_OR);
        expect(p, TK_RPAREN, "expected `)`");
        p->brackets--;
        p->depth--;
        parse_postfix(p);
    } else {
        parse_operand(p);
        emit(p, node, at);
        p->depth--;
    }
}

static NOINLINE bool
parse_catch_handler(Parser *p, int at)
{
    TokenKind kind = p->tokens.kinds[p->pos];
    if (kind == TK_LBRACE) {
        parse_block(p);
        emit(p, NODE_CATCH, at);
        return true;
    }
    if (kind != TK_IDENT || p->tokens.kinds[p->pos + 1] != TK_FAT_ARROW) {
        return false;
    }

    emit(p, NODE_CATCH_NAME, p->pos);
    p->pos += 2;
    if (p->tokens.kinds[p->pos] == TK_LBRACE) {
        parse_block(p);
    } else {
        parse_binary(p, PREC_OR);
    }
    emit(p, NODE_CATCH_NAMED, at);
    return true;
}

static NOINLINE void
parse_value_if(Parser *p)
{
    if (!descend(p)) {
        return;
    }

    int at = p->pos++;
    parse_binary(p, PREC_OR);
    if (p->tokens.kinds[p->pos] != TK_THEN) {
        fail(p, "expected `then` after the condition");
        return;
    }
    emit(p, NODE_VALUE_IF_THEN, p->pos++);
    parse_binary(p, PREC_OR);
    if (p->tokens.kinds[p->pos] != TK_ELSE) {
        fail(p, "expected `else`; an `if` expression needs both values");
        return;
    }
    emit(p, NODE_VALUE_IF_ELSE, p->pos++);
    parse_binary(p, PREC_OR);
    emit(p, NODE_VALUE_IF, at);
    p->depth--;
}

static void
parse_binary(Parser *p, int min_prec)
{
    if (min_prec == PREC_OR && p->tokens.kinds[p->pos] == TK_IF) {
        parse_value_if(p);
        return;
    }
    parse_operand(p);

    int last = PREC_NONE;
    for (;;) {
        BinaryOp op = binary_ops[p->tokens.kinds[p->pos]];

        if (op.prec < min_prec) {
            return;
        }
        if (p->in_arm && p->brackets == 0 && starts_line(p)) {
            return;
        }

        if (op.prec == PREC_COMPARE && last == PREC_COMPARE) {
            fail(p, "comparisons cannot be chained; add parentheses");
            return;
        }

        last = op.prec;

        int at = p->pos++;
        if (op.marker != NODE_NONE) {
            emit(p, op.marker, at);
            if (op.node == NODE_CATCH && parse_catch_handler(p, at)) {
                continue;
            }
        }
        parse_binary(p, op.prec + 1);
        emit(p, op.node, at);
    }
}

static bool
ends_statement(Parser *p)
{
    TokenKind kind = p->tokens.kinds[p->pos];
    return kind == TK_SEMI || kind == TK_RBRACE || starts_line(p) ||
           (p->in_arm && kind == TK_COMMA);
}

static void
parse_type(Parser *p)
{
    TokenKind next = p->tokens.kinds[p->pos + 1];
    if (p->tokens.kinds[p->pos] == TK_IDENT && next != TK_PERIOD && next != TK_NOT) {
        emit(p, NODE_NAME, p->pos++);
        return;
    }
    parse_composite_type(p);
}

static void
parse_fn_type(Parser *p)
{
    emit(p, NODE_FN_START, p->pos++);
    int open = p->pos;
    if (p->tokens.kinds[open] != TK_LPAREN) {
        fail(p, "expected `(`");
        return;
    }

    p->pos++;
    p->brackets++;
    while (p->tokens.kinds[p->pos] != TK_RPAREN) {
        int name = p->pos;
        if (p->tokens.kinds[name] == TK_IDENT && p->tokens.kinds[name + 1] == TK_COLON) {
            p->pos += 2;
            parse_type(p);
            emit(p, NODE_PARAM_TYPE, name);
        } else {
            parse_type(p);
        }

        if (p->tokens.kinds[p->pos] != TK_COMMA) {
            break;
        }
        p->pos++;
    }
    p->brackets--;

    if (p->tokens.kinds[p->pos] != TK_RPAREN) {
        fail(p, "expected `,` or `)`");
        return;
    }
    p->pos++;

    if (p->tokens.kinds[p->pos] == TK_ARROW) {
        int arrow = p->pos++;
        parse_type(p);
        emit(p, NODE_FN_RETURN, arrow);
    }
    emit(p, NODE_FN_TYPE, open);
}

static bool
expect_value_type(Parser *p, int bang)
{
    TokenKind kind = p->tokens.kinds[p->pos];
    bool type = kind == TK_IDENT || kind == TK_STAR || kind == TK_QUESTION ||
                kind == TK_OR_ELSE || kind == TK_LBRACK || kind == TK_FN ||
                kind == TK_EXTERN;
    if (type && (p->brackets > 0 || !starts_line(p))) {
        return true;
    }

    p->pos = bang;
    fail(p, "expected a type after `!`, such as `void`");
    return false;
}

static void
parse_composite_type(Parser *p)
{
    int at = p->pos;
    TokenKind kind = p->tokens.kinds[at];
    if (kind == TK_IDENT) {
        emit(p, NODE_NAME, p->pos++);
        while (p->tokens.kinds[p->pos] == TK_PERIOD &&
               lexer_kind_has_id(p->tokens.kinds[p->pos + 1])) {
            emit(p, NODE_FIELD, p->pos + 1);
            p->pos += 2;
        }
        if (p->tokens.kinds[p->pos] == TK_NOT) {
            if (!descend(p)) {
                return;
            }
            int bang = p->pos++;
            if (expect_value_type(p, bang)) {
                parse_type(p);
                emit(p, NODE_ERROR_UNION, bang);
            }
            p->depth--;
        }
        return;
    }

    bool optional = kind == TK_QUESTION || kind == TK_OR_ELSE;
    if (kind != TK_STAR && kind != TK_LBRACK && kind != TK_FN &&
        kind != TK_EXTERN && kind != TK_NOT && !optional) {
        fail(p, "expected a type");
        return;
    }
    if (kind == TK_EXTERN && p->tokens.kinds[at + 1] != TK_FN) {
        p->pos++;
        fail(p, "expected `fn` after `extern`");
        return;
    }
    if (!descend(p)) {
        return;
    }

    if (kind == TK_FN) {
        parse_fn_type(p);
    } else if (kind == TK_EXTERN) {
        p->pos++;
        parse_fn_type(p);
        emit(p, NODE_EXTERN, at);
    } else if (kind == TK_STAR) {
        p->pos++;
        parse_type(p);
        emit(p, NODE_POINTER_TYPE, at);
    } else if (kind == TK_NOT) {
        p->pos++;
        if (expect_value_type(p, at)) {
            parse_type(p);
            emit(p, NODE_ERROR_UNION_INFERRED, at);
        }
    } else if (optional) {
        p->pos++;
        parse_type(p);
        emit(p, NODE_OPTIONAL_TYPE, at);
        if (kind == TK_OR_ELSE) {
            emit(p, NODE_OPTIONAL_TYPE, at);
        }
    } else if (p->tokens.kinds[at + 1] == TK_RBRACK) {
        p->pos += 2;
        parse_type(p);
        emit(p, NODE_SLICE_TYPE, at);
    } else if (p->tokens.kinds[at + 1] == TK_STAR &&
               p->tokens.kinds[at + 2] == TK_RBRACK) {
        p->pos += 3;
        parse_type(p);
        emit(p, NODE_MANY_TYPE, at);
    } else {
        p->pos++;
        p->brackets++;
        int n = parse_indices(p);
        p->brackets--;
        if (p->tokens.kinds[p->pos] != TK_RBRACK) {
            fail(p, "expected `]`");
            return;
        }
        p->pos++;
        parse_type(p);
        emit(p, array_nodes[n], at);
    }
    p->depth--;
}

typedef struct
{
    u8 inferred;
    u8 typed;
    u8 typed_value;
    bool allows_undefined;
} BindingNodes;

static const BindingNodes var_nodes = { NODE_VAR_INIT, NODE_VAR_TYPE, NODE_VAR_TYPE_INIT, true };
static const BindingNodes param_nodes = { NODE_PARAM_INIT,
                                          NODE_PARAM_TYPE,
                                          NODE_PARAM_TYPE_INIT,
                                          false };
static const BindingNodes const_nodes = { NODE_CONST_INIT,
                                          NODE_NONE,
                                          NODE_CONST_TYPE_INIT,
                                          false };
static const BindingNodes member_nodes = { NODE_MEMBER_INIT,
                                           NODE_MEMBER_TYPE,
                                           NODE_MEMBER_TYPE_INIT,
                                           true };

static void
parse_binding(Parser *p, BindingNodes nodes)
{
    int name = p->pos;
    bool inferred = p->tokens.kinds[name + 1] == TK_DASSIGN;
    p->pos += 2;

    if (inferred) {
        parse_binary(p, PREC_OR);
        emit(p, nodes.inferred, name);
        return;
    }

    parse_type(p);
    if (p->tokens.kinds[p->pos] == TK_EQ) {
        emit(p, NODE_VALUE, p->pos++);
        if (p->tokens.kinds[p->pos] == TK_UNDEFINED && nodes.allows_undefined) {
            emit(p, NODE_UNDEFINED, p->pos++);
        } else {
            parse_binary(p, PREC_OR);
        }
        emit(p, nodes.typed_value, name);
        return;
    }

    if (nodes.typed == NODE_NONE) {
        fail(p, "expected `=`");
        return;
    }
    emit(p, nodes.typed, name);
}

static void
parse_simple(Parser *p)
{
    int at = p->pos;
    parse_binary(p, PREC_OR);

    NodeKind assign = assign_nodes[p->tokens.kinds[p->pos]];
    if (assign == NODE_NONE) {
        emit(p, NODE_EXPR_STMT, at);
        return;
    }

    int op = p->pos++;
    emit(p, assign == NODE_OR_ELSE_ASSIGN ? NODE_OR_ELSE_TARGET : NODE_VALUE, op);
    parse_binary(p, PREC_OR);
    emit(p, assign, op);

    if (assign_nodes[p->tokens.kinds[p->pos]] != NODE_NONE) {
        fail(p, "assignments cannot be chained");
    }
}

static void
parse_return(Parser *p)
{
    int at = p->pos++;
    if (ends_statement(p)) {
        emit(p, NODE_RETURN, at);
        return;
    }

    emit(p, NODE_RETURN_START, at);
    parse_binary(p, PREC_OR);
    emit(p, NODE_RETURN_VALUE, at);
}

static void
fail_statement_end(Parser *p)
{
    TokenKind next = p->tokens.kinds[p->pos];
    const char *msg = next == TK_DCOLON
                        ? "use `const name := value` instead of `::`"
                    : next == TK_QUESTION_EQ ? "use `?\?=` instead of `?=`"
                                             : "expected `;` or a new line";
    fail(p, msg);
}

static void parse_statement(Parser *p);

static void
parse_statements(Parser *p)
{
    for (;;) {
        TokenKind kind = p->tokens.kinds[p->pos];
        if (kind == TK_SEMI) {
            p->pos++;
            continue;
        }
        if (kind == TK_RBRACE || kind == TK_EOF) {
            return;
        }

        parse_statement(p);
        if (!ends_statement(p)) {
            fail_statement_end(p);
        }
    }
}

static void
parse_block(Parser *p)
{
    if (p->tokens.kinds[p->pos] != TK_LBRACE) {
        fail(p, "expected `{`");
        return;
    }
    if (!descend(p)) {
        return;
    }

    int brackets = p->brackets;
    bool in_arm = p->in_arm;
    p->brackets = 0;
    p->in_arm = false;
    emit(p, NODE_BLOCK_START, p->pos++);
    parse_statements(p);
    p->brackets = brackets;
    p->in_arm = in_arm;
    p->depth--;

    if (p->tokens.kinds[p->pos] != TK_RBRACE) {
        fail(p, "expected `}`");
        return;
    }
    emit(p, NODE_BLOCK, p->pos++);
}

static void
parse_if(Parser *p)
{
    int at = p->pos;
    emit(p, NODE_IF_START, at);

    for (;;) {
        p->pos++;
        parse_binary(p, PREC_OR);
        emit(p, NODE_IF_THEN, p->pos);
        parse_block(p);

        if (p->tokens.kinds[p->pos] != TK_ELSE) {
            break;
        }

        int else_at = p->pos++;
        if (p->tokens.kinds[p->pos] != TK_IF) {
            emit(p, NODE_ELSE, else_at);
            parse_block(p);
            break;
        }
        emit(p, NODE_ELSE_IF, else_at);
    }

    emit(p, NODE_IF, at);
}

static void
parse_while(Parser *p)
{
    int at = p->pos;
    emit(p, NODE_WHILE_START, p->pos++);

    parse_binary(p, PREC_OR);
    emit(p, NODE_WHILE_BODY, p->pos);
    parse_block(p);

    emit(p, NODE_WHILE, at);
}

static void
parse_range(Parser *p)
{
    parse_binary(p, PREC_OR);

    TokenKind kind = p->tokens.kinds[p->pos];
    if (kind != TK_RANGE && kind != TK_RANGE_INCLUSIVE) {
        return;
    }

    int at = p->pos++;
    parse_binary(p, PREC_OR);
    emit(p, kind == TK_RANGE ? NODE_RANGE : NODE_RANGE_INCLUSIVE, at);
}

static void
parse_for(Parser *p)
{
    emit(p, NODE_FOR_START, p->pos++);

    int value = p->pos;
    if (p->tokens.kinds[value] != TK_IDENT) {
        fail(p, "expected a name");
        return;
    }
    p->pos++;

    int index = -1;
    if (p->tokens.kinds[p->pos] == TK_COMMA) {
        index = ++p->pos;
        if (p->tokens.kinds[index] != TK_IDENT) {
            fail(p, "expected a name");
            return;
        }
        p->pos++;
    }

    int in = p->pos;
    expect(p, TK_IN, "expected `in`");
    parse_range(p);

    emit(p, NODE_FOR_VALUE, value);
    if (index >= 0) {
        emit(p, NODE_FOR_INDEX, index);
    }
    parse_block(p);

    emit(p, NODE_FOR, in);
}

static bool
next_member(Parser *p)
{
    TokenKind kind = p->tokens.kinds[p->pos];
    if (kind == TK_COMMA || kind == TK_SEMI) {
        p->pos++;
        return true;
    }
    if (kind == TK_RBRACE || kind == TK_EOF || starts_line(p)) {
        return true;
    }

    fail(p, "expected `,`, `;` or a new line");
    return false;
}

static bool
leaves_arm(TokenKind kind)
{
    return kind == TK_LBRACE || kind == TK_RETURN || kind == TK_BREAK ||
           kind == TK_CONTINUE;
}

static void
parse_switch(Parser *p, bool value)
{
    if (!descend(p)) {
        return;
    }

    int at = p->pos;
    emit(p, value ? NODE_VALUE_SWITCH_START : NODE_SWITCH_START, p->pos++);

    bool by_pointer = p->tokens.kinds[p->pos] == TK_AMP;
    int name = p->pos + by_pointer;
    if (p->tokens.kinds[name] == TK_IDENT && p->tokens.kinds[name + 1] == TK_IN) {
        p->pos = name + 2;
        parse_binary(p, PREC_OR);
        emit(p, by_pointer ? NODE_SWITCH_REF : NODE_SWITCH_VALUE, name);
    } else {
        parse_binary(p, PREC_OR);
    }
    expect(p, TK_LBRACE, "expected `{`");

    int brackets = p->brackets;
    p->brackets = 0;
    while (p->tokens.kinds[p->pos] != TK_RBRACE && p->tokens.kinds[p->pos] != TK_EOF) {
        emit(p, NODE_CASE_START, p->pos);
        for (;;) {
            parse_range(p);
            if (p->tokens.kinds[p->pos] != TK_COMMA) {
                break;
            }
            p->pos++;
        }

        int arrow = p->pos;
        if (p->tokens.kinds[arrow] != TK_FAT_ARROW) {
            fail(p, "expected `,` or `=>`");
            return;
        }
        emit(p, NODE_CASE_BODY, p->pos++);
        bool in_arm = p->in_arm;
        p->in_arm = true;
        if (value && !leaves_arm(p->tokens.kinds[p->pos])) {
            parse_binary(p, PREC_OR);
        } else {
            parse_statement(p);
        }
        p->in_arm = in_arm;
        emit(p, NODE_CASE, arrow);

        if (!next_member(p)) {
            return;
        }
    }
    p->brackets = brackets;
    p->depth--;

    if (p->tokens.kinds[p->pos] != TK_RBRACE) {
        fail(p, "expected `}`");
        return;
    }
    p->pos++;
    emit(p, value ? NODE_VALUE_SWITCH : NODE_SWITCH, at);
}

static void
parse_defer(Parser *p)
{
    int at = p->pos;
    if (!descend(p)) {
        return;
    }

    emit(p, NODE_DEFER_START, p->pos++);
    NodeKind node = NODE_DEFER;
    if (p->tokens.kinds[p->pos] == TK_CATCH) {
        p->pos++;
        node = NODE_DEFER_CATCH;
        if (p->tokens.kinds[p->pos] == TK_IDENT &&
            p->tokens.kinds[p->pos + 1] == TK_FAT_ARROW) {
            emit(p, NODE_CATCH_NAME, p->pos);
            p->pos += 2;
            node = NODE_DEFER_CATCH_NAMED;
        }
    }
    parse_statement(p);
    emit(p, node, at);
    p->depth--;
}

static void
parse_params(Parser *p, bool variadic)
{
    while (p->tokens.kinds[p->pos] != TK_RPAREN) {
        if (p->tokens.kinds[p->pos] == TK_RANGE) {
            if (!variadic) {
                fail(p, "only an `extern fn` can end with `..`");
                return;
            }
            if (p->tokens.kinds[p->pos + 1] != TK_RPAREN) {
                fail(p, "`..` must be the last parameter");
                return;
            }
            emit(p, NODE_VARIADIC, p->pos++);
            return;
        }
        if (p->tokens.kinds[p->pos] != TK_IDENT) {
            fail(p, "expected a parameter name");
            return;
        }

        TokenKind next = p->tokens.kinds[p->pos + 1];
        if (next == TK_COLON && p->tokens.kinds[p->pos + 2] == TK_RANGE) {
            int name = p->pos;
            int dots = p->pos + 2;
            p->pos += 3;
            parse_type(p);
            emit(p, NODE_REST_TYPE, dots);
            emit(p, NODE_PARAM_TYPE, name);
            if (p->tokens.kinds[p->pos] != TK_RPAREN) {
                fail(p, "`..` must be the last parameter");
            }
            return;
        }
        if (next == TK_DASSIGN || next == TK_COLON) {
            parse_binding(p, param_nodes);
        } else {
            emit(p, NODE_PARAM, p->pos++);
        }

        if (p->tokens.kinds[p->pos] != TK_COMMA) {
            return;
        }
        p->pos++;
    }
}

static int
parse_signature(Parser *p, bool named, bool variadic)
{
    emit(p, NODE_FN_START, p->pos++);
    if (named) {
        p->pos++;
    }

    if (p->tokens.kinds[p->pos] != TK_LPAREN) {
        fail(p, "expected `(`");
        return -1;
    }
    p->pos++;
    p->brackets++;
    parse_params(p, variadic);
    p->brackets--;
    if (p->tokens.kinds[p->pos] != TK_RPAREN) {
        fail(p, "expected `,` or `)`");
        return -1;
    }
    int close = p->pos++;

    if (p->tokens.kinds[p->pos] == TK_ARROW) {
        int arrow = p->pos++;
        parse_type(p);
        emit(p, NODE_FN_RETURN, arrow);
    }
    return close;
}

static void
parse_function(Parser *p, bool named)
{
    if (!descend(p)) {
        return;
    }

    int name = p->pos + 1;
    int close = parse_signature(p, named, false);
    if (close < 0) {
        return;
    }

    int body = p->pos;
    TokenKind kind = p->tokens.kinds[body];
    if (kind != TK_LBRACE && kind != TK_FAT_ARROW) {
        fail(p, "expected `{` or `=>`");
        return;
    }

    emit(p, NODE_FN_BODY, body);
    if (kind == TK_LBRACE) {
        parse_block(p);
    } else {
        p->pos++;
        emit(p, NODE_RETURN_START, close);
        parse_binary(p, PREC_OR);
        emit(p, NODE_RETURN_VALUE, body);
    }

    emit(p, named ? NODE_FN : NODE_FN_LITERAL, name);
    p->depth--;
}

static void
parse_const(Parser *p)
{
    p->pos++;
    if (p->tokens.kinds[p->pos] != TK_IDENT) {
        fail(p, "expected a name");
        return;
    }

    TokenKind next = p->tokens.kinds[p->pos + 1];
    if (next != TK_DASSIGN && next != TK_COLON) {
        p->pos++;
        fail(p, "expected `:=` or `:`");
        return;
    }
    parse_binding(p, const_nodes);
}

static int
parse_decl_name(Parser *p)
{
    int name = p->pos;
    if (p->tokens.kinds[name] != TK_IDENT) {
        fail(p, "expected a name");
        return -1;
    }
    p->pos++;
    return name;
}

static void
close_members(Parser *p, NodeKind node, int name)
{
    if (p->tokens.kinds[p->pos] != TK_RBRACE) {
        fail(p, "expected `}`");
        return;
    }
    emit(p, node, name);
    p->pos++;
}

static void
parse_union_members(Parser *p, bool tagged)
{
    int brackets = p->brackets;
    p->brackets = 0;
    while (p->tokens.kinds[p->pos] != TK_RBRACE && p->tokens.kinds[p->pos] != TK_EOF) {
        int member = p->pos;
        if (!lexer_kind_has_id(p->tokens.kinds[member])) {
            fail(p, "expected a name");
            return;
        }

        if (p->tokens.kinds[member + 1] == TK_COLON) {
            p->pos += 2;
            parse_type(p);
            emit(p, NODE_MEMBER_TYPE, member);
        } else if (tagged) {
            emit(p, NODE_EMPTY_MEMBER, p->pos++);
        } else {
            p->pos++;
            fail(p, "expected `:`");
            return;
        }

        if (!next_member(p)) {
            return;
        }
    }
    p->brackets = brackets;
}

static void
parse_anon_union(Parser *p)
{
    if (!descend(p)) {
        return;
    }

    int at = p->pos;
    emit(p, NODE_UNION_START, at);
    p->pos += 2;
    parse_union_members(p, false);
    close_members(p, NODE_ANON_UNION, at);
    p->depth--;
}

static void
parse_struct(Parser *p)
{
    if (!descend(p)) {
        return;
    }

    emit(p, NODE_STRUCT_START, p->pos++);
    int name = parse_decl_name(p);
    if (name < 0) {
        return;
    }
    expect(p, TK_LBRACE, "expected `{`");

    int brackets = p->brackets;
    p->brackets = 0;
    while (p->tokens.kinds[p->pos] != TK_RBRACE && p->tokens.kinds[p->pos] != TK_EOF) {
        TokenKind kind = p->tokens.kinds[p->pos];
        TokenKind next = p->tokens.kinds[p->pos + 1];
        if (kind == TK_UNION && next == TK_LBRACE) {
            parse_anon_union(p);
        } else if (!lexer_kind_has_id(kind)) {
            fail(p, "expected a field name");
            return;
        } else if (next != TK_COLON && next != TK_DASSIGN) {
            p->pos++;
            fail(p, "expected `:` or `:=`");
            return;
        } else {
            parse_binding(p, member_nodes);
        }

        if (!next_member(p)) {
            return;
        }
    }
    p->brackets = brackets;

    close_members(p, NODE_STRUCT, name);
    p->depth--;
}

static void
parse_enum(Parser *p)
{
    if (!descend(p)) {
        return;
    }

    emit(p, NODE_ENUM_START, p->pos++);
    int name = parse_decl_name(p);
    if (name < 0) {
        return;
    }

    if (p->tokens.kinds[p->pos] == TK_COLON) {
        int colon = p->pos++;
        parse_type(p);
        emit(p, NODE_ENUM_BACKING, colon);
    }
    expect(p, TK_LBRACE, "expected `{`");

    int brackets = p->brackets;
    p->brackets = 0;
    while (p->tokens.kinds[p->pos] != TK_RBRACE && p->tokens.kinds[p->pos] != TK_EOF) {
        int member = p->pos;
        if (!lexer_kind_has_id(p->tokens.kinds[member])) {
            fail(p, "expected a name");
            return;
        }

        if (p->tokens.kinds[member + 1] == TK_EQ) {
            p->pos += 2;
            parse_binary(p, PREC_OR);
            emit(p, NODE_ENUM_VALUE, member);
        } else {
            emit(p, NODE_ENUM_MEMBER, p->pos++);
        }

        if (!next_member(p)) {
            return;
        }
    }
    p->brackets = brackets;

    close_members(p, NODE_ENUM, name);
    p->depth--;
}

static NOINLINE void
parse_union(Parser *p)
{
    if (!descend(p)) {
        return;
    }

    emit(p, NODE_UNION_START, p->pos++);
    int name = parse_decl_name(p);
    if (name < 0) {
        return;
    }

    bool tagged = p->tokens.kinds[p->pos] == TK_COLON;
    if (tagged) {
        int colon = p->pos++;
        parse_type(p);
        emit(p, NODE_UNION_TAG, colon);
    }
    expect(p, TK_LBRACE, "expected `{`");

    parse_union_members(p, tagged);
    close_members(p, NODE_UNION, name);
    p->depth--;
}

static_assert(NODE_BREAK_VALUE == NODE_BREAK + 2, "break nodes must be consecutive");
static_assert(NODE_CONTINUE_VALUE == NODE_CONTINUE + 2, "continue nodes must be consecutive");

static NOINLINE void
parse_jump(Parser *p, NodeKind bare)
{
    int at = p->pos++;
    if (ends_statement(p)) {
        emit(p, bare, at);
        return;
    }

    int label = p->pos;
    if (p->tokens.kinds[label] != TK_IDENT) {
        fail(p, "expected a label");
        return;
    }
    p->pos++;
    if (ends_statement(p)) {
        emit(p, bare + 1, label);
        return;
    }

    parse_binary(p, PREC_OR);
    emit(p, bare + 2, label);
}

static NOINLINE void
parse_package(Parser *p)
{
    if (p->count != 0 || p->depth != 0) {
        fail(p, "`package` must be the first line");
        return;
    }

    p->pos++;
    if (p->tokens.kinds[p->pos] != TK_IDENT) {
        fail(p, "expected a package name");
        return;
    }
    emit(p, NODE_PACKAGE, p->pos++);
}

static NOINLINE void
parse_import(Parser *p)
{
    p->pos++;
    if (p->tokens.kinds[p->pos] != TK_STRING) {
        fail(p, "expected a path in quotes");
        return;
    }
    emit(p, NODE_IMPORT_DECL, p->pos++);
}

static NOINLINE void
parse_export(Parser *p)
{
    int at = p->pos;
    if (p->tokens.kinds[at + 1] != TK_FN || p->tokens.kinds[at + 2] != TK_IDENT) {
        fail(p, "`export` must come before `fn`");
        return;
    }

    p->pos++;
    parse_function(p, true);
    emit(p, NODE_EXPORT, at);
}

static NOINLINE void
parse_extern(Parser *p)
{
    int at = p->pos;
    if (p->tokens.kinds[at + 1] == TK_IDENT && p->tokens.kinds[at + 2] == TK_COLON) {
        p->pos += 3;
        parse_type(p);
        if (p->tokens.kinds[p->pos] == TK_EQ) {
            fail(p, "an `extern` global has no starting value");
            return;
        }
        emit(p, NODE_VAR_TYPE, at + 1);
        emit(p, NODE_EXTERN, at);
        return;
    }
    if (p->tokens.kinds[at + 1] != TK_FN || p->tokens.kinds[at + 2] != TK_IDENT) {
        fail(p, "`extern` must come before `fn` or `name: type`");
        return;
    }

    p->pos++;
    if (parse_signature(p, true, true) < 0) {
        return;
    }
    TokenKind next = p->tokens.kinds[p->pos];
    if (next == TK_LBRACE || next == TK_FAT_ARROW) {
        fail(p, "an `extern fn` has no body");
        return;
    }
    emit(p, NODE_FN, at + 2);
    emit(p, NODE_EXTERN, at);
}

static NOINLINE void
parse_pub(Parser *p)
{
    int at = p->pos;
    TokenKind kind = p->tokens.kinds[at + 1];
    TokenKind after = p->tokens.kinds[at + 2];
    bool declaration = kind == TK_STRUCT || kind == TK_ENUM || kind == TK_UNION ||
                       kind == TK_CONST || kind == TK_EXPORT ||
                       kind == TK_EXTERN || (kind == TK_FN && after == TK_IDENT) ||
                       (kind == TK_IDENT && (after == TK_DASSIGN || after == TK_COLON));
    if (!declaration) {
        fail(p, "`pub` must come before a declaration");
        return;
    }

    p->pos++;
    parse_statement(p);
    emit(p, NODE_PUB, at);
}

static bool
starts_labeled(TokenKind kind)
{
    return kind == TK_FOR || kind == TK_WHILE || kind == TK_SWITCH || kind == TK_LBRACE;
}

static NOINLINE void
parse_labeled(Parser *p)
{
    int name = p->pos;
    emit(p, NODE_LABEL_START, name);
    p->pos += 2;
    parse_statement(p);
    emit(p, NODE_LABELED, name);
}

static void
parse_statement(Parser *p)
{
    switch (p->tokens.kinds[p->pos]) {
        case TK_LBRACE: parse_block(p); return;
        case TK_IF: parse_if(p); return;
        case TK_WHILE: parse_while(p); return;
        case TK_FOR: parse_for(p); return;
        case TK_SWITCH: parse_switch(p, false); return;
        case TK_RETURN: parse_return(p); return;
        case TK_BREAK: parse_jump(p, NODE_BREAK); return;
        case TK_CONTINUE: parse_jump(p, NODE_CONTINUE); return;
        case TK_DEFER: parse_defer(p); return;
        case TK_CONST: parse_const(p); return;
        case TK_STRUCT: parse_struct(p); return;
        case TK_ENUM: parse_enum(p); return;
        case TK_UNION: parse_union(p); return;
        case TK_PACKAGE: parse_package(p); return;
        case TK_IMPORT: parse_import(p); return;
        case TK_PUB: parse_pub(p); return;
        case TK_EXPORT: parse_export(p); return;
        case TK_EXTERN: parse_extern(p); return;
        case TK_FN:
            if (p->tokens.kinds[p->pos + 1] == TK_IDENT) {
                parse_function(p, true);
                return;
            }
            break;
        case TK_IDENT: {
            TokenKind next = p->tokens.kinds[p->pos + 1];
            if (next == TK_COLON && starts_labeled(p->tokens.kinds[p->pos + 2])) {
                parse_labeled(p);
                return;
            }
            if (next == TK_DASSIGN || next == TK_COLON) {
                parse_binding(p, var_nodes);
                return;
            }
            break;
        }
        default: break;
    }

    parse_simple(p);
}

int
parser_parse(Parser *p, Tokens tokens, int ntokens, Nodes out)
{
    assert(ntokens > 0 && tokens.kinds[ntokens - 1] == TK_EOF);
    *p = (Parser){ .tokens = tokens, .end = ntokens - 1, .out = out };

    int top_count = 0;
    for (;;) {
        TokenKind kind = p->tokens.kinds[p->pos];
        if (kind == TK_SEMI) {
            p->pos++;
            continue;
        }
        if (kind == TK_RBRACE || kind == TK_EOF) {
            break;
        }

        parse_statement(p);
        if (!ends_statement(p)) {
            fail_statement_end(p);
        }
        out.top_ends[top_count++] = (u32)p->count;
    }
    p->top_count = top_count;

    if (p->tokens.kinds[p->pos] == TK_RBRACE) {
        fail(p, "unmatched `}`");
    }

    return p->error == NULL ? p->count : -1;
}

void
parser_print_error(FILE *out, const Parser *p, const char *path, const char *src)
{
    print_error_at(out, path, src, p->error_pos, p->error);
}

Nodes
parser_alloc_nodes(Arena *arena, int ntokens)
{
    size_t n = 2 * (size_t)ntokens;
    return (Nodes){
        .kinds = arena_alloc(arena, n, 1),
        .tokens = arena_alloc(arena, n * sizeof(u32), alignof(u32)),
        .top_ends = arena_alloc(arena, (size_t)ntokens * sizeof(u32), alignof(u32)),
    };
}

#define LIST_START(name, start, before, label) [name] = start,
static const u8 list_starts[NNODES] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, LIST_START, CHIBI_SKIP)
};

#define IS_MARKER(name) [name] = true,
static const bool is_marker[NNODES] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, IS_MARKER)
};

static const bool shows_name[NNODES] = {
    [NODE_VAR_INIT] = true,
    [NODE_VAR_TYPE] = true,
    [NODE_VAR_TYPE_INIT] = true,
    [NODE_PARAM_INIT] = true,
    [NODE_PARAM_TYPE] = true,
    [NODE_PARAM_TYPE_INIT] = true,
    [NODE_CONST_INIT] = true,
    [NODE_CONST_TYPE_INIT] = true,
    [NODE_FN] = true,
    [NODE_ENUM_LITERAL] = true,
    [NODE_IMPORT] = true,
    [NODE_IMPORT_DECL] = true,
    [NODE_PACKAGE] = true,
    [NODE_MEMBER_INIT] = true,
    [NODE_MEMBER_TYPE] = true,
    [NODE_MEMBER_TYPE_INIT] = true,
    [NODE_ENUM_VALUE] = true,
    [NODE_STRUCT] = true,
    [NODE_ENUM] = true,
    [NODE_UNION] = true,
    [NODE_SWITCH_REF] = true,
    [NODE_BREAK_LABEL] = true,
    [NODE_BREAK_VALUE] = true,
    [NODE_CONTINUE_LABEL] = true,
    [NODE_CONTINUE_VALUE] = true,
    [NODE_LABELED] = true,
    [NODE_LABELED_VALUE] = true,
    [NODE_LABELED_ITEM] = true,
};

#define NODE_LABEL(name, arity, label) [name] = label,
#define LIST_LABEL(name, start, before, label) [name] = label,
static const char *const node_labels[NNODES] = {
    CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, NODE_LABEL, LIST_LABEL, CHIBI_SKIP)
};

static void
print_token(FILE *out, const char *src, Tokens tokens, u32 token)
{
    TokenKind kind = tokens.kinds[token];
    u32 start = tokens.starts[token];
    fprintf(out, "%.*s", (int)lexer_token_length(src, kind, start), src + start);
}

void
parser_dump(FILE *out, Arena *scratch, const char *src, Tokens tokens, Nodes nodes, int n)
{
    ArenaMark mark = arena_mark(scratch);
    int *first = arena_alloc(scratch, (size_t)n * sizeof *first, alignof(int));
    int *roots = arena_alloc(scratch, (size_t)n * sizeof *roots, alignof(int));
    int *todo = arena_alloc(scratch, (2 * (size_t)n + 1) * sizeof *todo, alignof(int));

    int nroots = 0;
    for (int i = 0; i < n; i++) {
        NodeKind kind = nodes.kinds[i];
        NodeKind start = list_starts[kind];
        if (start != NODE_NONE) {
            while (nodes.kinds[roots[nroots - 1]] != start) {
                nroots--;
            }
            nroots--;
        }
        nroots -= node_arity[kind];
        assert(nroots >= 0);

        bool leaf = node_arity[kind] == 0 && start == NODE_NONE;
        first[i] = leaf ? i : first[roots[nroots]];
        roots[nroots++] = i;
    }

    for (int r = 0; r < nroots; r++) {
        int ntodo = 0;
        todo[ntodo++] = roots[r];
        bool root = true;
        while (ntodo > 0) {
            int item = todo[--ntodo];
            if (item < 0) {
                int closed = -1 - item;
                if (nodes.kinds[closed] == NODE_FIELD) {
                    fputc(' ', out);
                    print_token(out, src, tokens, nodes.tokens[closed]);
                }
                fputc(')', out);
                continue;
            }

            int i = item;
            NodeKind kind = nodes.kinds[i];
            if (kind == NODE_EXPR_STMT) {
                todo[ntodo++] = i - 1;
                continue;
            }

            if (!root) {
                fputc(' ', out);
            }
            root = false;

            int arity = node_arity[kind];
            NodeKind start = list_starts[kind];
            if (arity == 0 && start == NODE_NONE && node_labels[kind] == NULL) {
                print_token(out, src, tokens, nodes.tokens[i]);
                continue;
            }

            fputc('(', out);
            if (node_labels[kind] != NULL) {
                fputs(node_labels[kind], out);
            } else {
                print_token(out, src, tokens, nodes.tokens[i]);
            }
            if (shows_name[kind]) {
                fputc(' ', out);
                print_token(out, src, tokens, nodes.tokens[i]);
            }
            todo[ntodo++] = -1 - i;

            int child = i - 1;
            if (start != NODE_NONE) {
                while (nodes.kinds[child] != start) {
                    if (!is_marker[nodes.kinds[child]]) {
                        todo[ntodo++] = child;
                    }
                    child = first[child] - 1;
                }
                child--;
            }
            for (int k = 0; k < arity; k++) {
                if (!is_marker[nodes.kinds[child]]) {
                    todo[ntodo++] = child;
                }
                child = first[child] - 1;
            }
        }
        fputc('\n', out);
    }

    arena_restore(scratch, mark);
}
