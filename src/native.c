#define _DEFAULT_SOURCE

#include "native.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

static void
jit_write(bool enable)
{
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(enable ? 0 : 1);
#else
    (void)enable;
#endif
}

bool
native_begin(Native *n, const Checker *c, size_t code_size, bool executable)
{
    *n = (Native){ 0 };
    const Ir *ir = &c->ir;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t slots = (size_t)c->decls.count + 1;
    size_t table = (slots * sizeof(u64) + page - 1) & ~(page - 1);
    size_t code = table + (((size_t)ir->ndata + page - 1) & ~(page - 1));
    size_t size = code + ((code_size + page - 1) & ~(page - 1));
    if (size == code) {
        size += page;
    }

    int prot = PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0);
    int flags = MAP_PRIVATE | MAP_ANON;
#if defined(__APPLE__) && defined(__aarch64__)
    flags |= executable ? MAP_JIT : 0;
#endif
    void *memory = mmap(NULL, size, prot, flags, -1, 0);
    if (memory == MAP_FAILED) {
        n->error = "can't map memory for machine code";
        return false;
    }

    n->memory = memory;
    n->size = size;
    n->entries = memory;
    n->nentries = c->decls.count;
    n->data = n->memory + table;
    n->code = n->memory + code;

    if (ir->globals_size) {
        size_t globals_size = ((size_t)ir->globals_size + page - 1) & ~(page - 1);
        void *globals = mmap(NULL, globals_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (globals == MAP_FAILED) {
            n->error = "can't map memory for globals";
            return false;
        }
        n->globals = globals;
        n->globals_size = globals_size;
        if (ir->nglobals) {
            memcpy(n->globals, ir->globals, ir->nglobals);
            ir_relocate(n->globals, ir->global_relocs, n->data);
        }
    }

    jit_write(true);
    if (ir->ndata) {
        memcpy(n->data, ir->data, ir->ndata);
        ir_relocate(n->data, ir->data_relocs, n->data);
    }
    return true;
}

void
native_end(Native *n, size_t code_size)
{
    assert(n->code + code_size <= n->memory + n->size);
    n->code_size = code_size;
    jit_write(false);

#if defined(__APPLE__) && defined(__aarch64__)
    sys_icache_invalidate(n->code, code_size);
#elif defined(__aarch64__)
    __builtin___clear_cache((char *)n->code, (char *)(n->code + code_size));
#endif
}

void *
native_entry(const Native *n, u32 decl)
{
    return (void *)(uintptr_t)n->entries[decl];
}

void *
native_export(const Native *n, u32 i)
{
    assert(i < n->nexports);
    return (void *)(uintptr_t)n->exports[i];
}

void
native_replace(Native *n, u32 decl, void *entry)
{
    jit_write(true);
    n->entries[decl] = (u64)(uintptr_t)entry;
    jit_write(false);
}

void
native_set_context(Native *n, void *context)
{
    jit_write(true);
    n->entries[n->nentries] = (u64)(uintptr_t)context;
    jit_write(false);
}

void
native_describe(Native *n, u8 *symfile, size_t size)
{
    JitCodeEntry *entry = malloc(sizeof *entry);
    JitCodeEntry *first = __jit_debug_descriptor.first_entry;
    *entry = (JitCodeEntry){ .next_entry = first,
                             .prev_entry = NULL,
                             .symfile_addr = symfile,
                             .symfile_size = size };
    if (first) {
        first->prev_entry = entry;
    }
    __jit_debug_descriptor.first_entry = entry;
    __jit_debug_descriptor.relevant_entry = entry;
    __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
    __jit_debug_register_code();

    n->symfile = symfile;
    n->symfile_size = size;
    n->debugger = entry;
}

static void
forget_symfile(JitCodeEntry *entry)
{
    JitCodeEntry *prev = entry->prev_entry;
    JitCodeEntry *next = entry->next_entry;
    if (prev) {
        prev->next_entry = next;
    } else {
        __jit_debug_descriptor.first_entry = next;
    }
    if (next) {
        next->prev_entry = prev;
    }
    __jit_debug_descriptor.relevant_entry = entry;
    __jit_debug_descriptor.action_flag = JIT_UNREGISTER_FN;
    __jit_debug_register_code();
    free(entry);
}

void
native_free(Native *n)
{
    if (n->debugger) {
        forget_symfile(n->debugger);
    }
    free(n->symfile);
    if (n->memory) {
        munmap(n->memory, n->size);
    }
    if (n->globals) {
        munmap(n->globals, n->globals_size);
    }
    free(n->exports);
    *n = (Native){ 0 };
}
