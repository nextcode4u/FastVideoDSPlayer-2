// Keep UI, libc and filesystem call chains away from the decoder tables in DTCM.
// IRQ/SVC stacks remain in DTCM; only the foreground application uses this stack.
.arm
.cpu arm946e-s
.text
.align 2
.global fv_runApplication
.type fv_runApplication, %function
fv_runApplication:
    push {r4, lr}
    mov r4, sp
    ldr sp, =fv_applicationStack + 32768
    bl applicationMain
    mov sp, r4
    pop {r4, pc}
.size fv_runApplication, .-fv_runApplication

.bss
.balign 32
.global fv_applicationStack
fv_applicationStack:
    .space 32768
