/***************************************************************************
 * tx_systick_handler.c — SysTick 独立快速入口
 *
 * 参考 WCH 官方:
 * "因为Systick中断经常处理，所以Systick中断不从统一中断入口处理，
 *  并使用了 WCH-Interrupt-fast 修饰，Systick中调用的函数栈不大，
 *  所以未切换为中断栈，提升速度。"
 *
 * 对于 ThreadX: SysTick 仅负责 tick 计数,
 * 如需上下文切换则触发 SW 中断
 ***************************************************************************/

#include "tx_api.h"
#include "tx_port.h"

/* 前向声明 */
extern void _tx_timer_interrupt(VOID);

/***************************************************************************
 * SysTick 中断处理
 * 
 * 使用 WCH-Interrupt-fast 属性:
 * - 编译器自动生成硬件压栈入口/出口代码
 * - 不保存 callee-saved (假设不调用复杂函数)
 * - mret 时硬件自动出栈
 * 
 * 注意: 此函数在中断栈上执行(不切换), 栈消耗极小
 ***************************************************************************/
__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void _tx_systick_handler(void)
{
    /* 清除 SysTick 中断标志 */
    STK_SR |= STK_SR_CNTIF;

    /* 调用 ThreadX 定时器中断处理 */
    _tx_timer_interrupt();

    /* 
     * 注意: _tx_timer_interrupt 内部如果需要上下文切换,
     * 会调用 TX_PORT_TRIGGER_CONTEXT_SWITCH() 触发 SW 中断。
     * SysTick 本身不做上下文切换, 直接 mret 返回。
     * 硬件自动出栈恢复 caller-saved 寄存器。
     */
}