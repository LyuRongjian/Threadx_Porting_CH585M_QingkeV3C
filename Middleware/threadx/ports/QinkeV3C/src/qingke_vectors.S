/***************************************************************************
 * qingke_vectors.S — QingKe V4C 中断向量表
 *
 * V4C 向量表: mtvec[1:0] = 0b11 (绝对地址模式)
 * 每个条目为 32 位函数指针
 *
 * 参考: WCH 官方 FreeRTOS 移植
 * - SysTick(12): 独立 WCH-Interrupt-fast 入口 (不走统一入口)
 * - SW中断(14):  上下文切换专用入口
 * - 外部中断(16+): 统一中断入口
 ***************************************************************************/

    .section .interrupt_vector, "aw", @progbits
    .align 10                               /* 1024字节对齐 */
    .global _interrupt_vector_table
    .type   _interrupt_vector_table, @object

_interrupt_vector_table:
    /* 0-11: 保留 → HardFault */
    .rept 12
    .word   _hardfault_handler
    .endr

    /* 12: SysTick — 独立快速入口 (WCH-Interrupt-fast) */
    .word   _tx_systick_handler

    /* 13: 保留 */
    .word   _hardfault_handler

    /* 14: 软件中断 — 上下文切换 */
    .word   _tx_context_switch_isr

    /* 15: 保留 */
    .word   _hardfault_handler

    /* 16-255: 外部中断 → 统一入口 */
    .rept 240
    .word   _tx_unified_interrupt_entry
    .endr

    .size _interrupt_vector_table, . - _interrupt_vector_table