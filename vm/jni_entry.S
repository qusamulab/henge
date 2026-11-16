// One static AAPCS64 call bridge. x0=function, x1=call_frame, w2=return descriptor.
.text
.p2align 2
.globl _vm_jni_call
_vm_jni_call:
    stp x29, x30, [sp, #-48]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    str x21, [sp, #32]
    mov x20, x0
    mov x19, x1
    mov w21, w2
    ldr x9, [x19, #2176] // nstack, after gp[8], fp[8], stack[256]
    add x10, x9, #1
    and x10, x10, #-2
    lsl x10, x10, #3
    sub sp, sp, x10
    add x11, x19, #128
    mov x12, sp
    cbz x9, 2f
1:  ldr x13, [x11], #8
    str x13, [x12], #8
    subs x9, x9, #1
    b.ne 1b
2:  ldp d0, d1, [x19, #64]
    ldp d2, d3, [x19, #80]
    ldp d4, d5, [x19, #96]
    ldp d6, d7, [x19, #112]
    ldp x0, x1, [x19, #0]
    ldp x2, x3, [x19, #16]
    ldp x4, x5, [x19, #32]
    ldp x6, x7, [x19, #48]
    blr x20
    cmp w21, #'F'
    b.ne 3f
    fmov w0, s0
3:  cmp w21, #'D'
    b.ne 4f
    fmov x0, d0
4:  mov sp, x29
    ldp x19, x20, [sp, #16]
    ldr x21, [sp, #32]
    ldp x29, x30, [sp], #48
    ret
