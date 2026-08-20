/***************************************************************************
 * tx_thread_interrupt_control.S -- 中断开关控制 (QingKe V3C / CH58x)
 *
 * UINT _tx_thread_interrupt_control(UINT new_posture)
 *   入口: a0 = 0 关中断 / 0x8 开中断
 *   返回: a0 = 原中断状态 (mstatus.MIE 位)
 *
 * 关中断后补 3 个 nop, 等待 QingKe 3 级流水线确认 MIE 生效
 * (与 WCH 官方 FreeRTOS/RT-Thread 移植做法一致)。
 ***************************************************************************/

    .section .text
    .align 2
    .global _tx_thread_interrupt_control
    .type   _tx_thread_interrupt_control, @function

_tx_thread_interrupt_control:

    /* 取原状态 */
    csrr    a1, mstatus
    andi    a1, a1, 0x08

    beqz    a0, .Ltx_interrupt_disable

    /* 开中断 */
    csrsi   mstatus, 0x08
    j       .Ltx_interrupt_exit

.Ltx_interrupt_disable:
    /* 关中断 + 流水线延时 */
    csrci   mstatus, 0x08
    nop
    nop
    nop

.Ltx_interrupt_exit:
    mv      a0, a1
    ret

    .size   _tx_thread_interrupt_control, . - _tx_thread_interrupt_control
