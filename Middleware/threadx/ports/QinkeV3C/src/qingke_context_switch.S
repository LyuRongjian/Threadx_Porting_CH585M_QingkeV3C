/***************************************************************************
 * qingke_context_switch.S — 上下文切换 ISR (软件中断入口)
 *
 * 参考 WCH 官方 FreeRTOS 移植模式:
 *   1. 硬件压栈 caller-saved 16 regs (HPE)
 *   2. mscratch 暂存当前 SP
 *   3. 切换到独立中断栈
 *   4. 保存 callee-saved + mepc + mstatus 到线程栈
 *   5. 调用调度器
 *   6. 恢复新线程上下文
 *   7. mret (GIHWSTKNEN=1, 不出栈)
 *
 * 硬件压栈布局 (sp → sp+63):
 *   +0:  x1(ra)  +4:  x5(t0)  +8:  x6(t1)  +12: x7(t2)
 *   +16: x10(a0) +20: x11(a1) +24: x12(a2) +28: x13(a3)
 *   +32: x14(a4) +36: x15(a5) +40: x16(a6) +44: x17(a7)
 *   +48: x28(t3) +52: x29(t4) +56: x30(t5) +60: x31(t6)
 ***************************************************************************/

    .section .highcode, "ax", @progbits
    .align 2

/* ========================================================================
 * 上下文切换 ISR 入口 (SW_IRQn = 14)
 * ======================================================================== */
    .global _tx_context_switch_isr
    .type   _tx_context_switch_isr, @function
_tx_context_switch_isr:

    /* ---- 1. 设置 GIHWSTKNEN=1 (mret不出栈) ---- */
    li      t0, 0x20
    csrs    0x804, t0

    /* ---- 2. mscratch 暂存当前SP (硬件压栈后的sp) ---- */
    csrw    mscratch, sp

    /* ---- 3. 切换到独立中断栈 ---- */
    la      sp, _tx_irq_stack_top

    /* ---- 4. 保存 mepc 和 mstatus ---- */
    csrr    a0, mepc
    csrr    a1, mstatus

    /* ---- 5. 计算原始SP (硬件压栈前) ---- */
    csrr    a2, mscratch               /* 硬件压栈后的sp */
    addi    a2, a2, 64                 /* sp_orig = hw_sp + 64 */

    /* ---- 6. 在中断栈上保存完整上下文 ---- */
    /* 软件帧: mepc + mstatus + s0-s11 + gp + tp + sp_orig + frame_type = 18 words */
    addi    sp, sp, -(18 * 4)

    sw      a0, 17*4(sp)              /* mepc */
    sw      a1, 16*4(sp)              /* mstatus */
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

    /* ---- 7. 保存SP到当前线程TCB ---- */
    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    beqz    t1, .Lcs_no_current
    sw      sp, 8(t1)                  /* tx_thread_stack_ptr */

.Lcs_no_current:

    /* ---- 8. 清除软件中断标志 ---- */
    li      t0, 0xE000F004            /* STK_SR 地址 */
    lw      t1, 0(t0)
    li      t2, ~(1 << 31)
    and     t1, t1, t2
    sw      t1, 0(t0)

    /* ---- 9. 调用调度器 (不返回) ---- */
    j       _tx_thread_schedule

    .size _tx_context_switch_isr, . - _tx_context_switch_isr


/* ========================================================================
 * 上下文恢复 (从调度器跳转过来)
 * sp 指向线程栈帧
 * ======================================================================== */
    .global _tx_restore_context
    .type   _tx_restore_context, @function
_tx_restore_context:

    /* 检查栈帧类型 */
    lw      t0, 0(sp)
    beqz    t0, .Lrestore_solicited

    /* ==== 恢复中断栈帧 ==== */

    /* 恢复 mepc */
    lw      t0, 17*4(sp)
    csrw    mepc, t0

    /* 恢复 mstatus (强制 MPP=M, MPIE=1, MIE=0) */
    lw      t0, 16*4(sp)
    li      t1, 0x1880
    or      t0, t0, t1
    li      t1, ~0x08
    and     t0, t0, t1
    csrw    mstatus, t0

    /* 恢复 callee-saved */
    lw      s0,  15*4(sp)
    lw      s1,  14*4(sp)
    lw      s2,  13*4(sp)
    lw      s3,  12*4(sp)
    lw      s4,  11*4(sp)
    lw      s5,  10*4(sp)
    lw      s6,   9*4(sp)
    lw      s7,   8*4(sp)
    lw      s8,   7*4(sp)
    lw      s9,   6*4(sp)
    lw      s10,  5*4(sp)
    lw      s11,  4*4(sp)

    /* 恢复 gp, tp */
    lw      gp,   3*4(sp)
    lw      tp,   2*4(sp)

    /* 获取 sp_orig (硬件压栈前的SP) */
    lw      a2,   1*4(sp)

    /* 跳过软件帧到硬件帧区域 */
    addi    sp, sp, 18*4

    /* 从硬件帧恢复 caller-saved 寄存器 */
    /* 注意: a2 保存 sp_orig, 不能被覆盖 */
    lw      t3,  48(sp)               /* x28 */
    lw      t4,  52(sp)               /* x29 */
    lw      t5,  56(sp)               /* x30 */
    lw      t6,  60(sp)               /* x31 */
    lw      a7,  44(sp)               /* x17 */
    lw      a6,  40(sp)               /* x16 */
    lw      a5,  36(sp)               /* x15 */
    lw      a4,  32(sp)               /* x14 */
    lw      a3,  28(sp)               /* x13 */
    /* a2 已保存 sp_orig, 跳过 x12(a2) */
    lw      a2,  24(sp)               /* 先恢复a2到临时位置... 不对 */
    
    /* 修正: 用 mscratch 暂存 sp_orig */
    csrw    mscratch, a2              /* 保存 sp_orig 到 mscratch */
    
    lw      a2,  24(sp)               /* x12 */
    lw      a1,  20(sp)               /* x11 */
    lw      a0,  16(sp)               /* x10 */
    lw      t2,  12(sp)               /* x7 */
    lw      t1,   8(sp)               /* x6 */
    lw      t0,   4(sp)               /* x5 */
    lw      ra,   0(sp)               /* x1 */

    /* 恢复 SP 到硬件压栈前的值 */
    csrr    sp, mscratch              /* sp = sp_orig */

    /* 清除 GIHWSTKNEN (恢复正常硬件压栈行为) */
    li      t0, 0x20
    csrc    0x804, t0

    /* mret — 由于GIHWSTKNEN已清除且sp已恢复, 不会再次出栈 */
    mret

.Lrestore_solicited:
    /* ==== 恢复 Solicited 栈帧 ==== */
    /* 格式: frame_type(0) + s0-s11(12) + ra(1) + mstatus(1) = 15 words */

    lw      t0, 14*4(sp)
    csrw    mstatus, t0

    lw      ra,  13*4(sp)

    lw      s0,  12*4(sp)
    lw      s1,  11*4(sp)
    lw      s2,  10*4(sp)
    lw      s3,   9*4(sp)
    lw      s4,   8*4(sp)
    lw      s5,   7*4(sp)
    lw      s6,   6*4(sp)
    lw      s7,   5*4(sp)
    lw      s8,   4*4(sp)
    lw      s9,   3*4(sp)
    lw      s10,  2*4(sp)
    lw      s11,  1*4(sp)

    addi    sp, sp, 15*4
    ret

    .size _tx_restore_context, . - _tx_restore_context