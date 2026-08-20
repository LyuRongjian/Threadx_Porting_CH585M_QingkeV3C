/***************************************************************************
 * qingke_hpe_isr.S -- QingKe V3C (CH58x) HPE 统一中断包装
 *
 * 结构参考官方 ThreadX risc-v32 移植 (_tx_thread_context_save/restore)
 * 与 WCH EVT FreeRTOS/RT-Thread 的 HPE 中断入口:
 *
 *   硬件 (HPE, INTSYSCR.HWSTKEN=1, 启动文件已配置):
 *     进入中断时自动将 16 个调用者保存寄存器压入被打断者的栈, mret 自动弹出。
 *
 *   本包装 (每个中断入口):
 *     1. 在 HPE 硬件帧之下追加 16 字软帧: 类型/s0-s11/mstatus/mepc;
 *     2. _tx_thread_system_state++ (内核由此识别 ISR 上下文);
 *     3. 首层中断且有线程被打断: 保存 sp 到 TCB 并切换到系统栈
 *        (嵌套中断/初始化/空闲期间不切换, 直接在当前栈上处理);
 *     4. 执行中断服务 (SysTick 调 _tx_timer_interrupt, 外设中断查
 *        _real_user_vector_base 表分发);
 *     5. 退出时 system_state--, 并判定:
 *        - 仍在嵌套/初始化/空闲: 恢复软帧后 mret 返回被打断处;
 *        - 无抢占: 从 TCB 取回栈帧, mret 返回被打断线程;
 *        - 需抢占: 保存剩余时间片, 清 current, 跳转调度器
 *          (完整上下文已在入口保存)。
 *
 * SysTick 与全部外部中断共用本包装, 调度不需要 SW 软件中断。
 ***************************************************************************/

    .section .highcode.qingke_hpe_isr, "ax", @progbits
    .align 2

    .global SysTick_Handler
    .global unified_interrupt_entry

    .extern _tx_timer_interrupt
    .extern _tx_thread_schedule
    .extern _real_user_vector_base

/* ==================== 公共序言 ====================
 * 入口: HPE 已压栈, sp 指向硬件帧, mepc/mstatus 保存被打断现场。
 */
.macro TX_HPE_PROLOGUE
    addi    sp, sp, -64
    li      t0, 1
    sw      t0, 0x00(sp)                          /* 帧类型 = 1 (中断) */
    sw      zero, 0x04(sp)                        /* 保留 */
    sw      s0,  0x08(sp)
    sw      s1,  0x0C(sp)
    sw      s2,  0x10(sp)
    sw      s3,  0x14(sp)
    sw      s4,  0x18(sp)
    sw      s5,  0x1C(sp)
    sw      s6,  0x20(sp)
    sw      s7,  0x24(sp)
    sw      s8,  0x28(sp)
    sw      s9,  0x2C(sp)
    sw      s10, 0x30(sp)
    sw      s11, 0x34(sp)
    csrr    t0, mstatus
    sw      t0, 0x38(sp)
    csrr    t0, mepc
    sw      t0, 0x3C(sp)

    /* _tx_thread_system_state++ */
    la      t0, _tx_thread_system_state
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

    /* 嵌套中断 (或初始化期间 state 很大): 不动 TCB, 不换栈 */
    li      t2, 1
    bgt     t1, t2, 91f

    /* 首层中断: 线程被打断则保存栈指针并切到系统栈 */
    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    beqz    t1, 91f                               /* 空闲/初始化: 已在系统栈 */
    sw      sp, 8(t1)                             /* TCB->tx_thread_stack_ptr */
    la      t0, _tx_thread_system_stack_ptr
    lw      sp, 0(t0)
91:
.endm

/* ==================== 公共尾声 ====================
 * 做出抢占判定并恢复上下文。
 */
.macro TX_HPE_EPILOGUE
    /* _tx_thread_system_state-- */
    la      t0, _tx_thread_system_state
    lw      t1, 0(t0)
    addi    t1, t1, -1
    sw      t1, 0(t0)
    bgtz    t1, 80f                               /* 嵌套/初始化中: 直接返回被打断处 */

    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    beqz    t1, 80f                               /* 空闲: 返回调度循环 */

    /* preempt_disable 置位时不抢占 */
    la      t0, _tx_thread_preempt_disable
    lw      t2, 0(t0)
    bgtz    t2, 82f

    /* execute == current: 无抢占 */
    la      t0, _tx_thread_execute_ptr
    lw      t2, 0(t0)
    beq     t1, t2, 82f

    /* ==== 需要抢占: 上下文已在入口完整保存 ==== */
    la      t0, _tx_timer_time_slice              /* 保存剩余时间片 */
    lw      t2, 0(t0)
    beqz    t2, 83f
    sw      t2, 24(t1)
    sw      zero, 0(t0)
83:
    la      t0, _tx_thread_current_ptr            /* current = NULL */
    sw      zero, 0(t0)
    la      t0, _tx_thread_system_stack_ptr       /* 回系统栈 */
    lw      sp, 0(t0)
    j       _tx_thread_schedule                   /* 不再返回 */

82: /* 无抢占: 从 TCB 取回被打断线程的栈帧 */
    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    lw      sp, 8(t1)

80: /* 恢复软帧并 mret (HPE 硬件弹出调用者保存寄存器) */
    lw      t0, 0x3C(sp)
    csrw    mepc, t0
    lw      t0, 0x38(sp)
    csrw    mstatus, t0
    lw      s0,  0x08(sp)
    lw      s1,  0x0C(sp)
    lw      s2,  0x10(sp)
    lw      s3,  0x14(sp)
    lw      s4,  0x18(sp)
    lw      s5,  0x1C(sp)
    lw      s6,  0x20(sp)
    lw      s7,  0x24(sp)
    lw      s8,  0x28(sp)
    lw      s9,  0x2C(sp)
    lw      s10, 0x30(sp)
    lw      s11, 0x34(sp)
    addi    sp, sp, 64                            /* sp -> HPE 硬件帧 */
    mret
.endm

/* ========================================================================
 * SysTick 中断 (向量表第 12 项, 覆盖启动文件中的弱符号)
 * ======================================================================== */
    .type   SysTick_Handler, @function
SysTick_Handler:
    TX_HPE_PROLOGUE

    /* 清计数中断标志 (STK_SR @ 0xE000F004) */
    li      t0, 0xE000F004
    sw      zero, 0(t0)

    /* ThreadX 系统 tick 处理 */
    call    _tx_timer_interrupt

    TX_HPE_EPILOGUE
    .size   SysTick_Handler, . - SysTick_Handler

/* ========================================================================
 * 外部中断统一入口 (向量表第 16~39 项)
 * 按 mcause 查 _real_user_vector_base 表调用用户 ISR (普通 C 函数)。
 * ======================================================================== */
    .type   unified_interrupt_entry, @function
unified_interrupt_entry:
    TX_HPE_PROLOGUE

    csrr    a0, mcause
    andi    a0, a0, 0x7F                          /* 去掉中断标志位, 取 IRQ 号 */
    slli    a0, a0, 2
    addi    a0, a0, -64                           /* (IRQn - 16) * 4 */
    la      t0, _real_user_vector_base
    add     t0, t0, a0
    lw      t0, 0(t0)
    jalr    t0                                    /* 调用用户 ISR */

    TX_HPE_EPILOGUE
    .size   unified_interrupt_entry, . - unified_interrupt_entry
