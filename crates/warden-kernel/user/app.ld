/* Static, non-PIE programs for the Warden user window. Three segments on
 * separate pages so the kernel can map them R-X, R-- and RW- (W^X). */
ENTRY(_start)
PHDRS {
    text   PT_LOAD FLAGS(5);   /* R X */
    rodata PT_LOAD FLAGS(4);   /* R   */
    data   PT_LOAD FLAGS(6);   /* R W */
}
SECTIONS {
    . = 0x800000;
    .text   : { *(.text.start) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    .data   : { *(.data .data.*) } :data
    .bss    : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame) *(.eh_frame_hdr) *(.note.*) *(.comment) }
}
