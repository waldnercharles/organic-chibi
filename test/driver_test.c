#define _DEFAULT_SOURCE

#include "../src/common.h"
#include "../src/driver.h"
#include "../vendor/pico_unit/pico_unit.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef CHIBI_TEST_CC

static const char USAGE[] =
    "usage: chibi build [-o program] file.chibi... [-- C compiler arguments]\n"
    "       chibi c [-o program.c] file.chibi...\n"
    "       chibi tokens file.chibi\n";

static const char FIB_MAIN[] =
    "extern fn puts(s: [*]u8) -> i32\n"
    "extern fn putchar(c: i32) -> i32\n"
    "extern fn atoi(s: [*]u8) -> i32\n"
    "\n"
    "fn put_number(n: int) {\n"
    "    digits: [20]u8\n"
    "    k := 0\n"
    "    m := n\n"
    "    while true {\n"
    "        digits[k] = u8(48 + m % 10)\n"
    "        k += 1\n"
    "        m = m / 10\n"
    "        if m == 0 { break }\n"
    "    }\n"
    "    while k > 0 {\n"
    "        k -= 1\n"
    "        _ = putchar(i32(digits[k]))\n"
    "    }\n"
    "}\n"
    "\n"
    "export fn main(argc: i32, argv: [*][*]u8) -> i32 {\n"
    "    _ = puts(argv[1])\n"
    "    put_number(fib(int(atoi(argv[2]))))\n"
    "    _ = putchar(10)\n"
    "    return argc\n"
    "}\n";

static const char TWICE[] =
    "extern fn chibi_test_twice(x: int) -> int\n"
    "\n"
    "export fn main(argc: i32, argv: [*][*]u8) -> i32 {\n"
    "    return i32(chibi_test_twice(int(argc)))\n"
    "}\n";

static const char TWICE_C[] =
    "#include <stdint.h>\n"
    "\n"
    "int64_t\n"
    "chibi_test_twice(int64_t x)\n"
    "{\n"
    "    return 2 * x;\n"
    "}\n";

static const char ENTRY[] =
    "export fn main() -> i32 {\n"
    "    return 0\n"
    "}\n";

static const char CODE[] =
    "fn g() -> int {\n"
    "    return 2\n"
    "}\n"
    "_ = g()\n";

static const char ODD_NAME[] = "b \"q\\?\?=.chibi";

static const char FIB[] =
    "fn fib(n: int) -> int {\n"
    "    a := 0\n"
    "    b := 1\n"
    "    i := 0\n"
    "    while i < n {\n"
    "        t := a + b\n"
    "        a = b\n"
    "        b = t\n"
    "        i += 1\n"
    "    }\n"
    "    return a\n"
    "}\n";

typedef struct
{
    const char *name;
    const char *text;
    const char *error;
    bool build_only;
} BadFile;

static const BadFile BAD_FILES[] = {
    { "missing.chibi", NULL, "chibi: %s/missing.chibi: cannot open\n", false },
    { "bad_string.chibi", "fn f() -> int {\n    return \"abc\n}\n", "%s/bad_string.chibi:2:16: unterminated string\n", false },
    { "bad_parse.chibi", "fn f() -> int {\n    return 1 +\n}\n", "%s/bad_parse.chibi:3:1: expected an expression\n", false },
    { "bad_check.chibi", "fn f() -> int {\n    return x\n}\n", "%s/bad_check.chibi:2:12: unknown name `x`\n", false },
    { "no_main.chibi", "fn main() -> i32 {\n    return 0\n}\n", "chibi: the program has no `export fn main`\n", true },
    { "int_main.chibi", "export fn main() -> int {\n    return 1\n}\n", "%s/int_main.chibi:1:11: `export fn main` must return `i32`\n", true },
    { "only_dots.chibi",
      "extern fn tally(..) -> int\nexport fn main() -> i32 {\n    return 0\n}\n",
      "chibi: C11 can't declare an `extern fn` that takes only `..`\n",
      false },
};

typedef struct
{
    const char *args[5];
    const char *error;
    bool usage;
} BadArgs;

static const BadArgs BAD_ARGS[] = {
    { { "chibi" }, "", true },
    { { "chibi", "build" }, "", true },
    { { "chibi", "c" }, "", true },
    { { "chibi", "run", "a.chibi" }, "", true },
    { { "chibi", "tokens" }, "", true },
    { { "chibi", "tokens", "a.chibi", "b.chibi" }, "", true },
    { { "chibi", "build", "-x", "a.chibi" }, "chibi: unknown option -x\n", true },
    { { "chibi", "c", "--", "a.chibi" }, "chibi: unknown option --\n", true },
    { { "chibi", "build", "a.chibi", "-o" }, "chibi: -o needs a file name\n", false },
    { { "chibi", "build", "program" }, "chibi: name the program with -o\n", false },
};

