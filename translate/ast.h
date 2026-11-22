#ifndef CHIBI_TRANSLATE_AST_H
#define CHIBI_TRANSLATE_AST_H

#include "../src/common.h"

#include <stdio.h>

typedef struct
{
    char *bytes;
    u64 used;
    u64 bytes_cap;
    u32 *starts;
    u32 *lens;
    u32 count;
    u32 cap;
    u32 *slots;
    u32 mask;
} Strings;

enum
{
    AST_LVALUE = 1 << 0,
    AST_ARROW = 1 << 1,
    AST_POSTFIX = 1 << 2,
    AST_HAS_ELSE = 1 << 3,
    AST_IMPLICIT = 1 << 4,
    AST_INLINE = 1 << 5,
    AST_USED = 1 << 6,
    AST_COMPLETE = 1 << 7,
    AST_VARIADIC = 1 << 8,
    AST_FILLER = 1 << 9,
    AST_MACRO = 1 << 10,
    AST_MACRO_ARG = 1 << 11,
    AST_HAS_INIT = 1 << 12,
    AST_IN_CAST = 1 << 13,
    AST_REFERENCED = 1 << 14,
};

typedef struct
{
    u32 count;
    u32 cap;
    u32 *kinds;
    u32 *ends;
    u64 *ids;
    u32 *files;
    u32 *offsets;
    u32 *lines;
    u32 *spellings;
    u32 *range_begins;
    u32 *range_ends;
    u32 *names;
    u32 *types;
    u32 *desugared;
    u32 *details;
    u32 *arg_types;
    u32 *result_types;
    u64 *refs;
    u32 *flags;
    u32 units;
    Strings strings;
} Ast;

enum
{
    STRINGS_MISSING = UINT32_MAX,
};

u32 strings_find(const Strings *s, const char *text, u32 len);
u32 strings_intern(Strings *s, const char *text, u32 len);

static inline const char *
strings_text(const Strings *s, u32 id)
{
    return s->bytes + s->starts[id];
}

static inline u32
strings_len(const Strings *s, u32 id)
{
    return s->lens[id];
}

bool ast_load(Ast *ast, const char *path, FILE *err);
void ast_free(Ast *ast);

#endif
