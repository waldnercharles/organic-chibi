#include "ast.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    u32 file;
    u32 offset;
    u32 line;
} Place;

typedef struct
{
    Place expansion;
    Place spelling;
    bool macro;
    bool macro_arg;
} Loc;

typedef struct
{
    const char *p;
    const char *base;
    Ast *ast;
    u32 last_file;
    u32 last_line;
    u64 tag;
    u32 generic_kind;
    FILE *err;
} Reader;

static void *
resize(void *p, size_t count, size_t size)
{
    return realloc(p, count * size);
}

static u64
hash_bytes(const char *text, u32 len)
{
    u64 h = 0xcbf29ce484222325ULL;
    for (u32 i = 0; i < len; i++) {
        h = (h ^ (u8)text[i]) * 0x100000001b3ULL;
    }
    return h;
}

static void
grow_slots(Strings *s)
{
    u32 n = s->mask ? (s->mask + 1) * 2 : 1024;
    free(s->slots);
    s->slots = calloc(n, sizeof *s->slots);
    s->mask = n - 1;
    for (u32 id = 0; id < s->count; id++) {
        u32 i = (u32)hash_bytes(s->bytes + s->starts[id], s->lens[id]) & s->mask;
        while (s->slots[i] != 0) {
            i = (i + 1) & s->mask;
        }
        s->slots[i] = id + 1;
    }
}

u32
strings_find(const Strings *s, const char *text, u32 len)
{
    if (s->mask == 0) {
        return STRINGS_MISSING;
    }

    u32 i = (u32)hash_bytes(text, len) & s->mask;
    for (; s->slots[i] != 0; i = (i + 1) & s->mask) {
        u32 id = s->slots[i] - 1;
        if (s->lens[id] == len && memcmp(s->bytes + s->starts[id], text, len) == 0) {
            return id;
        }
    }
    return STRINGS_MISSING;
}

u32
strings_intern(Strings *s, const char *text, u32 len)
{
    if (s->count * 2 >= s->mask) {
        grow_slots(s);
    }

    u32 i = (u32)hash_bytes(text, len) & s->mask;
    for (; s->slots[i] != 0; i = (i + 1) & s->mask) {
        u32 id = s->slots[i] - 1;
        if (s->lens[id] == len && memcmp(s->bytes + s->starts[id], text, len) == 0) {
            return id;
        }
    }

    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 1024;
        s->starts = resize(s->starts, s->cap, sizeof *s->starts);
        s->lens = resize(s->lens, s->cap, sizeof *s->lens);
    }
    if (s->used + len + 1 > s->bytes_cap) {
        s->bytes_cap = s->bytes_cap ? s->bytes_cap * 2 : 1 << 16;
        while (s->used + len + 1 > s->bytes_cap) {
            s->bytes_cap *= 2;
        }
        s->bytes = resize(s->bytes, s->bytes_cap, 1);
    }

    u32 id = s->count++;
    s->starts[id] = (u32)s->used;
    s->lens[id] = len;
    memcpy(s->bytes + s->used, text, len);
    s->bytes[s->used + len] = 0;
    s->used += len + 1;
    s->slots[i] = id + 1;
    return id;
}

static NORETURN void
fail(Reader *r, const char *msg)
{
    fprintf(r->err, "translate: JSON byte %zu: %s\n", (size_t)(r->p - r->base), msg);
    exit(1);
}

static void
skip_ws(Reader *r)
{
    while (*r->p == ' ' || *r->p == '\n' || *r->p == '\r' || *r->p == '\t') {
        r->p++;
    }
}

static void
expect(Reader *r, char c)
{
    skip_ws(r);
    if (*r->p != c) {
        fail(r, "unexpected byte");
    }
    r->p++;
}

static const char *
read_string(Reader *r, u32 *len)
{
    skip_ws(r);
    if (*r->p != '"') {
        fail(r, "expected a string");
    }

    const char *s = ++r->p;
    while (*r->p != '"') {
        if (*r->p == 0) {
            fail(r, "unterminated string");
        }
        r->p += *r->p == '\\' ? 2 : 1;
    }
    *len = (u32)(r->p - s);
    r->p++;
    return s;
}

