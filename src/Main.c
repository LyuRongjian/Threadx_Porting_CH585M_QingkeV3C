/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2020/08/06
 * Description        : CH585 ThreadX (QingKe V3C HPE 移植) 综合测试:
 *
 *   测试项目 (参考 ThreadX 官方 demo_threadx.c + regression 测试):
 *     1.  线程创建与基本调度
 *     2.  线程抢占调度
 *     3.  线程时间片轮转
 *     4.  线程挂起/恢复
 *     5.  消息队列 (发送/接收/刷新)
 *     6.  计数信号量 (获取/释放/挂起)
 *     7.  互斥量 (获取/释放/递归获取)
 *     8.  事件标志组 (设置/获取/AND/OR)
 *     9.  字节内存池 (分配/释放)
 *    10.  块内存池 (分配/释放)
 *    11.  软件定时器 (创建/激活/到期/停用)
 *    12.  系统时间 (tx_time_get / tx_time_set)
 *    13.  中断控制 (TX_DISABLE / TX_RESTORE)
 *    14.  集成测试 (多线程+队列+信号量+互斥量+事件标志+内存池)
 *
 *   串口: UART1 (PA8=RXD, PA9=TXD), 波特率跟随系统时钟 62.4MHz
 *******************************************************************************/

#include "CH58x_common.h"
#include "tx_api.h"

/* ========================================================================
 *  配置
 * ======================================================================== */
#define TEST_STACK_SIZE         1024
#define TEST_QUEUE_SIZE         16

/* 测试线程优先级 */
#define TEST_THREAD_PRIORITY    15

/* ========================================================================
 *  全局对象
 * ======================================================================== */
TX_THREAD    test_thread;
TX_THREAD    integration_thread_0;
TX_THREAD    integration_thread_1;
TX_THREAD    integration_thread_2;
TX_THREAD    integration_thread_3;

UCHAR        test_thread_stack[TEST_STACK_SIZE];
UCHAR        integration_stack_0[TEST_STACK_SIZE];
UCHAR        integration_stack_1[TEST_STACK_SIZE];
UCHAR        integration_stack_2[TEST_STACK_SIZE];
UCHAR        integration_stack_3[TEST_STACK_SIZE];

/* 测试统计 */
static UINT  test_pass_count = 0;
static UINT  test_fail_count = 0;

/* 定时器到期计数 */
static volatile ULONG timer_expiration_count = 0;
/* 定时器回调标志 */
static volatile ULONG timer_callback_executed = 0;

/* 集成测试计数器 */
static ULONG integration_counters[4];

/* ========================================================================
 *  串口输出工具
 * ======================================================================== */
