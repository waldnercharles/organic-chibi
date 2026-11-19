.text

.macro PROLOGUE
    sub sp, sp, #64
    str x2, [sp, #8]
    str x3, [sp, #24]
    str xzr, [sp, #32]
.endm

.macro LOAD_INPUTS
    ldr x9, [x0], #8
    str x9, [sp, #0]
    ldr x9, [x1], #8
    str x9, [sp, #16]
.endm

.macro COUNT
    ldr x10, [sp, #32]
    add x10, x10, #1
    str x10, [sp, #32]
.endm

.macro EPILOGUE
    ldr x0, [sp, #32]
    add sp, sp, #64
    ret
.endm

.p2align 6
.globl _run_slot
_run_slot:
    PROLOGUE
1:
    LOAD_INPUTS
    ldr x10, [sp, #0]
    ldr x11, [sp, #8]
    cmp x10, x11
    cset w12, lt
    strb w12, [sp, #40]
    ldrb w13, [sp, #40]
    cbz w13, 2f
    ldr x10, [sp, #16]
    ldr x11, [sp, #24]
    cmp x10, x11
    cset w12, lt
    strb w12, [sp, #40]
2:
    ldrb w13, [sp, #40]
    cbz w13, 3f
    COUNT
3:
    subs x4, x4, #1
    b.ne 1b
    EPILOGUE

.p2align 6
.globl _run_value
_run_value:
    PROLOGUE
1:
    LOAD_INPUTS
    ldr x10, [sp, #0]
    ldr x11, [sp, #8]
    cmp x10, x11
    cset w12, lt
    cbz w12, 2f
    ldr x10, [sp, #16]
    ldr x11, [sp, #24]
    cmp x10, x11
    cset w13, lt
    mov w14, w13
    b 4f
2:
    mov w14, #0
4:
    cbz w14, 3f
    COUNT
3:
    subs x4, x4, #1
    b.ne 1b
    EPILOGUE

.p2align 6
.globl _run_value_fused
_run_value_fused:
    PROLOGUE
1:
    LOAD_INPUTS
    ldr x10, [sp, #0]
    ldr x11, [sp, #8]
    cmp x10, x11
    b.ge 2f
    ldr x10, [sp, #16]
    ldr x11, [sp, #24]
    cmp x10, x11
    cset w14, lt
    b 4f
2:
    mov w14, #0
4:
    cbz w14, 3f
    COUNT
3:
    subs x4, x4, #1
    b.ne 1b
    EPILOGUE

.p2align 6
.globl _run_branch
_run_branch:
    PROLOGUE
1:
    LOAD_INPUTS
    ldr x10, [sp, #0]
    ldr x11, [sp, #8]
    cmp x10, x11
    b.ge 3f
    ldr x10, [sp, #16]
    ldr x11, [sp, #24]
    cmp x10, x11
    b.ge 3f
    COUNT
3:
    subs x4, x4, #1
    b.ne 1b
    EPILOGUE
