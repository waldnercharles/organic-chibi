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

static int
digit_value(char c)
{
    if (is_digit(c)) {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 99;
}

static const char *const
    MISPLACED_SEPARATOR = "`_` must sit between two digits";

static int
skip_digits(RefLexer *rl, const char *src, int i, int base)
{
    for (;;) {
        if (digit_value(src[i]) < base) {
            i++;
        } else if (src[i] == '_' && digit_value(src[i + 1]) < base) {
            i++;
        } else if (src[i] == '_') {
            rl->error = MISPLACED_SEPARATOR;
            rl->error_pos = i;
            return i;
        } else {
            return i;
        }
    }
}

// clang-format off
static const struct
{
    const char *text;
    TokenKind kind;
} KEYWORDS[] = {
    { "if", TK_IF },           { "else", TK_ELSE },
    { "for", TK_FOR },         { "while", TK_WHILE },
    { "switch", TK_SWITCH },   { "break", TK_BREAK },
    { "continue", TK_CONTINUE }, { "return", TK_RETURN },
    { "defer", TK_DEFER },     { "struct", TK_STRUCT },
    { "union", TK_UNION },     { "enum", TK_ENUM },
    { "fn", TK_FN },           { "package", TK_PACKAGE },
    { "import", TK_IMPORT },   { "pub", TK_PUB },
    { "true", TK_TRUE },       { "false", TK_FALSE },
    { "nil", TK_NIL },         { "is", TK_IS },
    { "in", TK_IN },           { "bitcast", TK_BITCAST },
    { "and", TK_AND },         { "or", TK_OR },
    { "const", TK_CONST },     { "export", TK_EXPORT },
    { "extern", TK_EXTERN },   { "undefined", TK_UNDEFINED },
    { "try", TK_TRY },         { "catch", TK_CATCH },
};
// clang-format on

#define BUILTIN_NAME_TEXT(name, text) text,

static const char *const BUILTIN_NAMES[] = {
    CHIBI_BUILTIN_NAMES(BUILTIN_NAME_TEXT, BUILTIN_NAME_TEXT)
};

// ref_next takes the first match, so longer operators must come before shorter ones.
static const struct
{
    const char *text;
    TokenKind kind;
} OPS[] = {
    { "<<=", TK_SHLEQ },
    { ">>=", TK_SHREQ },
    { "..=", TK_RANGE_INCLUSIVE },
    { "+%=", TK_ADD_WRAP_EQ },
    { "-%=", TK_SUB_WRAP_EQ },
    { "*%=", TK_MUL_WRAP_EQ },
    { "?\?=", TK_OR_ELSE_EQ },
    { ".*", TK_DEREF },
    { "!!", TK_UNWRAP },
    { "??", TK_OR_ELSE },
    { "?=", TK_QUESTION_EQ },
    { "<<", TK_SHL },
    { ">>", TK_SHR },
    { "&&", TK_ANDAND },
    { "||", TK_OROR },
    { "+=", TK_ADDEQ },
    { "-=", TK_SUBEQ },
    { "*=", TK_MULEQ },
    { "+%", TK_ADD_WRAP },
    { "-%", TK_SUB_WRAP },
    { "*%", TK_MUL_WRAP },
    { "/=", TK_DIVEQ },
    { "%=", TK_MODEQ },
    { "&=", TK_ANDEQ },
    { "|=", TK_OREQ },
    { "^=", TK_XOREQ },
    { "->", TK_ARROW },
    { "=>", TK_FAT_ARROW },
    { "==", TK_EQEQ },
    { "!=", TK_NEQ },
    { "<=", TK_LTE },
    { ">=", TK_GTE },
    { "..", TK_RANGE },
    { "::", TK_DCOLON },
    { ":=", TK_DASSIGN },
    { "+", TK_PLUS },
    { "-", TK_MINUS },
    { "*", TK_STAR },
    { "/", TK_SLASH },
    { "%", TK_PERCENT },
    { "=", TK_EQ },
    { "!", TK_NOT },
    { "@", TK_AT },
    { "$", TK_DOLLAR },
    { "?", TK_QUESTION },
    { "&", TK_AMP },
    { "|", TK_PIPE },
    { "^", TK_XOR },
    { "~", TK_COMPL },
    { "<", TK_LT },
    { ">", TK_GT },
    { ":", TK_COLON },
    { ";", TK_SEMI },
    { ".", TK_PERIOD },
    { ",", TK_COMMA },
    { "{", TK_LBRACE },
    { "}", TK_RBRACE },
    { "(", TK_LPAREN },
    { ")", TK_RPAREN },
    { "[", TK_LBRACK },
    { "]", TK_RBRACK },
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
    for (size_t k = 0; k < sizeof BUILTIN_NAMES / sizeof BUILTIN_NAMES[0]; k++) {
        intern(rl, BUILTIN_NAMES[k], (int)strlen(BUILTIN_NAMES[k]));
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

static RefToken
fail(RefLexer *rl, int pos, const char *msg)
{
    rl->error = msg;
    rl->error_pos = pos;
    return (RefToken){ TK_EOF, pos, 0, 0, false };
}

static RefToken
ref_token(RefLexer *rl, const char *src, int start)
{
    int i = start;
    char c = src[i];
    if (c == '\0') {
        return (RefToken){ TK_EOF, start, 0, 0, false };
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
        return (RefToken){ kind, start, len, intern(rl, src + start, len), false };
    }

    if (is_digit(c)) {
        char second = src[i + 1];
        if (c == '0' && (second == 'X' || second == 'B' || second == 'O')) {
            return fail(rl, i + 1, "use a lowercase prefix like `0x`");
        }

        int base = 10;
        if (c == '0' && (second == 'x' || second == 'b' || second == 'o')) {
            base = second == 'x' ? 16 : second == 'b' ? 2 : 8;
            i += 2;
            if (src[i] == '_') {
                return fail(rl, i, MISPLACED_SEPARATOR);
            }
            if (digit_value(src[i]) >= base) {
                return fail(
                    rl,
                    i,
                    base == 16  ? "expected a hex digit"
                    : base == 2 ? "expected a binary digit"
                                : "expected an octal digit"
                );
            }
        } else if (c == '0' && (is_digit(second) || (second == '_' && is_digit(src[i + 2])))) {
            return fail(rl, i, "leading zeros are not allowed; use `0o` for octal");
        }

        bool is_float = false;
        i = skip_digits(rl, src, i, base);
        if (rl->error == NULL && base == 10 && src[i] == '.' && is_digit(src[i + 1])) {
            is_float = true;
            i = skip_digits(rl, src, i + 1, 10);
        }
        if (rl->error == NULL && base == 10 && (src[i] == 'e' || src[i] == 'E')) {
            int digits_at = (src[i + 1] == '+' || src[i + 1] == '-') ? i + 2 : i + 1;
            if (is_digit(src[digits_at])) {
                is_float = true;
                i = skip_digits(rl, src, digits_at, 10);
            }
        }
        if (rl->error != NULL) {
            return (RefToken){ TK_EOF, rl->error_pos, 0, 0, false };
        }

        if (is_ident(src[i])) {
            const char *msg = base == 10 ? "a name cannot follow a number"
                            : base == 16 ? "invalid digit in hex number"
                            : base == 2  ? "invalid digit in binary number"
                                         : "invalid digit in octal number";
            return fail(rl, i, msg);
        }
        return (RefToken){ is_float ? TK_FLOAT : TK_INT, start, i - start, 0, false };
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
        return (RefToken){ TK_STRING, start, i + 1 - start, 0, false };
    }

    for (size_t k = 0; k < sizeof OPS / sizeof OPS[0]; k++) {
        const char *op = OPS[k].text;
        size_t n = strlen(op);
        if (strncmp(src + start, op, n) == 0) {
            return (RefToken){ OPS[k].kind, start, (int)n, 0, false };
        }
    }

    if ((u8)c >= 0x21) {
        return (RefToken){ TK_OP, start, 1, 0, false };
    }
    return fail(rl, start, "unexpected character");
}

RefToken
ref_next(RefLexer *rl, const char *src, int pos)
{
    int i = pos;
    bool starts_line = pos == 0;

    for (;;) {
        if (is_space(src[i])) {
            starts_line |= src[i] == '\n';
            i++;
        } else if (src[i] == '/' && src[i + 1] == '/') {
            while (src[i] != '\n' && src[i] != '\0') {
                i++;
            }
        } else {
            break;
        }
    }

    RefToken t = ref_token(rl, src, i);
    t.starts_line = starts_line || t.kind == TK_EOF;
    return t;
}