typedef struct
{
    int status;
    char *out;
    size_t out_len;
    char *err;
    size_t err_len;
} Drive;

static Drive
drive(char **argv)
{
    int argc = 0;
    while (argv[argc]) {
        argc++;
    }

    Drive d = { 0 };
    FILE *out = open_memstream(&d.out, &d.out_len);
    FILE *err = open_memstream(&d.err, &d.err_len);
    d.status = driver_run(out, err, argc, argv);
    fclose(out);
    fclose(err);
    return d;
}

static void
release(Drive *d)
{
    free(d->out);
    free(d->err);
}

static bool
write_file(const char *dir, const char *name, const char *text)
{
    char path[96];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }
    fputs(text, f);
    return fclose(f) == 0;
}

static void
remove_files(const char *dir, const char *const *names, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        char path[96];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        remove(path);
    }
    rmdir(dir);
}

static bool
drives_to(char **argv, int status, const char *out, const char *err)
{
    Drive d = drive(argv);
    bool ok = d.status == status && strcmp(d.out, out) == 0 && strcmp(d.err, err) == 0;
    if (!ok) {
        fputs("driver test:", stderr);
        for (u32 i = 0; argv[i]; i++) {
            fprintf(stderr, " %s", argv[i]);
        }
        fprintf(stderr, " gave %d, want %d\n  out: %s\n  err: %s  want err: %s", d.status, status, d.out, d.err, err);
    }
    release(&d);
    return ok;
}

static int
run(char **argv, char *out, size_t cap)
{
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 });
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);

    size_t n = 0;
    for (ssize_t got; n + 1 < cap && (got = read(fds[0], out + n, cap - 1 - n)) > 0;) {
        n += (size_t)got;
    }
    out[n] = '\0';
    close(fds[0]);

    int status = -1;
    waitpid(pid, &status, 0);
    return status;
}

static bool
runs_to(char **argv, const char *want, int code, int signal)
{
    char out[4096];
    int status = run(argv, out, sizeof out);
    bool ok = strcmp(out, want) == 0 &&
              (signal ? WIFSIGNALED(status) && WTERMSIG(status) == signal
                      : WIFEXITED(status) && WEXITSTATUS(status) == code);
    if (!ok) {
        fprintf(stderr, "driver test: %s %s %s gave status %d and\n%s  want exit %d, signal %d and\n%s", argv[0], argv[1], argv[2], status, out, code, signal, want);
    }
    return ok;
}

TEST_CASE(builds_programs)
{
    char dir[] = "/tmp/chibi-d-XXXXXX";
    REQUIRE(mkdtemp(dir));
    setenv("CC", CHIBI_TEST_CC, 1);

    char a[64], b[64], program[64];
    snprintf(a, sizeof a, "%s/a.chibi", dir);
    snprintf(b, sizeof b, "%s/%s", dir, ODD_NAME);
    snprintf(program, sizeof program, "%s/it's a program", dir);
    bool ok = write_file(dir, "a.chibi", FIB_MAIN) && write_file(dir, ODD_NAME, FIB);

    ok = ok && drives_to((char *[]){ "chibi", "build", a, "-o", program, b, NULL }, 0, "", "");
    ok = ok && runs_to((char *[]){ program, "hello", "91", NULL }, "hello\n4660046610375530309\n", 3, 0);
    ok = ok && runs_to((char *[]){ program, "zero", "0", NULL }, "zero\n0\n", 3, 0);
    ok = ok && runs_to((char *[]){ program, "x", "92", NULL }, "x\ntrap: integer overflow\n", 0, SIGABRT);

    static const char *const files[] = { "a.chibi", ODD_NAME, "it's a program" };
    remove_files(dir, files, sizeof files / sizeof files[0]);
    REQUIRE(ok);
    return true;
}

