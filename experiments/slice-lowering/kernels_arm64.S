#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

.macro LOAD_SLOT reg, name
#if defined(__APPLE__)
    adrp \reg, \name@PAGE
    ldr \reg, [\reg, \name@PAGEOFF]
#else
    adrp \reg, \name
    ldr \reg, [\reg, :lo12:\name]
#endif
.endm

.macro LOOP_HEAD
    nop
    .p2align 4
.endm

.data
.p2align 3
.globl SYM(slot_first_a)
SYM(slot_first_a):
    .quad SYM(first_a)
.globl SYM(slot_first_a1)
SYM(slot_first_a1):
    .quad SYM(first_a1)
.globl SYM(slot_first_c)
SYM(slot_first_c):
    .quad SYM(first_c)

.text

.p2align 6
.globl SYM(sum_a)
SYM(sum_a):
    mov x2, #0
    mov x3, #0
    ldr x4, [x0, #8]
    LOOP_HEAD
1:
    cmp x3, x4
    b.ge 2f
    ldp x10, x11, [x0]
    cmp x3, x11
    b.hs 8f
    ldr x9, [x10, x3, lsl #3]
    adds x2, x2, x9
    b.vs 9f
    add x3, x3, #1
    b 1b
2:
    mov x0, x2
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(sum_c)
SYM(sum_c):
    mov x2, #0
    mov x3, #0
    mov x4, x1
    LOOP_HEAD
1:
    cmp x3, x4
    b.ge 2f
    cmp x3, x1
    b.hs 8f
    ldr x9, [x0, x3, lsl #3]
    adds x2, x2, x9
    b.vs 9f
    add x3, x3, #1
    b 1b
2:
    mov x0, x2
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(count_a)
SYM(count_a):
    sub sp, sp, #16
    and w1, w1, #0xff
    mov x2, #0
    ldp x9, x10, [x0]
    stp x9, x10, [sp]
    LOOP_HEAD
1:
    ldr x9, [sp, #8]
    cmp x9, #0
    b.le 2f
    ldp x10, x11, [sp]
    cmp x11, #0
    b.ls 8f
    ldrb w9, [x10]
    cmp w9, w1
    b.ne 3f
    adds x2, x2, #1
    b.vs 9f
3:
    ldp x10, x11, [sp]
    cmp x11, #1
    b.lo 8f
    add x10, x10, #1
    sub x11, x11, #1
    stp x10, x11, [sp]
    b 1b
2:
    mov x0, x2
    add sp, sp, #16
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(count_c)
SYM(count_c):
    and w2, w2, #0xff
    mov x3, #0
    mov x4, x0
    mov x5, x1
    LOOP_HEAD
1:
    cmp x5, #0
    b.le 2f
    cmp x5, #0
    b.ls 8f
    ldrb w9, [x4]
    cmp w9, w2
    b.ne 3f
    adds x3, x3, #1
    b.vs 9f
3:
    cmp x5, #1
    b.lo 8f
    add x4, x4, #1
    sub x5, x5, #1
    b 1b
2:
    mov x0, x3
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(first_a)
SYM(first_a):
    ldp x9, x10, [x0]
    cmp x10, #0
    b.ls 8f
    ldrb w0, [x9]
    ret
8:
    brk #6

.p2align 6
.globl SYM(first_a1)
SYM(first_a1):
    sub sp, sp, #16
    stp x0, x1, [sp]
    ldp x9, x10, [sp]
    cmp x10, #0
    b.ls 8f
    ldrb w0, [x9]
    add sp, sp, #16
    ret
8:
    brk #6

.p2align 6
.globl SYM(first_c)
SYM(first_c):
    cmp x1, #0
    b.ls 8f
    ldrb w0, [x0]
    ret
8:
    brk #6

.p2align 6
.globl SYM(total_a)
SYM(total_a):
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #48
    stp x19, x20, [x29, #-16]
    mov x19, #0
    ldp x9, x10, [x0]
    stp x9, x10, [sp]
    LOOP_HEAD
1:
    ldr x9, [sp, #8]
    cmp x9, #0
    b.le 2f
    ldp x9, x10, [sp]
    stp x9, x10, [sp, #16]
    add x0, sp, #16
    LOAD_SLOT x16, SYM(slot_first_a)
    blr x16
    mov x9, x0
    adds x19, x19, x9
    b.vs 9f
    ldp x10, x11, [sp]
    cmp x11, #1
    b.lo 8f
    add x10, x10, #1
    sub x11, x11, #1
    stp x10, x11, [sp]
    b 1b
2:
    mov x0, x19
    ldp x19, x20, [x29, #-16]
    mov sp, x29
    ldp x29, x30, [sp], #16
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(total_a1)
SYM(total_a1):
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #32
    stp x19, x20, [x29, #-16]
    mov x19, #0
    stp x0, x1, [sp]
    LOOP_HEAD
1:
    ldr x9, [sp, #8]
    cmp x9, #0
    b.le 2f
    ldp x0, x1, [sp]
    LOAD_SLOT x16, SYM(slot_first_a1)
    blr x16
    mov x9, x0
    adds x19, x19, x9
    b.vs 9f
    ldp x10, x11, [sp]
    cmp x11, #1
    b.lo 8f
    add x10, x10, #1
    sub x11, x11, #1
    stp x10, x11, [sp]
    b 1b
2:
    mov x0, x19
    ldp x19, x20, [x29, #-16]
    mov sp, x29
    ldp x29, x30, [sp], #16
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(total_c)
SYM(total_c):
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #32
    stp x19, x20, [x29, #-16]
    stp x21, x22, [x29, #-32]
    mov x19, #0
    mov x20, x0
    mov x21, x1
    LOOP_HEAD
1:
    cmp x21, #0
    b.le 2f
    mov x0, x20
    mov x1, x21
    LOAD_SLOT x16, SYM(slot_first_c)
    blr x16
    mov x9, x0
    adds x19, x19, x9
    b.vs 9f
    cmp x21, #1
    b.lo 8f
    add x20, x20, #1
    sub x21, x21, #1
    b 1b
2:
    mov x0, x19
    ldp x19, x20, [x29, #-16]
    ldp x21, x22, [x29, #-32]
    mov sp, x29
    ldp x29, x30, [sp], #16
    ret
8:
    brk #6
9:
    brk #1

.p2align 6
.globl SYM(calibrate)
SYM(calibrate):
    mov x1, x0
    mov x2, #1
1:
    .rept 64
    add x0, x0, x2
    .endr
    subs x1, x1, #1
    b.ne 1b
    ret

#if defined(__linux__)
.section .note.GNU-stack,"",%progbits
#endif