static u32
read_u32(Reader *r)
{
    skip_ws(r);
    if (*r->p < '0' || *r->p > '9') {
        fail(r, "expected a number");
    }

    u64 n = 0;
    while (*r->p >= '0' && *r->p <= '9') {
        n = n * 10 + (u64)(*r->p - '0');
        if (n > UINT32_MAX) {
            fail(r, "number too large");
        }
        r->p++;
    }
    return (u32)n;
}

static bool
read_bool(Reader *r)
{
    skip_ws(r);
    if (strncmp(r->p, "true", 4) == 0) {
        r->p += 4;
        return true;
    }
    if (strncmp(r->p, "false", 5) == 0) {
        r->p += 5;
        return false;
    }
    fail(r, "expected true or false");
}

static u64
read_id(Reader *r)
{
    u32 len;
    const char *s = read_string(r, &len);
    if (len < 3 || s[0] != '0' || s[1] != 'x') {
        fail(r, "expected an id");
    }

    u64 id = 0;
    for (u32 i = 2; i < len; i++) {
        char c = s[i];
        u32 digit = c >= 'a' ? (u32)(c - 'a' + 10) : (u32)(c - '0');
        id = id << 4 | digit;
    }
    assert(id >> 56 == 0);
    return id | r->tag;
}

static u32
read_scalar(Reader *r)
{
    skip_ws(r);
    if (*r->p == '"') {
        u32 len;
        const char *s = read_string(r, &len);
        return strings_intern(&r->ast->strings, s, len);
    }

    const char *s = r->p;
    while ((*r->p >= '0' && *r->p <= '9') || (*r->p >= 'a' && *r->p <= 'z') ||
           *r->p == '-' || *r->p == '+' || *r->p == '.' || *r->p == 'E') {
        r->p++;
    }
    if (r->p == s) {
        fail(r, "expected a value");
    }
    return strings_intern(&r->ast->strings, s, (u32)(r->p - s));
}

static const char *
object_key(Reader *r, bool *first, u32 *len)
{
    skip_ws(r);
    if (*r->p == '}') {
        r->p++;
        return NULL;
    }
    if (!*first) {
        if (*r->p != ',') {
            fail(r, "expected , or }");
        }
        r->p++;
    }
    *first = false;

    const char *k = read_string(r, len);
    expect(r, ':');
    skip_ws(r);
    return k;
}

static bool
array_item(Reader *r, bool *first)
{
    skip_ws(r);
    if (*r->p == ']') {
        r->p++;
        return false;
    }
    if (!*first) {
        if (*r->p != ',') {
            fail(r, "expected , or ]");
        }
        r->p++;
    }
    *first = false;
    skip_ws(r);
    return true;
}

static bool
key_is(const char *k, u32 len, const char *name)
{
    return strlen(name) == len && memcmp(k, name, len) == 0;
}

static void skip_value(Reader *r);
static void parse_loc(Reader *r, Loc *loc);

static void
skip_object(Reader *r)
{
    expect(r, '{');
    bool first = true;
    u32 len;
    const char *k;
    while ((k = object_key(r, &first, &len))) {
        if (key_is(k, len, "loc") || key_is(k, len, "begin") || key_is(k, len, "end")) {
            Loc loc;
            parse_loc(r, &loc);
        } else {
            skip_value(r);
        }
    }
}

static void
skip_value(Reader *r)
{
    skip_ws(r);
    char c = *r->p;
    if (c == '{') {
        skip_object(r);
    } else if (c == '[') {
        r->p++;
        bool first = true;
        while (array_item(r, &first)) {
            skip_value(r);
        }
    } else if (c == '"') {
        u32 len;
        read_string(r, &len);
    } else if (c == 't' || c == 'f') {
        read_bool(r);
    } else if (strncmp(r->p, "null", 4) == 0) {
        r->p += 4;
    } else {
        read_scalar(r);
    }
}

static Place
parse_place(Reader *r, bool *macro_arg)
{
    expect(r, '{');
    Place at = { r->last_file, 0, r->last_line };
    bool first = true;
    u32 len;
    const char *k;
    while ((k = object_key(r, &first, &len))) {
        if (key_is(k, len, "offset")) {
            at.offset = read_u32(r);
        } else if (key_is(k, len, "file")) {
            u32 n;
            const char *s = read_string(r, &n);
            at.file = strings_intern(&r->ast->strings, s, n);
            r->last_file = at.file;
        } else if (key_is(k, len, "line")) {
            at.line = read_u32(r);
            r->last_line = at.line;
        } else if (key_is(k, len, "isMacroArgExpansion")) {
            *macro_arg = read_bool(r);
        } else {
            skip_value(r);
        }
    }
    return at;
}

