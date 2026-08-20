/***************************************************************************
 * tx_port.h �� ThreadX Port for WCH CH585 (QingKe V3C RISC-V)
 *
 * �ο�: openwch/ch585 EVT FreeRTOS/RT-Thread �ٷ���ֲ
 * �ؼ�����:
 *   - Ӳ��ѹջ (HPE) ������ͨ�жϿ�����Ӧ
 *   - mscratch ��Ϊ SP ��ʱ�Ĵ��� (WCH �ٷ�ģʽ)
 *   - �����ж�ջ
 *   - SysTick ʹ�� WCH-Interrupt-fast �������
 *   - ������ռ���ȼ� (PFIC_IPRIOR bit7)
 ***************************************************************************/
#ifndef TX_PORT_H
#define TX_PORT_H

#include <stdint.h>

/* ==================== �������� ==================== */
#define VOID                                    void
typedef char                                    CHAR;
typedef unsigned char                           UCHAR;
typedef int                                     INT;
typedef unsigned int                            UINT;
typedef long                                    LONG;
typedef unsigned long                           ULONG;
typedef unsigned long long                      ULONG64;
typedef short                                   SHORT;
typedef unsigned short                          USHORT;
#define ULONG64_DEFINED

/* ==================== ThreadX ���� ==================== */
#ifndef TX_MAX_PRIORITIES
#define TX_MAX_PRIORITIES                       32
#endif

#ifndef TX_MINIMUM_STACK
#define TX_MINIMUM_STACK                        512
#endif

#ifndef TX_TIMER_THREAD_STACK_SIZE
#define TX_TIMER_THREAD_STACK_SIZE              1024
#endif

#ifndef TX_TIMER_THREAD_PRIORITY
#define TX_TIMER_THREAD_PRIORITY                0
#endif

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND               1000
#endif

/* ==================== QingKe V3C / CH585 Ӳ������ ==================== */

/* PFIC ����ַ */
#define PFIC_BASE                               0xE000E000UL

/* PFIC �ж�ʹ�ܼĴ��� */
#define PFIC_IENR0                              (*(volatile ULONG *)(PFIC_BASE + 0x100))
#define PFIC_IENR1                              (*(volatile ULONG *)(PFIC_BASE + 0x104))
#define PFIC_IENR2                              (*(volatile ULONG *)(PFIC_BASE + 0x108))
#define PFIC_IENR3                              (*(volatile ULONG *)(PFIC_BASE + 0x10C))

/* PFIC �жϹ���/��� */
#define PFIC_IPSR0                              (*(volatile ULONG *)(PFIC_BASE + 0x200))
#define PFIC_IPRR0                              (*(volatile ULONG *)(PFIC_BASE + 0x280))

/* PFIC ϵͳ���� */
#define PFIC_SCTLR                              (*(volatile ULONG *)(PFIC_BASE + 0xD10))

/* PFIC �ж����ȼ� (ÿ�ж�1�ֽ�, bit7=��ռ���ȼ�) */
#define PFIC_IPRIOR_BASE                        (PFIC_BASE + 0x400)
#define PFIC_IPRIOR(n)                          (*(volatile UCHAR *)(PFIC_IPRIOR_BASE + (n)))

/* SysTick �Ĵ��� (QingKe V3C) */
#define STK_CTLR                                (*(volatile ULONG *)0xE000F000)
#define STK_SR                                  (*(volatile ULONG *)0xE000F004)
#define STK_CNTL                                (*(volatile ULONG *)0xE000F008)
#define STK_CNTH                                (*(volatile ULONG *)0xE000F00C)
#define STK_CMPLR                               (*(volatile ULONG *)0xE000F010)
#define STK_CMPHR                               (*(volatile ULONG *)0xE000F014)

/* SysTick ����λ */
#define STK_CTLR_STE                            (1UL << 0)    /* ������ʹ�� */
#define STK_CTLR_STIE                           (1UL << 1)    /* �ж�ʹ�� */
#define STK_CTLR_STCLK                          (1UL << 2)    /* 1=HCLK, 0=�ⲿʱ�� */
#define STK_CTLR_STRE                           (1UL << 3)    /* �Զ���װ�� */
#define STK_CTLR_MODE                           (1UL << 4)    /* 1=���¼��� */

