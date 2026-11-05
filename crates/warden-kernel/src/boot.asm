; Multiboot1 entry for QEMU -kernel: 32-bit protected mode -> long mode -> kmain.
; Identity-maps the first 1 GiB with 2 MiB supervisor pages. User memory is
; mapped per thread by mm.rs; this boot map is the kernel's own view.

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x3                 ; page-align modules, provide memory map
section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)

section .bss
align 4096
global boot_pml4
global boot_pd0
boot_pml4:    resb 4096
boot_pdpt:    resb 4096
boot_pd0:     resb 4096
global kernel_stack_top
kernel_stack_bottom: resb 65536
kernel_stack_top:

section .data
align 16
global gdt64
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF           ; 0x08 kernel code, 64-bit
    dq 0x00CF92000000FFFF           ; 0x10 kernel data
    dq 0x00AFFA000000FFFF           ; 0x18 user code, 64-bit, DPL 3
    dq 0x00CFF2000000FFFF           ; 0x20 user data, DPL 3
    dq 0                            ; 0x28 TSS (16 bytes, filled by Rust)
    dq 0
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64

section .text
bits 32
global _start
extern kmain
_start:
    cli
    mov esp, kernel_stack_top
    mov edi, ebx                    ; multiboot info pointer
    mov esi, eax                    ; multiboot magic

    ; Upper levels carry the user bit; the 2 MiB entries decide per page.
    mov eax, boot_pd0
    or eax, 0x7
    mov [boot_pdpt], eax
    mov eax, boot_pdpt
    or eax, 0x7
    mov [boot_pml4], eax
    xor ecx, ecx
.fill_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83                    ; present | writable | 2 MiB, supervisor
    mov [boot_pd0 + ecx*8], eax
    mov dword [boot_pd0 + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .fill_pd

    mov eax, boot_pml4
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5                  ; PAE
    mov cr4, eax
    mov ecx, 0xC0000080             ; EFER
    rdmsr
    or eax, (1 << 8) | (1 << 11)    ; LME, NXE
    wrmsr
    mov eax, cr0
    or eax, (1 << 31) | (1 << 16)   ; PG, WP
    mov cr0, eax
    lgdt [gdt64_ptr]
    jmp 0x08:long_mode

bits 64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, kernel_stack_top
    ; SSE for ring 3 (Linux programs use it freely): CR0.EM off, CR0.MP on,
    ; CR4.OSFXSR and OSXMMEXCPT on. The kernel itself is built soft-float;
    ; per-thread state is saved with fxsave/fxrstor around every switch.
    mov rax, cr0
    and ax, ~(1 << 2)
    or ax, 1 << 1
    mov cr0, rax
    mov rax, cr4
    or eax, (1 << 9) | (1 << 10)
    mov cr4, rax
    mov edi, edi                    ; zero-extend the 32-bit values
    mov esi, esi
    call kmain
.hang:
    cli
    hlt
    jmp .hang

; ---- trap entry -----------------------------------------------------------
; Every vector pushes (error code if the CPU did not) and its number, then
; saves all general registers. The frame then matches `TrapFrame` in Rust.
%macro TRAP_NOERR 1
global trap_%1
trap_%1:
    push qword 0
    push qword %1
    jmp trap_common
%endmacro
%macro TRAP_ERR 1
global trap_%1
trap_%1:
    push qword %1
    jmp trap_common
%endmacro

TRAP_NOERR 0
TRAP_NOERR 1
TRAP_NOERR 2
TRAP_NOERR 3
TRAP_NOERR 4
TRAP_NOERR 5
TRAP_NOERR 6
TRAP_NOERR 7
TRAP_ERR   8
TRAP_NOERR 9
TRAP_ERR   10
TRAP_ERR   11
TRAP_ERR   12
TRAP_ERR   13
TRAP_ERR   14
TRAP_NOERR 15
TRAP_NOERR 16
TRAP_ERR   17
TRAP_NOERR 18
TRAP_NOERR 19
TRAP_NOERR 20
TRAP_ERR   21
TRAP_NOERR 22
TRAP_NOERR 23
TRAP_NOERR 24
TRAP_NOERR 25
TRAP_NOERR 26
TRAP_NOERR 27
TRAP_NOERR 28
TRAP_ERR   29
TRAP_ERR   30
TRAP_NOERR 31
TRAP_NOERR 32                       ; the partition timer
TRAP_NOERR 128                      ; syscall: int 0x80

; syscall entry (Linux personality). The CPU loaded CS/SS from STAR, masked
; IF via SFMASK, and left rip in rcx and rflags in r11. Build the same
; TrapFrame as an interrupt would (vector 256), on the kernel stack.
section .bss
syscall_user_rsp: resq 1
section .text
global syscall_entry
syscall_entry:
    mov [syscall_user_rsp], rsp
    mov rsp, kernel_stack_top
    push qword 0x23                 ; user ss
    push qword [syscall_user_rsp]   ; user rsp
    push r11                        ; rflags
    push qword 0x1B                 ; user cs
    push rcx                        ; rip (after the syscall instruction)
    push qword 0                    ; error code
    push qword 256                  ; vector: Linux syscall
    jmp trap_common

extern trap
trap_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    and rsp, -16
    call trap                       ; never returns
.hang:
    cli
    hlt
    jmp .hang

; enter_user(frame: *const TrapFrame, cr3: u64) -> !  Switches address space,
; restores a saved frame and iretq's into ring 3. The frame memory (kernel
; heap, mapped in every space) is used as the stack to pop from.
global enter_user
enter_user:
    mov cr3, rsi                    ; always: also flushes the TLB after unmap/protect
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                     ; vector, error code
    iretq
