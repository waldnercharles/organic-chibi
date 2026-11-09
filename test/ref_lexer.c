//  Simple, not fast.
#include "ref_lexer.h"

#include <stdlib.h>
#include <string.h>

static bool
is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool
is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool
is_ident(char c)
{
    return is_ident_start(c) || is_digit(c);
}

static bool
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool
is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static const struct
{
    const char *text;
    TokenKind kind;
} KEYWORDS[] = {
    { "if", TK_IF },           { "else", TK_ELSE },
    { "for", TK_FOR },         { "while", TK_WHILE },
    { "switch", TK_SWITCH },   { "case", TK_CASE },
    { "break", TK_BREAK },     { "continue", TK_CONTINUE },
    { "return", TK_RETURN },   { "defer", TK_DEFER },
    { "yield", TK_YIELD },     { "goto", TK_GOTO },
    { "default", TK_DEFAULT }, { "type", TK_TYPE },
    { "struct", TK_STRUCT },   { "union", TK_UNION },
    { "enum", TK_ENUM },       { "fn", TK_FN },
    { "package", TK_PACKAGE }, { "import", TK_IMPORT },
    { "pub", TK_PUB },         { "true", TK_TRUE },
    { "false", TK_FALSE },     { "nil", TK_NIL },
    { "is", TK_IS },           { "in", TK_IN },
    { "as", TK_AS },           { "sizeof", TK_SIZEOF },
    { "alignof", TK_ALIGNOF }, { "offsetof", TK_OFFSETOF },
    { "typeof", TK_TYPEOF },   { "cast", TK_CAST },
};

// ref_next takes the first match, so two-char operators must come before one-char ones.
static const struct
{
    const char *text;
    TokenKind kind;
} OPS[] = {
    { "<<", TK_SHL },      { ">>", TK_SHR },       { "&&", TK_ANDAND },
    { "||", TK_OROR },     { "+=", TK_ADDEQ },     { "-=", TK_SUBEQ },
    { "*=", TK_MULEQ },    { "/=", TK_DIVEQ },     { "%=", TK_MODEQ },
    { "&=", TK_ANDEQ },    { "|=", TK_OREQ },      { "^=", TK_XOREQ },
    { "->", TK_ARROW },    { "=>", TK_FAT_ARROW }, { "==", TK_EQEQ },
    { "!=", TK_NEQ },      { "<=", TK_LTE },       { ">=", TK_GTE },
    { "..", TK_ELLIPSIS }, { "::", TK_DCOLON },    { ":=", TK_DASSIGN },
    { "+", TK_PLUS },      { "-", TK_MINUS },      { "*", TK_STAR },
    { "/", TK_SLASH },     { "%", TK_PERCENT },    { "=", TK_EQ },
    { "!", TK_NOT },       { "@", TK_AT },         { "$", TK_DOLLAR },
    { "?", TK_QUESTION },  { "&", TK_AND },        { "|", TK_OR },
    { "^", TK_XOR },       { "~", TK_COMPL },      { "<", TK_LT },
    { ">", TK_GT },        { ":", TK_COLON },      { ";", TK_SEMI },
    { ".", TK_PERIOD },    { ",", TK_COMMA },      { "{", TK_LBRACE },
    { "}", TK_RBRACE },    { "(", TK_LPAREN },     { ")", TK_RPAREN },
    { "[", TK_LBRACK },    { "]", TK_RBRACK },
};

static u32
fnv1a(const char *s, int len)
{
    u32 h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h = (h ^ (u8)s[i]) * 16777619u;
    }
    return h;
}

static u32
intern(RefLexer *rl, const char *s, int len)
{
    RefName **head = &rl->bucket[fnv1a(s, len) % REF_BUCKETS];
    for (RefName *n = *head; n != NULL; n = n->next) {
        if (n->len == len && memcmp(n->text, s, (size_t)len) == 0) {
            return n->id;
        }
    }
    RefName *n = malloc(sizeof *n);
    *n = (RefName){ s, len, rl->next_id++, *head };
    *head = n;
    return n->id;
}

