    .section .text.entry, "ax"
    .arm
    .global entry_table
entry_table:
    b a7300_display_transfer
    b a7300_move_pointer
    b a7300_panel_update
    b a7300_write_debug_string
    b a7300_refresh
tear_on_parameter:
    mov r0, #1
    mov r1, #0
    ldr pc, panel_write
panel_write:
    .word 0xc06f72e4
