    .section .text.entry, "ax"
    .arm
    .global entry_table
entry_table:
    push {r0, lr}
    ldr r1, ring_address
    bl debug_log_append
    pop {r0, lr}
    ldr pc, original_write_debug_string
original_write_debug_string:
    .word 0
ring_address:
    .word 0
