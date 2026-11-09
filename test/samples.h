#ifndef CHIBI_SAMPLES_H
#define CHIBI_SAMPLES_H

#include "../src/common.h"

#define SAMPLES_DIR "/tmp/chibi-samples"

typedef struct
{
    char *path;
    char *text;
    size_t len;
} Sample;

typedef struct
{
    Sample *files;
    u32 count, cap;
    size_t total_len, max_len;
} Samples;

Sample sample_read(const char *path);

void sample_free(Sample s);

Samples samples_load(const char *dir);

void samples_free(Samples s);

#endif
