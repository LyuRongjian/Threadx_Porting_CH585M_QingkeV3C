/********************************** (C) COPYRIGHT *******************************
 * File Name          : startup_CH585_TX.S
 * Description        : CH585M ThreadX 启动文件 (QingKe V3C HPE)
 *
 * 与原厂 startup_CH585.S 的区别:
 *   - 外部中断 (IRQn 16-39, 除 LLE) 统一经 unified_interrupt_entry 分发,
 *     该入口在 qingke_hpe_isr.S 中定义, 负责 ThreadX 上下文保存/抢占判定;
 *   - 新增 _real_user_vector_base 表供 unified_interrupt_entry 按 mcause 查找用户 ISR;
 *   - LLE_IRQHandler (IRQn 21) 保持直接入口, 兼容 BLE 库时序要求;
 *   - _BB_IRQHandler_base / _LLE_IRQHandler_base 全局标签供 BLE 库运行时替换。
 *
 * 使用方法: 在 WCH IDE 工程中将本文件替换 Startup/startup_CH585.S 即可。
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

    .section    .init,"ax",@progbits
    .global     _start
    .global     _vector_base
    .align      1
_start:
    j   handle_reset

    .section    .vector,"ax",@progbits
    .global     _real_user_vector_base
    .global     _BB_IRQHandler_base
    .global     _LLE_IRQHandler_base
    .align      1
_vector_base:
    .option norvc

    .word   0
    .word   0
    .word   NMI_Handler                 /* NMI Handler */
    .word   HardFault_Handler           /* Hard Fault Handler */
    .word   0xF3F9BDA9
    .word   Ecall_M_Mode_Handler        /* 5 */
    .word   0
    .word   0
    .word   Ecall_U_Mode_Handler       /* 8 */
    .word   Break_Point_Handler         /* 9 */
    .word   0
    .word   0
    .word   SysTick_Handler            /* 12: SysTick */
    .word   0
    .word   SW_Handler                 /* 14: SW */
    .word   0
    /* External Interrupts (IRQn 16-39) */
    .word   unified_interrupt_entry     /* 16: TMR0 */
    .word   unified_interrupt_entry     /* 17: GPIOA */
    .word   unified_interrupt_entry     /* 18: GPIOB */
    .word   unified_interrupt_entry     /* 19: SPI0 */
    .word   unified_interrupt_entry     /* 20: BB */
    .word   LLE_IRQHandler              /* 21: LLE (direct, BLE lib) */
    .word   unified_interrupt_entry     /* 22: USB */
    .word   0                           /* 23: reserved */
    .word   unified_interrupt_entry     /* 24: TMR1 */
    .word   unified_interrupt_entry     /* 25: TMR2 */
    .word   unified_interrupt_entry     /* 26: UART0 */
    .word   unified_interrupt_entry     /* 27: UART1 */
    .word   unified_interrupt_entry     /* 28: RTC */
    .word   unified_interrupt_entry     /* 29: ADC */
    .word   unified_interrupt_entry     /* 30: I2C */
    .word   unified_interrupt_entry     /* 31: PWMX_SPI1 */
    .word   unified_interrupt_entry     /* 32: TMR3 */
    .word   unified_interrupt_entry     /* 33: UART2 */
    .word   unified_interrupt_entry     /* 34: UART3 */
    .word   unified_interrupt_entry     /* 35: WDOG_BAT */
    .word   unified_interrupt_entry     /* 36: NFC */
    .word   unified_interrupt_entry     /* 37: USB2_DEVICE */
    .word   unified_interrupt_entry     /* 38: USB2_HOST */
    .word   unified_interrupt_entry     /* 39: LED */

_real_user_vector_base:
    .option norvc
    .word   TMR0_IRQHandler             /* IRQn 16 */
    .word   GPIOA_IRQHandler            /* IRQn 17 */
    .word   GPIOB_IRQHandler            /* IRQn 18 */
    .word   SPI0_IRQHandler             /* IRQn 19 */
_BB_IRQHandler_base:
    .word   BB_IRQHandler               /* IRQn 20 */