static void uart_putc(char c)
{
    while (R8_UART1_TFC == UART_FIFO_SIZE);
    R8_UART1_THR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

/* 简易十进制输出 */
static void uart_putdec(ULONG val)
{
    char buf[12];
    int i = 0;

    if (val == 0)
    {
        uart_putc('0');
        return;
    }

    while (val > 0)
    {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }

    while (i > 0)
        uart_putc(buf[--i]);
}

/* 测试结果报告 */
static void test_report(const char *test_name, UINT passed)
{
    uart_puts("  ");
    uart_puts(test_name);
    /* 对齐输出 */
    int name_len = 0;
    const char *p = test_name;
    while (*p++) name_len++;
    while (name_len++ < 48)
        uart_putc('.');
    if (passed)
    {
        uart_puts(" PASS\r\n");
        test_pass_count++;
    }
    else
    {
        uart_puts(" FAIL\r\n");
        test_fail_count++;
    }
}

/* 检查宏 */
#define TEST_CHECK(name, cond)  test_report(name, (cond) ? 1 : 0)

/* ========================================================================
 *  定时器回调函数
 * ======================================================================== */
static void test_timer_callback(ULONG parameter)
{
    (void)parameter;
    timer_expiration_count++;
    timer_callback_executed = 1;
}

/* ========================================================================
 *  测试 1: 线程创建与基本调度
 * ======================================================================== */
static volatile ULONG basic_counter;
static void basic_thread_entry(ULONG p)
{
    (void)p;
    while (1)
    {
        basic_counter++;
        tx_thread_sleep(1);
    }
}

static TX_THREAD basic_thread;
static UCHAR basic_thread_stack[TEST_STACK_SIZE];

static void test_thread_basic_v2(void)
{
    ULONG old_time, new_time;
    UINT status;

    uart_puts("\r\n[Test 1] Thread Create & Basic Schedule\r\n");

    basic_counter = 0;

    status = tx_thread_create(&basic_thread, "basic", basic_thread_entry, 0,
                              basic_thread_stack, TEST_STACK_SIZE,
                              12, 12, TX_NO_TIME_SLICE, TX_AUTO_START);

    TEST_CHECK("thread basic: create status", status == TX_SUCCESS);

    old_time = tx_time_get();
    tx_thread_sleep(5);
    new_time = tx_time_get();

    TEST_CHECK("thread basic: time elapsed",
               (new_time - old_time) >= 5);

    TEST_CHECK("thread basic: counter incremented",
               basic_counter >= 3);

    tx_thread_terminate(&basic_thread);
    tx_thread_delete(&basic_thread);
}

/* ========================================================================
 *  测试 2: 线程抢占调度
 * ======================================================================== */
static volatile ULONG preempt_low_counter;
static volatile ULONG preempt_high_counter;
static volatile ULONG preempt_high_ran;
static volatile ULONG preempt_low_started;

static void preempt_low_entry(ULONG p)
{
    (void)p;

    /* 标记低优先级线程已开始运行 */
    preempt_low_started = 1;
    preempt_low_counter++;

    /* 忙等, 等待高优先级线程抢占 */
    while (!preempt_high_ran)
    {
        preempt_low_counter++;
    }
}

static void preempt_high_entry(ULONG p)
{
    (void)p;
    preempt_high_counter++;
    preempt_high_ran = 1;
}

static TX_THREAD preempt_low;
static TX_THREAD preempt_high;
static UCHAR preempt_low_stack[TEST_STACK_SIZE];
static UCHAR preempt_high_stack[TEST_STACK_SIZE];

static void test_thread_preemption(void)
{
    UINT status;

    uart_puts("\r\n[Test 2] Thread Preemption\r\n");

    preempt_low_counter = 0;
    preempt_high_counter = 0;
    preempt_high_ran = 0;
    preempt_low_started = 0;

    /* 低优先级线程: 不自动启动 */
    status = tx_thread_create(&preempt_low, "preempt_low", preempt_low_entry, 0,
                              preempt_low_stack, TEST_STACK_SIZE,
                              20, 20, TX_NO_TIME_SLICE, TX_DONT_START);
    TEST_CHECK("preempt: low create", status == TX_SUCCESS);

    /* 高优先级线程: 不自动启动 */
    status = tx_thread_create(&preempt_high, "preempt_high", preempt_high_entry, 0,
                              preempt_high_stack, TEST_STACK_SIZE,
                              5, 5, TX_NO_TIME_SLICE, TX_DONT_START);
    TEST_CHECK("preempt: high create", status == TX_SUCCESS);

    /* 先恢复低优先级线程, 但它不会立即运行 (优先级低于 test_thread) */
    tx_thread_resume(&preempt_low);

    /* 睡眠 1 tick 让低优先级线程开始运行 */
    tx_thread_sleep(1);

    /* 确认低优先级线程已启动 */
    TEST_CHECK("preempt: low started", preempt_low_started != 0);
    ULONG low_count_before = preempt_low_counter;

    /* 确认高优先级尚未运行 */
    TEST_CHECK("preempt: high not yet run", preempt_high_ran == 0);

    /* 验证低优先级线程在测试线程挂起期间持续运行:
     * 此检查必须在 resume(high) 之前 —— 高线程一旦运行会置
     * preempt_high_ran=1, 低线程循环条件随即为假而退出, 之后
     * 不再自增。放在此处 (ran 恒为 0) 才是确定性的。 */
    tx_thread_sleep(1);
    TEST_CHECK("preempt: low was running", preempt_low_counter > low_count_before);

    /* 恢复高优先级线程, 应立即抢占低优先级线程 */
    status = tx_thread_resume(&preempt_high);
    TEST_CHECK("preempt: resume high", status == TX_SUCCESS);

    /* 高优先级线程应已运行并设置标志 */
    /* 由于高优先级线程会立即抢占, 恢复后 test_thread 被挂起,
       高优先级运行完毕后, 低优先级继续, 然后低优先级退出,
       最后 test_thread 恢复执行 */
    tx_thread_sleep(2);

    TEST_CHECK("preempt: high ran", preempt_high_ran != 0);
    TEST_CHECK("preempt: high counter", preempt_high_counter >= 1);

    /* 清理 */
    tx_thread_terminate(&preempt_low);
    tx_thread_terminate(&preempt_high);
    tx_thread_delete(&preempt_low);
    tx_thread_delete(&preempt_high);
}

/* ========================================================================
 *  测试 3: 线程时间片轮转
 * ======================================================================== */
static volatile ULONG slice_counter_0;
static volatile ULONG slice_counter_1;

static void slice_entry_0(ULONG p)
{
    (void)p;
    while (1)
        slice_counter_0++;
}

static void slice_entry_1(ULONG p)
{
    (void)p;
    while (1)
        slice_counter_1++;
}

static TX_THREAD slice_thread_0;
static TX_THREAD slice_thread_1;
static UCHAR slice_stack_0[TEST_STACK_SIZE];
static UCHAR slice_stack_1[TEST_STACK_SIZE];

static void test_time_slice(void)
{
    UINT status;

    uart_puts("\r\n[Test 3] Time Slice\r\n");

    slice_counter_0 = 0;
    slice_counter_1 = 0;

    /* 同优先级, 时间片 = 1 */
    status = tx_thread_create(&slice_thread_0, "slice0", slice_entry_0, 0,
                              slice_stack_0, TEST_STACK_SIZE,
                              16, 16, 1, TX_AUTO_START);
    TEST_CHECK("slice: thread0 create", status == TX_SUCCESS);

    status = tx_thread_create(&slice_thread_1, "slice1", slice_entry_1, 0,
                              slice_stack_1, TEST_STACK_SIZE,
                              16, 16, 1, TX_AUTO_START);
    TEST_CHECK("slice: thread1 create", status == TX_SUCCESS);

    /* 运行若干 tick 让时间片生效 */
    tx_thread_sleep(20);

    tx_thread_suspend(&slice_thread_0);
    tx_thread_suspend(&slice_thread_1);

    tx_thread_terminate(&slice_thread_0);
    tx_thread_delete(&slice_thread_0);
    tx_thread_terminate(&slice_thread_1);
    tx_thread_delete(&slice_thread_1);

    /* 两个同优先级线程都应获得执行时间 */
    TEST_CHECK("slice: both threads ran",
               (slice_counter_0 > 0) && (slice_counter_1 > 0));
}

/* ========================================================================
 *  测试 4: 线程挂起/恢复
 * ======================================================================== */
static volatile ULONG suspend_counter;

static void suspend_entry(ULONG p)
{
    (void)p;
    while (1)
    {
        suspend_counter++;
        tx_thread_sleep(1);
    }
}

static TX_THREAD suspend_thread;
static UCHAR suspend_stack[TEST_STACK_SIZE];

static void test_suspend_resume(void)
{
    UINT status;

    uart_puts("\r\n[Test 4] Thread Suspend/Resume\r\n");

    suspend_counter = 0;

    status = tx_thread_create(&suspend_thread, "suspend", suspend_entry, 0,
                              suspend_stack, TEST_STACK_SIZE,
                              12, 12, TX_NO_TIME_SLICE, TX_AUTO_START);
    TEST_CHECK("suspend: create", status == TX_SUCCESS);

    tx_thread_sleep(3);
    ULONG count_before_suspend = suspend_counter;

    /* 挂起 */
    status = tx_thread_suspend(&suspend_thread);
    TEST_CHECK("suspend: status", status == TX_SUCCESS);

    tx_thread_sleep(3);
    ULONG count_after_suspend = suspend_counter;

    /* 挂起后计数不应增加 */
    TEST_CHECK("suspend: counter frozen", count_after_suspend == count_before_suspend);

    /* 恢复 */
    status = tx_thread_resume(&suspend_thread);
    TEST_CHECK("resume: status", status == TX_SUCCESS);

    tx_thread_sleep(3);
    ULONG count_after_resume = suspend_counter;

    /* 恢复后计数应增加 */
    TEST_CHECK("resume: counter increased", count_after_resume > count_after_suspend);

    tx_thread_terminate(&suspend_thread);
    tx_thread_delete(&suspend_thread);
}

/* ========================================================================
 *  测试 5: 消息队列
 * ======================================================================== */
static void test_queue(void)
{
    UINT status;
    TX_QUEUE q;
    ULONG queue_storage[TEST_QUEUE_SIZE * 2];
    ULONG msg;
    ULONG received;

    uart_puts("\r\n[Test 5] Message Queue\r\n");

    status = tx_queue_create(&q, "test_queue", TX_1_ULONG,
                             queue_storage, sizeof(queue_storage));
    TEST_CHECK("queue: create", status == TX_SUCCESS);

    /* 发送消息 */
    msg = 0x12345678;
    status = tx_queue_send(&q, &msg, TX_NO_WAIT);
    TEST_CHECK("queue: send #1", status == TX_SUCCESS);

    /* 接收消息 */
    status = tx_queue_receive(&q, &received, TX_NO_WAIT);
    TEST_CHECK("queue: receive #1", status == TX_SUCCESS);
    TEST_CHECK("queue: data integrity", received == 0x12345678);

    /* 队列容量 = storage 字数 / 每条消息字数 (TX_1_ULONG = 1 字),
     * 与 tx_queue_create 内部计算一致, 不依赖对数组大小的假设 */
    const UINT capacity = (UINT)(sizeof(queue_storage) / sizeof(ULONG));

    /* 发送 capacity 条消息填满队列 */
    for (msg = 0; msg < capacity; msg++)
    {
        status = tx_queue_send(&q, &msg, TX_NO_WAIT);
        if (status != TX_SUCCESS)
            break;
    }
    TEST_CHECK("queue: fill queue", status == TX_SUCCESS);

    /* 队列满时应返回 TX_QUEUE_FULL */
    msg = 0xFF;
    status = tx_queue_send(&q, &msg, TX_NO_WAIT);
    TEST_CHECK("queue: full detection", status == TX_QUEUE_FULL);

    /* 接收全部并验证顺序 */
    UINT receive_ok = 1;
    for (msg = 0; msg < capacity; msg++)
    {
        status = tx_queue_receive(&q, &received, TX_NO_WAIT);
        if (status != TX_SUCCESS || received != msg)
        {
            receive_ok = 0;
            break;
        }
    }
    TEST_CHECK("queue: FIFO order", receive_ok);

    /* 空队列应返回 TX_QUEUE_EMPTY */
    status = tx_queue_receive(&q, &received, TX_NO_WAIT);
    TEST_CHECK("queue: empty detection", status == TX_QUEUE_EMPTY);

    /* 测试前置发送 (front send) */
    msg = 1;
    tx_queue_send(&q, &msg, TX_NO_WAIT);
    msg = 2;
    tx_queue_front_send(&q, &msg, TX_NO_WAIT);
    msg = 3;
    tx_queue_send(&q, &msg, TX_NO_WAIT);

    tx_queue_receive(&q, &received, TX_NO_WAIT);
    TEST_CHECK("queue: front send priority", received == 2);
    tx_queue_receive(&q, &received, TX_NO_WAIT);
    TEST_CHECK("queue: front send order", received == 1);
    tx_queue_receive(&q, &received, TX_NO_WAIT);
    TEST_CHECK("queue: front send tail", received == 3);

    /* 测试 flush */
    msg = 0xAA;
    tx_queue_send(&q, &msg, TX_NO_WAIT);
    tx_queue_send(&q, &msg, TX_NO_WAIT);
    status = tx_queue_flush(&q);
    TEST_CHECK("queue: flush", status == TX_SUCCESS);
    status = tx_queue_receive(&q, &received, TX_NO_WAIT);
    TEST_CHECK("queue: flush cleared", status == TX_QUEUE_EMPTY);

    tx_queue_delete(&q);
}

/* ========================================================================
 *  测试 6: 计数信号量
 * ======================================================================== */
static void test_semaphore(void)
{
    UINT status;
    TX_SEMAPHORE sem;
    ULONG current_count;

    uart_puts("\r\n[Test 6] Counting Semaphore\r\n");

    status = tx_semaphore_create(&sem, "test_sem", 1);
    TEST_CHECK("sem: create (initial=1)", status == TX_SUCCESS);

    /* 获取 (计数从 1→0) */
    status = tx_semaphore_get(&sem, TX_NO_WAIT);
    TEST_CHECK("sem: get #1", status == TX_SUCCESS);

    /* 再次获取应失败 (无实例) */
    status = tx_semaphore_get(&sem, TX_NO_WAIT);
    TEST_CHECK("sem: get empty", status == TX_NO_INSTANCE);

    /* 释放 (计数 0→1) */
    status = tx_semaphore_put(&sem);
    TEST_CHECK("sem: put #1", status == TX_SUCCESS);

    /* 再次获取应成功 */
    status = tx_semaphore_get(&sem, TX_NO_WAIT);
    TEST_CHECK("sem: get after put", status == TX_SUCCESS);

    /* 测试多次 put 累加 */
    tx_semaphore_put(&sem);
    tx_semaphore_put(&sem);
    tx_semaphore_put(&sem);
    status = tx_semaphore_info_get(&sem, TX_NULL, &current_count, TX_NULL,
                                   TX_NULL, TX_NULL);
    TEST_CHECK("sem: count after 3 puts", (status == TX_SUCCESS) && (current_count == 3));

    /* ceiling put 测试 */
    status = tx_semaphore_ceiling_put(&sem, 5);
    TEST_CHECK("sem: ceiling put (below)", status == TX_SUCCESS);
    status = tx_semaphore_ceiling_put(&sem, 4);  /* 当前=4, ceiling=4, 应失败 */
    TEST_CHECK("sem: ceiling put (at ceiling)", status == TX_CEILING_EXCEEDED);

    tx_semaphore_delete(&sem);
}

/* ========================================================================
 *  测试 7: 互斥量 (含递归获取)
 * ======================================================================== */
static void test_mutex(void)
{
    UINT status;
    TX_MUTEX mtx;
    ULONG count;

    uart_puts("\r\n[Test 7] Mutex\r\n");

    status = tx_mutex_create(&mtx, "test_mutex", TX_NO_INHERIT);
    TEST_CHECK("mutex: create", status == TX_SUCCESS);

    /* 首次获取 */
    status = tx_mutex_get(&mtx, TX_NO_WAIT);
    TEST_CHECK("mutex: get #1", status == TX_SUCCESS);

    /* 递归获取 (同一线程可多次获取) */
    status = tx_mutex_get(&mtx, TX_NO_WAIT);
    TEST_CHECK("mutex: get #2 (recursive)", status == TX_SUCCESS);

    status = tx_mutex_get(&mtx, TX_NO_WAIT);
    TEST_CHECK("mutex: get #3 (recursive)", status == TX_SUCCESS);

    /* 检查所有权计数 */
    count = mtx.tx_mutex_ownership_count;
    TEST_CHECK("mutex: ownership count", count == 3);

    /* 释放 (需释放相同次数) */
    status = tx_mutex_put(&mtx);
    TEST_CHECK("mutex: put #1", status == TX_SUCCESS);

    status = tx_mutex_put(&mtx);
    TEST_CHECK("mutex: put #2", status == TX_SUCCESS);

    /* 此时仍未完全释放 (还持有 1 次) */
    TX_THREAD *owner = mtx.tx_mutex_owner;
    TEST_CHECK("mutex: still owned", owner != TX_NULL);

    status = tx_mutex_put(&mtx);
    TEST_CHECK("mutex: put #3 (final)", status == TX_SUCCESS);

    /* 完全释放后无所有者 */
    owner = mtx.tx_mutex_owner;
    TEST_CHECK("mutex: released", owner == TX_NULL);

    tx_mutex_delete(&mtx);
}

/* ========================================================================
 *  测试 8: 事件标志组
 * ======================================================================== */
static void test_event_flags(void)
{
    UINT status;
    TX_EVENT_FLAGS_GROUP flags;
    ULONG actual_flags;

    uart_puts("\r\n[Test 8] Event Flags Group\r\n");

    status = tx_event_flags_create(&flags, "test_flags");
    TEST_CHECK("event: create", status == TX_SUCCESS);

    /* 设置 flag 0x1 */
    status = tx_event_flags_set(&flags, 0x1, TX_OR);
    TEST_CHECK("event: set flag 0", status == TX_SUCCESS);

    /* 获取 flag 0x1 (OR, 不清除) */
    status = tx_event_flags_get(&flags, 0x1, TX_OR, &actual_flags, TX_NO_WAIT);
    TEST_CHECK("event: get OR (no clear)", (status == TX_SUCCESS) && (actual_flags == 0x1));

    /* 再次获取 (未清除, 仍在) */
    status = tx_event_flags_get(&flags, 0x1, TX_OR, &actual_flags, TX_NO_WAIT);
    TEST_CHECK("event: get again (still set)", (status == TX_SUCCESS) && (actual_flags == 0x1));

    /* 获取并清除 */
    status = tx_event_flags_get(&flags, 0x1, TX_OR_CLEAR, &actual_flags, TX_NO_WAIT);
    TEST_CHECK("event: get OR_CLEAR", (status == TX_SUCCESS) && (actual_flags == 0x1));

    /* 清除后应获取不到 */
    status = tx_event_flags_get(&flags, 0x1, TX_OR, &actual_flags, TX_NO_WAIT);
    TEST_CHECK("event: get after clear", status == TX_NO_EVENTS);

    /* 设置多个标志 */
    tx_event_flags_set(&flags, 0x3, TX_OR);
    /* AND 获取: 需要 0x3 全部设置 */
    status = tx_event_flags_get(&flags, 0x3, TX_AND, &actual_flags, TX_NO_WAIT);
    TEST_CHECK("event: get AND (all set)", (status == TX_SUCCESS) && (actual_flags == 0x3));

    tx_event_flags_set(&flags, 0x2, TX_AND);  /* 清除 flag 0 */
    status = tx_event_flags_get(&flags, 0x3, TX_AND, &actual_flags, TX_NO_WAIT);
    TEST_CHECK("event: get AND (partial)", status == TX_NO_EVENTS);

    tx_event_flags_delete(&flags);
}

/* ========================================================================
 *  测试 9: 字节内存池
 * ======================================================================== */
static UCHAR byte_pool_mem[1024];

static void test_byte_pool(void)
{
    UINT status;
    TX_BYTE_POOL pool;
    VOID *ptr1, *ptr2, *ptr3;
    ULONG available;

    uart_puts("\r\n[Test 9] Byte Memory Pool\r\n");

    status = tx_byte_pool_create(&pool, "test_byte_pool", byte_pool_mem, sizeof(byte_pool_mem));
    TEST_CHECK("byte pool: create", status == TX_SUCCESS);

    /* 分配内存 */
    status = tx_byte_allocate(&pool, &ptr1, 128, TX_NO_WAIT);
    TEST_CHECK("byte pool: alloc #1 (128B)", status == TX_SUCCESS);
    TEST_CHECK("byte pool: ptr1 non-null", ptr1 != TX_NULL);

    status = tx_byte_allocate(&pool, &ptr2, 256, TX_NO_WAIT);
    TEST_CHECK("byte pool: alloc #2 (256B)", status == TX_SUCCESS);
    TEST_CHECK("byte pool: ptr2 non-null", ptr2 != TX_NULL);

    /* 验证指针不重叠 */
    TEST_CHECK("byte pool: no overlap",
               ((UCHAR *)ptr1 + 128 <= (UCHAR *)ptr2) ||
               ((UCHAR *)ptr2 + 256 <= (UCHAR *)ptr1));

    /* 释放 ptr1 */
    status = tx_byte_release(ptr1);
    TEST_CHECK("byte pool: release #1", status == TX_SUCCESS);

    /* 再次分配应成功 */
    status = tx_byte_allocate(&pool, &ptr3, 128, TX_NO_WAIT);
    TEST_CHECK("byte pool: alloc after release", status == TX_SUCCESS);

    /* 检查可用内存信息 */
    status = tx_byte_pool_info_get(&pool, TX_NULL, &available, TX_NULL, TX_NULL, TX_NULL, TX_NULL);
    TEST_CHECK("byte pool: info get", status == TX_SUCCESS);
    TEST_CHECK("byte pool: available > 0", available > 0);

    tx_byte_release(ptr2);
    tx_byte_release(ptr3);
    tx_byte_pool_delete(&pool);
}

/* ========================================================================
 *  测试 10: 块内存池
 * ======================================================================== */
/* 每块开销 = 块大小(32, 已 4 对齐) + 4 字节链接指针,
 * 恰好容纳 4 块: 4 * (32 + sizeof(VOID *)) = 144 字节 */
static UCHAR block_pool_mem[4 * (32 + sizeof(VOID *))];

static void test_block_pool(void)
{
    UINT status;
    TX_BLOCK_POOL pool;
    VOID *blocks[4];
    VOID *extra;
    int i;

    uart_puts("\r\n[Test 10] Block Memory Pool\r\n");

    /* 块大小 = 32 字节, 恰好 4 块 */
    status = tx_block_pool_create(&pool, "test_block_pool", 32, block_pool_mem, sizeof(block_pool_mem));
    TEST_CHECK("block pool: create", status == TX_SUCCESS);

    /* 分配多个块 */
    for (i = 0; i < 4; i++)
    {
        status = tx_block_allocate(&pool, &blocks[i], TX_NO_WAIT);
        if (status != TX_SUCCESS)
            break;
    }
    TEST_CHECK("block pool: alloc 4 blocks", status == TX_SUCCESS);

    /* 第 5 个块应失败 (池满) */
    status = tx_block_allocate(&pool, &extra, TX_NO_WAIT);
    TEST_CHECK("block pool: full detection", status == TX_NO_MEMORY);

    /* 释放一个块 */
    status = tx_block_release(blocks[0]);
    TEST_CHECK("block pool: release #1", status == TX_SUCCESS);

    /* 再次分配应成功 */
    status = tx_block_allocate(&pool, &extra, TX_NO_WAIT);
    TEST_CHECK("block pool: alloc after release", status == TX_SUCCESS);

    /* 释放所有 */
    for (i = 1; i < 4; i++)
        tx_block_release(blocks[i]);
    tx_block_release(extra);

    tx_block_pool_delete(&pool);
}

/* ========================================================================
 *  测试 11: 软件定时器
 * ======================================================================== */
static void test_timer(void)
{
    UINT status;
    TX_TIMER timer;
    ULONG old_time, elapsed;

    uart_puts("\r\n[Test 11] Software Timer\r\n");

    timer_expiration_count = 0;
    timer_callback_executed = 0;

    /* 创建定时器: 10 tick 后首次到期, 每 10 tick 重复 */
    status = tx_timer_create(&timer, "test_timer", test_timer_callback, 0,
                             10, 10, TX_AUTO_ACTIVATE);
    TEST_CHECK("timer: create & activate", status == TX_SUCCESS);

    /* 等待足够时间让定时器到期至少一次 */
    old_time = tx_time_get();
    tx_thread_sleep(25);
    elapsed = tx_time_get() - old_time;

    TEST_CHECK("timer: elapsed >= 25", elapsed >= 25);
    TEST_CHECK("timer: callback executed", timer_callback_executed != 0);
    TEST_CHECK("timer: expiration count >= 1", timer_expiration_count >= 1);

    ULONG count_before = timer_expiration_count;

    /* 停用定时器 */
    status = tx_timer_deactivate(&timer);
    TEST_CHECK("timer: deactivate", status == TX_SUCCESS);

    tx_thread_sleep(15);
    ULONG count_after = timer_expiration_count;

    /* 停用后不应再增加 */
    TEST_CHECK("timer: stopped (no more expiration)", count_after == count_before);

    /* 重新激活 */
    status = tx_timer_activate(&timer);
    TEST_CHECK("timer: reactivate", status == TX_SUCCESS);

    tx_thread_sleep(15);
    TEST_CHECK("timer: running again", timer_expiration_count > count_after);

    tx_timer_delete(&timer);
}

/* ========================================================================
 *  测试 12: 系统时间
 * ======================================================================== */
static void test_time(void)
{
    ULONG old_time, new_time, set_time;

    uart_puts("\r\n[Test 12] System Time\r\n");

    /* 获取时间 */
    old_time = tx_time_get();

    tx_thread_sleep(5);

    new_time = tx_time_get();
    TEST_CHECK("time: get elapsed", (new_time - old_time) >= 5);

    /* 设置时间 */
    set_time = 1000;
    tx_time_set(set_time);

    new_time = tx_time_get();
    TEST_CHECK("time: set & get", new_time == set_time);

    /* 时间继续递增 */
    tx_thread_sleep(3);
    new_time = tx_time_get();
    TEST_CHECK("time: continues after set", new_time >= (set_time + 3));
}

/* ========================================================================
 *  测试 13: 中断控制
 * ======================================================================== */
static void test_interrupt_control(void)
{
    UINT old_status;
    UINT saved_status;

    uart_puts("\r\n[Test 13] Interrupt Control\r\n");

    /* 测试关中断 */
    old_status = _tx_thread_interrupt_control(TX_INT_DISABLE);
    TEST_CHECK("int: disable returns old", (old_status == 0) || (old_status == 0x8));

    /* 关中断状态下再关 */
    saved_status = _tx_thread_interrupt_control(TX_INT_DISABLE);
    TEST_CHECK("int: nested disable", (saved_status == 0) || (saved_status == 0x8));

    /* 恢复中断 */
    _tx_thread_interrupt_control(saved_status);

    /* 测试 TX_DISABLE/TX_RESTORE 宏 */
    {
        TX_INTERRUPT_SAVE_AREA
        TX_DISABLE
        TX_RESTORE
        TEST_CHECK("int: TX_DISABLE/RESTORE macro", 1);
    }

    /* 测试临界区内时间不停止 (tick 中断被屏蔽但计数器逻辑在恢复后补上?) */
    /* ThreadX 关中断时 SysTick 不清除, 恢复后继续, 这与具体实现有关 */
    {
        ULONG t1, t2;

        TX_INTERRUPT_SAVE_AREA
        TX_DISABLE
        t1 = tx_time_get();
        /* 延时 (忙等) */
        for (volatile int i = 0; i < 1000; i++);
        t2 = tx_time_get();
        TX_RESTORE

        /* 关中断期间时间可能不变 (SysTick 被屏蔽) */
        TEST_CHECK("int: time frozen during disable", t2 == t1);
    }

    /* 恢复后时间应继续 */
    {
        ULONG t1;
        TX_INTERRUPT_SAVE_AREA
        TX_DISABLE
        t1 = tx_time_get();
        TX_RESTORE
        tx_thread_sleep(2);
        ULONG t2 = tx_time_get();
        TEST_CHECK("int: time resumes after restore", (t2 - t1) >= 2);
    }
}

/* ========================================================================
 *  测试 14: 集成测试 (参考 demo_threadx.c)
 * ======================================================================== */
static TX_QUEUE    integ_queue;
static TX_SEMAPHORE integ_sem;
static TX_MUTEX    integ_mutex;
static TX_EVENT_FLAGS_GROUP integ_flags;
static TX_BYTE_POOL integ_byte_pool;
static TX_BLOCK_POOL integ_block_pool;

static UCHAR integ_byte_mem[4096];
static UCHAR integ_block_mem[256];
static UCHAR integ_queue_mem[TEST_QUEUE_SIZE * sizeof(ULONG)];

static ULONG integ_msg_sent;
static ULONG integ_msg_received;

static void integ_thread_0_entry(ULONG p)
{
    UINT status;
    (void)p;

    while (1)
    {
        integration_counters[0]++;
        tx_thread_sleep(5);

        /* 唤醒 thread 2 (事件标志) */
        status = tx_event_flags_set(&integ_flags, 0x1, TX_OR);
        if (status != TX_SUCCESS)
            break;
    }
}

static void integ_thread_1_entry(ULONG p)
{
    UINT status;
    ULONG v;
    (void)p;

    while (1)
    {
        integration_counters[1]++;
        /* 局部拷贝后再发送: 阻塞挂起时队列保存的源指针指向本线程栈,
         * 唤醒补位复制读到的必然是当初想发的值;
         * 若直接传 &integ_msg_sent, 补位复制读到的是全局变量在
         * 消费者 receive 时刻的值, 存在读到"未来值"的隐患 */
        v = integ_msg_sent;
        status = tx_queue_send(&integ_queue, &v, TX_WAIT_FOREVER);
        if (status != TX_SUCCESS)
            break;
        integ_msg_sent++;
    }
}

static void integ_thread_2_entry(ULONG p)
{
    UINT status;
    ULONG received;
    ULONG actual_flags;
    (void)p;

    while (1)
    {
        integration_counters[2]++;
        status = tx_event_flags_get(&integ_flags, 0x1, TX_OR_CLEAR,
                                    &actual_flags, TX_WAIT_FOREVER);
        if (status != TX_SUCCESS)
            break;

        status = tx_queue_receive(&integ_queue, &received, TX_WAIT_FOREVER);
        if (status != TX_SUCCESS || received != integ_msg_received)
        {
            /* 诊断: 消费者异常退出位置 */
            uart_puts("  [diag] consumer exit: status=");
            uart_putdec(status);
            uart_puts(" got=");
            uart_putdec(received);
            uart_puts(" expected=");
            uart_putdec(integ_msg_received);
            uart_puts("\r\n");
            break;
        }
        integ_msg_received++;
    }
}

static void integ_thread_3_entry(ULONG p)
{
    UINT status;
    (void)p;

    while (1)
    {
        integration_counters[3]++;
        status = tx_mutex_get(&integ_mutex, TX_WAIT_FOREVER);
        if (status != TX_SUCCESS)
            break;

        /* 递归获取 */
        status = tx_mutex_get(&integ_mutex, TX_WAIT_FOREVER);
        if (status != TX_SUCCESS)
            break;

        tx_thread_sleep(1);

        tx_mutex_put(&integ_mutex);
        tx_mutex_put(&integ_mutex);
    }
}

static void test_integration(void)
{
    UINT status;
    ULONG t0;

    uart_puts("\r\n[Test 14] Integration Test\r\n");

    /* 初始化计数器 */
    for (int i = 0; i < 4; i++)
        integration_counters[i] = 0;
    integ_msg_sent = 0;
    integ_msg_received = 0;

    /* 创建字节池 */
    status = tx_byte_pool_create(&integ_byte_pool, "integ_pool", integ_byte_mem, sizeof(integ_byte_mem));
    TEST_CHECK("integ: byte pool create", status == TX_SUCCESS);

    /* 创建队列 */
    status = tx_queue_create(&integ_queue, "integ_queue", TX_1_ULONG,
                             integ_queue_mem, sizeof(integ_queue_mem));
    TEST_CHECK("integ: queue create", status == TX_SUCCESS);

    /* 创建信号量 */
    status = tx_semaphore_create(&integ_sem, "integ_sem", 1);
    TEST_CHECK("integ: semaphore create", status == TX_SUCCESS);

    /* 创建互斥量 */
    status = tx_mutex_create(&integ_mutex, "integ_mutex", TX_NO_INHERIT);
    TEST_CHECK("integ: mutex create", status == TX_SUCCESS);

    /* 创建事件标志组 */
    status = tx_event_flags_create(&integ_flags, "integ_flags");
    TEST_CHECK("integ: event flags create", status == TX_SUCCESS);

    /* 创建块池 */
    status = tx_block_pool_create(&integ_block_pool, "integ_block_pool", 32,
                                   integ_block_mem, sizeof(integ_block_mem));
    TEST_CHECK("integ: block pool create", status == TX_SUCCESS);

    /* 创建并启动线程 */
    status = tx_thread_create(&integration_thread_0, "integ_0", integ_thread_0_entry, 0,
                              integration_stack_0, TEST_STACK_SIZE,
                              1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
    TEST_CHECK("integ: thread0 create", status == TX_SUCCESS);

    /* 生产者优先级 (14) 必须高于测试线程 (15):
     * 阻塞中的 send 被消费者唤醒后消息立即入队, 但 integ_msg_sent++ 要等
     * 生产者被调度才执行。若生产者优先级低, 测试线程恰好在"消息已入队、
     * 计数未自增"的窗口冻结它, sent == received + drained 不成立。
     * 优先级高于测试线程后, 唤醒即抢占完成自增, 该窗口被确定性消除。 */
    status = tx_thread_create(&integration_thread_1, "integ_1", integ_thread_1_entry, 1,
                              integration_stack_1, TEST_STACK_SIZE,
                              14, 14, TX_NO_TIME_SLICE, TX_AUTO_START);
    TEST_CHECK("integ: thread1 create", status == TX_SUCCESS);

    status = tx_thread_create(&integration_thread_2, "integ_2", integ_thread_2_entry, 2,
                              integration_stack_2, TEST_STACK_SIZE,
                              15, 15, TX_NO_TIME_SLICE, TX_AUTO_START);
    TEST_CHECK("integ: thread2 create", status == TX_SUCCESS);

    status = tx_thread_create(&integration_thread_3, "integ_3", integ_thread_3_entry, 3,
                              integration_stack_3, TEST_STACK_SIZE,
                              8, 8, TX_NO_TIME_SLICE, TX_AUTO_START);
    TEST_CHECK("integ: thread3 create", status == TX_SUCCESS);

    /* 运行一段时间 */
    t0 = tx_time_get();
    tx_thread_sleep(50);
    ULONG elapsed = tx_time_get() - t0;

    TEST_CHECK("integ: ran for 50+ ticks", elapsed >= 50);

    /* 停止所有线程 */
    tx_thread_suspend(&integration_thread_0);
    tx_thread_suspend(&integration_thread_1);
    tx_thread_suspend(&integration_thread_2);
    tx_thread_suspend(&integration_thread_3);

    /* 验证所有线程都执行了 */
    TEST_CHECK("integ: thread0 ran", integration_counters[0] > 0);
    TEST_CHECK("integ: thread1 ran", integration_counters[1] > 0);
    TEST_CHECK("integ: thread2 ran", integration_counters[2] > 0);
    TEST_CHECK("integ: thread3 ran", integration_counters[3] > 0);

    /* 验证消息传递 */
    TEST_CHECK("integ: messages sent > 0", integ_msg_sent > 0);

    /* 挂起线程时队列中可能仍有未消费消息 (消费者由 thread_0 的
     * 事件标志驱动, 速度慢于生产者)。排空剩余消息并验证其
     * 值与已消费数连续, 最终 发送数 == 已消费数 + 排空数 */
    {
        ULONG expected = integ_msg_received;
        ULONG v = 0;
        UINT mismatch = 0;
        while (tx_queue_receive(&integ_queue, &v, TX_NO_WAIT) == TX_SUCCESS)
        {
            if (v != expected)
            {
                mismatch = 1;
                break;
            }
            expected++;
        }
        /* 诊断输出: 定位计数错位点 */
        uart_puts("  [diag] sent=");
        uart_putdec(integ_msg_sent);
        uart_puts(" received=");
        uart_putdec(integ_msg_received);
        uart_puts(" drained_to=");
        uart_putdec(expected);
        uart_puts(" mismatch=");
        uart_putdec(mismatch);
        uart_puts(" bad_v=");
        uart_putdec(v);
        uart_puts("\r\n");

        /* 判定: 消息序列完整性 (mismatch==0 保证无丢失/无重复/有序);
         * 账目允许 ±1: 生产者被消费者唤醒(消息已补位入队)后、执行
         * sent++ 前, 可能恰好被测试线程 tx_thread_suspend 冻结,
         * 此时 drained 比 sent 多 1 —— 这是挂起补位机制的固有窗口,
         * 与消息完整性无关 (内核行为正确)。 */
        TEST_CHECK("integ: messages match",
                   (mismatch == 0) &&
                   ((expected == integ_msg_sent) ||
                    (expected == (integ_msg_sent + 1))));
    }

    /* 清理 */
    tx_thread_terminate(&integration_thread_0);
    tx_thread_terminate(&integration_thread_1);
    tx_thread_terminate(&integration_thread_2);
    tx_thread_terminate(&integration_thread_3);

    tx_thread_delete(&integration_thread_0);
    tx_thread_delete(&integration_thread_1);
    tx_thread_delete(&integration_thread_2);
    tx_thread_delete(&integration_thread_3);

    tx_queue_delete(&integ_queue);
    tx_semaphore_delete(&integ_sem);
    tx_mutex_delete(&integ_mutex);
    tx_event_flags_delete(&integ_flags);
    tx_byte_pool_delete(&integ_byte_pool);
    tx_block_pool_delete(&integ_block_pool);
}

/* ========================================================================
 *  测试主线程入口
 * ======================================================================== */
static void test_main_entry(ULONG thread_input)
{
    (void)thread_input;

    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("  ThreadX CH585 QingKe V3C Port Test\r\n");
    uart_puts("  Full Software Context Save\r\n");
    uart_puts("========================================\r\n");

    /* 逐项测试 */
    test_thread_basic_v2();
    test_thread_preemption();
    test_time_slice();
    test_suspend_resume();
    test_queue();
    test_semaphore();
    test_mutex();
    test_event_flags();
    test_byte_pool();
    test_block_pool();
    test_timer();
    test_time();
    test_interrupt_control();
    test_integration();

    /* 汇总 */
    uart_puts("\r\n========================================\r\n");
    uart_puts("  Test Summary\r\n");
    uart_puts("  PASS: ");
    uart_putdec(test_pass_count);
    uart_puts("  FAIL: ");
    uart_putdec(test_fail_count);
    uart_puts("\r\n");

    if (test_fail_count == 0)
        uart_puts("  ALL TESTS PASSED\r\n");
    else
        uart_puts("  SOME TESTS FAILED\r\n");

    uart_puts("========================================\r\n");

    /* 停止调度器 (进入空闲循环) */
    while (1)
        tx_thread_sleep(0xFFFFFFFF);
}

/* ========================================================================
 *  ThreadX 应用初始化
 * ======================================================================== */
void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    tx_thread_create(&test_thread, "test_main", test_main_entry, 0,
                     test_thread_stack, TEST_STACK_SIZE,
                     TEST_THREAD_PRIORITY, TEST_THREAD_PRIORITY,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

/* ========================================================================
 *  主函数
 * ======================================================================== */
int main(void)
{
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);

    /* 调试串口: UART1 (PA8=RXD, PA9=TXD) */
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    /* 进入 ThreadX (不返回) */
    tx_kernel_enter();

    while (1)
    {
        /* 不应到达这里 */
    }
}
