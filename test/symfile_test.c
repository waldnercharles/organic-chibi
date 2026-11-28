#define _DEFAULT_SOURCE

#include "../src/jit.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "backend_programs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile file;
    Checker c;
    Native n;
} Program;

typedef struct
{
    const u8 *at;
    u64 size;
    u64 addr;
} Section;

typedef struct
{
    u64 addr;
    u32 line;
} Row;

static const char DESCRIBED[] = "fn add(a: int, b: int) -> int {\n"
                                "    c := a + b\n"
                                "    return c * 2\n"
                                "}\n"
                                "\n"
                                "fn twice(x: int) -> int {\n"
                                "    y := add(x, x)\n"
                                "    if y > 10 {\n"
                                "        return y - 1\n"
                                "    }\n"
                                "    return y\n"
                                "}\n";

static bool
compile(Program *p, const char *text, u32 flags)
{
    *p = (Program){ 0 };
    size_t len = strlen(text);
    lexer_init(&p->lx, &p->arena, len);
    char *src = arena_alloc(&p->arena, len + LEXER_SOURCE_PADDING, 16);
    memset(src, 0, len + LEXER_SOURCE_PADDING);
    memcpy(src, text, len);

    Tokens tokens = lexer_alloc_tokens(&p->arena, len);
    int ntokens = lexer_tokenize(&p->lx, src, tokens);
    Nodes nodes = parser_alloc_nodes(&p->arena, ntokens);
    Parser parser;
    int count = parser_parse(&parser, tokens, ntokens, nodes);
    if (count < 0) {
        return false;
    }

    p->file = (ParsedFile){ "described.chibi", src, tokens, nodes, (u32)count, (u32)parser.top_count,
                            false };
    check_init(&p->c, &p->lx.names, &p->file, 1);
    check_package(&p->c);
    return p->c.ndiags == 0 && jit_compile(&p->n, &p->c, CONVENTION_HOST, flags);
}

static void
release(Program *p)
{
    native_free(&p->n);
    check_free(&p->c);
    arena_free(&p->arena);
}

static u64
read_le(const u8 *p, u32 bytes)
{
    u64 v = 0;
    memcpy(&v, p, bytes);
    return v;
}

static Section
section_named(const u8 *elf, const char *name)
{
    u64 headers = read_le(elf + 0x28, 8);
    u32 count = (u32)read_le(elf + 0x3C, 2);
    const u8 *names_header = elf + headers + 64 * read_le(elf + 0x3E, 2);
    const char *names = (const char *)elf + read_le(names_header + 0x18, 8);
    for (u32 k = 0; k < count; k++) {
        const u8 *h = elf + headers + 64 * k;
        if (strcmp(names + read_le(h, 4), name) == 0) {
            return (Section){ elf + read_le(h + 0x18, 8), read_le(h + 0x20, 8), read_le(h + 0x10, 8) };
        }
    }
    return (Section){ 0 };
}

static u64
read_uleb(const u8 **p)
{
    u64 v = 0;
    for (u32 shift = 0;; shift += 7) {
        u8 b = *(*p)++;
        v |= (u64)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            return v;
        }
    }
}

static int64_t
read_sleb(const u8 **p)
{
    int64_t v = 0;
    u32 shift = 0;
    u8 b;
    do {
        b = *(*p)++;
        v |= (int64_t)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 64 && (b & 0x40)) {
        v |= -((int64_t)1 << shift);
    }
    return v;
}

static u32
decode_lines(Section s, Row *rows, u32 cap)
{
    const u8 *p = s.at + 4;
    const u8 *end = s.at + 4 + read_le(s.at, 4);
    p += 2;
    u64 header = read_le(p, 4);
    p += 4;
    const u8 *program = p + header;
    u32 min_length = p[0];
    int32_t line_base = (int8_t)p[3];
    u32 line_range = p[4];
    u32 opcode_base = p[5];
    const u8 *lengths = p + 6;

    u32 n = 0;
    u64 addr = 0;
    int64_t line = 1;
    for (p = program; p < end;) {
        u8 op = *p++;
        if (op >= opcode_base) {
            u32 adjusted = op - opcode_base;
            addr += (u64)(adjusted / line_range) * min_length;
            line += line_base + (int32_t)(adjusted % line_range);
            if (n < cap) {
                rows[n] = (Row){ addr, (u32)line };
            }
            n++;
        } else if (op == 0) {
            u64 len = read_uleb(&p);
            u8 sub = p[0];
            if (sub == 1) {
                addr = 0;
                line = 1;
            } else if (sub == 2) {
                addr = read_le(p + 1, 8);
            }
            p += len;
        } else if (op == 1) {
            if (n < cap) {
                rows[n] = (Row){ addr, (u32)line };
            }
            n++;
        } else if (op == 2) {
            addr += read_uleb(&p) * min_length;
        } else if (op == 3) {
            line += read_sleb(&p);
        } else {
            for (u32 k = 0; k < lengths[op - 1]; k++) {
                read_uleb(&p);
            }
        }
    }
    return n;
}

