/***************************************************************************
 * tx_port.h -- ThreadX 移植定义: WCH QingKe V3C (CH58x, RV32IMAC)
 *
 * 移植模型 (参考官方 risc-v32/risc-v64 移植 + WCH EVT FreeRTOS/RT-Thread):
 *   - 中断入口使用 QingKe V3C 硬件压栈 (HPE, INTSYSCR.HWSTKEN=1, 由启动文件配置),
 *     16 个调用者保存寄存器由硬件压入被打断者的栈, mret 时硬件自动弹出;
 *   - 移植层在每个中断入口追加保存 s0-s11/mstatus/mepc 构成 ThreadX 中断栈帧,
 *     并维护 _tx_thread_system_state, 中断退出时统一做出抢占判定;
 *   - 上下文切换不使用 SW 软件中断, 与官方 ThreadX 移植一致;
 *   - 中断处理期间全程关闭 MIE (软件嵌套由 system_state 计数保护)。
 *
 * 线程栈帧布局 (低地址在前, 单位: 字节):
 *   +0x00  帧类型: 0=主动(solicited), 1=中断(interrupt)
 *   +0x04  ra (主动帧) / 保留 (中断帧)
 *   +0x08  s0        +0x0C s1    ... +0x34 s11
 *   +0x38  mstatus
 *   +0x3C  mepc (中断帧) / 保留 (主动帧)
 *   中断帧之后紧跟 HPE 硬件帧 (16 字, 由硬件压栈/出栈, 软件不感知其布局)
 ***************************************************************************/
#ifndef TX_PORT_H
#define TX_PORT_H

/* Determine if the optional ThreadX user define file should be used.  */
#ifdef TX_INCLUDE_USER_DEFINE_FILE
#include "tx_user.h"
#endif

/* Define ThreadX basic types for this port.  */
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

/* Define the priority levels for ThreadX.  Legal values range
   from 32 to 1024 and MUST be evenly divisible by 32.  */
#ifndef TX_MAX_PRIORITIES
#define TX_MAX_PRIORITIES                       32
#endif

/* Define the minimum stack for a ThreadX thread on this processor.
   需容纳: 中断软帧(128B) + C 调用深度。  */
#ifndef TX_MINIMUM_STACK
#define TX_MINIMUM_STACK                        1024
#endif

/* Define the system timer thread's default stack size and priority.  */
#ifndef TX_TIMER_THREAD_STACK_SIZE
#define TX_TIMER_THREAD_STACK_SIZE              1024
#endif

#ifndef TX_TIMER_THREAD_PRIORITY
#define TX_TIMER_THREAD_PRIORITY                0
#endif

/* ==================== QingKe V3C / CH58x 移植参数 ==================== */

/* 每秒系统 tick 数, SysTick 重装值 = TX_PORT_SYSTICK_HZ / TX_TIMER_TICKS_PER_SECOND */
#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND               100
#endif

/* SysTick 计数时钟 (HCLK)。
   默认与本工程 CH58x_common.h 的 SYSCLK_FREQ (HSE_PLL_62_4MHz) 一致,
   若应用修改了系统时钟, 请在 tx_user.h 或编译选项中覆盖本宏。  */
#ifndef TX_PORT_SYSTICK_HZ
#define TX_PORT_SYSTICK_HZ                      62400000UL
#endif

/* ==================== 中断控制 ==================== */
/* ThreadX 中断开关约定: 0 = 关中断, 0x8 = mstatus.MIE */
#define TX_INT_DISABLE                          0x00000000
#define TX_INT_ENABLE                           0x00000008

#ifndef __ASSEMBLER__
UINT                    _tx_thread_interrupt_control(UINT new_posture);
#endif

/* 采用函数实现 (官方移植默认方式), 函数内部带 QingKe 3 级流水线延时,
   保证关中断立即生效。  */
#define TX_INTERRUPT_SAVE_AREA                  register UINT interrupt_save;

#define TX_DISABLE                              interrupt_save =  _tx_thread_interrupt_control(TX_INT_DISABLE);
#define TX_RESTORE                              _tx_thread_interrupt_control(interrupt_save);

/* ==================== PFIC / SysTick 寄存器 (MMIO 直访) ==================== */
#ifndef __ASSEMBLER__

#define PFIC_BASE                               0xE000E000UL
#define PFIC_IENR0                              (*(volatile ULONG *)(PFIC_BASE + 0x100))
/* IPRIOR[n]: 中断优先级寄存器, bit7=1 表示不可被硬件抢占, bit7=0 表示可被抢占 */
#define PFIC_IPRIOR(n)                          (*(volatile UCHAR *)(PFIC_BASE + 0x400 + (n)))

#define STK_CTLR                                (*(volatile ULONG *)0xE000F000)
#define STK_SR                                  (*(volatile ULONG *)0xE000F004)
#define STK_CNTL                                (*(volatile ULONG *)0xE000F008)
#define STK_CMPLR                               (*(volatile ULONG *)0xE000F010)

#define STK_CTLR_STE                            (1UL << 0)    /* 计数器使能 */
#define STK_CTLR_STIE                           (1UL << 1)    /* 中断使能 */
#define STK_CTLR_STCLK                          (1UL << 2)    /* 1=HCLK */
#define STK_CTLR_STRE                           (1UL << 3)    /* 自动重装 */

#endif /* __ASSEMBLER__ */

/* ==================== 内核对象扩展 (无) ==================== */
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

#define TX_BLOCK_POOL_CREATE_EXTENSION(pool_ptr)
#define TX_BYTE_POOL_CREATE_EXTENSION(pool_ptr)
#define TX_EVENT_FLAGS_GROUP_CREATE_EXTENSION(group_ptr)
#define TX_MUTEX_CREATE_EXTENSION(mutex_ptr)
#define TX_QUEUE_CREATE_EXTENSION(queue_ptr)
#define TX_SEMAPHORE_CREATE_EXTENSION(semaphore_ptr)
#define TX_TIMER_CREATE_EXTENSION(timer_ptr)

#define TX_BLOCK_POOL_DELETE_EXTENSION(pool_ptr)
#define TX_BYTE_POOL_DELETE_EXTENSION(pool_ptr)
#define TX_EVENT_FLAGS_GROUP_DELETE_EXTENSION(group_ptr)
#define TX_MUTEX_DELETE_EXTENSION(mutex_ptr)
#define TX_QUEUE_DELETE_EXTENSION(queue_ptr)
#define TX_SEMAPHORE_DELETE_EXTENSION(semaphore_ptr)
#define TX_TIMER_DELETE_EXTENSION(timer_ptr)

/* ==================== 构建选项 ==================== */
#define TX_PORT_SPECIFIC_BUILD_OPTIONS          0
#define TX_INLINE_INITIALIZATION

#ifdef TX_ENABLE_STACK_CHECKING
#undef TX_DISABLE_STACK_FILLING
#endif

/* ==================== 版本标识 ==================== */
#ifndef __ASSEMBLER__
#ifdef TX_THREAD_INIT
CHAR _tx_version_id[] =
    "(c) 2024 Microsoft Corp. ThreadX QingKe V3C/CH58x Port (HPE) 6.4.x";
#else
extern CHAR _tx_version_id[];
#endif
#endif

#endif /* TX_PORT_H */
