.intel_syntax noprefix

.section .text
.global _start

_start:
    xor rbp, rbp
    call main

    mov rdi, rax
    mov rax, 60
    syscall

.section .note.GNU-stack,"",@progbits