static u32
decl_named(Program *p, const char *name)
{
    u32 before = p->lx.names.count;
    u32 id = names_intern(&p->lx.names, name, (u32)strlen(name));
    return id <= before ? p->c.package_names[id] : 0;
}

static bool
same_code(const Native *a, const Native *b)
{
    return a->code_size == b->code_size && memcmp(a->code, b->code, a->code_size) == 0;
}

static bool
has_row(const Row *rows, u32 n, u64 lo, u64 hi, u32 line, u64 *addr)
{
    for (u32 k = 0; k < n; k++) {
        if (rows[k].addr >= lo && rows[k].addr < hi && rows[k].line == line) {
            *addr = rows[k].addr;
            return true;
        }
    }
    return false;
}

TEST_CASE(debugger_sees_functions_and_lines)
{
    Program p;
    REQUIRE(compile(&p, DESCRIBED, JIT_DEBUGGER));
    const u8 *elf = p.n.symfile;
    REQUIRE(elf && p.n.symfile_size > 64);
    REQUIRE(memcmp(elf, "\x7F" "ELF", 4) == 0);
    REQUIRE(__jit_debug_descriptor.first_entry == p.n.debugger);
    REQUIRE(__jit_debug_descriptor.action_flag == JIT_REGISTER_FN);
    REQUIRE(p.n.debugger->symfile_addr == elf && p.n.debugger->symfile_size == p.n.symfile_size);

    Section text = section_named(elf, ".text");
    REQUIRE(text.addr == (u64)(uintptr_t)p.n.code && text.size == p.n.code_size);

    Section symtab = section_named(elf, ".symtab");
    Section strtab = section_named(elf, ".strtab");
    REQUIRE(symtab.size == 3 * 24);
    const char *names[2] = { "add", "twice" };
    u64 lo[2];
    u64 hi[2];
    for (u32 k = 0; k < 2; k++) {
        const u8 *sym = symtab.at + 24 * (k + 1);
        REQUIRE(strcmp((const char *)strtab.at + read_le(sym, 4), names[k]) == 0);
        lo[k] = text.addr + read_le(sym + 8, 8);
        hi[k] = lo[k] + read_le(sym + 16, 8);
        REQUIRE(lo[k] == (u64)(uintptr_t)native_entry(&p.n, decl_named(&p, names[k])));
    }

    Row rows[64];
    u32 n = decode_lines(section_named(elf, ".debug_line"), rows, 64);
    REQUIRE(n <= 64);
    u64 at[12] = { 0 };
    REQUIRE(has_row(rows, n, lo[0], hi[0], 2, &at[2]));
    REQUIRE(has_row(rows, n, lo[0], hi[0], 3, &at[3]));
    REQUIRE(at[2] < at[3]);
    REQUIRE(has_row(rows, n, lo[1], hi[1], 6, &at[6]) && at[6] == lo[1]);
    REQUIRE(has_row(rows, n, lo[1], hi[1], 7, &at[7]));
    REQUIRE(has_row(rows, n, lo[1], hi[1], 8, &at[8]));
    REQUIRE(has_row(rows, n, lo[1], hi[1], 9, &at[9]));
    REQUIRE(has_row(rows, n, lo[1], hi[1], 11, &at[11]));
    REQUIRE(at[6] < at[7] && at[7] < at[8] && at[8] < at[9] && at[9] < at[11]);

    release(&p);
    REQUIRE(__jit_debug_descriptor.first_entry == NULL);
    REQUIRE(__jit_debug_descriptor.action_flag == JIT_UNREGISTER_FN);
    return true;
}