TEST_CASE(names_programs)
{
    char dir[] = "/tmp/chibi-d-XXXXXX";
    REQUIRE(mkdtemp(dir));

    char a[64], b[64], out[64], program[64], home[4096] = "";
    snprintf(a, sizeof a, "%s/a.chibi", dir);
    snprintf(b, sizeof b, "%s/b.chibi", dir);
    snprintf(out, sizeof out, "%s/out", dir);
    snprintf(program, sizeof program, "%s/out/a", dir);
    bool ok = write_file(dir, "a.chibi", FIB_MAIN) && write_file(dir, "b.chibi", FIB) &&
              mkdir(out, 0700) == 0 && getcwd(home, sizeof home) && chdir(out) == 0;

    ok = ok && drives_to((char *[]){ "chibi", "build", a, b, "--", "-O0", NULL }, 0, "", "");
    ok = chdir(home) == 0 && ok;
    ok = ok && runs_to((char *[]){ program, "hi", "10", NULL }, "hi\n55\n", 3, 0);

    static const char *const files[] = { "a.chibi", "b.chibi", "out/a", "out" };
    remove_files(dir, files, sizeof files / sizeof files[0]);
    REQUIRE(ok);
    return true;
}

TEST_CASE(links_c)
{
    char dir[] = "/tmp/chibi-d-XXXXXX";
    REQUIRE(mkdtemp(dir));

    char source[64], object[64], twice[64], program[64], command[256];
    snprintf(source, sizeof source, "%s/twice.c", dir);
    snprintf(object, sizeof object, "%s/twice.o", dir);
    snprintf(twice, sizeof twice, "%s/twice.chibi", dir);
    snprintf(program, sizeof program, "%s/twice", dir);
    snprintf(command, sizeof command, CHIBI_TEST_CC " -c -o '%s' '%s'", object, source);
    bool ok = write_file(dir, "twice.c", TWICE_C) && write_file(dir, "twice.chibi", TWICE) && system(command) == 0;

    ok = ok && drives_to((char *[]){ "chibi", "build", "-o", program, twice, "--", object, NULL }, 0, "", "");
    ok = ok && runs_to((char *[]){ program, "a", "b", NULL }, "", 6, 0);

    static const char *const files[] = { "twice.c", "twice.o", "twice.chibi", "twice" };
    remove_files(dir, files, sizeof files / sizeof files[0]);
    REQUIRE(ok);
    return true;
}

TEST_CASE(reports_compiler_failure)
{
    char dir[] = "/tmp/chibi-d-XXXXXX";
    REQUIRE(mkdtemp(dir));

    enum { CAP = 1 << 15 };
    char *text = malloc(CAP);
    int n = snprintf(text, CAP, "%s", ENTRY);
    for (int k = 0; k < 400; k++) {
        n += snprintf(text + n, CAP - (size_t)n, "fn g%d(x: int) -> int {\n    return x + %d\n}\n", k, k);
    }
    bool ok = write_file(dir, "big.chibi", text);
    free(text);

    char big[64], program[64], want[512];
    snprintf(big, sizeof big, "%s/big.chibi", dir);
    snprintf(program, sizeof program, "%s/big", dir);
    snprintf(want, sizeof want, "chibi: the C compiler failed: " CHIBI_TEST_CC " -std=c11 -O2 -w -o '%s' -x c - -x none '-fno-such-flag' 'it'\\''s'\n", program);

    int saved = dup(2);
    int null = open("/dev/null", O_WRONLY);
    dup2(null, 2);
    Drive d = drive((char *[]){ "chibi", "build", "-o", program, big, "--", "-fno-such-flag", "it's", NULL });
    dup2(saved, 2);
    close(saved);
    close(null);
    if (ok && (d.status != 1 || d.out_len != 0 || strcmp(d.err, want) != 0)) {
        fprintf(stderr, "driver test: a failing C compiler gave %d and\n%s  want 1 and\n%s", d.status, d.err, want);
        ok = false;
    }
    release(&d);

    static const char *const files[] = { "big.chibi", "big" };
    remove_files(dir, files, sizeof files / sizeof files[0]);
    REQUIRE(ok);
    return true;
}

