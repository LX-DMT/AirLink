OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { firmware (rwx) : ORIGIN = 0x8fe00000, LENGTH = 0x001f0000 }
STACK_SIZE = 0x4000;
SECTIONS {
 . = ORIGIN(firmware);
 .text : { KEEP(*(.text.start)) *(.text .text.*) } > firmware
 .rodata : ALIGN(16) { *(.rodata .rodata.*) } > firmware
 .data : ALIGN(16) { __global_pointer$ = . + 0x800; *(.sdata .sdata.*) *(.data .data.*) } > firmware
 .bss (NOLOAD) : ALIGN(16) { __bss_start = .; *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) __bss_end = ALIGN(16); } > firmware
 __firmware_end = ALIGN(16);
 __stack_top = ORIGIN(firmware) + LENGTH(firmware) - 16;
 __stack_bottom = __stack_top - STACK_SIZE;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
}
ASSERT(ADDR(.text) == 0x8fe00000, "R26 entry moved")
ASSERT(__firmware_end <= __stack_bottom, "R26 firmware overlaps stack")
ASSERT(__stack_top < 0x8fff0000, "R26 stack overlaps shared memory")
