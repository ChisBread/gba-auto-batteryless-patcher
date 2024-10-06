
asm(R"(.text

original_entrypoint:
    .word 0x080000c0
patched_entrypoint_addr:
    .word patched_entrypoint

.arm
patched_entrypoint:
    ldr r3, =0x09000002
    mov r4, # 0xBF
    strh r4, [r3]
    ldr r3, =0x09000004
    mov r4, # 0xD4
    strh r4, [r3]

    ldr pc, original_entrypoint

)");

asm(R"(
# The following footer must come last.
.balign 4
.ascii "<3 from ChisBread"

)");