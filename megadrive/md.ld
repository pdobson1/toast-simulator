/* Memory map: cartridge ROM from 0, 64KB of work RAM at 0xFF0000 (the top 512 bytes are left for the stack's start). */
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
    rom (rx)  : ORIGIN = 0x000000, LENGTH = 4M
    ram (rwx) : ORIGIN = 0xFF0000, LENGTH = 0xFE00
}

SECTIONS {
    .text : {
        KEEP(*(.text.vectors))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(2);
    } > rom

    .data : {
        _data_start = .;
        *(.data .data.*)
        . = ALIGN(2);
        _data_end = .;
    } > ram AT > rom
    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        *(.bss .bss.* COMMON)
        . = ALIGN(2);
    } > ram

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
