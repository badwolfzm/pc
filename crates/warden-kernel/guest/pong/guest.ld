/* Guest app layout inside the 2 MiB user window 0x800000..0xA00000.
   text+rodata at 0x800000 (mapped R-X by the kernel), data+bss+heap at 0x840000
   (mapped RW+NX), stack 0x9F0000..0xA00000. */
ENTRY(_start)
SECTIONS {
    . = 0x800000;
    .text   : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    __text_end = .;
    . = 0x840000;
    .data   : { *(.data .data.*) }
    __data_end = .;
    .bss    : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(4096);
    __heap_start = .;
    __heap_end = 0x9E0000;
    /DISCARD/ : { *(.eh_frame) *(.note.*) *(.comment) }
}