_LLE_IRQHandler_base:
    .word   LLE_IRQHandler               /* IRQn 21 */
    .word   USB_IRQHandler              /* IRQn 22 */
    .word   0                           /* IRQn 23 (reserved) */
    .word   TMR1_IRQHandler             /* IRQn 24 */
    .word   TMR2_IRQHandler             /* IRQn 25 */
    .word   UART0_IRQHandler            /* IRQn 26 */
    .word   UART1_IRQHandler            /* IRQn 27 */
    .word   RTC_IRQHandler              /* IRQn 28 */
    .word   ADC_IRQHandler               /* IRQn 29 */
    .word   I2C_IRQHandler              /* IRQn 30 */
    .word   PWMX_SPI1_IRQHandler        /* IRQn 31 */
    .word   TMR3_IRQHandler             /* IRQn 32 */
    .word   UART2_IRQHandler            /* IRQn 33 */
    .word   UART3_IRQHandler            /* IRQn 34 */
    .word   WDOG_BAT_IRQHandler         /* IRQn 35 */
    .word   NFC_IRQHandler              /* IRQn 36 */
    .word   USB2_DEVICE_IRQHandler      /* IRQn 37 */
    .word   USB2_HOST_IRQHandler        /* IRQn 38 */
    .word   LED_IRQHandler              /* IRQn 39 */

    .option rvc

    .section    .vector_handler, "ax", @progbits
    .weak   NMI_Handler
    .weak   HardFault_Handler
    .weak   Ecall_M_Mode_Handler
    .weak   Ecall_U_Mode_Handler
    .weak   Break_Point_Handler
    .weak   SysTick_Handler
    .weak   SW_Handler
    .weak   TMR0_IRQHandler
    .weak   GPIOA_IRQHandler
    .weak   GPIOB_IRQHandler
    .weak   SPI0_IRQHandler
    .weak   BB_IRQHandler
    .weak   LLE_IRQHandler
    .weak   USB_IRQHandler
    .weak   TMR1_IRQHandler
    .weak   TMR2_IRQHandler
    .weak   UART0_IRQHandler
    .weak   UART1_IRQHandler
    .weak   RTC_IRQHandler
    .weak   ADC_IRQHandler
    .weak   I2C_IRQHandler
    .weak   PWMX_SPI1_IRQHandler
    .weak   TMR3_IRQHandler
    .weak   UART2_IRQHandler
    .weak   UART3_IRQHandler
    .weak   WDOG_BAT_IRQHandler
    .weak   NFC_IRQHandler
    .weak   USB2_DEVICE_IRQHandler
    .weak   USB2_HOST_IRQHandler
    .weak   LED_IRQHandler

NMI_Handler:
HardFault_Handler:
Ecall_M_Mode_Handler:
Ecall_U_Mode_Handler:
Break_Point_Handler:
SysTick_Handler:
SW_Handler:
TMR0_IRQHandler:
GPIOA_IRQHandler:
GPIOB_IRQHandler:
SPI0_IRQHandler:
BB_IRQHandler:
LLE_IRQHandler:
USB_IRQHandler:
TMR1_IRQHandler:
TMR2_IRQHandler:
UART0_IRQHandler:
UART1_IRQHandler:
RTC_IRQHandler:
ADC_IRQHandler:
I2C_IRQHandler:
PWMX_SPI1_IRQHandler:
TMR3_IRQHandler:
UART2_IRQHandler:
UART3_IRQHandler:
WDOG_BAT_IRQHandler:
NFC_IRQHandler:
USB2_DEVICE_IRQHandler:
USB2_HOST_IRQHandler:
LED_IRQHandler:
1:
    j 1b

    .section    .handle_reset,"ax",@progbits
    .weak   handle_reset
    .align  1
handle_reset:
.option push
.option norelax
    la gp, __global_pointer$
.option pop
1:
    la sp, _eusrstack

/* Load highcode_init code section from flash to RAM */
2:
    la a0, _highcode_init_lma
    la a1, _highcode_init_vma_start
    la a2, _highcode_init_vma_end
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b

2:
    la a0, __highcode_init
    jr a0

    .section    .highcode_init,"ax",@progbits
    .align  1
__highcode_init:
    jal highcode_init

/* Load highcode code section from flash to RAM */
2:
    la a0, _highcode_lma
    la a1, _highcode_vma_start
    la a2, _highcode_vma_end
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b

/* Load data section from flash to RAM */
2:
    la a0, _data_lma
    la a1, _data_vma
    la a2, _edata
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
2:
    /* clear bss section */
    la a0, _sbss
    la a1, _ebss
    bgeu a0, a1, 2f
1:
    sw zero, (a0)
    addi a0, a0, 4
    bltu a0, a1, 1b
2:
    /* Prefetch Enable */
    li t0, 0x25
    csrw 0xbc0, t0
    /* Enable HPE (Hardware Push/Pop Entry) */
    li t0, 0x3
    csrw 0x804, t0
    li t0, 0x1
    csrw 0xbc1, t0

    /* MPP=Machine(0b11), MPIE=0, MIE=0 — mret 后 MIE=MPIE=0 (中断关闭),
     * 运行于 Machine 模式; 中断由 _tx_thread_schedule 开启。
     * 与 FreeRTOS 移植一致, 不可用 MPP=0b00(User) 否则 CSR 访问触发非法指令异常。 */
    li t0, 0x1800
    csrw mstatus, t0
    la t0, _vector_base
    /* Configure Vector Table Mode: Absolute Address */
    ori t0, t0, 3
    csrw mtvec, t0

    la t0, main
    csrw mepc, t0

    mret
