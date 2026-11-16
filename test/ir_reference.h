#ifndef CHIBI_IR_REFERENCE_H
#define CHIBI_IR_REFERENCE_H

#include "../src/arm64.h"

typedef struct
{
    const Checker *c;
    u32 *ends;
    u32 *elses;
    u32 *opens;
    u64 steps;
} IrReference;

void ir_reference_init(IrReference *r, const Checker *c);
int ir_reference_run(IrReference *r, u32 decl, const u64 *args, u64 *result);
void ir_reference_free(IrReference *r);

#endif
