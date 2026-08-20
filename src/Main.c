/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2020/08/06
 * Description        : CH585 ThreadX (QingKe V3C HPE 移植) 演示:
 *                      两个线程周期性通过 UART1 打印, 验证 tick/调度/休眠。
 *******************************************************************************/
#include "CH58x_common.h"
#include "tx_api.h"

#define DEMO_STACK_SIZE         1024
#define DEMO_THREAD0_PRIORITY   2
#define DEMO_THREAD1_PRIORITY   3

TX_THREAD   demo_thread_0;
TX_THREAD   demo_thread_1;

UCHAR       demo_thread_0_stack[DEMO_STACK_SIZE];
UCHAR       demo_thread_1_stack[DEMO_STACK_SIZE];

/* 线程 0: 每 100 tick (1s) 打印一次 */
static void demo_thread_0_entry(ULONG thread_input)
{
    (void)thread_input;
    while (1)
    {
        UART1_SendString((uint8_t *)"ThreadX CH585: thread 0\r\n", sizeof("ThreadX CH585: thread 0\r\n"));
        tx_thread_sleep(100);
    }
}

/* 线程 1: 每 50 tick (0.5s) 打印一次, 与线程 0 形成周期切换 */
static void demo_thread_1_entry(ULONG thread_input)
{
    (void)thread_input;
    while (1)
    {
        UART1_SendString((uint8_t *)"ThreadX CH585: thread 1\r\n", sizeof("ThreadX CH585: thread 1\r\n"));
        tx_thread_sleep(50);
    }
}

/*********************************************************************
 * @fn      tx_application_define
 *
 * @brief   ThreadX 应用初始化, tx_kernel_initialize 期间被调用
 *
 * @return  none
 */
void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    tx_thread_create(&demo_thread_0, "thread 0", demo_thread_0_entry, 0,
                     demo_thread_0_stack, DEMO_STACK_SIZE,
                     DEMO_THREAD0_PRIORITY, DEMO_THREAD0_PRIORITY,
                     TX_NO_TIME_SLICE, TX_AUTO_START);

    tx_thread_create(&demo_thread_1, "thread 1", demo_thread_1_entry, 0,
                     demo_thread_1_stack, DEMO_STACK_SIZE,
                     DEMO_THREAD1_PRIORITY, DEMO_THREAD1_PRIORITY,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数: 时钟/串口初始化后进入 ThreadX 内核
 *
 * @return  never returns
 */
int main(void)
{
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);

    /* 调试串口: UART1 (PA8=RXD, PA9=TXD) */
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    UART1_SendString((uint8_t *)"ThreadX CH585 port start\r\n", sizeof("ThreadX CH585 port start\r\n"));

    /* 进入 ThreadX (不返回) */
    tx_kernel_enter();

    while (1)
    {
        /* 不应到达这里 */
    }
}
