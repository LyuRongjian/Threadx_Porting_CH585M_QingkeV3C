/***************************************************************************
 * qingke_unified_isr.S — 统一外部中断入口
 *
 * 参考 WCH 官方 FreeRTOS 移植:
 * - 所有外部中断 (IRQ 16+) 统一进入此入口
 * - 硬件已压栈 caller-saved (HPE)
 * - 切换到独立中断栈
 * - 根据 mcause 分发到具体 ISR
 * - 退出时检查是否需要上下文切换
 * - mret 硬件自动出栈 (GIHWSTKNEN=0)
 ***************************************************************************/

    .section .highcode, "ax", @progbits
    .align 2

    .global _tx_unified_interrupt_entry
    .type   _tx_unified_interrupt_entry, @function
_tx_unified_interrupt_entry:

    /* 硬件已压栈 caller-saved 16 regs */
    /* mscratch 暂存当前 SP */
    csrw    mscratch, sp

    /* 切换到独立中断栈 */
    la      sp, _tx_irq_stack_top

    /* 保存少量 callee-saved (C函数可能修改) */
    addi    sp, sp, -32
    sw      ra,  28(sp)
    sw      s0,  24(sp)
    sw      s1,  20(sp)
    sw      a0,  16(sp)              /* 保存 mcause 中的中断号 */
    sw      a1,  12(sp)
    sw      a2,   8(sp)
    sw      a3,   4(sp)
    sw      t0,   0(sp)

    /* 读取中断号 */
    csrr    a0, mcause
    andi    a0, a0, 0xFF             /* 取中断号 (低8位) */

    /* 递增中断嵌套计数 */
    la      t0, _tx_thread_system_state
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

    /* 调用 C 语言中断分发函数 */
    /* a0 = 中断号 */
    jal     ra, _tx_external_irq_dispatch

    /* 递减中断嵌套计数 */
    la      t0, _tx_thread_system_state
    lw      t1, 0(t0)
    addi    t1, t1, -1
    sw      t1, 0(t0)

    /* 恢复保存的寄存器 */
    lw      ra,  28(sp)
    lw      s0,  24(sp)
    lw      s1,  20(sp)
    lw      a0,  16(sp)
    lw      a1,  12(sp)
    lw      a2,   8(sp)
    lw      a3,   4(sp)
    lw      t0,   0(sp)
    addi    sp, sp, 32

    /* 检查是否需要上下文切换 */
    bnez    t1, .Lext_no_switch       /* 嵌套中，不切换 */

    la      t0, _tx_thread_execute_ptr
    lw      t1, 0(t0)
    beqz    t1, .Lext_no_switch

    la      t2, _tx_thread_current_ptr
    lw      t3, 0(t2)
    beq     t1, t3, .Lext_no_switch

    /* ==== 需要上下文切换 ==== */
    /* 设置 GIHWSTKNEN (mret不出栈) */
    li      t0, 0x20
    csrs    0x804, t0
    fence.i

    /* 恢复线程栈上的硬件压栈SP */
    csrr    sp, mscratch

    /* 保存 mepc, mstatus */
    csrr    a0, mepc
    csrr    a1, mstatus

    /* 计算 sp_orig */
    addi    a2, sp, 64

    /* 保存完整软件帧到线程栈 */
    addi    sp, sp, -(18 * 4)
    sw      a0, 17*4(sp)
    sw      a1, 16*4(sp)
    sw      s0,  15*4(sp)
    sw      s1,  14*4(sp)
    sw      s2,  13*4(sp)
    sw      s3,  12*4(sp)
    sw      s4,  11*4(sp)
    sw      s5,  10*4(sp)
    sw      s6,   9*4(sp)
    sw      s7,   8*4(sp)
    sw      s8,   7*4(sp)
    sw      s9,   6*4(sp)
    sw      s10,  5*4(sp)
    sw      s11,  4*4(sp)
    sw      gp,   3*4(sp)
    sw      tp,   2*4(sp)
    sw      a2,   1*4(sp)             /* sp_orig */
    li      t0, 1
    sw      t0,   0*4(sp)             /* frame_type = INTERRUPT */

    /* 保存到 TCB */
    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    sw      sp, 8(t1)

    /* 切换到中断栈, 进入调度器 */
    la      sp, _tx_irq_stack_top
    j       _tx_thread_schedule

.Lext_no_switch:
    /* ==== 无需切换, 正常返回 ==== */
    /* 恢复原始 SP */
    csrr    sp, mscratch

    /* mret — 硬件自动出栈恢复 caller-saved */
    mret

    .size _tx_unified_interrupt_entry, . - _tx_unified_interrupt_entry


/* ========================================================================
 * C 语言中断分发函数 (弱定义, 用户可覆盖)
 * ======================================================================== */
    .section .text
    .weak   _tx_external_irq_dispatch
    .type   _tx_external_irq_dispatch, @function
_tx_external_irq_dispatch:
    /* a0 = 中断号 (16-255) */
    /* 用户在此根据中断号调用对应外设ISR */
    ret
    .size _tx_external_irq_dispatch, . - _tx_external_irq_dispatch