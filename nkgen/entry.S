#include "nkgen.h"

    .section .header, "a"
    .global nkgen_header
nkgen_header:
    .ascii NKGEN_HEADER_MAGIC
    .word NKGEN_HEADER_VERSION
    .word NKGEN_ABI_VERSION
    .word __link_address
    .word __file_length
    .word __bss_length
    .word __entry_offset
    .word 0
    .word NKGEN_PAK_FORMAT
    .ascii NKGEN_VERSION_TEXT
    .org NKGEN_HEADER_SIZE, 0

    .text
    .arm
    .global nkgen_save
    .type nkgen_save, %function
nkgen_save:
    stmia r0, {r4-r8, r10, r11, sp, lr}
    mov r0, #0
    bx lr

    .global nkgen_restore
    .type nkgen_restore, %function
nkgen_restore:
    ldmia r0, {r4-r8, r10, r11, sp, lr}
    mov r0, #1
    bx lr
