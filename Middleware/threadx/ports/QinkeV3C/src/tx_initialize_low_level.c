/***************************************************************************
 * tx_initialize_low_level.c — CH585 底层初始化
 *
 * 参考 WCH 官方 FreeRTOS/RT-Thread 移植:
 * - 硬件压栈默认开启 (HWSTKEN=1)
 * - 中断嵌套默认关闭 (INESTEN=0)
 * - mscratch 保留给中断处理使用
 * - 独立中断栈
 * - 两级抢占优先级 (bit7)
 * - SysTick 独立入口
 ***************************************************************************/

#include "tx_api.h"
#include "tx_port.h"

/* ==================== 独立栈定义 ==================== */
/* 系统栈: 调度器使用 */
static UCHAR _tx_system_stack[4096] __attribute__((aligned(8)));

/* 中断栈: 所有中断处理使用 (WCH 官方模式) */
static UCHAR _tx_irq_stack[4096] __attribute__((aligned(8)));

/* 全局变量 (汇编引用) */
ULONG _tx_thread_system_stack_ptr;
ULONG _tx_irq_stack_top;

/* ==================== 外部引用 ==================== */
extern void _tx_timer_initialize(VOID);

/* 系统时钟频率 */
#ifndef SYSTEM_CORE_CLOCK
#define SYSTEM_CORE_CLOCK               60000000UL  /* 60MHz */
#endif

/***************************************************************************
 * _tx_initialize_low_level
 ***************************************************************************/
void _tx_initialize_low_level(VOID)
{
    /* ===== 1. 初始化栈指针 ===== */
    _tx_thread_system_stack_ptr =
        (ULONG)(_tx_system_stack + sizeof(_tx_system_stack));

    _tx_irq_stack_top =
        (ULONG)(_tx_irq_stack + sizeof(_tx_irq_stack));

    /* ===== 2. 配置 intsyscr (CSR 0x804) ===== */
    /*
     * HWSTKEN  = 1  (使能硬件压栈)
     * INESTEN  = 0  (关闭中断嵌套, WCH官方默认)
     * GIHWSTKNEN = 0 (初始不关闭)
     * PMTCFG   = 00
     */
    __asm__ volatile(
        "li     t0, 0x01\n"            /* 仅 HWSTKEN=1 */
        "csrw   0x804, t0\n"
        "fence.i\n"
        ::: "t0", "memory"
    );

    /* ===== 3. 配置 mtvec ===== */
    /*
     * V4C: mtvec[1:0] = 0b11 (绝对地址向量表)
     */
    __asm__ volatile(
        "la     t0, _interrupt_vector_table\n"
        "ori    t0, t0, 0x3\n"
        "csrw   mtvec, t0\n"
        "fence.i\n"
        ::: "t0", "memory"
    );

    /* ===== 4. 配置 SysTick ===== */
    STK_CTLR = 0;                       /* 先停止 */
    STK_CNTL = 0;
    STK_CNTH = 0;

    /* 比较值 = 系统时钟 / tick频率 - 1 */
    ULONG reload = (SYSTEM_CORE_CLOCK / TX_TIMER_TICKS_PER_SECOND) - 1;
    STK_CMPLR = reload & 0xFFFFFFFF;
    STK_CMPHR = (reload >> 32) & 0xFFFFFFFF;

    /* 使能: STE + STIE + STCLK(HCLK) + STRE(自动重载) */
    STK_CTLR = STK_CTLR_STE | STK_CTLR_STIE |
               STK_CTLR_STCLK | STK_CTLR_STRE;

    /* ===== 5. 配置中断优先级 (两级, bit7) ===== */
    /*
     * WCH 官方: CH585 仅有两级抢占优先级
     * bit7=0: 高抢占优先级 (默认)
     * bit7=1: 低抢占优先级
     *
     * 策略:
     * - SysTick(12): 高优先级 (0x00) — 保证tick不丢失
     * - SW(14):      低优先级 (0x80) — 上下文切换可被外设中断打断
     * - 外部(16+):   高优先级 (0x00) — 快速响应
     */
    PFIC_IPRIOR(SYSTICK_IRQn) = 0x00;   /* 高优先级 */
    PFIC_IPRIOR(SW_IRQn)      = 0x80;   /* 低优先级 */

    for (int i = EXTERNAL_IRQ_BASE; i < 256; i++) {
        PFIC_IPRIOR(i) = 0x00;          /* 高优先级 */
    }

    /* ===== 6. 使能 SysTick 和软件中断 ===== */
    PFIC_IENR0 = (1UL << SYSTICK_IRQn) | (1UL << SW_IRQn);

    /* fence.i 同步 */
    __asm__ volatile("fence.i" ::: "memory");

    /* ===== 7. 初始化 ThreadX 定时器 ===== */
    _tx_timer_initialize();
}