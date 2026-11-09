#ifndef CHIBI_STR_H
#define CHIBI_STR_H

#include "arena.h"
#include "common.h"

#include <string.h>

typedef struct
{
    u64 hash;
    size_t len;
    const char *p;
} String;

void str_init(String *str, const char *s);

String str_slice(String str, size_t start, size_t len);

String str_concat(Arena *a, String x, String y);

bool str_eq(String a, String b);

char str_at(String str, size_t idx);

#endif
