ENTRY(_start)

SECTIONS {
    . = 1M;
    .multiboot : { KEEP(*(.multiboot)) }
    .text      : { *(.text .text.*) }
    .rodata    : { *(.rodata .rodata.*) }
    .data      : { *(.data .data.*) }
    .bss       : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(4K);
    __kernel_end = .;

    /* The user partition: one 2 MiB page the kernel maps user-accessible.
       Code placed here by #[link_section = ".user"] runs in ring 3 and can
       touch nothing outside this page except through syscalls. */
    . = 0x800000;
    .user : { __user_start = .; *(.user .user.*) }
    __user_end = .;

    /DISCARD/ : { *(.eh_frame) *(.eh_frame_hdr) *(.note.*) *(.comment) }
}
