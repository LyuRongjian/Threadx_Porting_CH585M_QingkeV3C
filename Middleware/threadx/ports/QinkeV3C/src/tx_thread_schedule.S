/***************************************************************************
 * tx_thread_schedule.S — ThreadX 线程调度器
 *
 * 从上下文切换ISR或系统返回跳转过来
 * 此时 sp 指向中断栈顶
 ***************************************************************************/

    .section .highcode, "ax", @progbits
    .align 2
    .global _tx_thread_schedule
    .type   _tx_thread_schedule, @function

_tx_thread_schedule:

    /* 使能全局中断 */
    csrsi   mstatus, 0x08

.Lschedule_wait:
    /* 关中断, 检查可执行线程 */
    csrci   mstatus, 0x08

    la      t0, _tx_thread_execute_ptr
    lw      t1, 0(t0)
    beqz    t1, .Lschedule_wfi

    /* 再次确认 */
    lw      t1, 0(t0)
    beqz    t1, .Lschedule_wait

    /* ---- 设置当前线程 ---- */
    la      t2, _tx_thread_current_ptr
    sw      t1, 0(t2)

    /* 增加运行计数 */
    lw      t3, 4(t1)
    addi    t3, t3, 1
    sw      t3, 4(t1)

    /* 设置时间片 */
    lw      t3, 24(t1)
    la      t4, _tx_timer_time_slice
    sw      t3, 0(t4)

    /* ---- 切换到线程栈 ---- */
    lw      sp, 8(t1)                  /* tx_thread_stack_ptr */

    /* ---- 恢复上下文 ---- */
    j       _tx_restore_context

.Lschedule_wfi:
    /* 无可执行线程, WFI 等待 */
    csrsi   mstatus, 0x08
    wfi
    j       .Lschedule_wait

    .size _tx_thread_schedule, . - _tx_thread_schedule