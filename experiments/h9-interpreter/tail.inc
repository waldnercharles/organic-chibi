#ifndef TAIL_TYPES
#define TAIL_TYPES
typedef struct
{
    Machine *m;
    const Function *functions;
    u8 *stack;
    u64 stack_size;
    Return *returns;
    u32 max_depth;
} TailContext;

#define TAIL_PARAMS                                                            \
    const Inst *ip, u8 *frame, const Inst *code, u8 *heap, u64 heap_size, u64 budget, u32 depth, TailContext *ctx
#define TAIL_CC __attribute__((preserve_none))

typedef TAIL_CC Status (*TailHandler)(TAIL_PARAMS);
#endif

#define TAIL_ARGS ip, frame, code, heap, heap_size, budget, depth, ctx
#define TAIL_PASTE2(a, b) a##b
#define TAIL_PASTE(a, b) TAIL_PASTE2(a, b)
#define TAIL_FN(name) TAIL_PASTE(TAIL_PREFIX, name)

#define TAIL_DECLARE(name) static TAIL_CC Status TAIL_FN(name)(TAIL_PARAMS);
H9_OPS(TAIL_DECLARE)

#define TAIL_ENTRY(name) TAIL_FN(name),
#if TAIL_SLOTS
static TailHandler TAIL_FN(slot_table)[NOPS] = { H9_OPS(TAIL_ENTRY) };
static u32 TAIL_FN(slot_of)[NOPS];
#define TAIL_TARGET(op) TAIL_FN(slot_table)[TAIL_FN(slot_of)[op]]
#else
static const TailHandler TAIL_FN(handlers)[NOPS] = { H9_OPS(TAIL_ENTRY) };
#define TAIL_TARGET(op) TAIL_FN(handlers)[op]
#endif

#define TAIL_DISPATCH()                                                        \
    do {                                                                       \
        [[clang::musttail]] return TAIL_TARGET(ip->op)(TAIL_ARGS);             \
    } while (0)

#define TAIL_NEXT()                                                            \
    do {                                                                       \
        ip++;                                                                  \
        TAIL_DISPATCH();                                                       \
    } while (0)

#define TAIL_FAIL(status)                                                      \
    do {                                                                       \
        ctx->m->fault_pc = (u32)(ip - code);                                   \
        return (status);                                                       \
    } while (0)

#define TAIL_BINARY(name, expr)                                                \
    static TAIL_CC Status TAIL_FN(name)(TAIL_PARAMS)                           \
    {                                                                          \
        Inst in = *ip;                                                         \
        u64 a = load_u64(frame + in.a), b = load_u64(frame + in.b);            \
        store_u64(frame + in.dst, (expr));                                     \
        TAIL_NEXT();                                                           \
    }

TAIL_BINARY(OP_ADD_I64, a + b)
TAIL_BINARY(OP_SUB_I64, a - b)
TAIL_BINARY(OP_MUL_I64, a * b)
TAIL_BINARY(OP_XOR_I64, a ^ b)
TAIL_BINARY(OP_SHR_U64, a >> (b & 63))

static TAIL_CC Status
TAIL_FN(OP_MOV_I64)(TAIL_PARAMS)
{
    Inst in = *ip;
    store_u64(frame + in.dst, load_u64(frame + in.a));
    TAIL_NEXT();
}

static TAIL_CC Status
TAIL_FN(OP_LT_I64)(TAIL_PARAMS)
{
    Inst in = *ip;
    frame[in.dst] = (int64_t)load_u64(frame + in.a) < (int64_t)load_u64(frame + in.b);
    TAIL_NEXT();
}

static TAIL_CC Status
TAIL_FN(OP_JUMP)(TAIL_PARAMS)
{
    ip = code + ip->dst;
    TAIL_DISPATCH();
}

static TAIL_CC Status
TAIL_FN(OP_JUMP_UNLESS)(TAIL_PARAMS)
{
    Inst in = *ip;
    if (!frame[in.a]) {
        ip = code + in.dst;
        TAIL_DISPATCH();
    }
    TAIL_NEXT();
}

