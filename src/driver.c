#define _DEFAULT_SOURCE

#include "driver.h"

#include "c_source.h"
#include "parser.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

static const char USAGE[] = "usage: chibi build [--release-fast] "
                            "[--target arch-os] [-o program] "
                            "file.chibi... [-- C compiler arguments]\n"
                            "       chibi c [--release-fast] "
                            "[--target arch-os] [--layouts] "
                            "[-o program.c] file.chibi...\n"
                            "       chibi tokens file.chibi\n";

typedef struct
{
    const char *output;
    char *named;
    const char **files;
    u32 nfiles;
    char **cc_args;
    u32 ncc_args;
    const char *target;
    bool release_fast;
    bool layouts;
} Options;

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile *files;
    Checker c;
    const char *target;
    bool checked;
    bool release_fast;
} Program;

static char *
cannot_read(FILE *err, int fd, const char *path, const char *msg)
{
    fprintf(err, "chibi: %s: %s\n", path, msg);
    if (fd >= 0) {
        close(fd);
    }
    return NULL;
}

static char *
read_source(FILE *err, Arena *arena, const char *path, size_t *out_len)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    off_t end = fd < 0 ? -1 : lseek(fd, 0, SEEK_END);
    if (end < 0 || lseek(fd, 0, SEEK_SET) != 0) {
        return cannot_read(err, fd, path, "cannot open");
    }

    size_t len = (size_t)end;
    if (len > LEXER_MAX_SOURCE) {
        return cannot_read(err, fd, path, "too large");
    }

    char *src = arena_alloc(arena, len + LEXER_SOURCE_PADDING, 16);
    for (size_t got = 0; got < len;) {
        ssize_t n = read(fd, src + got, len - got);
        if (n <= 0) {
            return cannot_read(err, fd, path, "cannot read");
        }
        got += (size_t)n;
    }
    close(fd);

    memset(src + len, 0, LEXER_SOURCE_PADDING);
    *out_len = len;
    return src;
}

static int
dump_tokens(FILE *out, FILE *err, const char *path)
{
    Arena arena = { 0 };
    size_t len;
    char *src = read_source(err, &arena, path, &len);
    if (!src) {
        arena_free(&arena);
        return 1;
    }

    Tokens tokens = lexer_alloc_tokens(&arena, len);
    Lexer lx;
    lexer_init(&lx, &arena, len);
    int ntok = lexer_tokenize(&lx, src, tokens);
    if (ntok == 0) {
        lexer_print_error(err, &lx, path, src);
    } else {
        lexer_dump(out, src, tokens, ntok - 1);
    }
    arena_free(&arena);
    return ntok == 0;
}

static bool
parse_options(FILE *err, Options *o, int argc, char **argv, bool executable)
{
    *o = (Options){ .files = malloc((size_t)argc * sizeof *o->files) };
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 == argc) {
                fputs("chibi: -o needs a file name\n", err);
                return false;
            }
            o->output = argv[++i];
        } else if (strcmp(argv[i], "--release-fast") == 0) {
            o->release_fast = true;
        } else if (!executable && strcmp(argv[i], "--layouts") == 0) {
            o->layouts = true;
        } else if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 == argc) {
                fputs("chibi: --target needs a target, such as arm64-linux\n", err);
                return false;
            }
            o->target = argv[++i];
        } else if (executable && strcmp(argv[i], "--") == 0) {
            o->cc_args = argv + i + 1;
            o->ncc_args = (u32)(argc - i - 1);
            break;
        } else if (argv[i][0] == '-') {
            fprintf(err, "chibi: unknown option %s\n%s", argv[i], USAGE);
            return false;
        } else {
            o->files[o->nfiles++] = argv[i];
        }
    }
    if (o->nfiles == 0) {
        fputs(USAGE, err);
        return false;
    }
    if (!executable || o->output) {
        return true;
    }

    const char *slash = strrchr(o->files[0], '/');
    const char *base = slash ? slash + 1 : o->files[0];
    size_t n = strlen(base);
    if (n <= 6 || strcmp(base + n - 6, ".chibi") != 0) {
        fputs("chibi: name the program with -o\n", err);
        return false;
    }
    o->named = malloc(n - 5);
    memcpy(o->named, base, n - 6);
    o->named[n - 6] = '\0';
    o->output = o->named;
    return true;
}

static bool
set_target(FILE *err, Checker *c, const char *text)
{
    const char *dash = strchr(text, '-');
    if (dash && check_target(c, TARGET_ARCH, text, (u32)(dash - text)) &&
        check_target(c, TARGET_OS, dash + 1, (u32)strlen(dash + 1))) {
        return true;
    }
    fprintf(err, "chibi: unknown target %s; a target is an arch and an OS, such as arm64-linux\n", text);
    return false;
}

