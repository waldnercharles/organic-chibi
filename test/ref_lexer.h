#ifndef CHIBI_REF_LEXER_H
#define CHIBI_REF_LEXER_H

#include "../src/lexer.h"

#define REF_BUCKETS 4096

typedef struct RefName
{
    const char *text;
    int len;
    u32 id;
    struct RefName *next;
} RefName;

typedef struct
{
    RefName *bucket[REF_BUCKETS];
    u32 next_id;
    const char *error;
    int error_pos;
} RefLexer;

RefLexer *ref_lexer_new(void);
void ref_lexer_free(RefLexer *rl);

Token ref_next(RefLexer *rl, const char *src, int pos);

#endif
