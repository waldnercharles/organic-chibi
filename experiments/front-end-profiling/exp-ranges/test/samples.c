#define _DEFAULT_SOURCE

#include "samples.h"

#include "../src/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

Sample
sample_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    void *text;
    if (posix_memalign(&text, (size_t)sysconf(_SC_PAGESIZE), len + LEXER_SOURCE_PADDING) != 0 ||
        fread(text, 1, len, f) != len) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    memset((char *)text + len, 0, LEXER_SOURCE_PADDING);

    return (Sample){ strdup(path), text, len };
}

void
sample_free(Sample s)
{
    free(s.path);
    free(s.text);
}