static bool
check_program(FILE *err, Program *p, const char *const *paths, u32 n)
{
    char **srcs = arena_alloc(&p->arena, n * sizeof *srcs, alignof(char *));
    size_t *lens = arena_alloc(&p->arena, n * sizeof *lens, alignof(size_t));
    size_t total = 0;
    for (u32 i = 0; i < n; i++) {
        char *src = read_source(err, &p->arena, paths[i], &lens[i]);
        if (!src) {
            return false;
        }
        srcs[i] = src;
        total += lens[i];
    }

    lexer_init(&p->lx, &p->arena, total);
    p->files = arena_alloc(&p->arena, n * sizeof *p->files, alignof(ParsedFile));
    for (u32 i = 0; i < n; i++) {
        Tokens tokens = lexer_alloc_tokens(&p->arena, lens[i]);
        int ntokens = lexer_tokenize(&p->lx, srcs[i], tokens);
        if (ntokens == 0) {
            lexer_print_error(err, &p->lx, paths[i], srcs[i]);
            return false;
        }

        Nodes nodes = parser_alloc_nodes(&p->arena, ntokens);
        Parser parser;
        int count = parser_parse(&parser, tokens, ntokens, nodes);
        if (count < 0) {
            parser_print_error(err, &parser, paths[i], srcs[i]);
            return false;
        }
        p->files[i] = (ParsedFile){
            paths[i], srcs[i], tokens, nodes, (u32)count, (u32)parser.top_count,
            i == 0,
        };
    }

    check_init(&p->c, &p->lx.names, p->files, n);
    p->c.release_fast = p->release_fast;
    p->checked = true;
    if (p->target && !set_target(err, &p->c, p->target)) {
        return false;
    }
    check_package(&p->c);
    if (p->c.ndiags) {
        check_print_diagnostics(err, &p->c);
        return false;
    }
    return true;
}

static bool
has_main(FILE *err, const Program *p)
{
    const Checker *c = &p->c;
    for (u32 d = 1; d < c->decls.count; d++) {
        Name n = names_get(c->names, c->decls.names[d]);
        if (c->decls.kinds[d] != DECL_FN || !(c->decls.flags[d] & DECL_EXPORT) ||
            n.len != 4 || memcmp(n.text, "main", 4) != 0) {
            continue;
        }
        if ((u32)c->types.arg1[c->decls.types[d]] == TYPE_ID_I32) {
            return true;
        }

        const ParsedFile *f = &p->files[c->decls.files[d]];
        u32 pos = f->tokens.starts[f->nodes.tokens[c->decls.roots[d]]];
        print_error_at(err, f->path, f->src, pos, "`export fn main` must return `i32`");
        return false;
    }
    fputs("chibi: the program has no `export fn main`\n", err);
    return false;
}

static void
put_program(FILE *out, const Checker *c)
{
    c_source_prelude(out);
    c_source_runtime(out);
    c_source_package(out, c, "p_");
}

static void
put_quoted(FILE *out, const char *s)
{
    fputs(" '", out);
    for (; *s; s++) {
        if (*s == '\'') {
            fputs("'\\''", out);
        } else {
            fputc(*s, out);
        }
    }
    fputc('\'', out);
}

static bool
build_program(FILE *err, const Checker *c, const Options *o)
{
    char *command = NULL;
    size_t size = 0;
    FILE *m = open_memstream(&command, &size);
    const char *cc = getenv("CC");
    fprintf(m, "%s -std=c11 -O2 -w -o", cc && *cc ? cc : "cc");
    put_quoted(m, o->output);
    fputs(" -x c - -x none", m);
    for (u32 i = 0; i < o->ncc_args; i++) {
        put_quoted(m, o->cc_args[i]);
    }
    fclose(m);

    void (*on_pipe)(int) = signal(SIGPIPE, SIG_IGN);
    FILE *pipe = popen(command, "w");
    int status = -1;
    if (pipe) {
        put_program(pipe, c);
        status = pclose(pipe);
    }
    signal(SIGPIPE, on_pipe);

    if (status != 0) {
        fprintf(err, "chibi: the C compiler failed: %s\n", command);
    }
    free(command);
    return status == 0;
}

static bool
write_c(FILE *out, FILE *err, const Checker *c, const Options *o)
{
    FILE *f = o->output ? fopen(o->output, "w") : out;
    if (!f) {
        fprintf(err, "chibi: %s: cannot create\n", o->output);
        return false;
    }

    if (o->layouts) {
        c_source_layouts(f, c);
    } else {
        put_program(f, c);
    }
    if (o->output && fclose(f) != 0) {
        fprintf(err, "chibi: %s: cannot write\n", o->output);
        return false;
    }
    return true;
}

static int
compile_program(FILE *out, FILE *err, int argc, char **argv, bool executable)
{
    Options o;
    Program p;
    p.arena = (Arena){ 0 };
    p.checked = false;
    bool ok = parse_options(err, &o, argc, argv, executable);
    p.target = o.target;
    p.release_fast = o.release_fast;
    ok = ok && check_program(err, &p, o.files, o.nfiles);

    const char *unsupported = ok ? c_source_unsupported(&p.c) : NULL;
    if (unsupported) {
        fprintf(err, "chibi: %s\n", unsupported);
        ok = false;
    }
    ok = ok && (!executable || has_main(err, &p));
    ok = ok &&
         (executable ? build_program(err, &p.c, &o) : write_c(out, err, &p.c, &o));

    if (p.checked) {
        check_free(&p.c);
    }
    arena_free(&p.arena);
    free(o.named);
    free(o.files);
    return !ok;
}

int
driver_run(FILE *out, FILE *err, int argc, char **argv)
{
    const char *command = argc > 1 ? argv[1] : "";
    if (argc == 3 && strcmp(command, "tokens") == 0) {
        return dump_tokens(out, err, argv[2]);
    }
    if (strcmp(command, "build") == 0 || strcmp(command, "c") == 0) {
        return compile_program(out, err, argc, argv, command[0] == 'b');
    }
    fputs(USAGE, err);
    return 1;
}
