    .section .text.start, "ax"
    .arm
    .global start
start:
    ldr sp, =0x4f000000
    ldr r0, =vectors
    mcr p15, 0, r0, c12, c0, 0
    mrc p15, 0, r0, c1, c0, 0
    orr r0, r0, #2
    mcr p15, 0, r0, c1, c0, 0
    bl harness_main
    b semihosting_exit

    .global semihosting_call
semihosting_call:
    svc 0x123456
    bx lr

    .global semihosting_exit
semihosting_exit:
    cmp r0, #0
    ldreq r1, =0x20026
    ldrne r1, =0x20023
    mov r0, #0x18
    svc 0x123456
    b .

    .balign 32
vectors:
    b fault
    b fault
    b fault
    b fault
    b fault
    b fault
    b fault
    b fault
fault:
    ldr sp, =0x4f000000
    bl harness_fault
    mov r0, #1
    b semihosting_exit