RefLexer *
ref_lexer_new(void)
{
    RefLexer *rl = calloc(1, sizeof *rl);
    rl->next_id = 1;
    for (size_t k = 0; k < sizeof KEYWORDS / sizeof KEYWORDS[0]; k++) {
        intern(rl, KEYWORDS[k].text, (int)strlen(KEYWORDS[k].text));
    }
    return rl;
}

void
ref_lexer_free(RefLexer *rl)
{
    for (int b = 0; b < REF_BUCKETS; b++) {
        for (RefName *n = rl->bucket[b], *next; n != NULL; n = next) {
            next = n->next;
            free(n);
        }
    }
    free(rl);
}

static Token
fail(RefLexer *rl, int pos, const char *msg)
{
    rl->error = msg;
    rl->error_pos = pos;
    return (Token){ TK_EOF, pos, 0, 0 };
}

Token
ref_next(RefLexer *rl, const char *src, int pos)
{
    int i = pos;

    for (;;) {
        if (is_space(src[i])) {
            i++;
        } else if (src[i] == '/' && src[i + 1] == '/') {
            while (src[i] != '\n' && src[i] != '\0') {
                i++;
            }
        } else {
            break;
        }
    }

    int start = i;
    char c = src[i];

    if (c == '\0') {
        return (Token){ TK_EOF, start, 0, 0 };
    }

    if (is_ident_start(c)) {
        while (is_ident(src[i])) {
            i++;
        }
        int len = i - start;
        TokenKind kind = TK_IDENT;
        for (size_t k = 0; k < sizeof KEYWORDS / sizeof KEYWORDS[0]; k++) {
            const char *kw = KEYWORDS[k].text;
            if ((int)strlen(kw) == len && memcmp(kw, src + start, (size_t)len) == 0) {
                kind = KEYWORDS[k].kind;
            }
        }
        return (Token){ kind, start, len, intern(rl, src + start, len) };
    }

    if (is_digit(c)) {
        bool is_float = false;
        while (is_digit(src[i])) {
            i++;
        }
        if (src[i] == '.' && is_digit(src[i + 1])) {
            is_float = true;
            i++;
            while (is_digit(src[i])) {
                i++;
            }
        }
        if (src[i] == 'e' || src[i] == 'E') {
            int digits_at = (src[i + 1] == '+' || src[i + 1] == '-') ? i + 2 : i + 1;
            if (is_digit(src[digits_at])) {
                is_float = true;
                i = digits_at;
                while (is_digit(src[i])) {
                    i++;
                }
            }
        }
        return (Token){ is_float ? TK_FLOAT : TK_INT, start, i - start, 0 };
    }

    if (c == '"') {
        i++;
        while (src[i] != '"') {
            if (src[i] == '\0' || src[i] == '\n') {
                return fail(rl, i, "unterminated string");
            }
            if (src[i] == '\\') {
                char e = src[i + 1];
                if (e != '\0' && strchr("\"\\/bfnrt", e) != NULL) {
                    i += 2;
                } else if (e != 'u') {
                    return fail(rl, i, "unknown escape sequence");
                } else if (is_hex(src[i + 2]) && is_hex(src[i + 3]) && is_hex(src[i + 4]) && is_hex(src[i + 5])) {
                    i += 6;
                } else {
                    return fail(rl, i, "invalid unicode hex escape");
                }
            } else {
                i++;
            }
        }
        return (Token){ TK_STRING, start, i + 1 - start, 0 };
    }

    for (size_t k = 0; k < sizeof OPS / sizeof OPS[0]; k++) {
        const char *op = OPS[k].text;
        size_t n = strlen(op);
        if (strncmp(src + start, op, n) == 0) {
            return (Token){ OPS[k].kind, start, (int)n, 0 };
        }
    }

    if ((u8)c >= 0x21) {
        return (Token){ TK_OP, start, 1, 0 };
    }
    return fail(rl, start, "unexpected character");
}