/* STK_SR λ���� */
#define STK_SR_CNTIF                            (1UL << 0)    /* �����жϱ�־ */
#define STK_SR_SWIE                             (1UL << 31)   /* �����ж�ʹ��/���� */

/* intsyscr CSR (0x804) λ���� */
#define INTSYSCR_HWSTKEN                        (1UL << 0)    /* Ӳ��ѹջʹ�� */
#define INTSYSCR_INESTEN                        (1UL << 1)    /* �ж�Ƕ��ʹ�� */
#define INTSYSCR_GIHWSTKNEN                     (1UL << 5)    /* ȫ�ֽ�ֹӲ����ջ(mret) */

/* �жϺ� */
#define SYSTICK_IRQn                            12
#define SW_IRQn                                 14
#define EXTERNAL_IRQ_BASE                       16

/* ջ֡���ͱ�� */
#define FRAME_TYPE_SOLICITED                    0
#define FRAME_TYPE_INTERRUPT                    1

/* Ӳ��ѹջ��С: 16 regs �� 4 bytes = 64 bytes */
#define HW_FRAME_SIZE                           64

/* ==================== �жϿ��ƺ� ==================== */
#define TX_INT_DISABLE                          0x00000000
#define TX_INT_ENABLE                           0x00000008

#define TX_INTERRUPT_SAVE_AREA                  register UINT interrupt_save;

#define TX_DISABLE                              \
    __asm__ volatile(                           \
        "csrrci %0, mstatus, 8"                 \
        : "=r"(interrupt_save)                  \
        :: "memory"                             \
    );

#define TX_RESTORE                              \
    if (interrupt_save & 0x8) {                 \
        __asm__ volatile(                       \
            "csrsi mstatus, 8"                  \
            ::: "memory"                        \
        );                                      \
    }

/* ==================== �����������л� ==================== */
/* ͨ�����������ж� (SW_IRQn=14) ʵ���������л� */
#define TX_PORT_TRIGGER_CONTEXT_SWITCH()        \
    do {                                        \
        STK_SR |= STK_SR_SWIE;                 \
    } while(0)

/* ==================== ��չ���� ==================== */
#define TX_THREAD_EXTENSION_0
#define TX_THREAD_EXTENSION_1
#define TX_THREAD_EXTENSION_2
#define TX_THREAD_EXTENSION_3

#define TX_BLOCK_POOL_EXTENSION
#define TX_BYTE_POOL_EXTENSION
#define TX_EVENT_FLAGS_GROUP_EXTENSION
#define TX_MUTEX_EXTENSION
#define TX_QUEUE_EXTENSION
#define TX_SEMAPHORE_EXTENSION
#define TX_TIMER_EXTENSION

#ifndef TX_THREAD_USER_EXTENSION
#define TX_THREAD_USER_EXTENSION
#endif

#define TX_THREAD_CREATE_EXTENSION(thread_ptr)
#define TX_THREAD_DELETE_EXTENSION(thread_ptr)
#define TX_THREAD_COMPLETED_EXTENSION(thread_ptr)
#define TX_THREAD_TERMINATED_EXTENSION(thread_ptr)

#define TX_PORT_SPECIFIC_BUILD_OPTIONS          0
#define TX_INLINE_INITIALIZATION

#ifdef TX_ENABLE_STACK_CHECKING
#undef TX_DISABLE_STACK_FILLING
#endif

/* ==================== �汾��ʶ ==================== */
#ifndef __ASSEMBLER__
#ifdef TX_THREAD_INIT
CHAR _tx_version_id[] =
    "(c) 2024 Microsoft Corp. ThreadX QingKe V3C/CH585 Port (WCH-style)";
#else
extern CHAR _tx_version_id[];
#endif
#endif

#endif /* TX_PORT_H */