static void
parse_loc(Reader *r, Loc *loc)
{
    skip_ws(r);
    const char *start = r->p;
    *loc = (Loc){ 0 };

    expect(r, '{');
    bool first = true;
    u32 len;
    const char *k = object_key(r, &first, &len);
    if (k == NULL) {
        loc->expansion = loc->spelling = (Place){ r->last_file, 0, r->last_line };
        return;
    }
    if (!key_is(k, len, "spellingLoc")) {
        r->p = start;
        bool unused = false;
        loc->expansion = loc->spelling = parse_place(r, &unused);
        return;
    }

    loc->spelling = parse_place(r, &loc->macro_arg);
    loc->macro = true;
    while ((k = object_key(r, &first, &len))) {
        if (key_is(k, len, "expansionLoc")) {
            loc->expansion = parse_place(r, &loc->macro_arg);
        } else {
            skip_value(r);
        }
    }
}

static u32
parse_type(Reader *r, u32 *desugared)
{
    expect(r, '{');
    u32 type = 0;
    bool first = true;
    u32 len;
    const char *k;
    while ((k = object_key(r, &first, &len))) {
        if (key_is(k, len, "qualType")) {
            type = read_scalar(r);
        } else if (key_is(k, len, "desugaredQualType") && desugared) {
            *desugared = read_scalar(r);
        } else {
            skip_value(r);
        }
    }
    return type;
}

static u64
parse_ref(Reader *r)
{
    expect(r, '{');
    u64 id = 0;
    bool first = true;
    u32 len;
    const char *k;
    while ((k = object_key(r, &first, &len))) {
        if (key_is(k, len, "id")) {
            id = read_id(r);
        } else {
            skip_value(r);
        }
    }
    return id;
}

static u32
add_node(Ast *a)
{
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 4096;
        a->kinds = resize(a->kinds, a->cap, sizeof *a->kinds);
        a->ends = resize(a->ends, a->cap, sizeof *a->ends);
        a->ids = resize(a->ids, a->cap, sizeof *a->ids);
        a->files = resize(a->files, a->cap, sizeof *a->files);
        a->offsets = resize(a->offsets, a->cap, sizeof *a->offsets);
        a->lines = resize(a->lines, a->cap, sizeof *a->lines);
        a->spellings = resize(a->spellings, a->cap, sizeof *a->spellings);
        a->range_begins = resize(a->range_begins, a->cap, sizeof *a->range_begins);
        a->range_ends = resize(a->range_ends, a->cap, sizeof *a->range_ends);
        a->names = resize(a->names, a->cap, sizeof *a->names);
        a->types = resize(a->types, a->cap, sizeof *a->types);
        a->desugared = resize(a->desugared, a->cap, sizeof *a->desugared);
        a->details = resize(a->details, a->cap, sizeof *a->details);
        a->arg_types = resize(a->arg_types, a->cap, sizeof *a->arg_types);
        a->result_types = resize(a->result_types, a->cap, sizeof *a->result_types);
        a->refs = resize(a->refs, a->cap, sizeof *a->refs);
        a->flags = resize(a->flags, a->cap, sizeof *a->flags);
    }

    u32 i = a->count++;
    a->kinds[i] = a->ends[i] = a->files[i] = a->offsets[i] = a->lines[i] = 0;
    a->spellings[i] = a->range_begins[i] = a->range_ends[i] = 0;
    a->names[i] = a->types[i] = a->desugared[i] = a->details[i] = 0;
    a->arg_types[i] = a->result_types[i] = a->flags[i] = 0;
    a->ids[i] = a->refs[i] = 0;
    return i;
}

static void parse_node(Reader *r, u32 flags);

static void
parse_children(Reader *r, u32 flags)
{
    expect(r, '[');
    bool first = true;
    while (array_item(r, &first)) {
        parse_node(r, flags);
    }
}

