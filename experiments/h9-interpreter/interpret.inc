static Status
INTERPRET(Machine *m, u64 arg)
{
    const Function *functions = m->functions;
    const Inst *code = functions[0].code;
    u8 *stack = m->stack;
    u8 *frame = stack;
    u64 stack_size = m->stack_size;
    Return *returns = m->returns;
    u32 max_depth = m->max_depth;
    u8 *heap = m->heap;
    u64 heap_size = m->heap_size;
    u64 budget = m->budget;
    u32 pc = 0, depth = 0;
    u64 ops = 0;
    Inst in;

    store_u64(frame, arg);
    copy_consts(frame, &functions[0]);

#if COUNTING
#define COUNT_OP ops++
#else
#define COUNT_OP (void)0
#endif

#define FAIL(status)                                                           \
    do {                                                                       \
        m->fault_pc = pc - 1;                                                  \
        m->ops = ops;                                                          \
        return (status);                                                       \
    } while (0)

#if THREADED
#define OP_LABEL(name) &&L_##name,
    static void *const targets[] = { H9_OPS(OP_LABEL) };

#define OP(name) L_##name:
#define NEXT                                                                   \
    do {                                                                       \
        in = code[pc++];                                                       \
        COUNT_OP;                                                              \
        goto *targets[in.op];                                                  \
    } while (0)

    NEXT;
#else
#define OP(name) case name:
#define NEXT continue

    for (;;) {
        in = code[pc++];
        COUNT_OP;
        switch (in.op) {
#endif

    OP(OP_MOV_I64)
    {
        store_u64(frame + in.dst, load_u64(frame + in.a));
        NEXT;
    }

    OP(OP_ADD_I64)
    {
        store_u64(frame + in.dst, load_u64(frame + in.a) + load_u64(frame + in.b));
        NEXT;
    }

    OP(OP_SUB_I64)
    {
        store_u64(frame + in.dst, load_u64(frame + in.a) - load_u64(frame + in.b));
        NEXT;
    }

    OP(OP_MUL_I64)
    {
        store_u64(frame + in.dst, load_u64(frame + in.a) * load_u64(frame + in.b));
        NEXT;
    }

    OP(OP_XOR_I64)
    {
        store_u64(frame + in.dst, load_u64(frame + in.a) ^ load_u64(frame + in.b));
        NEXT;
    }

    OP(OP_SHR_U64)
    {
        store_u64(frame + in.dst, load_u64(frame + in.a) >> (load_u64(frame + in.b) & 63));
        NEXT;
    }

    OP(OP_LT_I64)
    {
        frame[in.dst] = (int64_t)load_u64(frame + in.a) < (int64_t)load_u64(frame + in.b);
        NEXT;
    }

    OP(OP_JUMP)
    {
        pc = in.dst;
        NEXT;
    }

    OP(OP_JUMP_UNLESS)
    {
        if (!frame[in.a]) {
            pc = in.dst;
        }
        NEXT;
    }

    OP(OP_LOOP_IF)
    {
        if (frame[in.a]) {
            if (UNLIKELY(budget == 0)) {
                FAIL(STATUS_OUT_OF_BUDGET);
            }
            budget--;
            pc = in.dst;
        }
        NEXT;
    }

    OP(OP_LOAD_U8)
    {
        u64 address = load_u64(frame + in.a);
        if (UNLIKELY(address >= heap_size)) {
            FAIL(STATUS_OUT_OF_BOUNDS);
        }

        store_u64(frame + in.dst, heap[address]);
        NEXT;
    }

    OP(OP_STORE_U8)
    {
        u64 address = load_u64(frame + in.a);
        if (UNLIKELY(address >= heap_size)) {
            FAIL(STATUS_OUT_OF_BOUNDS);
        }

        heap[address] = (u8)load_u64(frame + in.b);
        NEXT;
    }

    OP(OP_CALL)
    {
        const Function *callee = &functions[in.dst];
        u8 *callee_frame = frame + in.a;
        if (UNLIKELY(depth == max_depth || (u64)(callee_frame - stack) + callee->frame_size > stack_size)) {
            FAIL(STATUS_STACK_OVERFLOW);
        }
        if (UNLIKELY(budget == 0)) {
            FAIL(STATUS_OUT_OF_BUDGET);
        }
        budget--;

        returns[depth++] = (Return){ code, pc, (u32)(frame - stack) };
        copy_consts(callee_frame, callee);

        code = callee->code;
        frame = callee_frame;
        pc = 0;
        NEXT;
    }

    OP(OP_RET_I64)
    {
        store_u64(frame, load_u64(frame + in.a));
        if (depth == 0) {
            m->result = load_u64(frame);
            m->ops = ops;
            return STATUS_OK;
        }

        Return r = returns[--depth];
        code = r.code;
        pc = r.pc;
        frame = stack + r.base;
        NEXT;
    }

#if !THREADED
        default:
            unreachable();
        }
    }
#endif
}

#undef COUNT_OP
#undef FAIL
#undef OP_LABEL
#undef OP
#undef NEXT
#undef INTERPRET
#undef THREADED
#undef COUNTING
