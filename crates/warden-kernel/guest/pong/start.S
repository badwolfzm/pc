/* Entry: the kernel enters at 0x800000 with rsp at the stack top. */
    .section .text.start, "ax"
    .globl _start
_start:
    xor %rbp, %rbp
    call pong_main
1:  mov %eax, %edi        /* SYS_EXIT(code = pong_main's return) */
    mov $6, %eax

    int $0x80
    jmp 1b
    .section .note.GNU-stack,"",@progbits
