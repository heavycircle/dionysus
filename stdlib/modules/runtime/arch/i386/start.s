.intel_syntax noprefix

.section .text
.global _start

_start:
    xor ebp, ebp
    call main

    mov ebx, eax
    mov eax, 1
    int 0x80

.section .note.GNU-stack,"",@progbits
