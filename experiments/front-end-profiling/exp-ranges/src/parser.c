#include "parser.h"

#include <assert.h>

enum
{
    PREC_NONE,
    PREC_OR,
    PREC_AND,
    PREC_COMPARE,
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

static void
parse_args(Parser *p)
{
    while (p->tokens.kinds[p->pos] != TK_RPAREN) {
        int at = p->pos;
        const u8 *kinds = p->tokens.kinds;
        if (kinds[at] == TK_PERIOD && kinds[at + 1] == TK_IDENT &&
            kinds[at + 2] == TK_EQ) {
            emit(p, NODE_ARG_NAME, at + 1);
            emit(p, NODE_VALUE, at + 2);
            p->pos += 3;
            parse_binary(p, PREC_OR);
            emit(p, NODE_NAMED_ARG, at);
        } else {
            parse_binary(p, PREC_OR);
        }

        if (p->tokens.kinds[p->pos] != TK_COMMA) {
            return;
        }
        p->pos++;
    }
}

static void
parse_postfix(Parser *p)
{
    for (;;) {
        int at = p->pos;
        switch (p->tokens.kinds[at]) {
            case TK_PERIOD:
                p->pos++;
                if (!lexer_kind_has_id(p->tokens.kinds[p->pos])) {
                    fail(p, "expected a field name after `.`");
                    return;
                }
                emit(p, NODE_FIELD, p->pos++);
                break;

            case TK_DEREF: emit(p, NODE_DEREF, p->pos++); break;

            case TK_LBRACK:
                if (p->brackets == 0 && starts_line(p)) {
                    return;
                }
                if (!descend(p)) {
                    return;
                }
                p->pos++;
                p->brackets++;
                parse_binary(p, PREC_OR);
                expect(p, TK_RBRACK, "expected `]`");
                emit(p, NODE_INDEX, at);
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
                parse_args(p);
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

    if (kind == TK_FN) {
        parse_function(p, false);
        return;
    }

    if (node == NODE_NONE && kind != TK_LPAREN) {
        fail(p, "expected an expression");
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

static void
parse_binary(Parser *p, int min_prec)
{
    parse_operand(p);

    int last = PREC_NONE;
    for (;;) {
        BinaryOp op = binary_ops[p->tokens.kinds[p->pos]];

        if (op.prec < min_prec) {
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
        }
        parse_binary(p, op.prec + 1);
        emit(p, op.node, at);
    }
}

static bool
ends_statement(Parser *p)
{
    TokenKind kind = p->tokens.kinds[p->pos];
    return kind == TK_SEMI || kind == TK_RBRACE || starts_line(p);
}

static void
parse_type(Parser *p)
{
    if (p->tokens.kinds[p->pos] != TK_IDENT) {
        fail(p, "expected a type");
        return;
    }
    emit(p, NODE_NAME, p->pos++);
}

typedef struct
{
    u8 inferred;
    u8 typed;
    u8 typed_value;
} BindingNodes;

static const BindingNodes var_nodes = { NODE_VAR_INIT, NODE_VAR_TYPE, NODE_VAR_TYPE_INIT };
static const BindingNodes param_nodes = { NODE_PARAM_INIT,
                                          NODE_PARAM_TYPE,
                                          NODE_PARAM_TYPE_INIT };
static const BindingNodes const_nodes = { NODE_CONST_INIT, NODE_NONE, NODE_CONST_TYPE_INIT };

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
        parse_binary(p, PREC_OR);
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
    emit(p, NODE_VALUE, op);
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

    parse_binary(p, PREC_OR);
    emit(p, NODE_RETURN_VALUE, at);
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
            fail(p, "expected `;` or a new line");
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
    p->brackets = 0;
    emit(p, NODE_BLOCK_START, p->pos++);
    parse_statements(p);
    p->brackets = brackets;
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
parse_defer(Parser *p)
{
    int at = p->pos;
    if (!descend(p)) {
        return;
    }

    emit(p, NODE_DEFER_START, p->pos++);
    parse_statement(p);
    emit(p, NODE_DEFER, at);
    p->depth--;
}

static void
parse_params(Parser *p)
{
    while (p->tokens.kinds[p->pos] != TK_RPAREN) {
        if (p->tokens.kinds[p->pos] != TK_IDENT) {
            fail(p, "expected a parameter name");
            return;
        }

        TokenKind next = p->tokens.kinds[p->pos + 1];
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

static void
parse_function(Parser *p, bool named)
{
    if (!descend(p)) {
        return;
    }

    emit(p, NODE_FN_START, p->pos++);
    int name = p->pos;
    if (named) {
        p->pos++;
    }

    if (p->tokens.kinds[p->pos] != TK_LPAREN) {
        fail(p, "expected `(`");
        return;
    }
    p->pos++;
    p->brackets++;
    parse_params(p);
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

static void
parse_statement(Parser *p)
{
    switch (p->tokens.kinds[p->pos]) {
        case TK_LBRACE: parse_block(p); return;
        case TK_IF: parse_if(p); return;
        case TK_WHILE: parse_while(p); return;
        case TK_RETURN: parse_return(p); return;
        case TK_BREAK: emit(p, NODE_BREAK, p->pos++); return;
        case TK_CONTINUE: emit(p, NODE_CONTINUE, p->pos++); return;
        case TK_DEFER: parse_defer(p); return;
        case TK_CONST: parse_const(p); return;
        case TK_FN:
            if (p->tokens.kinds[p->pos + 1] == TK_IDENT) {
                parse_function(p, true);
                return;
            }
            break;
        case TK_IDENT: {
            TokenKind next = p->tokens.kinds[p->pos + 1];
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

    parse_statements(p);
    if (p->tokens.kinds[p->pos] == TK_RBRACE) {
        fail(p, "unmatched `}`");
    }

    return p->error == NULL ? p->count : -1;
}

void
parser_print_error(const Parser *p, const char *src)
{
    print_error_at(src, p->error_pos, p->error);
}

Nodes
parser_alloc_nodes(Arena *arena, int ntokens)
{
    size_t n = 2 * (size_t)ntokens;
    return (Nodes){
        .kinds = arena_alloc(arena, n, 1),
        .tokens = arena_alloc(arena, n * sizeof(u32), alignof(u32)),
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
            if (arity == 0 && start == NODE_NONE) {
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