static void
set_place(Ast *a, u32 i, Loc loc)
{
    a->files[i] = loc.expansion.file;
    a->offsets[i] = loc.expansion.offset;
    a->lines[i] = loc.expansion.line;
    a->spellings[i] = loc.spelling.offset;
    a->flags[i] |= loc.macro ? AST_MACRO : 0;
    a->flags[i] |= loc.macro_arg ? AST_MACRO_ARG : 0;
}

static void
parse_range(Reader *r, u32 i, bool has_loc)
{
    Ast *a = r->ast;
    expect(r, '{');
    bool first = true;
    u32 len;
    const char *k;
    while ((k = object_key(r, &first, &len))) {
        Loc loc;
        if (key_is(k, len, "begin")) {
            parse_loc(r, &loc);
            a->range_begins[i] = loc.expansion.offset;
            if (!has_loc) {
                set_place(a, i, loc);
            }
        } else if (key_is(k, len, "end")) {
            parse_loc(r, &loc);
            a->range_ends[i] = loc.expansion.offset;
        } else {
            skip_value(r);
        }
    }
}

static u32
flag_of(const char *k, u32 len)
{
    static const struct
    {
        const char *key;
        u32 flag;
    } flags[] = {
        { "isArrow", AST_ARROW },
        { "isPostfix", AST_POSTFIX },
        { "hasElse", AST_HAS_ELSE },
        { "isImplicit", AST_IMPLICIT },
        { "inline", AST_INLINE },
        { "isUsed", AST_USED },
        { "completeDefinition", AST_COMPLETE },
        { "variadic", AST_VARIADIC },
        { "isPartOfExplicitCast", AST_IN_CAST },
        { "isReferenced", AST_REFERENCED },
        { "isTagOwned", AST_TAG_OWNED },
        { "selected", AST_SELECTED },
    };
    for (u32 f = 0; f < sizeof flags / sizeof flags[0]; f++) {
        if (key_is(k, len, flags[f].key)) {
            return flags[f].flag;
        }
    }
    return 0;
}

static void
move_node(Ast *a, u32 to, u32 from)
{
    a->kinds[to] = a->kinds[from];
    a->ends[to] = a->ends[from];
    a->ids[to] = a->ids[from];
    a->files[to] = a->files[from];
    a->offsets[to] = a->offsets[from];
    a->lines[to] = a->lines[from];
    a->spellings[to] = a->spellings[from];
    a->range_begins[to] = a->range_begins[from];
    a->range_ends[to] = a->range_ends[from];
    a->names[to] = a->names[from];
    a->types[to] = a->types[from];
    a->desugared[to] = a->desugared[from];
    a->details[to] = a->details[from];
    a->arg_types[to] = a->arg_types[from];
    a->result_types[to] = a->result_types[from];
    a->refs[to] = a->refs[from];
    a->flags[to] = a->flags[from];
}

static void
select_generic(Ast *a, u32 i, u32 flags)
{
    u32 chosen = 0;
    for (u32 c = i + 1; c < a->ends[i]; c = a->ends[c]) {
        if (a->flags[c] & AST_SELECTED) {
            chosen = c;
        }
    }
    assert(chosen != 0);

    u32 e = chosen + 1;
    while (a->ends[e] < a->ends[chosen]) {
        e = a->ends[e];
    }

    u32 shift = e - i;
    u32 end = a->ends[e];
    for (u32 k = e; k < end; k++) {
        move_node(a, k - shift, k);
        a->ends[k - shift] -= shift;
    }
    a->flags[i] |= flags;
    a->count = end - shift;
}

