/***************************************************************************
 * tx_timer_interrupt.S -- SysTick 系统 tick 处理 (QingKe V3C / CH58x)
 *
 * 由 SysTick_Handler (qingke_hpe_isr.S) 调用, 在系统栈上执行:
 *   1. _tx_timer_system_clock++;
 *   2. 时间片递减, 到期置 _tx_timer_expired_time_slice 并调用
 *      _tx_thread_time_slice (同优先级轮转);
 *   3. 定时器链表推进, 有定时器到期时调用 _tx_timer_expiration_process
 *      (官方 risc-v32 移植方式); 该函数内部唤醒 _tx_timer_thread
 *      处理过期定时器链表, 并维护 preempt_disable 平衡。
 *
 * 逻辑与官方 risc-v32/risc-v64 移植的 tx_timer_interrupt.S 一致。
 ***************************************************************************/

    .section .highcode.tx_timer_interrupt, "ax", @progbits
    .align 2
    .global _tx_timer_interrupt
    .type   _tx_timer_interrupt, @function

_tx_timer_interrupt:

    /* _tx_timer_system_clock++ */
    la      t0, _tx_timer_system_clock
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

    /* 读时间片, 清本地到期标志 */
    la      t2, _tx_timer_time_slice
    lw      t3, 0(t2)
    li      t6, 0

    /* ==== 时间片处理 ==== */
    beqz    t3, .Ltimer_no_time_slice
    addi    t3, t3, -1
    sw      t3, 0(t2)
    bgtz    t3, .Ltimer_no_time_slice

    li      t1, 1
    la      t4, _tx_timer_expired_time_slice
    sw      t1, 0(t4)
    ori     t6, t6, 1

.Ltimer_no_time_slice:

    /* ==== 定时器链表处理 ==== */
    la      t0, _tx_timer_current_ptr
    lw      t1, 0(t0)
    lw      t3, 0(t1)
    beqz    t3, .Ltimer_no_timer

    /* 有定时器到期 */
    la      t2, _tx_timer_expired
    li      t4, 1
    sw      t4, 0(t2)
    ori     t6, t6, 2
    j       .Ltimer_done

.Ltimer_no_timer:
    /* 无到期: current_ptr++, 越界回卷 */
    la      t2, _tx_timer_list_end
    lw      t3, 0(t2)
    addi    t1, t1, 4
    sw      t1, 0(t0)
    bne     t1, t3, .Ltimer_done
    la      t2, _tx_timer_list_start
    lw      t4, 0(t2)
    sw      t4, 0(t0)

.Ltimer_done:
    beqz    t6, .Ltimer_nothing_expired

    addi    sp, sp, -16
    sw      t6, 0(sp)
    sw      ra, 4(sp)

    /* 定时器到期处理 -- 始终调用 _tx_timer_expiration_process
       (与官方 risc-v32 移植一致)。该函数内部会:
         1) preempt_disable++  (平衡后续 _tx_thread_system_resume 的 preempt_disable--)
         2) 唤醒 _tx_timer_thread 处理过期定时器链表
       若直接调用 _tx_thread_system_resume 会缺少 preempt_disable++,
       导致 preempt_disable 下溢, 线程无法睡眠, 系统挂起。 */
    andi    t2, t6, 2
    beqz    t2, .Ltimer_dont_activate
    call    _tx_timer_expiration_process
    lw      t6, 0(sp)

.Ltimer_dont_activate:
    /* 时间片到期处理 */
    andi    t2, t6, 1
    beqz    t2, .Ltimer_not_ts_expiration
    call    _tx_thread_time_slice

.Ltimer_not_ts_expiration:
    lw      ra, 4(sp)
    addi    sp, sp, 16

.Ltimer_nothing_expired:
    ret

    .size   _tx_timer_interrupt, . - _tx_timer_interrupt
