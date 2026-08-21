/***************************************************************************
 * tx_thread_schedule.S -- ThreadX 线程调度 (QingKe V3C / CH58x)
 *
 * 等待 _tx_thread_execute_ptr 出现就绪线程并恢复其上下文。
 * 恢复分两类:
 *   - 中断帧(新线程/被打断线程, 128 字节全量软件帧): 恢复
 *     mepc/mstatus/ra/t0-t6/a0-a7/s0-s11 后 sp 跳过软帧, mret;
 *   - 主动帧(_tx_thread_system_return 保存, 64 字节): 恢复
 *     mstatus/ra/s0-s11 后 ret。
 *
 * 本移植关闭 HPE 硬件压栈 (CSR 0x804 = 0), 中断帧为全量软件帧:
 * mret 的硬件弹栈受内部状态位控制, 从线程主动让出路径进入本函数时
 * 该状态位未置位, 弹栈不会发生 (sp 少加 64 字节 + 寄存器丢失),
 * 故不能依赖 HPE 弹栈恢复调用者保存寄存器。
 *
 * 入口: 可从 C (_tx_initialize_kernel_enter)、_tx_thread_system_return、
 *       中断退出抢占路径进入, sp 位于系统栈, 中断可能开启。
 ***************************************************************************/

    .section .highcode.tx_thread_schedule, "ax", @progbits
    .align 2
    .global _tx_thread_schedule
    .type   _tx_thread_schedule, @function

_tx_thread_schedule:

    /* 开中断, 等待就绪线程 (与官方移植一致的调度循环) */
    csrsi   mstatus, 0x08

.Lschedule_loop:
    la      t0, _tx_thread_execute_ptr
    lw      t1, 0(t0)
    bnez    t1, .Lschedule_found
    wfi                                         /* 无就绪线程, 等中断 */
    j       .Lschedule_loop

.Lschedule_found:
    /* 关中断 (QingKe 3 级流水线, 补 3 个 nop 保证立即生效) */
    csrci   mstatus, 0x08
    nop
    nop
    nop

    /* 二次确认, 防止关中断前瞬间发生变化 */
    lw      t1, 0(t0)
    beqz    t1, .Lschedule_loop

    /* _tx_thread_current_ptr = execute_ptr */
    la      t2, _tx_thread_current_ptr
    sw      t1, 0(t2)

    /* run_count++ (偏移 4) */
    lw      t2, 4(t1)
    addi    t2, t2, 1
    sw      t2, 4(t1)

    /* _tx_timer_time_slice = 线程 time_slice (偏移 24) */
    lw      t2, 24(t1)
    la      t3, _tx_timer_time_slice
    sw      t2, 0(t3)

    /* 切到线程栈 (tx_thread_stack_ptr, 偏移 8) */
    lw      sp, 8(t1)

    /* 判断帧类型 */
    lw      t0, 0(sp)
    beqz    t0, .Lschedule_solicited

    /* ==== 恢复中断帧 (128 字节全量软件帧, 不依赖 HPE 硬件弹栈) ==== */
    lw      t0, 0x78(sp)                         /* mepc  */
    csrw    mepc, t0
    lw      t0, 0x74(sp)                         /* mstatus */
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

.Lschedule_solicited:
    /* ==== 恢复主动帧 ==== */
    lw      t0, 0x38(sp)                         /* mstatus */
    csrw    mstatus, t0
    lw      ra,  0x04(sp)
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
    addi    sp, sp, 64
    ret                                         /* 返回线程调用点 */

    .size   _tx_thread_schedule, . - _tx_thread_schedule