TEST_CASE(frames_follow_the_prologue)
{
    Program p;
    REQUIRE(compile(&p, DESCRIBED, JIT_DEBUGGER));
    const u8 *elf = p.n.symfile;
    u32 machine = (u32)read_le(elf + 0x12, 2);
    u32 frame_reg = machine == 62 ? 6 : 29;

    Section frames = section_named(elf, ".debug_frame");
    REQUIRE(read_le(frames.at + 4, 4) == 0xFFFFFFFFu);
    const u8 *p8 = frames.at + 4 + read_le(frames.at, 4);
    u32 fdes = 0;
    bool framed = false;
    while (p8 < frames.at + frames.size) {
        u64 len = read_le(p8, 4);
        REQUIRE(read_le(p8 + 4, 4) == 0);
        u64 start = read_le(p8 + 8, 8);
        u64 range = read_le(p8 + 16, 8);
        REQUIRE(start >= (u64)(uintptr_t)p.n.code && start + range <= (u64)(uintptr_t)p.n.code + p.n.code_size);
        for (const u8 *q = p8 + 24; q + 1 < p8 + 4 + len; q++) {
            framed |= q[0] == 0x0D && q[1] == frame_reg;
        }
        fdes++;
        p8 += 4 + len;
    }
    REQUIRE(fdes == 2);
    REQUIRE(framed);
    release(&p);
    return true;
}

static bool
is_trap(const u8 *at, u32 machine, u32 kind)
{
    if (machine == 62) {
        return at[0] == 0x0F && at[1] == 0xB9 && at[2] == 0x40 && at[3] == kind;
    }
    return read_le(at, 4) == (0xD4200000u | kind << 5);
}

static u32
trap_rows(const Row *rows, u32 n, u64 lo, u64 hi, u32 line, u32 machine)
{
    u32 found = 0;
    for (u32 k = 0; k < n; k++) {
        u64 a = rows[k].addr;
        found += a >= lo && a < hi && rows[k].line == line &&
                 is_trap((const u8 *)(uintptr_t)a, machine, TRAP_OVERFLOW);
    }
    return found;
}

TEST_CASE(failed_checks_stop_at_their_lines)
{
    Program p;
    REQUIRE(compile(&p, DESCRIBED, JIT_DEBUGGER));
    const u8 *elf = p.n.symfile;
    u32 machine = (u32)read_le(elf + 0x12, 2);
    Section text = section_named(elf, ".text");
    const u8 *sym = section_named(elf, ".symtab").at + 24;
    u64 lo = text.addr + read_le(sym + 8, 8);
    u64 hi = lo + read_le(sym + 16, 8);

    Row rows[64];
    u32 n = decode_lines(section_named(elf, ".debug_line"), rows, 64);
    REQUIRE(n <= 64);
    REQUIRE(trap_rows(rows, n, lo, hi, 2, machine) == 1);
    REQUIRE(trap_rows(rows, n, lo, hi, 3, machine) == 1);
    release(&p);
    return true;
}

TEST_CASE(line_tables_leave_code_unchanged)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getpid());
    u32 compared = 0;
    u32 failed = 0;
    for (u32 k = 0; k < NBACKEND_PROGRAMS; k++) {
        Program plain;
        if (!compile(&plain, BACKEND_PROGRAMS[k], 0)) {
            release(&plain);
            continue;
        }
        Program again = { 0 };
        Program mapped = { 0 };
        bool built = compile(&again, BACKEND_PROGRAMS[k], 0) &&
                     compile(&mapped, BACKEND_PROGRAMS[k], JIT_PERF_MAP);
        bool stable = built && same_code(&plain.n, &again.n);
        failed += !built || (stable && !same_code(&plain.n, &mapped.n));
        compared += stable;
        release(&mapped);
        release(&again);
        release(&plain);
    }
    unlink(path);
    REQUIRE(failed == 0);
    REQUIRE(compared > 20);
    return true;
}

TEST_CASE(perf_map_names_functions)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getpid());
    unlink(path);
    Program p;
    REQUIRE(compile(&p, DESCRIBED, JIT_PERF_MAP));
    REQUIRE(p.n.symfile == NULL);
    FILE *f = fopen(path, "r");
    REQUIRE(f);
    char line[128];
    u32 lines = 0;
    const char *names[2] = { "add", "twice" };
    while (fgets(line, sizeof line, f)) {
        unsigned long long addr;
        unsigned size;
        char name[32];
        REQUIRE(lines < 2 && sscanf(line, "%llx %x %31s", &addr, &size, name) == 3);
        REQUIRE(strcmp(name, names[lines]) == 0);
        REQUIRE(addr == (unsigned long long)(uintptr_t)native_entry(&p.n, decl_named(&p, name)));
        REQUIRE(size > 0);
        lines++;
    }
    fclose(f);
    unlink(path);
    REQUIRE(lines == 2);
    release(&p);
    return true;
}

TEST_SUITE(symfile_tests)
{
    RUN_TEST_CASE(debugger_sees_functions_and_lines);
    RUN_TEST_CASE(frames_follow_the_prologue);
    RUN_TEST_CASE(failed_checks_stop_at_their_lines);
    RUN_TEST_CASE(line_tables_leave_code_unchanged);
    RUN_TEST_CASE(perf_map_names_functions);
}
