/***************************************************************************
 * qingke_vectors.S �� QingKe V3C �ж�������
 *
 * V3C ������: mtvec[1:0] = 0b11 (���Ե�ַģʽ)
 * ÿ����ĿΪ 32 λ����ָ��
 *
 * �ο�: WCH ������ FreeRTOS ��ֲ
 * - SysTick(12): ���� WCH-Interrupt-fast ���? (����ͳһ���?)
 * - SW�ж�(14):  �������л�ר�����?
 * - �ⲿ�ж�(16+): ͳһ�ж����?
 ***************************************************************************/

    .section .interrupt_vector, "aw", @progbits
    .align 10                               /* 1024�ֽڶ��� */
    .global _interrupt_vector_table
    .type   _interrupt_vector_table, @object

_interrupt_vector_table:
    /* 0-11: ���� �� HardFault */
    .rept 12
    .word   _hardfault_handler
    .endr

    /* 12: SysTick �� �����������? (WCH-Interrupt-fast) */
    .word   _tx_systick_handler

    /* 13: ���� */
    .word   _hardfault_handler

    /* 14: �����ж� �� �������л� */
    .word   _tx_context_switch_isr

    /* 15: ���� */
    .word   _hardfault_handler

    /* 16-255: �ⲿ�ж� �� ͳһ���? */
    .rept 240
    .word   _tx_unified_interrupt_entry
    .endr

    .size _interrupt_vector_table, . - _interrupt_vector_table