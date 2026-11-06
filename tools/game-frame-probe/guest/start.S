/* Independent Warden-native guest startup: no libc. */
.section .text.start, "ax"
.global _start
_start:
    xor %rbp, %rbp
    call game_probe_main
1:
    mov %eax, %edi
    mov $6, %eax
    int $0x80
    jmp 1b
.section .note.GNU-stack,"",@progbits
