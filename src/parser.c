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
static const u8 prefix_nodes[NKINDS] = { CHIBI_NODES(PREFIX_NODE, PREFIX_NODE, CHIBI_SKIP) };

typedef struct
{
    u8 prec;
    u8 node;
} BinaryOp;

#define BINARY_OP(name, token, prec) [token] = { prec, name },
static const BinaryOp binary_ops[NKINDS] = { CHIBI_NODES(CHIBI_SKIP, CHIBI_SKIP, BINARY_OP) };

#define ARITY_0(name, ...) [name] = 0,
#define ARITY_1(name, ...) [name] = 1,
#define ARITY_2(name, ...) [name] = 2,
static const u8 node_arity[NNODES] = { CHIBI_NODES(ARITY_0, ARITY_1, ARITY_2) };

static void
fail(Parser *p, const char *msg)
{
    if (p->error == NULL) {
        p->error = msg;
        p->error_pos = p->tokens.starts[p->pos];
    }
    p->pos = p->end;
}

static void
emit(Parser *p, NodeKind kind, int token)
{
    p->out.kinds[p->count] = (u8)kind;
    p->out.tokens[p->count] = (u32)token;
    p->count++;
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

static void parse_binary(Parser *p, int min_prec);

static void
parse_operand(Parser *p)
{
    int at = p->pos;
    TokenKind kind = p->tokens.kinds[at];
    NodeKind node = prefix_nodes[kind];

    if (node != NODE_NONE && node_arity[node] == 0) {
        p->pos++;
        emit(p, node, at);
        return;
    }

    if (node == NODE_NONE && kind != TK_LPAREN) {
        fail(p, "expected an expression");
        return;
    }

    if (p->depth == PARSER_MAX_DEPTH) {
        fail(p, "expression nested too deeply");
        return;
    }

    p->depth++;
    p->pos++;
    if (kind == TK_LPAREN) {
        parse_binary(p, PREC_OR);
        expect(p, TK_RPAREN, "expected `)`");
    } else {
        parse_operand(p);
        emit(p, node, at);
    }
    p->depth--;
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
        parse_binary(p, op.prec + 1);
        emit(p, op.node, at);
    }
}

int
parser_parse(Parser *p, Tokens tokens, int ntokens, Nodes out)
{
    assert(ntokens > 0 && tokens.kinds[ntokens - 1] == TK_EOF);
    *p = (Parser){ .tokens = tokens, .end = ntokens - 1, .out = out };

    parse_binary(p, PREC_OR);
    expect(p, TK_EOF, "expected an operator or the end of the input");

    return p->error == NULL ? p->count : 0;
}

void
parser_print_error(const Parser *p, const char *src)
{
    print_error_at(src, p->error_pos, p->error);
}

Nodes
parser_alloc_nodes(Arena *arena, int ntokens)
{
    return (Nodes){
        .kinds = arena_alloc(arena, (size_t)ntokens, 1),
        .tokens = arena_alloc(arena, (size_t)ntokens * sizeof(u32), alignof(u32)),
    };
}

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
        int arity = node_arity[nodes.kinds[i]];
        nroots -= arity;
        assert(nroots >= 0);
        first[i] = arity == 0 ? i : first[roots[nroots]];
        roots[nroots++] = i;
    }

    enum
    {
        CLOSE = -1
    };

    for (int r = 0; r < nroots; r++) {
        int ntodo = 0;
        todo[ntodo++] = roots[r];
        bool root = true;
        while (ntodo > 0) {
            int i = todo[--ntodo];
            if (i == CLOSE) {
                fputc(')', out);
                continue;
            }
            if (!root) {
                fputc(' ', out);
            }
            root = false;

            int arity = node_arity[nodes.kinds[i]];
            if (arity == 0) {
                print_token(out, src, tokens, nodes.tokens[i]);
                continue;
            }

            fputc('(', out);
            print_token(out, src, tokens, nodes.tokens[i]);
            todo[ntodo++] = CLOSE;
            todo[ntodo++] = i - 1;
            if (arity == 2) {
                todo[ntodo++] = first[i - 1] - 1;
            }
        }
        fputc('\n', out);
    }

    arena_restore(scratch, mark);
}
