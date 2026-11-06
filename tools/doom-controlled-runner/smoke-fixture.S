/* Standalone Multiboot V1 assembly fixture. NOT Warden and NOT Doom.
 * Emits a fixed serial marker and guest debug-exit value zero. */
.section .multiboot,"a"
.align 4
.long 0x1BADB002
.long 0x00000000
.long -(0x1BADB002)
.section .text,"ax"
.global _start
_start:
    mov $0x3f8, %dx
    mov $'W', %al
    out %al, %dx
    mov $'A', %al
    out %al, %dx
    mov $'R', %al
    out %al, %dx
    mov $'D', %al
    out %al, %dx
    mov $'E', %al
    out %al, %dx
    mov $'N', %al
    out %al, %dx
    mov $'_', %al
    out %al, %dx
    mov $'S', %al
    out %al, %dx
    mov $'M', %al
    out %al, %dx
    mov $'O', %al
    out %al, %dx
    mov $'K', %al
    out %al, %dx
    mov $'E', %al
    out %al, %dx
    mov $'\n', %al
    out %al, %dx
    mov $0xf4, %dx
    xor %eax, %eax
    out %eax, %dx
halt:
    hlt
    jmp halt