TEST_CASE(reports_errors)
{
    char dir[] = "/tmp/chibi-d-XXXXXX";
    REQUIRE(mkdtemp(dir));

    char never[64];
    snprintf(never, sizeof never, "%s/never", dir);
    bool ok = true;
    const char *names[sizeof BAD_FILES / sizeof BAD_FILES[0]];
    for (u32 k = 0; k < sizeof BAD_FILES / sizeof BAD_FILES[0]; k++) {
        const BadFile *f = &BAD_FILES[k];
        names[k] = f->name;
        ok = ok && (!f->text || write_file(dir, f->name, f->text));

        char path[96], want[256];
        snprintf(path, sizeof path, "%s/%s", dir, f->name);
        snprintf(want, sizeof want, f->error, dir);
        ok = ok && drives_to((char *[]){ "chibi", "build", "-o", never, path, NULL }, 1, "", want);
        ok = ok && (f->build_only || drives_to((char *[]){ "chibi", "c", path, NULL }, 1, "", want));
    }

    char entry[96], code[96], want[256];
    snprintf(entry, sizeof entry, "%s/entry.chibi", dir);
    snprintf(code, sizeof code, "%s/code.chibi", dir);
    ok = ok && write_file(dir, "entry.chibi", ENTRY) && write_file(dir, "code.chibi", CODE);
    snprintf(want, sizeof want, "%s:4:1: only the file you run may contain code\n", code);
    ok = ok && drives_to((char *[]){ "chibi", "build", "-o", never, entry, code, NULL }, 1, "", want);
    snprintf(want, sizeof want, "%s:4:1: the checker doesn't support this yet\n", code);
    ok = ok && drives_to((char *[]){ "chibi", "build", "-o", never, code, entry, NULL }, 1, "", want);

    for (u32 k = 0; ok && k < sizeof BAD_ARGS / sizeof BAD_ARGS[0]; k++) {
        char want[512];
        snprintf(want, sizeof want, "%s%s", BAD_ARGS[k].error, BAD_ARGS[k].usage ? USAGE : "");
        ok = drives_to((char **)BAD_ARGS[k].args, 1, "", want);
    }

    remove(entry);
    remove(code);
    remove(never);
    remove_files(dir, names, sizeof names / sizeof names[0]);
    REQUIRE(ok);
    return true;
}

TEST_CASE(writes_c)
{
    char dir[] = "/tmp/chibi-d-XXXXXX";
    REQUIRE(mkdtemp(dir));

    char a[64], b[64], c[64];
    snprintf(a, sizeof a, "%s/a.chibi", dir);
    snprintf(b, sizeof b, "%s/b.chibi", dir);
    snprintf(c, sizeof c, "%s/p.c", dir);
    bool ok = write_file(dir, "a.chibi", FIB_MAIN) && write_file(dir, "b.chibi", FIB);

    Drive d = drive((char *[]){ "chibi", "c", a, b, NULL });
    const char *runtime = strstr(d.out, "\nchibi_trap(int kind)\n");
    const char *lines = strstr(d.out, "\n#line ");
    char adapter[160];
    snprintf(
        adapter,
        sizeof adapter,
        "\n#line 21 \"%s\"\nint32_t chibi_p_adapter_main(int32_t s0, uint8_t * s1) { return ",
        a
    );
    ok = ok && d.status == 0 && d.err_len == 0 &&
         strncmp(d.out, "#include <stdint.h>\n", 20) == 0 &&
         strstr(d.out, "\nCHIBI_FN int64_t p_fib(void *ctx, int64_t s0)\n") &&
         runtime && lines && runtime < lines && strstr(d.out, adapter);
    ok = ok && drives_to((char *[]){ "chibi", "c", "-o", c, a, b, NULL }, 0, "", "");

    FILE *f = fopen(c, "r");
    char *text = calloc(d.out_len + 2, 1);
    size_t n = f ? fread(text, 1, d.out_len + 1, f) : 0;
    ok = ok && n == d.out_len && memcmp(text, d.out, n) == 0;
    if (f) {
        fclose(f);
    }
    free(text);
    release(&d);

    FILE *t = fopen("test/hello.txt", "r");
    char want[8192];
    size_t m = t ? fread(want, 1, sizeof want - 1, t) : 0;
    want[m] = '\0';
    if (t) {
        fclose(t);
    }
    ok = ok && m > 0 && drives_to((char *[]){ "chibi", "tokens", "test/hello.chibi", NULL }, 0, want, "");

    static const char *const files[] = { "a.chibi", "b.chibi", "p.c" };
    remove_files(dir, files, sizeof files / sizeof files[0]);
    REQUIRE(ok);
    return true;
}

#endif

TEST_SUITE(driver_tests)
{
#ifdef CHIBI_TEST_CC
    RUN_TEST_CASE(builds_programs);
    RUN_TEST_CASE(names_programs);
    RUN_TEST_CASE(links_c);
    RUN_TEST_CASE(reports_compiler_failure);
    RUN_TEST_CASE(reports_errors);
    RUN_TEST_CASE(writes_c);
#endif
}
