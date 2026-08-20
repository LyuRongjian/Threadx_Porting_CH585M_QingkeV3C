/***************************************************************************
 * tx_port.h — ThreadX Port for WCH CH585 (QingKe V4C RISC-V)
 *
 * 参考: openwch/ch585 EVT FreeRTOS/RT-Thread 官方移植
 * 关键特性:
 *   - 硬件压栈 (HPE) 用于普通中断快速响应
 *   - mscratch 作为 SP 临时寄存器 (WCH 官方模式)
 *   - 独立中断栈
 *   - SysTick 使用 WCH-Interrupt-fast 独立入口
 *   - 两级抢占优先级 (PFIC_IPRIOR bit7)
 ***************************************************************************/
#ifndef TX_PORT_H
#define TX_PORT_H

#include <stdint.h>

/* ==================== 基本类型 ==================== */
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

/* ==================== ThreadX 配置 ==================== */
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

/* ==================== QingKe V4C / CH585 硬件定义 ==================== */

/* PFIC 基地址 */
#define PFIC_BASE                               0xE000E000UL

/* PFIC 中断使能寄存器 */
#define PFIC_IENR0                              (*(volatile ULONG *)(PFIC_BASE + 0x100))
#define PFIC_IENR1                              (*(volatile ULONG *)(PFIC_BASE + 0x104))
#define PFIC_IENR2                              (*(volatile ULONG *)(PFIC_BASE + 0x108))
#define PFIC_IENR3                              (*(volatile ULONG *)(PFIC_BASE + 0x10C))

/* PFIC 中断挂起/清除 */
#define PFIC_IPSR0                              (*(volatile ULONG *)(PFIC_BASE + 0x200))
#define PFIC_IPRR0                              (*(volatile ULONG *)(PFIC_BASE + 0x280))

/* PFIC 系统控制 */
#define PFIC_SCTLR                              (*(volatile ULONG *)(PFIC_BASE + 0xD10))

/* PFIC 中断优先级 (每中断1字节, bit7=抢占优先级) */
#define PFIC_IPRIOR_BASE                        (PFIC_BASE + 0x400)
#define PFIC_IPRIOR(n)                          (*(volatile UCHAR *)(PFIC_IPRIOR_BASE + (n)))

/* SysTick 寄存器 (QingKe V3B/V4C) */
#define STK_CTLR                                (*(volatile ULONG *)0xE000F000)
#define STK_SR                                  (*(volatile ULONG *)0xE000F004)
#define STK_CNTL                                (*(volatile ULONG *)0xE000F008)
#define STK_CNTH                                (*(volatile ULONG *)0xE000F00C)
#define STK_CMPLR                               (*(volatile ULONG *)0xE000F010)
#define STK_CMPHR                               (*(volatile ULONG *)0xE000F014)

/* SysTick 控制位 */
#define STK_CTLR_STE                            (1UL << 0)    /* 计数器使能 */
#define STK_CTLR_STIE                           (1UL << 1)    /* 中断使能 */
#define STK_CTLR_STCLK                          (1UL << 2)    /* 1=HCLK, 0=外部时钟 */
#define STK_CTLR_STRE                           (1UL << 3)    /* 自动重装载 */
#define STK_CTLR_MODE                           (1UL << 4)    /* 1=向下计数 */

/* STK_SR 位定义 */
#define STK_SR_CNTIF                            (1UL << 0)    /* 计数中断标志 */
#define STK_SR_SWIE                             (1UL << 31)   /* 软件中断使能/触发 */

/* intsyscr CSR (0x804) 位定义 */
#define INTSYSCR_HWSTKEN                        (1UL << 0)    /* 硬件压栈使能 */
#define INTSYSCR_INESTEN                        (1UL << 1)    /* 中断嵌套使能 */
#define INTSYSCR_GIHWSTKNEN                     (1UL << 5)    /* 全局禁止硬件出栈(mret) */

/* 中断号 */
#define SYSTICK_IRQn                            12
#define SW_IRQn                                 14
#define EXTERNAL_IRQ_BASE                       16

/* 栈帧类型标记 */
#define FRAME_TYPE_SOLICITED                    0
#define FRAME_TYPE_INTERRUPT                    1

/* 硬件压栈大小: 16 regs × 4 bytes = 64 bytes */
#define HW_FRAME_SIZE                           64

/* ==================== 中断控制宏 ==================== */
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

/* ==================== 触发上下文切换 ==================== */
/* 通过触发软件中断 (SW_IRQn=14) 实现上下文切换 */
#define TX_PORT_TRIGGER_CONTEXT_SWITCH()        \
    do {                                        \
        STK_SR |= STK_SR_SWIE;                 \
    } while(0)

/* ==================== 扩展定义 ==================== */
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

/* ==================== 版本标识 ==================== */
#ifndef __ASSEMBLER__
#ifdef TX_THREAD_INIT
CHAR _tx_version_id[] =
    "(c) 2024 Microsoft Corp. ThreadX QingKe V4C/CH585 Port (WCH-style)";
#else
extern CHAR _tx_version_id[];
#endif
#endif

#endif /* TX_PORT_H */