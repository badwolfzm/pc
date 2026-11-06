/* 32-bit Multiboot fixture; not the Warden linker script. */
ENTRY(_start)
SECTIONS {
  . = 1M;
  .multiboot : { *(.multiboot) }
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
}
