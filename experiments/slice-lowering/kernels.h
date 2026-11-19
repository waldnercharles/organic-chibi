#include <stdint.h>

typedef struct
{
    const int64_t *ptr;
    int64_t len;
} Ints;

typedef struct
{
    const uint8_t *ptr;
    int64_t len;
} Bytes;

int64_t sum_a(const Ints *xs);
int64_t sum_c(Ints xs);
int64_t sum_ref(Ints xs);

int64_t count_a(const Bytes *s, uint8_t c);
int64_t count_c(Bytes s, uint8_t c);
int64_t count_ref(Bytes s, uint8_t c);

int64_t total_a(const Bytes *s);
int64_t total_a1(Bytes s);
int64_t total_c(Bytes s);
int64_t total_ref(Bytes s);

int64_t calibrate(int64_t n);
