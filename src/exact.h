#ifndef CHIBI_EXACT_H
#define CHIBI_EXACT_H

#include "common.h"

#include <stdio.h>

#define EXACT_INT_BITS 512
#define EXACT_FRACTION_BITS 4095

typedef enum
{
    EXACT_OK,
    EXACT_TOO_LARGE,
    EXACT_DIVIDE_BY_ZERO,
} ExactStatus;

typedef enum
{
    EXACT_ADD,
    EXACT_SUB,
    EXACT_MUL,
    EXACT_QUO,
    EXACT_DIV,
    EXACT_REM,
    EXACT_AND,
    EXACT_OR,
    EXACT_XOR,
    EXACT_SHL,
    EXACT_SHR,
} ExactOp;

typedef struct
{
    int64_t small;
    u16 num;
    u16 den;
    bool big;
    bool negative;
} Exact;

static_assert(sizeof(Exact) == 16, "one constant value is 16 bytes; larger values keep their limbs in Exacts.limbs");

enum
{
    EXACT_ZERO,
    EXACT_ONE,
    EXACT_MINUS_ONE,
};

typedef struct
{
    Exact *values;
    u32 count;
    u32 cap;
    u64 *limbs;
    u32 nlimbs;
    u32 limbs_cap;
} Exacts;

void exacts_init(Exacts *e);

void exacts_free(Exacts *e);

u32 exact_int(Exacts *e, int64_t v);

u32 exact_parse(Exacts *e, const char *text, u32 limit, ExactStatus *status);

u32 exact_binary(Exacts *e, ExactOp op, u32 a, u32 b, u32 limit, ExactStatus *status);

int exact_compare(const Exacts *e, u32 a, u32 b);

int exact_sign(const Exacts *e, u32 a);

bool exact_is_int(const Exacts *e, u32 a);

bool exact_fits(const Exacts *e, u32 a, u32 bits, bool is_signed);

u64 exact_bits(const Exacts *e, u32 a);

u64 exact_bits_high(const Exacts *e, u32 a);

u64 exact_float(const Exacts *e, u32 a, u32 width, ExactStatus *status);

u32 exact_from_float(Exacts *e, u64 bits, u32 width);

void exact_print(FILE *out, const Exacts *e, u32 a);

#endif
