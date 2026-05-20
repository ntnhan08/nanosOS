/* NanosOS boot/startup.S - Xtensa LX7 reset vector + context switch */
    .section .iram.vectors,"ax"
    .align 4
    .global _vector_table
_vector_table:
    .org 0x000
    j   _reset_vector
    .org 0x180
    j   _user_exception_handler
    .org 0x1C0
    j   _kernel_exception_handler
    .org 0x200
    j   _double_exception_handler
    .org 0x240
    j   _level1_isr
    .org 0x280
    j   _level2_isr
    .org 0x2C0
    j   _level3_isr
    .org 0x300
    j   _level4_isr
    .org 0x340
    j   _level5_isr
    .org 0x3C0
    j   _nmi_handler

    .section .iram.text,"ax"
    .align 4
    .global _reset_vector
    .type _reset_vector,@function
_reset_vector:
    movi a0, 0x00040F
    wsr  a0, PS
    rsync
    movi a0, _vector_table
    wsr  a0, VECBASE
    rsync
    movi a1, _cpu0_stack_top
    /* Clear BSS */
    movi a2, _bss_start
    movi a3, _bss_end
    movi a4, 0
.Lbss: beq a2,a3,.Lbss_done
    s32i a4,a2,0
    addi a2,a2,4
    j .Lbss
.Lbss_done:
    /* Copy .data Flash->DRAM */
    movi a2, _data_lma
    movi a3, _data_vma_start
    movi a4, _data_vma_end
.Ldata: beq a3,a4,.Ldata_done
    l32i a5,a2,0
    s32i a5,a3,0
    addi a2,a2,4
    addi a3,a3,4
    j .Ldata
.Ldata_done:
    /* Enable I/D cache */
    movi a2, 0x600C4000
    l32i a3, a2, 0
    movi a4, 0x3
    or   a3, a3, a4
    s32i a3, a2, 0
    isync
    dsync
    /* Enable FPU */
    movi a2, 0x00F00000
    wsr  a2, CPENABLE
    rsync
    call0 kernel_init
.Lhalt:
    waiti 0
    j .Lhalt

/* Level-5 ISR: kernel tick (1kHz, preemption) */
    .global _level5_isr
    .align 4
_level5_isr:
    addi a1, a1, -96
    s32i a0, a1,  0
    s32i a2, a1,  4
    s32i a3, a1,  8
    rsr  a0, EPC5
    s32i a0, a1, 12
    rsr  a0, EPS5
    s32i a0, a1, 16
    /* Clear TIMG0 T0 interrupt */
    movi a2, 0x6001F0A8
    movi a3, 1
    s32i a3, a2, 0
    call0 sched_tick_isr
    l32i a0, a1, 16
    wsr  a0, EPS5
    l32i a0, a1, 12
    wsr  a0, EPC5
    l32i a0, a1, 0
    l32i a2, a1, 4
    l32i a3, a1, 8
    addi a1, a1, 96
    rfil

/* User exception handler */
    .global _user_exception_handler
    .align 4
_user_exception_handler:
    addi a1, a1, -32
    s32i a0, a1, 0
    rsr  a2, EXCCAUSE
    call0 _c_exception_handler
.Lhalt2:
    waiti 0
    j .Lhalt2

    .global _kernel_exception_handler
_kernel_exception_handler:
    j _user_exception_handler

    .global _double_exception_handler
_double_exception_handler:
    j _user_exception_handler

    .global _nmi_handler
    .align 4
_nmi_handler:
    addi a1, a1, -32
    s32i a0, a1, 0
    call0 _c_nmi_handler
    l32i a0, a1, 0
    addi a1, a1, 32
    rfnmi

    .global _level1_isr
_level1_isr:
    rfil
    .global _level2_isr
_level2_isr:
    rfil
    .global _level3_isr
_level3_isr:
    rfil
    .global _level4_isr
_level4_isr:
    rfil

/* context_switch(old_tcb a6, new_tcb a7) */
    .global context_switch
    .type context_switch,@function
    .align 4
context_switch:
    s32i a0,  a6,  0
    s32i a1,  a6,  4
    s32i a2,  a6,  8
    s32i a3,  a6, 12
    s32i a4,  a6, 16
    s32i a5,  a6, 20
    s32i a8,  a6, 32
    s32i a9,  a6, 36
    s32i a10, a6, 40
    s32i a11, a6, 44
    s32i a12, a6, 48
    s32i a13, a6, 52
    s32i a14, a6, 56
    s32i a15, a6, 60
    rsr  a0, SAR
    s32i a0,  a6, 64
    l32i a8,  a7, 32
    l32i a9,  a7, 36
    l32i a10, a7, 40
    l32i a11, a7, 44
    l32i a12, a7, 48
    l32i a13, a7, 52
    l32i a14, a7, 56
    l32i a15, a7, 60
    l32i a0,  a7, 64
    wsr  a0, SAR
    rsync
    l32i a2,  a7,  8
    l32i a3,  a7, 12
    l32i a4,  a7, 16
    l32i a5,  a7, 20
    l32i a1,  a7,  4
    l32i a0,  a7,  0
    ret

    .section .noinit,"aw"
    .align 16
    .global _cpu0_stack
_cpu0_stack:
    .space 4096
    .global _cpu0_stack_top
_cpu0_stack_top:
    .global _isr_stack
_isr_stack:
    .space 2048
    .global _isr_stack_top
_isr_stack_top:
