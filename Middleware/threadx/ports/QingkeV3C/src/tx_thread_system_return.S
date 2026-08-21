/***************************************************************************
 * tx_thread_system_return.S -- 线程主动让出 CPU (QingKe V3C / CH58x, HPE)
 *
 * 内核服务需要重新调度时从线程上下文调用 (调用者保存寄存器视为已失效,
 * 只需保存 ra/s0-s11/mstatus 构成主动帧), 随后返回调度器。
 ***************************************************************************/

    .section .highcode.tx_thread_system_return, "ax", @progbits
    .align 2
    .global _tx_thread_system_return
    .type   _tx_thread_system_return, @function

_tx_thread_system_return:

    /* 原子保存调用前的中断状态并关闭 MIE。
       主动帧恢复时必须恢复调用前的 mstatus；若先关中断再读取，
       所有经调度器恢复的线程都会带着 MIE=0 返回。 */
    csrrci  t0, mstatus, 0x08
    nop
    nop
    nop

    /* 构建主动栈帧 (16 字) */
    addi    sp, sp, -64
    sw      zero, 0(sp)                           /* 帧类型 = 0 (主动) */
    sw      t0, 0x38(sp)                          /* 先保存 mstatus (含 MIE 原状态) */
    sw      ra,  0x04(sp)
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

    /* 保存线程栈指针到 TCB (偏移 8)
       先清空 current_ptr，避免中断在这段窗口里看到旧线程仍在执行并覆盖掉
       这里刚刚保存的栈指针。 */
    la      t0, _tx_thread_current_ptr
    lw      t1, 0(t0)
    beqz    t1, .Lsystem_return_no_current
    sw      zero, 0(t0)
    sw      sp, 8(t1)

    /* 保存剩余时间片: thread->time_slice = _tx_timer_time_slice; 后者清零 */
    la      t0, _tx_timer_time_slice
    lw      t3, 0(t0)
    beqz    t3, .Lsystem_return_no_ts
    sw      t3, 24(t1)
    sw      zero, 0(t0)
.Lsystem_return_no_ts:

    /* current = NULL, 切系统栈, 进调度器 */
    la      t0, _tx_thread_system_stack_ptr
    lw      sp, 0(t0)
    j       _tx_thread_schedule

.Lsystem_return_no_current:
    /* current_ptr 已为空：直接切到系统栈并调度 */
    la      t0, _tx_thread_system_stack_ptr
    lw      sp, 0(t0)
    j       _tx_thread_schedule

    .size   _tx_thread_system_return, . - _tx_thread_system_return
