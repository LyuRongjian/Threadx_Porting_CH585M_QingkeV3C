/***************************************************************************
 * tx_initialize_low_level.c -- ThreadX 底层初始化 (QingKe V3C / CH58x)
 *
 * 由 _tx_initialize_kernel_enter 调用, 职责 (对齐官方 risc-v 移植):
 *   1. 保存系统栈指针: 复用 main 启动栈 (_eusrstack 向下) 作为
 *      调度循环与中断处理使用的系统栈;
 *   2. 记录首个空闲内存地址 (_end, .bss 结束) 供 tx_application_define;
 *   3. 配置 SysTick 周期中断 (HCLK, 自动重装), 使能 PFIC SysTick;
 *   4. SysTick / SWI 设为非抢占优先级 (IPRIOR bit7=1),
 *      禁止硬件中断嵌套, 嵌套统一由移植层 system_state 计数管理。
 *
 * 注意: 不在此调用任何内核内部初始化 (定时器链表等由内核自行完成)。
 * 向量表 (mtvec, 绝对地址模式) 由启动文件 startup_CH585_TX.S 配置;
 * HPE 硬件压栈已关闭 (0x804 = 0), 全软件上下文保存, 见 qingke_hpe_isr.S。
 ***************************************************************************/

#include "tx_api.h"
#include "tx_initialize.h"
#include "tx_thread.h"

/* 链接脚本 Link.ld: .bss 结束 (PROVIDE _end) */
extern CHAR _end[];

/***************************************************************************
 * _tx_initialize_low_level
 ***************************************************************************/
VOID  _tx_initialize_low_level(VOID)
{
    ULONG  sp_now;
    ULONG  reload;

    /* 1. 保存系统栈指针 (当前 main 栈位置, 之下空间供内核/中断使用) */
    __asm__ volatile ("mv %0, sp" : "=r"(sp_now));
    _tx_thread_system_stack_ptr = (VOID *)sp_now;

    /* 2. 首个空闲内存地址 */
    _tx_initialize_unused_memory = (VOID *)_end;

    /* 3. 配置 SysTick (先停再配, 保证首个 tick 在一个完整周期后到来,
     *    此时内核初始化已全部完成) */
    STK_CTLR = 0;
    STK_CNTL = 0;
    STK_SR   = 0;

    reload = (TX_PORT_SYSTICK_HZ / TX_TIMER_TICKS_PER_SECOND) - 1;
    STK_CMPLR = reload;

    /* 4. 非抢占优先级 (bit7=1): 禁止硬件中断嵌套, 嵌套统一由
     *    移植层 system_state 计数管理 */
    PFIC_IPRIOR(12) = 0x80;                    /* SysTick */
    PFIC_IPRIOR(14) = 0x80;                    /* SWI (未使用) */

    /* 5. 启动 SysTick: 计数使能 + 中断使能 + HCLK + 自动重装 */
    STK_CTLR = STK_CTLR_STE | STK_CTLR_STIE | STK_CTLR_STCLK | STK_CTLR_STRE;

    /* 6. PFIC 使能 SysTick (IENR 写 1 置位) */
    PFIC_IENR0 = (1UL << 12);

    __asm__ volatile ("fence.i" ::: "memory");
}
