#include "str.h"

#define XXH_STATIC_LINKING_ONLY
#include "../vendor/xxhash/xxhash.h"

#define str_hash(str, len) ((u64)XXH64(str, len, 0))

void
str_init(String *str, const char *s)
{
    str->p = s;
    str->len = strlen(s);
    str->hash = str_hash(str->p, str->len);
}

String
str_slice(String str, size_t start, size_t len)
{
    if (start > str.len) {
        return (String){ .p = "", .len = 0 };
    }
    if (len > str.len - start) {
        len = str.len - start;
    }
    const char *s = str.p + start;
    return (String){ .p = s, .len = len, .hash = str_hash(s, len) };
}

String
str_concat(Arena *a, String x, String y)
{
    size_t len = x.len + y.len;
    char *s = arena_alloc(a, len + 1, 1);
    if (s == NULL) {
        return (String){ .p = "", .len = 0 };
    }
    memcpy(s, x.p, x.len);
    memcpy(s + x.len, y.p, y.len);
    s[len] = '\0';
    return (String){ .p = s, .len = len, .hash = str_hash(s, len) };
}

bool
str_eq(String a, String b)
{
    if (a.len != b.len) {
        return false;
    }
    return memcmp(a.p, b.p, a.len) == 0;
}

char
str_at(String str, size_t idx)
{
    if (idx >= str.len) {
        return -1;
    }
    return str.p[idx];
}
