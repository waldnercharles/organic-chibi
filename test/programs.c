#include "programs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static u64
splitmix64(u64 *state)
{
    u64 z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

int
random_below(u64 *rng, int n)
{
    return (int)(splitmix64(rng) % (u64)n);
}

// clang-format off
static const char *const PIECES[] = {
    "if", "else", "for", "while", "continue", "offsetof", "fn", "return", "true", "as",
    "x", "_", "name", "Name_2", "abcdefg", "abcdefgh", "abcdefghx",
    "abcdefghijklmno", "abcdefghijklmnop", "abcdefghijklmnopxy", "a_name_longer_than_sixteen_bytes",
    "0", "7", "42", "3.14", "1.", "1e5", "1E9", "2e+8", "6e-3", "1e",
    "\"\"", "\"text\"", "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"", "\"\\u00e9\\uABCD\"", "\"caf\xc3\xa9\"",
    "\"// not a comment\"", "// comment\n", "//\n", "// \"quote\" \\ backslash\n",
    "<<", ">>", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "->", "=>", "==",
    "!=", "<=", ">=", "..", "::", ":=", "+", "-", "*", "/", "%", "=", "!", "@", "$", "?", "&",
    "|", "^", "~", "<", ">", ":", ";", ".", ",", "{", "}", "(", ")", "[", "]", "#", "'", "\\",
    "`", "\x7f", "\xc3\xa9",
    " ", " ", " ", " ", "  ", "        ", "\t", "\n", "\n", "\r\n",
};
// clang-format on

const Pieces all_pieces = { PIECES, sizeof PIECES / sizeof PIECES[0] };

size_t
random_program(u64 *rng, char *buf, Pieces pieces, int count)
{
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        char name[16];
        const char *piece = pieces.items[random_below(rng, pieces.count)];
        if (random_below(rng, 8) == 0) {
            snprintf(name, sizeof name, "v%d", random_below(rng, 100000));
            piece = name;
        }

        size_t n = strlen(piece);
        assert(n <= PIECE_MAX_LEN);
        memcpy(buf + len, piece, n);
        len += n;
    }

    return len;
}
