/***************************************************************************
 * qingke_hpe_isr.S -- QingKe V3C (CH58x) 统一中断包装 (全软件上下文)
 *
 * 重要: 本移植关闭 HPE 硬件压栈 (启动文件写 CSR 0x804 = 0)。
 *
 * 原因 (对照 WCH 原厂 FreeRTOS/RT-Thread CH585 移植确认):
 *   HPE 的 mret 硬件弹栈由内部状态位控制 (中断进入置位, mret 弹栈清除)。
 *   调度器从"线程主动让出"(_tx_thread_system_return) 或首次调度进入时
 *   该状态位未置位, mret 不弹栈 —— 恢复"中断帧"线程将导致 sp 少加
 *   64 字节、调用者保存寄存器丢失, 栈错位破坏, 随机崩溃。
 *   原厂移植同样不依赖 HPE 弹栈恢复线程: 全部采用完整软件帧,
 *   且 SW_Handler 在 mret 前用 csrs 0x804, 0x20 显式禁止弹栈。
 *
 * 本包装结构 (与官方 ThreadX risc-v32 移植一致, 128 字节全量软件帧):
 *   1. 中断入口软件保存全部易失寄存器: ra/t0-t6/a0-a7/s0-s11
 *      + mstatus/mepc, 帧头标类型=1;
 *   2. _tx_thread_system_state++ (内核由此识别 ISR 上下文);
 *   3. 首层中断且有线程被打断: 保存 sp 到 TCB 并切换到系统栈
 *      (嵌套中断/初始化/空闲期间不切换, 直接在当前栈上处理);
 *   4. 执行中断服务 (SysTick 调 _tx_timer_interrupt, 外设中断查
 *      _real_user_vector_base 表分发);
 *   5. 退出时 system_state--, 并判定:
 *      - 仍在嵌套/初始化/空闲: 恢复软件帧后 mret 返回被打断处;
 *      - 无抢占: 从 TCB 取回栈帧, mret 返回被打断线程;
 *      - 需抢占: 保存剩余时间片, 清 current, 跳转调度器
 *          (完整上下文已在入口保存)。
 *
 * SysTick 与全部外部中断共用本包装, 调度不需要 SW 软件中断。
 *
 * 中断帧布局 (32 字, 128 字节):
 *   +0x00 类型(1)  +0x04 ra       +0x08..0x20 t0-t6
 *   +0x24..0x40 a0-a7            +0x44..0x70 s0-s11
 *   +0x74 mstatus  +0x78 mepc    +0x7C 保留
 ***************************************************************************/

    .section .highcode.qingke_hpe_isr, "ax", @progbits
    .align 2

    .global SysTick_Handler
    .global unified_interrupt_entry

    .extern _tx_timer_interrupt
    .extern _tx_thread_schedule
    .extern _real_user_vector_base

/* ==================== 公共序言 ====================
 * 入口: sp 指向被打断者的栈顶, mepc/mstatus 保存被打断现场。
 */
.macro TX_ISR_PROLOGUE
    addi    sp, sp, -128
    sw      ra,  0x04(sp)
    sw      t0,  0x08(sp)
    sw      t1,  0x0C(sp)
    sw      t2,  0x10(sp)
    sw      t3,  0x14(sp)
    sw      t4,  0x18(sp)
    sw      t5,  0x1C(sp)
    sw      t6,  0x20(sp)
    sw      a0,  0x24(sp)
    sw      a1,  0x28(sp)
    sw      a2,  0x2C(sp)
    sw      a3,  0x30(sp)
    sw      a4,  0x34(sp)
    sw      a5,  0x38(sp)
    sw      a6,  0x3C(sp)
    sw      a7,  0x40(sp)
    sw      s0,  0x44(sp)
    sw      s1,  0x48(sp)
    sw      s2,  0x4C(sp)
    sw      s3,  0x50(sp)
    sw      s4,  0x54(sp)
    sw      s5,  0x58(sp)
    sw      s6,  0x5C(sp)
    sw      s7,  0x60(sp)
    sw      s8,  0x64(sp)
    sw      s9,  0x68(sp)
    sw      s10, 0x6C(sp)
    sw      s11, 0x70(sp)
    csrr    t0, mstatus
    sw      t0,  0x74(sp)
    csrr    t0, mepc
    sw      t0,  0x78(sp)
    li      t0, 1
    sw      t0,  0x00(sp)                          /* 帧类型 = 1 (中断) */

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
.macro TX_ISR_EPILOGUE
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

80: /* 恢复软件帧 (全量) 并 mret */
    lw      t0, 0x78(sp)
    csrw    mepc, t0
    lw      t0, 0x74(sp)
    csrw    mstatus, t0
    lw      ra,  0x04(sp)
    lw      t0,  0x08(sp)
    lw      t1,  0x0C(sp)
    lw      t2,  0x10(sp)
    lw      t3,  0x14(sp)
    lw      t4,  0x18(sp)
    lw      t5,  0x1C(sp)
    lw      t6,  0x20(sp)
    lw      a0,  0x24(sp)
    lw      a1,  0x28(sp)
    lw      a2,  0x2C(sp)
    lw      a3,  0x30(sp)
    lw      a4,  0x34(sp)
    lw      a5,  0x38(sp)
    lw      a6,  0x3C(sp)
    lw      a7,  0x40(sp)
    lw      s0,  0x44(sp)
    lw      s1,  0x48(sp)
    lw      s2,  0x4C(sp)
    lw      s3,  0x50(sp)
    lw      s4,  0x54(sp)
    lw      s5,  0x58(sp)
    lw      s6,  0x5C(sp)
    lw      s7,  0x60(sp)
    lw      s8,  0x64(sp)
    lw      s9,  0x68(sp)
    lw      s10, 0x6C(sp)
    lw      s11, 0x70(sp)
    addi    sp, sp, 128
    mret
.endm

/* ========================================================================
 * SysTick 中断 (向量表第 12 项, 覆盖启动文件中的弱符号)
 * ======================================================================== */
    .type   SysTick_Handler, @function
SysTick_Handler:
    TX_ISR_PROLOGUE

    /* 清计数中断标志 (STK_SR @ 0xE000F004) */
    li      t0, 0xE000F004
    sw      zero, 0(t0)

    /* ThreadX 系统 tick 处理 */
    call    _tx_timer_interrupt

    TX_ISR_EPILOGUE
    .size   SysTick_Handler, . - SysTick_Handler

/* ========================================================================
 * 外部中断统一入口 (向量表第 16~39 项)
 * 按 mcause 查 _real_user_vector_base 表调用用户 ISR (普通 C 函数)。
 * ======================================================================== */
    .type   unified_interrupt_entry, @function
unified_interrupt_entry:
    TX_ISR_PROLOGUE

    csrr    a0, mcause
    andi    a0, a0, 0x7F                          /* 去掉中断标志位, 取 IRQ 号 */
    slli    a0, a0, 2
    addi    a0, a0, -64                           /* (IRQn - 16) * 4 */
    la      t0, _real_user_vector_base
    add     t0, t0, a0
    lw      t0, 0(t0)
    jalr    t0                                    /* 调用用户 ISR */

    TX_ISR_EPILOGUE
    .size   unified_interrupt_entry, . - unified_interrupt_entry