static void
parse_node(Reader *r, u32 flags)
{
    Ast *a = r->ast;
    expect(r, '{');
    u32 i = add_node(a);
    a->flags[i] = flags;

    bool has_loc = false;
    bool first = true;
    u32 len;
    const char *k;
    while ((k = object_key(r, &first, &len))) {
        u32 flag = flag_of(k, len);
        if (flag != 0) {
            if (read_bool(r)) {
                a->flags[i] |= flag;
            }
        } else if (key_is(k, len, "id")) {
            a->ids[i] = read_id(r);
        } else if (key_is(k, len, "kind")) {
            a->kinds[i] = read_scalar(r);
        } else if (key_is(k, len, "loc")) {
            Loc loc;
            parse_loc(r, &loc);
            set_place(a, i, loc);
            has_loc = true;
        } else if (key_is(k, len, "range")) {
            parse_range(r, i, has_loc);
        } else if (key_is(k, len, "type")) {
            u32 desugared = 0;
            a->types[i] = parse_type(r, &desugared);
            a->desugared[i] = desugared;
        } else if (key_is(k, len, "name")) {
            a->names[i] = read_scalar(r);
        } else if (key_is(k, len, "valueCategory")) {
            u32 n;
            const char *s = read_string(r, &n);
            a->flags[i] |= n == 6 && memcmp(s, "lvalue", 6) == 0 ? AST_LVALUE : 0;
        } else if (key_is(k, len, "castKind") || key_is(k, len, "opcode") ||
                   key_is(k, len, "value") || key_is(k, len, "storageClass") ||
                   key_is(k, len, "tagUsed")) {
            a->details[i] = read_scalar(r);
        } else if (key_is(k, len, "init")) {
            skip_value(r);
            a->flags[i] |= AST_HAS_INIT;
        } else if (key_is(k, len, "nonOdrUseReason")) {
            u32 n;
            const char *s = read_string(r, &n);
            a->flags[i] |= n == 11 && memcmp(s, "unevaluated", 11) == 0 ? AST_UNEVALUATED : 0;
        } else if (key_is(k, len, "ownedTagDecl")) {
            a->refs[i] = parse_ref(r);
            a->flags[i] |= AST_TAG_OWNED;
        } else if (key_is(k, len, "referencedDecl") || key_is(k, len, "decl") ||
                   key_is(k, len, "field")) {
            a->refs[i] = parse_ref(r);
        } else if (key_is(k, len, "referencedMemberDecl") ||
                   key_is(k, len, "previousDecl")) {
            a->refs[i] = read_id(r);
        } else if (key_is(k, len, "argType") || key_is(k, len, "computeLHSType")) {
            a->arg_types[i] = parse_type(r, NULL);
        } else if (key_is(k, len, "computeResultType")) {
            a->result_types[i] = parse_type(r, NULL);
        } else if (key_is(k, len, "inner")) {
            parse_children(r, 0);
        } else if (key_is(k, len, "array_filler")) {
            parse_children(r, AST_FILLER);
        } else {
            skip_value(r);
        }
    }
    a->ends[i] = a->count;

    if (a->kinds[i] == r->generic_kind) {
        select_generic(a, i, flags);
    }
}

bool
ast_load(Ast *ast, const char *path, FILE *err)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        fprintf(err, "translate: %s: cannot open\n", path);
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }

    size_t size = (size_t)st.st_size;
    char *text = malloc(size + 1);
    for (size_t got = 0; got < size;) {
        ssize_t n = read(fd, text + got, size - got);
        if (n <= 0) {
            fprintf(err, "translate: %s: cannot read\n", path);
            close(fd);
            free(text);
            return false;
        }
        got += (size_t)n;
    }
    close(fd);
    text[size] = 0;

    if (ast->units == 0) {
        *ast = (Ast){ 0 };
        strings_intern(&ast->strings, "", 0);
    }
    assert(ast->units < 256);
    u32 generic_kind = strings_intern(&ast->strings, "GenericSelectionExpr", 20);
    Reader r = { .p = text, .base = text, .ast = ast, .tag = (u64)ast->units << 56,
                 .generic_kind = generic_kind, .err = err };
    parse_node(&r, 0);
    ast->units++;
    skip_ws(&r);
    if (*r.p != 0) {
        fail(&r, "text after the translation unit");
    }
    free(text);
    return true;
}

void
ast_free(Ast *a)
{
    free(a->kinds);
    free(a->ends);
    free(a->ids);
    free(a->files);
    free(a->offsets);
    free(a->lines);
    free(a->spellings);
    free(a->range_begins);
    free(a->range_ends);
    free(a->names);
    free(a->types);
    free(a->desugared);
    free(a->details);
    free(a->arg_types);
    free(a->result_types);
    free(a->refs);
    free(a->flags);
    free(a->strings.bytes);
    free(a->strings.starts);
    free(a->strings.lens);
    free(a->strings.slots);
    *a = (Ast){ 0 };
}