static TAIL_CC Status
TAIL_FN(OP_LOOP_IF)(TAIL_PARAMS)
{
    Inst in = *ip;
    if (frame[in.a]) {
        if (UNLIKELY(budget == 0)) {
            TAIL_FAIL(STATUS_OUT_OF_BUDGET);
        }
        budget--;
        ip = code + in.dst;
        TAIL_DISPATCH();
    }
    TAIL_NEXT();
}

static TAIL_CC Status
TAIL_FN(OP_LOAD_U8)(TAIL_PARAMS)
{
    Inst in = *ip;
    u64 address = load_u64(frame + in.a);
    if (UNLIKELY(address >= heap_size)) {
        TAIL_FAIL(STATUS_OUT_OF_BOUNDS);
    }
    store_u64(frame + in.dst, heap[address]);
    TAIL_NEXT();
}

static TAIL_CC Status
TAIL_FN(OP_STORE_U8)(TAIL_PARAMS)
{
    Inst in = *ip;
    u64 address = load_u64(frame + in.a);
    if (UNLIKELY(address >= heap_size)) {
        TAIL_FAIL(STATUS_OUT_OF_BOUNDS);
    }
    heap[address] = (u8)load_u64(frame + in.b);
    TAIL_NEXT();
}

static TAIL_CC Status
TAIL_FN(OP_CALL)(TAIL_PARAMS)
{
    Inst in = *ip;
    const Function *callee = &ctx->functions[in.dst];
    u8 *callee_frame = frame + in.a;
    if (UNLIKELY(depth == ctx->max_depth ||
                 (u64)(callee_frame - ctx->stack) + callee->frame_size > ctx->stack_size)) {
        TAIL_FAIL(STATUS_STACK_OVERFLOW);
    }
    if (UNLIKELY(budget == 0)) {
        TAIL_FAIL(STATUS_OUT_OF_BUDGET);
    }
    budget--;

    ctx->returns[depth++] = (Return){ code, (u32)(ip + 1 - code), (u32)(frame - ctx->stack) };
    copy_consts(callee_frame, callee);

    code = callee->code;
    frame = callee_frame;
    ip = code;
    TAIL_DISPATCH();
}

static TAIL_CC Status
TAIL_FN(OP_RET_I64)(TAIL_PARAMS)
{
    Inst in = *ip;
    store_u64(frame, load_u64(frame + in.a));
    if (depth == 0) {
        ctx->m->result = load_u64(frame);
        return STATUS_OK;
    }

    Return r = ctx->returns[--depth];
    code = r.code;
    ip = code + r.pc;
    frame = ctx->stack + r.base;
    TAIL_DISPATCH();
}

static NOINLINE Status
TAIL_FN(interpret)(Machine *m, u64 arg)
{
#if TAIL_SLOTS
    for (u32 i = 0; i < NOPS; i++) {
        TAIL_FN(slot_of)[i] = i;
    }
#endif
    TailContext ctx = { m, m->functions, m->stack, m->stack_size, m->returns, m->max_depth };
    const Function *f = &m->functions[0];
    u8 *frame = m->stack;
    store_u64(frame, arg);
    copy_consts(frame, f);

    const Inst *code = f->code;
    Status s = TAIL_TARGET(code->op)(code, frame, code, m->heap, m->heap_size, m->budget, 0, &ctx);
    return s;
}

#undef TAIL_ARGS
#undef TAIL_PASTE2
#undef TAIL_PASTE
#undef TAIL_FN
#undef TAIL_TARGET
#undef TAIL_PREFIX
#undef TAIL_SLOTS
#undef TAIL_DECLARE
#undef TAIL_ENTRY
#undef TAIL_DISPATCH
#undef TAIL_NEXT
#undef TAIL_FAIL
#undef TAIL_BINARY
