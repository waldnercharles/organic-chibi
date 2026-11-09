#define _DEFAULT_SOURCE

#include "samples.h"

#include "../src/lexer.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static void
walk(Samples *s, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            walk(s, path);
            continue;
        }

        size_t n = strlen(e->d_name);
        if (!S_ISREG(st.st_mode) || n <= 6 || strcmp(e->d_name + n - 6, ".chibi") != 0) {
            continue;
        }
        if (s->count == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 256;
            s->files = realloc(s->files, s->cap * sizeof *s->files);
        }
        Sample f = sample_read(path);
        s->files[s->count++] = f;
        s->total_len += f.len;
        s->max_len = f.len > s->max_len ? f.len : s->max_len;
    }
    closedir(d);
}

Samples
samples_load(const char *dir)
{
    Samples s = { 0 };
    walk(&s, dir);
    return s;
}

void
samples_free(Samples s)
{
    for (u32 i = 0; i < s.count; i++) {
        sample_free(s.files[i]);
    }
    free(s.files);
}
