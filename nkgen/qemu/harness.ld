ENTRY(start)
SECTIONS
{
    . = 0x40100000;
    .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note* .ARM.attributes) }
}
