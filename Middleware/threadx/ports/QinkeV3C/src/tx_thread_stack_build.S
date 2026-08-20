/***************************************************************************
 * tx_thread_stack_build.S — 构建线程初始栈帧
 *
 * 创建"中断栈帧"格式 (与硬件压栈兼容):
 * [软件帧: 18 words] + [硬件帧: 16 words]
 *
 * 输入: a0 = thread_ptr, a1 = function_ptr
 * 输出: a0 = 初始栈指针
 ***************************************************************************/

    .section .text
    .align 2
    .global _tx_thread_stack_build
    .type   _tx_thread_stack_build, @function

_tx_thread_stack_build:
    /* 获取栈顶并4字节对齐 */
    lw      t0, 12(a0)                  /* tx_thread_stack_end */
    andi    t0, t0, ~3

    /* 分配: 软件帧(18) + 硬件帧(16) = 34 words = 136 bytes */
    addi    t0, t0, -(34 * 4)

    /* ==== 填充软件帧 (低地址, offset 0-17) ==== */
    li      t1, 1
    sw      t1, 0*4(t0)                /* frame_type = INTERRUPT */

    /* sp_orig = 栈顶 (新线程首次运行时的sp) */
    addi    t1, t0, 34*4
    sw      t1, 1*4(t0)                /* sp_orig */

    li      t1, 0
    sw      t1, 2*4(t0)                /* tp = 0 */
    sw      t1, 3*4(t0)                /* gp = 0 */

    /* s0-s11 = 0 */
    .rept 12
    sw      t1, (4 + 11 - 0)*4(t0)    /* 逐个清零 */
    .endr
    /* 手动展开更清晰 */
    sw      zero,  4*4(t0)             /* s11 */
    sw      zero,  5*4(t0)             /* s10 */
    sw      zero,  6*4(t0)             /* s9 */
    sw      zero,  7*4(t0)             /* s8 */
    sw      zero,  8*4(t0)             /* s7 */
    sw      zero,  9*4(t0)             /* s6 */
    sw      zero, 10*4(t0)             /* s5 */
    sw      zero, 11*4(t0)             /* s4 */
    sw      zero, 12*4(t0)             /* s3 */
    sw      zero, 13*4(t0)             /* s2 */
    sw      zero, 14*4(t0)             /* s1 */
    sw      zero, 15*4(t0)             /* s0 */

    /* mstatus = 0x1880 (MPP=M, MPIE=1, MIE=0) */
    li      t1, 0x1880
    sw      t1, 16*4(t0)

    /* mepc = 线程入口函数 */
    sw      a1, 17*4(t0)

    /* ==== 填充硬件帧 (高地址, offset 18-33) ==== */
    /* ra = _tx_thread_shell_entry (线程shell入口) */
    la      t1, _tx_thread_shell_entry
    sw      t1, (18+0)*4(t0)           /* x1(ra) */

    /* 其余 caller-saved = 0 */
    sw      zero, (18+1)*4(t0)         /* x5(t0) */
    sw      zero, (18+2)*4(t0)         /* x6(t1) */
    sw      zero, (18+3)*4(t0)         /* x7(t2) */
    sw      a0,   (18+4)*4(t0)         /* x10(a0) = thread_ptr */
    sw      zero, (18+5)*4(t0)         /* x11(a1) */
    sw      zero, (18+6)*4(t0)         /* x12(a2) */
    sw      zero, (18+7)*4(t0)         /* x13(a3) */
    sw      zero, (18+8)*4(t0)         /* x14(a4) */
    sw      zero, (18+9)*4(t0)         /* x15(a5) */
    sw      zero, (18+10)*4(t0)        /* x16(a6) */
    sw      zero, (18+11)*4(t0)        /* x17(a7) */
    sw      zero, (18+12)*4(t0)        /* x28(t3) */
    sw      zero, (18+13)*4(t0)        /* x29(t4) */
    sw      zero, (18+14)*4(t0)        /* x30(t5) */
    sw      zero, (18+15)*4(t0)        /* x31(t6) */

    /* 返回初始栈指针 */
    mv      a0, t0
    ret

    .size _tx_thread_stack_build, . - _tx_thread_stack_build