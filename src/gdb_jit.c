#include "gdb_jit.h"

JitDescriptor __jit_debug_descriptor = { 1, JIT_NOACTION, NULL, NULL };

NOINLINE __attribute__((used)) void
__jit_debug_register_code(void)
{
    __asm__ volatile("" ::: "memory");
}
