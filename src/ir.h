#ifndef CHIBI_IR_H
#define CHIBI_IR_H

#include "common.h"

// clang-format off
#define CHIBI_IR(X)                                                            \
    X(IR_LOAD, "load")                                                         \
    X(IR_ADD, "add")                                                           \
    X(IR_SUB, "sub")                                                           \
    X(IR_MUL, "mul")                                                           \
    X(IR_DIV, "div")                                                           \
    X(IR_REM, "rem")                                                           \
    X(IR_ADD_WRAP, "add-wrap")                                                 \
    X(IR_SUB_WRAP, "sub-wrap")                                                 \
    X(IR_MUL_WRAP, "mul-wrap")                                                 \
    X(IR_SHL, "shl")                                                           \
    X(IR_SHR, "shr")                                                           \
    X(IR_AND, "and")                                                           \
    X(IR_OR, "or")                                                             \
    X(IR_XOR, "xor")                                                           \
    X(IR_NEG, "neg")                                                           \
    X(IR_NEG_WRAP, "neg-wrap")                                                 \
    X(IR_NOT, "not")                                                           \
    X(IR_BIT_NOT, "bit-not")                                                   \
    X(IR_EQ, "eq")                                                             \
    X(IR_NE, "ne")                                                             \
    X(IR_LT, "lt")                                                             \
    X(IR_LE, "le")                                                             \
    X(IR_GT, "gt")                                                             \
    X(IR_GE, "ge")                                                             \
    X(IR_CONVERT, "convert")                                                   \
    X(IR_NARROW, "narrow")                                                     \
    X(IR_TRUNCATE, "truncate")                                                 \
    X(IR_CALL, "call")                                                         \
    X(IR_STORE, "store")                                                       \
    X(IR_DROP, "drop")                                                         \
    X(IR_RET, "ret")                                                           \
    X(IR_BLOCK, "block")                                                       \
    X(IR_LOOP, "loop")                                                         \
    X(IR_IF, "if")                                                             \
    X(IR_ELSE, "else")                                                         \
    X(IR_END, "end")                                                           \
    X(IR_EXIT, "exit")                                                         \
    X(IR_REPEAT, "repeat")                                                     \
    X(IR_NOP, "nop")                                                           \
    X(IR_SWITCH, "switch")                                                     \
    X(IR_CASE, "case")
// clang-format on

#define CHIBI_IR_KIND(name, text) name,
typedef enum
{
    CHIBI_IR(CHIBI_IR_KIND) NIR_KINDS,
} IrKind;

enum
{
    IR_CONST = 1u << 31,
};

typedef struct
{
    u32 name;
    u32 type;
    u32 node;
    u32 weight;
} IrSlot;

typedef struct
{
    u64 bits;
    u32 type;
} IrConst;

typedef struct
{
    u8 *kinds;
    u32 *types;
    u32 *as;
    u32 *bs;
    u32 count;
    u32 cap;
    u32 *extra;
    u32 nextra;
    u32 extra_cap;
    IrSlot *slots;
    u32 nslots;
    u32 slots_cap;
    IrConst *consts;
    u32 nconsts;
    u32 consts_cap;
} IrCode;

typedef struct
{
    u32 decl;
    u32 nparams;
    u32 first;
    u32 count;
    u32 first_extra;
    u32 nextra;
    u32 first_slot;
    u32 nslots;
    u32 first_const;
    u32 nconsts;
    u32 calls;
} IrFunc;

typedef struct
{
    IrCode code;
    IrFunc *funcs;
    u32 nfuncs;
    u32 funcs_cap;
} Ir;

#endif
