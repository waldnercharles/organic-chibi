#ifndef CHIBI_GDB_JIT_H
#define CHIBI_GDB_JIT_H

#include "common.h"

enum
{
    JIT_NOACTION = 0,
    JIT_REGISTER_FN = 1,
    JIT_UNREGISTER_FN = 2,
};

typedef struct JitCodeEntry
{
    struct JitCodeEntry *next_entry;
    struct JitCodeEntry *prev_entry;
    const u8 *symfile_addr;
    u64 symfile_size;
} JitCodeEntry;

typedef struct
{
    u32 version;
    u32 action_flag;
    JitCodeEntry *relevant_entry;
    JitCodeEntry *first_entry;
} JitDescriptor;

extern JitDescriptor __jit_debug_descriptor;

void __jit_debug_register_code(void);

#endif
