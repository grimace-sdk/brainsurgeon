ENTRY(entry_table)
SECTIONS
{
    . = 0;
    .text : { *(.text.entry) *(.text .text.* .rodata .rodata.*) }
    /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.* .comment .note*) }
}
