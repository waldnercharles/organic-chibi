#ifndef CHIBI_SAMPLES_H
#define CHIBI_SAMPLES_H

#include "../src/common.h"

typedef struct
{
    char *path;
    char *text;
    size_t len;
} Sample;

Sample sample_read(const char *path);
void sample_free(Sample s);

#endif
