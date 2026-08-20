/***************************************************************************
 * tx_initialize_low_level.c �� CH585 �ײ��ʼ��
 *
 * �ο� WCH �ٷ� FreeRTOS/RT-Thread ��ֲ:
 * - Ӳ��ѹջĬ�Ͽ��� (HWSTKEN=1)
 * - �ж�Ƕ��Ĭ�Ϲر� (INESTEN=0)
 * - mscratch �������жϴ���ʹ��
 * - �����ж�ջ
 * - ������ռ���ȼ� (bit7)
 * - SysTick �������
 ***************************************************************************/

#include "tx_api.h"
#include "tx_port.h"

/* ==================== ����ջ���� ==================== */
/* ϵͳջ: ������ʹ�� */
static UCHAR _tx_system_stack[4096] __attribute__((aligned(8)));

/* �ж�ջ: �����жϴ���ʹ�� (WCH �ٷ�ģʽ) */
static UCHAR _tx_irq_stack[4096] __attribute__((aligned(8)));

/* ȫ�ֱ��� (�������) */
ULONG _tx_thread_system_stack_ptr;
ULONG _tx_irq_stack_top;

/* ==================== �ⲿ���� ==================== */
extern void _tx_timer_initialize(VOID);

/* ϵͳʱ��Ƶ�� */
#ifndef SYSTEM_CORE_CLOCK
#define SYSTEM_CORE_CLOCK               60000000UL  /* 60MHz */
#endif

/***************************************************************************
 * _tx_initialize_low_level
 ***************************************************************************/
void _tx_initialize_low_level(VOID)
{
    /* ===== 1. ��ʼ��ջָ�� ===== */
    _tx_thread_system_stack_ptr =
        (ULONG)(_tx_system_stack + sizeof(_tx_system_stack));

    _tx_irq_stack_top =
        (ULONG)(_tx_irq_stack + sizeof(_tx_irq_stack));

    /* ===== 2. ���� intsyscr (CSR 0x804) ===== */
    /*
     * HWSTKEN  = 1  (ʹ��Ӳ��ѹջ)
     * INESTEN  = 0  (�ر��ж�Ƕ��, WCH�ٷ�Ĭ��)
     * GIHWSTKNEN = 0 (��ʼ���ر�)
     * PMTCFG   = 00
     */
    __asm__ volatile(
        "li     t0, 0x01\n"            /* �� HWSTKEN=1 */
        "csrw   0x804, t0\n"
        "fence.i\n"
        ::: "t0", "memory"
    );

    /* ===== 3. ���� mtvec ===== */
    /*
     * V3C: mtvec[1:0] = 0b11 (���Ե�ַ������)
     */
    __asm__ volatile(
        "la     t0, _interrupt_vector_table\n"
        "ori    t0, t0, 0x3\n"
        "csrw   mtvec, t0\n"
        "fence.i\n"
        ::: "t0", "memory"
    );

    /* ===== 4. ���� SysTick ===== */
    STK_CTLR = 0;                       /* ��ֹͣ */
    STK_CNTL = 0;
    STK_CNTH = 0;

    /* �Ƚ�ֵ = ϵͳʱ�� / tickƵ�� - 1 */
    ULONG reload = (SYSTEM_CORE_CLOCK / TX_TIMER_TICKS_PER_SECOND) - 1;
    STK_CMPLR = reload & 0xFFFFFFFF;
    STK_CMPHR = (reload >> 32) & 0xFFFFFFFF;

    /* ʹ��: STE + STIE + STCLK(HCLK) + STRE(�Զ�����) */
    STK_CTLR = STK_CTLR_STE | STK_CTLR_STIE |
               STK_CTLR_STCLK | STK_CTLR_STRE;

    /* ===== 5. �����ж����ȼ� (����, bit7) ===== */
    /*
     * WCH �ٷ�: CH585 ����������ռ���ȼ�
     * bit7=0: ����ռ���ȼ� (Ĭ��)
     * bit7=1: ����ռ���ȼ�
     *
     * ����:
     * - SysTick(12): �����ȼ� (0x00) �� ��֤tick����ʧ
     * - SW(14):      �����ȼ� (0x80) �� �������л��ɱ������жϴ��
     * - �ⲿ(16+):   �����ȼ� (0x00) �� ������Ӧ
     */
    PFIC_IPRIOR(SYSTICK_IRQn) = 0x00;   /* �����ȼ� */
    PFIC_IPRIOR(SW_IRQn)      = 0x80;   /* �����ȼ� */

    for (int i = EXTERNAL_IRQ_BASE; i < 256; i++) {
        PFIC_IPRIOR(i) = 0x00;          /* �����ȼ� */
    }

    /* ===== 6. ʹ�� SysTick �������ж� ===== */
    PFIC_IENR0 = (1UL << SYSTICK_IRQn) | (1UL << SW_IRQn);

    /* fence.i ͬ�� */
    __asm__ volatile("fence.i" ::: "memory");

    /* ===== 7. ��ʼ�� ThreadX ��ʱ�� ===== */
    _tx_timer_initialize();
}