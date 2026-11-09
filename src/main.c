#include "arena.h"
#include "lexer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

static NORETURN void
fatal(const char *path, const char *msg)
{
    fprintf(stderr, "chibi: %s: %s\n", path, msg);
    exit(1);
}

static char *
read_source(Arena *arena, const char *path, size_t *out_len)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        fatal(path, "cannot open");
    }
    size_t len = (size_t)st.st_size;
    if (len > LEXER_MAX_SOURCE) {
        fatal(path, "too large");
    }

    char *src = arena_alloc(arena, len + LEXER_SOURCE_PADDING, 16);
    if (!src) {
        fatal(path, "out of memory");
    }
    for (size_t got = 0; got < len;) {
        ssize_t n = read(fd, src + got, len - got);
        if (n <= 0) {
            fatal(path, "cannot read");
        }
        got += (size_t)n;
    }
    close(fd);

    memset(src + len, 0, LEXER_SOURCE_PADDING);
    *out_len = len;
    return src;
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: chibi <file.chibi>\n");
        return 1;
    }

    Arena arena = { 0 };
    size_t len;
    char *src = read_source(&arena, argv[1], &len);

    Token *tokens = arena_alloc(&arena, lexer_max_tokens(len) * sizeof *tokens, alignof(Token));
    if (!tokens) {
        fatal(argv[1], "out of memory");
    }

    Lexer lx;
    lexer_init(&lx, &arena, len);
    int ntok = lexer_tokenize(&lx, src, tokens);
    if (ntok == 0) {
        lexer_print_error(&lx, src);
        return 1;
    }
    lexer_dump(stdout, src, tokens, ntok - 1);
    arena_free(&arena);
    return 0;
}
