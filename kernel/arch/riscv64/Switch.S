.global SwitchFrame

.type SwitchFrame,@function
.size SwitchFrame,(_EndOfSwitchFrame - SwitchFrame)

# void SwitchFrame(TrapFrame** current, TrapFrame* next)
SwitchFrame:
    bnez a1, 1f
    ret # just return if the next frame is null, we cant load that.
1:
    beqz a0, SkipSave
    mv t0, sp
    addi sp, sp, -280 # allow space on the stack for the frame

    sd t0, 0x8(sp)  # we only save the required registers here, this does
    sd gp, 0x10(sp) # result in junk being reload into the unsaved registers
    sd tp, 0x18(sp) # later on, but those are caller-saved so this is fine.
    sd s0, 0x38(sp)
    sd s1, 0x40(sp)
    sd s2, 0x88(sp)
    sd s3, 0x90(sp)
    sd s4, 0x98(sp)
    sd s5, 0xA0(sp)
    sd s6, 0xA8(sp)
    sd s7, 0xB0(sp)
    sd s8, 0xB8(sp)
    sd s9, 0xC0(sp)
    sd s10, 0xC8(sp)
    sd s11, 0xD0(sp)

    sd ra, 0x108(sp) # store return address
    li t0, 1
    sw t0, 0x100(sp) # spp = 1 (s-mode)
    sw t0, 0x114(sp) # spie = 1 

    sd sp, (a0) # store the pointer to this trap frame

SkipSave:
    mv sp, a1 # load the new frame onto the stack

    # restore sepc
    ld t2, 0x108(sp)
    csrw sepc, t2
    # restore most GPRS:
    # x1 and x5 are temporaries used later in the end of this routine
    # x2 is the stack pointer, restored last
    # x4 is restored differently depending on the target mode
    ld x3, 0x10(sp)
    ld x6, 0x28(sp)
    ld x7, 0x30(sp)
    ld x8, 0x38(sp)
    ld x9, 0x40(sp)
    ld x10, 0x48(sp)
    ld x11, 0x50(sp)
    ld x12, 0x58(sp)
    ld x13, 0x60(sp)
    ld x14, 0x68(sp)
    ld x15, 0x70(sp)
    ld x16, 0x78(sp)
    ld x17, 0x80(sp)
    ld x18, 0x88(sp)
    ld x19, 0x90(sp)
    ld x20, 0x98(sp)
    ld x21, 0xA0(sp)
    ld x22, 0xA8(sp)
    ld x23, 0xB0(sp)
    ld x24, 0xB8(sp)
    ld x25, 0xC0(sp)
    ld x26, 0xC8(sp)
    ld x27, 0xD0(sp)
    ld x28, 0xD8(sp)
    ld x29, 0xE0(sp)
    ld x30, 0xE8(sp)
    ld x31, 0xF0(sp)

    # determine which mode we're returning to
    lw x1, 0x110(sp)
    beqz x1, GotoUMode
GotoSMode:
    # we're returning to s-mode, set spp and spie
    li x1, 1 << 8
    csrs sstatus, x1
    lw x1, 0x114(sp)
    li x5, 1 << 5

    beqz x1, 1f # set spie if interrupts were enabled
    csrs sstatus, x5
    j 2f
1:
    csrc sstatus, x5
2:
    # restore x1, x5 and stack pointer (x2)
    # x4/tp is not restored because this is a kernel -> kernel transition, so it remains
    # to hold the core-local info.
    ld x1, 0(sp)
    ld x5, 0x20(sp)
    addi sp, sp, 280
    sret

GotoUMode:
    # we're returning to u-mode, set spp and spie
    li x1, 1 << 8
    andi x1, x1, 1 << 5
    csrs sstatus, x1

    ld x1, 0x18(sp) # prime sscratch for a swap with x4 later on
    csrw sscratch, x1
    ld x1, 0x8(sp) # stash user stack in scratch slot
    sd x1, 0(x4)

    # restore x1 and x5
    ld x1, 0(sp)
    ld x5, 0x20(sp)

    ld sp, 0(x4) # load user stack from scratch slot
    csrrw x4, sscratch, x4 # restore real x4 and stash core-local info in sscratch
    sret
_EndOfSwitchFrame:
