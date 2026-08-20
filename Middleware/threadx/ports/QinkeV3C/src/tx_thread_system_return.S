/***************************************************************************
 * tx_thread_system_return.S — 线程主动让出CPU
 *
 * 保存 solicited 栈帧, 跳转到调度器
 * Solicited 格式: frame_type(0) + s0-s11(12) + ra(1) + mstatus(1) = 15 words
 ***************************************************************************/

    .section .highcode, "ax", @progbits
    .align 2
    .global _tx_thread_system_return
    .type   _tx_thread_system_return, @function

_tx_thread_system_return:
    /* 关中断 */
    csrci   mstatus, 0x08

    /* 分配 solicited 栈帧 */
    addi    sp, sp, -(15 * 4)

    /* frame_type = 0 (solicited) */
    sw      zero, 0*4(sp)

    /* 保存 callee-saved */
    sw      s0,   1*4(sp)
    sw      s1,   2*4(sp)
    sw      s2,   3*4(sp)
    sw      s3,   4*4(sp)
    sw      s4,   5*4(sp)
    sw      s5,   6*4(sp)
    sw      s6,   7*4(sp)
    sw      s7,   8*4(sp)
    sw      s8,   9*4(sp)
    sw      s9,  10*4(sp)
    sw      s10, 11*4(sp)
    sw      s11, 12*4(sp)

    /* 保存 ra */
    sw      ra,  13*4(sp)

    /* 保存 mstatus */
    csrr    t0, mstatus
    sw      t0,  14*4(sp)

    /* 保存栈指针到 TCB */
    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    sw      sp, 8(t1)

    /* 切换到中断栈 */
    la      sp, _tx_irq_stack_top

    /* 跳转到调度器 */
    j       _tx_thread_schedule

    .size _tx_thread_system_return, . - _tx_thread_system_return