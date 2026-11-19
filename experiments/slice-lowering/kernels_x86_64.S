#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

.intel_syntax noprefix

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
    xor edx, edx
    xor ecx, ecx
    mov r8, qword ptr [rdi + 8]
    .p2align 4
1:
    cmp rcx, r8
    jge 2f
    mov r9, qword ptr [rdi]
    mov r10, qword ptr [rdi + 8]
    cmp rcx, r10
    jae 8f
    mov rax, qword ptr [r9 + rcx*8]
    add rdx, rax
    jo 9f
    add rcx, 1
    jmp 1b
2:
    mov rax, rdx
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(sum_c)
SYM(sum_c):
    xor edx, edx
    xor ecx, ecx
    mov r8, rsi
    .p2align 4
1:
    cmp rcx, r8
    jge 2f
    cmp rcx, rsi
    jae 8f
    mov rax, qword ptr [rdi + rcx*8]
    add rdx, rax
    jo 9f
    add rcx, 1
    jmp 1b
2:
    mov rax, rdx
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(count_a)
SYM(count_a):
    sub rsp, 16
    movzx esi, sil
    xor edx, edx
    mov r8, qword ptr [rdi]
    mov r9, qword ptr [rdi + 8]
    mov qword ptr [rsp], r8
    mov qword ptr [rsp + 8], r9
    .p2align 4
1:
    mov r8, qword ptr [rsp + 8]
    cmp r8, 0
    jle 2f
    mov r9, qword ptr [rsp]
    mov r10, qword ptr [rsp + 8]
    cmp r10, 0
    jbe 8f
    movzx eax, byte ptr [r9]
    cmp eax, esi
    jne 3f
    add rdx, 1
    jo 9f
3:
    mov r9, qword ptr [rsp]
    mov r10, qword ptr [rsp + 8]
    cmp r10, 1
    jb 8f
    add r9, 1
    sub r10, 1
    mov qword ptr [rsp], r9
    mov qword ptr [rsp + 8], r10
    jmp 1b
2:
    mov rax, rdx
    add rsp, 16
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(count_c)
SYM(count_c):
    movzx edx, dl
    xor ecx, ecx
    mov r8, rdi
    mov r9, rsi
    .p2align 4
1:
    cmp r9, 0
    jle 2f
    cmp r9, 0
    jbe 8f
    movzx eax, byte ptr [r8]
    cmp eax, edx
    jne 3f
    add rcx, 1
    jo 9f
3:
    cmp r9, 1
    jb 8f
    add r8, 1
    sub r9, 1
    jmp 1b
2:
    mov rax, rcx
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(first_a)
SYM(first_a):
    mov r9, qword ptr [rdi]
    mov r10, qword ptr [rdi + 8]
    cmp r10, 0
    jbe 8f
    movzx eax, byte ptr [r9]
    ret
8:
    ud2

.p2align 6
.globl SYM(first_a1)
SYM(first_a1):
    sub rsp, 16
    mov qword ptr [rsp], rdi
    mov qword ptr [rsp + 8], rsi
    mov r9, qword ptr [rsp]
    mov r10, qword ptr [rsp + 8]
    cmp r10, 0
    jbe 8f
    movzx eax, byte ptr [r9]
    add rsp, 16
    ret
8:
    ud2

.p2align 6
.globl SYM(first_c)
SYM(first_c):
    cmp rsi, 0
    jbe 8f
    movzx eax, byte ptr [rdi]
    ret
8:
    ud2

.p2align 6
.globl SYM(total_a)
SYM(total_a):
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 40
    xor ebx, ebx
    mov r9, qword ptr [rdi]
    mov r10, qword ptr [rdi + 8]
    mov qword ptr [rsp], r9
    mov qword ptr [rsp + 8], r10
    .p2align 4
1:
    mov r8, qword ptr [rsp + 8]
    cmp r8, 0
    jle 2f
    mov r9, qword ptr [rsp]
    mov r10, qword ptr [rsp + 8]
    mov qword ptr [rsp + 16], r9
    mov qword ptr [rsp + 24], r10
    lea rdi, [rsp + 16]
    call qword ptr [rip + SYM(slot_first_a)]
    mov rcx, rax
    add rbx, rcx
    jo 9f
    mov r9, qword ptr [rsp]
    mov r10, qword ptr [rsp + 8]
    cmp r10, 1
    jb 8f
    add r9, 1
    sub r10, 1
    mov qword ptr [rsp], r9
    mov qword ptr [rsp + 8], r10
    jmp 1b
2:
    mov rax, rbx
    add rsp, 40
    pop rbx
    pop rbp
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(total_a1)
SYM(total_a1):
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 24
    xor ebx, ebx
    mov qword ptr [rsp], rdi
    mov qword ptr [rsp + 8], rsi
    .p2align 4
1:
    mov r8, qword ptr [rsp + 8]
    cmp r8, 0
    jle 2f
    mov rdi, qword ptr [rsp]
    mov rsi, qword ptr [rsp + 8]
    call qword ptr [rip + SYM(slot_first_a1)]
    mov rcx, rax
    add rbx, rcx
    jo 9f
    mov r9, qword ptr [rsp]
    mov r10, qword ptr [rsp + 8]
    cmp r10, 1
    jb 8f
    add r9, 1
    sub r10, 1
    mov qword ptr [rsp], r9
    mov qword ptr [rsp + 8], r10
    jmp 1b
2:
    mov rax, rbx
    add rsp, 24
    pop rbx
    pop rbp
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(total_c)
SYM(total_c):
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    sub rsp, 8
    xor ebx, ebx
    mov r12, rdi
    mov r13, rsi
    .p2align 4
1:
    cmp r13, 0
    jle 2f
    mov rdi, r12
    mov rsi, r13
    call qword ptr [rip + SYM(slot_first_c)]
    mov rcx, rax
    add rbx, rcx
    jo 9f
    cmp r13, 1
    jb 8f
    add r12, 1
    sub r13, 1
    jmp 1b
2:
    mov rax, rbx
    add rsp, 8
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
8:
    ud2
9:
    ud2

.p2align 6
.globl SYM(calibrate)
SYM(calibrate):
    mov rax, rdi
    mov rdx, 1
    .p2align 4
1:
    .rept 64
    add rax, rdx
    .endr
    sub rdi, 1
    jnz 1b
    ret

#if defined(__linux__)
.section .note.GNU-stack,"",%progbits
#endif
