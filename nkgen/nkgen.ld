ENTRY(nkgen_generate)
SECTIONS
{
    . = 0x42000000;
    __link_address = .;
    .header : { KEEP(*(.header)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4); }
    .data : { *(.data .data.*) . = ALIGN(4); }
    __file_end = .;
    .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; }
    __file_length = __file_end - __link_address;
    __bss_length = __bss_end - __bss_start;
    __entry_offset = nkgen_generate - __link_address;
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note* .ARM.attributes .got .got.plt) }
}
