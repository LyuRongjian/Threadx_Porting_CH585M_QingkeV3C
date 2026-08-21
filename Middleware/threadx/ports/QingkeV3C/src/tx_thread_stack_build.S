/***************************************************************************
 * tx_thread_stack_build.S -- 构建线程初始栈帧 (QingKe V3C / CH58x)
 *
 * 新线程以"中断帧"形式首次启动 (144 字节全量软件帧):
 *   [类型=1 / x1-x31=0 / mstatus=0x1880 / mepc=入口]
 *
 * 调度器恢复该帧后 mret: MIE<-MPIE=1 (开中断), 跳转到线程入口,
 * 全部寄存器从 0 开始 (线程入口参数由 TCB 提供, 不依赖寄存器)。
 *
 * 入口: a0 = thread_ptr, a1 = function_ptr (即 _tx_thread_shell_entry)
 * 出口: 无 (结果写入 TCB->tx_thread_stack_ptr, 偏移 8)
 ***************************************************************************/

    .section .text
    .align 2
    .global _tx_thread_stack_build
    .type   _tx_thread_stack_build, @function

_tx_thread_stack_build:

    /* 取栈底 (tx_thread_stack_end, 偏移 16), 16 字节对齐 */
    lw      t0, 16(a0)
    li      t1, ~15
    and     t0, t0, t1

    /* 预留中断软帧 (144 字节) */
    addi    t0, t0, -144

    /* ==== 中断帧 (与 qingke_hpe_isr.S 的帧布局一致) ==== */
    li      t1, 1
    sw      t1,  0x00(t0)                         /* 帧类型 = 1 (中断) */
    sw      zero, 0x04(t0)                        /* ra */
    sw      zero, 0x08(t0)                        /* sp (replaced below) */
     /* gp 必须继承启动代码设置的全局指针，否则新线程进入 C 代码后
         的 small-data 访问会跳到错误地址；tp 同样保持当前执行环境。 */
     sw      gp,   0x0C(t0)                        /* gp */
     sw      tp,   0x10(t0)                        /* tp */
    sw      zero, 0x14(t0)                        /* t0 */
    sw      zero, 0x18(t0)                        /* t1 */
    sw      zero, 0x1C(t0)                        /* t2 */
    sw      zero, 0x20(t0)                        /* t3 */
    sw      zero, 0x24(t0)                        /* t4 */
    sw      zero, 0x28(t0)                        /* t5 */
    sw      zero, 0x2C(t0)                        /* t6 */
    sw      zero, 0x30(t0)                        /* a0 */
    sw      zero, 0x34(t0)                        /* a1 */
    sw      zero, 0x38(t0)                        /* a2 */
    sw      zero, 0x3C(t0)                        /* a3 */
    sw      zero, 0x40(t0)                        /* a4 */
    sw      zero, 0x44(t0)                        /* a5 */
    sw      zero, 0x48(t0)                        /* a6 */
    sw      zero, 0x4C(t0)                        /* a7 */
    sw      zero, 0x50(t0)                        /* s0 */
    sw      zero, 0x54(t0)                        /* s1 */
    sw      zero, 0x58(t0)                        /* s2 */
    sw      zero, 0x5C(t0)                        /* s3 */
    sw      zero, 0x60(t0)                        /* s4 */
    sw      zero, 0x64(t0)                        /* s5 */
    sw      zero, 0x68(t0)                        /* s6 */
    sw      zero, 0x6C(t0)                        /* s7 */
    sw      zero, 0x70(t0)                        /* s8 */
    sw      zero, 0x74(t0)                        /* s9 */
    sw      zero, 0x78(t0)                        /* s10 */
    sw      zero, 0x7C(t0)                        /* s11 */
    li      t1, 0x1880                            /* MPP=Machine, MPIE=1, MIE=0 */
    sw      t1,  0x80(t0)                         /* mstatus */
    sw      a1,   0x84(t0)                        /* mepc = 线程入口 */
    sw      zero, 0x88(t0)                        /* 保留 */
    sw      zero, 0x8C(t0)                        /* 保留 */
    addi    t1, t0, 144
    sw      t1,  0x08(t0)                         /* 初始 sp */

    /* thread_ptr->tx_thread_stack_ptr = 初始栈指针 */
    sw      t0, 8(a0)
    ret

    .size   _tx_thread_stack_build, . - _tx_thread_stack_build
