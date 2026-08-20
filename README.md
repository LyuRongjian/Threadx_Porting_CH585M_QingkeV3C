# ThreadX QingKe V3C 移植 (CH585M)

Azure RTOS ThreadX 在 WCH QingKe V3C (CH585M, RV32IMAC) 上的移植实现。

## 概述

本移植针对 CH585M MCU 的 **QingKe V3C** RISC-V 内核，利用其独有的 **HPE（Hardware Push/Pop Entry）** 硬件压栈/出栈机制实现高效的中断上下文管理。

### 硬件平台

| 项目 | 说明 |
|------|------|
| MCU | CH585M (WCH) |
| 内核 | QingKe V3C (RV32IMAC, 32 位 RISC-V) |
| Flash | 448 KB (0x00000000) |
| SRAM | 128 KB (0x20000000) |
| 系统时钟 | 62.4 MHz (HSE PLL) |
| 中断控制器 | PFIC + HPE |
| 系统定时器 | SysTick (0xE000F000) |

### 移植参考

- **官方 ThreadX risc-v32/risc-v64 移植**：上下文切换逻辑、调度器结构
- **WCH 原厂 FreeRTOS/RT-Thread 移植**：HPE 配置、中断入口、流水线延时处理

---

## 目录结构

```
QingkeV3C/
├── inc/
│   └── tx_port.h                  # 移植层头文件：类型、栈帧、寄存器、构建选项
├── src/
│   ├── qingke_hpe_isr.S           # HPE 统一中断包装（PROLOGUE/EPILOGUE 宏）
│   ├── tx_initialize_low_level.c  # 底层初始化：系统栈、SysTick、PFIC
│   ├── tx_thread_interrupt_control.S  # 中断开关（mstatus.MIE + 流水线延时）
│   ├── tx_thread_schedule.S       # 调度器：查找就绪线程并恢复上下文
│   ├── tx_thread_stack_build.S    # 构建新线程初始栈帧
│   ├── tx_thread_system_return.S  # 线程主动让出 CPU
│   └── tx_timer_interrupt.S       # SysTick tick 处理
└── README.md                      # 本文件
```

---

## HPE 机制与栈帧布局

### HPE 硬件压栈

CH585 的 QingKe V3C 内核支持 HPE（CSR `0x804` INTSYSCR，启动文件中设为 `0x3` 启用）。中断入口和 `mret` 时，硬件自动压入/弹出 **16 个调用者保存寄存器**（a0-a7, t0-t6, ra）到被打断者的栈上。

软件无需感知 HPE 帧的内部布局，只需在 HPE 帧之上追加自己的软帧。

### 软件栈帧（64 字节）

移植层在每个中断入口追加保存的栈帧，布局如下：

```
偏移    内容                  说明
+0x00   帧类型                0 = 主动帧 (solicited), 1 = 中断帧 (interrupt)
+0x04   ra                    主动帧：返回地址；中断帧：保留
+0x08   s0                    被调用者保存寄存器
+0x0C   s1
+0x10   s2
+0x14   s3
+0x18   s4
+0x1C   s5
+0x20   s6
+0x24   s7
+0x28   s8
+0x2C   s9
+0x30   s10
+0x34   s11
+0x38   mstatus              机器状态寄存器
+0x3C   mepc                  中断帧：机器中断 PC；主动帧：保留
```

软帧之后紧跟 HPE 硬件帧（64 字节），由硬件在 `mret` 时弹出。

### 新线程初始栈帧

`tx_thread_stack_build.S` 为新线程构建两个帧：

1. **软帧**（64B）：类型 = 1（中断帧），s0-s11 = 0，mstatus = `0x1880`（MPP=Machine, MPIE=1, MIE=0），mepc = 线程入口函数
2. **伪 HPE 帧**（64B）：全零，供首次 `mret` 时硬件弹出并得到干净的调用者保存寄存器

---

## 中断管理

### 统一中断包装

`qingke_hpe_isr.S` 定义了两个宏，所有中断共用：

#### TX_HPE_PROLOGUE（中断入口）

1. 在 HPE 硬件帧之上构建 64B 软帧（类型 = 1，s0-s11，mstatus，mepc）
2. `_tx_thread_system_state++`（内核据此识别 ISR 上下文）
3. 首层中断（state == 1）且有线程被打断时：保存 sp 到 TCB 偏移 8，切到系统栈
4. 嵌套中断或初始化/空闲期间：不换栈，直接在当前栈上处理

#### TX_HPE_EPILOGUE（中断出口）

1. `_tx_thread_system_state--`
2. 若仍在中断嵌套/初始化/空闲：恢复软帧后 `mret` 返回
3. 检查抢占条件：
   - `preempt_disable > 0`：不抢占，恢复被打断线程
   - `execute_ptr == current_ptr`：无更高优先级就绪，恢复被打断线程
   - 需要抢占：保存剩余时间片，清 `current_ptr`，切系统栈，跳转 `_tx_thread_schedule`

### 中断向量表

`startup_CH585_TX.S` 定义了两张表：

| 表 | 用途 |
|----|------|
| `_vector_base` | 硬件向量表，IRQn 16-39 指向 `unified_interrupt_entry`（LLE 除外，直接入口） |
| `_real_user_vector_base` | 用户 ISR 函数指针表，`unified_interrupt_entry` 按 `mcause` 查表分发 |

外部中断统一经 `unified_interrupt_entry` 分发，该入口负责 ThreadX 上下文保存/抢占判定，然后按 `mcause` 索引 `_real_user_vector_base` 调用用户 ISR。

**LLE (IRQn 21)** 保持直接入口，兼容 BLE 库的时序要求。`_BB_IRQHandler_base` 和 `_LLE_IRQHandler_base` 全局标签供 BLE 库运行时替换。

### 中断嵌套控制

- HPE 硬件层面：禁止硬件嵌套（SysTick/SWI 的 IPRIOR bit7 = 1）
- 软件层面：`_tx_thread_system_state` 计数器管理，中断期间全程关闭 MIE
- CSR `0xbc1` = `0x1`：中断嵌套控制

### QingKe 3 级流水线延时

`csrci mstatus, 8`（关中断）后需补 3 个 `nop`，等待 3 级流水线确认 MIE 生效。这一做法与 WCH 官方 FreeRTOS/RT-Thread 移植一致，见 `tx_thread_interrupt_control.S` 和 `tx_thread_schedule.S`。

---

## 上下文切换

本移植不使用软件中断（SWI）触发上下文切换，与官方 ThreadX risc-v 移植一致。

### 主动让出 — `_tx_thread_system_return`

线程调用阻塞服务（如 `tx_thread_sleep`）时由内核调用：

1. 构建 64B 主动帧（类型 = 0，ra，s0-s11，mstatus）
2. `csrci mstatus, 0x08` + 3 NOP 关中断
3. 保存 sp 到 TCB 偏移 8，保存剩余时间片
4. 清 `current_ptr`，切系统栈
5. `j _tx_thread_schedule`

### 调度器 — `_tx_thread_schedule`

1. `csrsi mstatus, 0x08` 开中断，等待就绪线程
2. 轮询 `execute_ptr`，为空则 `wfi` 睡眠
3. 找到就绪线程后 `csrci` + 3 NOP 关中断
4. 设 `current_ptr = execute_ptr`，`run_count++`，设时间片
5. 切到线程栈（TCB 偏移 8）
6. 判断帧类型：
   - **中断帧**（type=1）：恢复 mepc/mstatus/s0-s11，sp 跳过软帧，`mret`（HPE 硬件弹出调用者保存寄存器）
   - **主动帧**（type=0）：恢复 mstatus/ra/s0-s11，sp 跳过软帧，`ret`

---

## 定时器处理

### SysTick 配置

`tx_initialize_low_level.c` 配置 SysTick：

| 参数 | 值 |
|------|----|
| 时钟源 | HCLK (62.4 MHz) |
| 重装值 | 623999 (62.4M / 100Hz - 1) |
| Tick 频率 | 100 Hz (10ms/tick) |
| 工作模式 | 自动重装 |

### Tick 中断流程

```
SysTick_Handler (qingke_hpe_isr.S)
  ├── TX_HPE_PROLOGUE          # 保存上下文, system_state++
  ├── 清 STK_SR (写 0 到 0xE000F004)
  ├── call _tx_timer_interrupt # tx_timer_interrupt.S
  │     ├── _tx_timer_system_clock++
  │     ├── 时间片递减, 到期调 _tx_thread_time_slice
  │     └── 定时器链表推进, 到期调 _tx_timer_expiration_process
  │           └── preempt_disable++ → 唤醒 _tx_timer_thread
  └── TX_HPE_EPILOGUE          # system_state--, 抢占判定
```

### preempt_disable 平衡机制

定时器到期时始终调用 `_tx_timer_expiration_process`（与官方 risc-v32 移植一致），该函数内部：

1. `preempt_disable++`（第 114 行）
2. 调用 `_tx_thread_system_resume(&_tx_timer_thread)`（内部 `preempt_disable--`，平衡）

定时器线程处理完过期定时器后挂起自身：
- `timer_thread_entry` 中 `preempt_disable++` → `_tx_thread_system_suspend` 中 `preempt_disable--` → 平衡

> **注意**：若绕过 `_tx_timer_expiration_process` 直接调用 `_tx_thread_system_resume`，会缺少 `preempt_disable++`，导致下溢（0 → 0xFFFFFFFF），进而使线程无法睡眠、系统挂起。

---

## 底层初始化

`tx_initialize_low_level.c` 由 `_tx_initialize_kernel_enter` 调用，职责：

1. 保存系统栈指针（复用 `main` 启动栈，`_eusrstack` 向下空间）
2. 记录首个空闲内存地址（`_end`，`.bss` 结束）
3. 配置 SysTick 周期中断
4. SysTick/SWI 设为非抢占优先级（IPRIOR bit7 = 1）
5. PFIC 使能 SysTick 中断

向量表（`mtvec`，绝对地址模式）和 HPE 硬件压栈（INTSYSCR = 0x3）由启动文件 `startup_CH585_TX.S` 配置。

---

## 启动流程

```
上电
  │
  ▼
_start → handle_reset (startup_CH585_TX.S)
  ├── 加载 .highcode_init / .highcode / .data 从 Flash 到 RAM
  ├── 清零 .bss
  ├── CSR 0xbc0 = 0x25 (Prefetch Enable)
  ├── CSR 0x804 = 0x3  (HPE Enable)
  ├── CSR 0xbc1 = 0x1  (中断嵌套控制)
  ├── mstatus = 0x1800  (MPP=Machine, MIE=0)
  ├── mtvec = _vector_base | 3 (绝对地址模式)
  └── mepc = main → mret
        │
        ▼
      main() (Main.c)
        ├── HSE/PLL 时钟配置
        ├── UART1 初始化
        ├── 打印启动信息
        └── tx_kernel_enter()
              │
              ▼
            _tx_initialize_kernel_enter
              ├── _tx_initialize_low_level()  # SysTick, 系统栈
              ├── _tx_initialize_high_level()  # 内核对象初始化
              ├── tx_application_define()      # 用户应用初始化
              └── _tx_thread_schedule()        # 开始调度
```

---

## 内存布局

```
Flash (0x00000000, 448KB):
  .init / .text / .rodata          # 代码和只读数据
  .highcode_lma                    # highcode 的 Flash 存储位置

RAM (0x20000000, 128KB):
  .highcode_init                   # 向量表 + 初始化代码 (从 Flash 加载)
  .highcode                        # 高频代码 (中断/调度, 从 Flash 加载, RAM 执行)
  .data                            # 初始化数据 (从 Flash 加载)
  .bss                             # 未初始化数据
  _end                             # ThreadX 首个空闲内存
  ...
  _eusrstack                       # 系统栈顶 (RAM 末尾)
```

**`.highcode` 段**：中断处理和调度代码放在 RAM 中执行（`@progbits` in `.highcode.*`），避免 Flash 访问延迟，保证中断响应时间。

---

## 构建配置

### 编译器选项

- **架构**：`-march=rv32imac -mabi=ilp32`
- **代码模型**：medany
- **Highcode 链接**：`.highcode` 段从 Flash 加载到 RAM 执行

### 关键宏定义

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `TX_MAX_PRIORITIES` | 32 | 优先级数量（32-1024，须为 32 的倍数） |
| `TX_MINIMUM_STACK` | 1024 | 最小线程栈大小（字） |
| `TX_TIMER_THREAD_STACK_SIZE` | 1024 | 定时器线程栈大小 |
| `TX_TIMER_THREAD_PRIORITY` | 0 | 定时器线程优先级（最高） |
| `TX_TIMER_TICKS_PER_SECOND` | 100 | 每秒 tick 数 |
| `TX_PORT_SYSTICK_HZ` | 62400000 | SysTick 计数时钟（HCLK） |

### 可选构建宏

在 `tx_user.h`（需定义 `TX_INCLUDE_USER_DEFINE_FILE`）或编译选项中配置：

| 宏 | 说明 |
|----|------|
| `TX_TIMER_PROCESS_IN_ISR` | 在 ISR 中直接处理定时器（默认用定时器线程） |
| `TX_DISABLE_PREEMPTION_THRESHOLD` | 禁用抢占阈值 |
| `TX_DISABLE_STACK_FILLING` | 禁用栈填充 |
| `TX_ENABLE_STACK_CHECKING` | 启用栈检查 |
| `TX_NOT_INTERRUPTABLE` | 内核代码不可中断（减小代码体积，增加中断延迟） |
| `TX_INLINE_THREAD_RESUME_SUSPEND` | 内联线程恢复/挂起 |

> **注意**：修改系统时钟时，需同步更新 `TX_PORT_SYSTICK_HZ`，否则 SysTick 周期不正确。

---

## CSR 寄存器速查

| CSR 地址 | 名称 | 值 | 说明 |
|----------|------|----|------|
| `0x300` | mstatus | `0x1800` (启动) | MPP=Machine, MIE=0；运行中 bit3=MIE |
| `0x305` | mtvec | `_vector_base \| 3` | 绝对地址向量模式 |
| `0x341` | mepc | 中断返回 PC | |
| `0x804` | INTSYSCR | `0x3` | HPE 使能 |
| `0xbc0` | - | `0x25` | 预取使能 |
| `0xbc1` | - | `0x1` | 中断嵌套控制 |
| `0xE000E000` | PFIC_BASE | - | 中断控制器基址 |
| `0xE000F000` | STK_CTLR | - | SysTick 控制 |

### mstatus 关键位

| Bit | 字段 | 说明 |
|-----|------|------|
| 3 | MIE | 全局中断使能 |
| 7 | MPIE | 中断前的 MIE 保存值 |
| 12:11 | MPP | 中断前的特权模式（`0b11` = Machine） |

新线程 mstatus = `0x1880`：MPP=Machine, MPIE=1, MIE=0（`mret` 后 MIE=MPIE=1，开中断）。

---

## TCB 关键字段偏移

| 偏移 | 字段 | 说明 |
|------|------|------|
| +4 | `tx_thread_run_count` | 运行计数 |
| +8 | `tx_thread_stack_ptr` | 栈指针 |
| +16 | `tx_thread_stack_end` | 栈底 |
| +24 | `tx_thread_time_slice` | 剩余时间片 |

---

## 已解决的问题

### 1. 持续复位崩溃

**现象**：上电后系统不断复位，疯狂打印启动信息。

**根因**：启动文件 `startup_CH585_TX.S` 设置 `mstatus = 0x88`（用户模式），`mret` 后 `main()` 运行在用户模式，CSR 访问触发非法指令异常 → 复位循环。

**修复**：`li t0, 0x88` → `li t0, 0x1800`（MPP = Machine 模式）。

### 2. 系统挂起

**现象**：打印启动信息和两轮线程输出（thread 0, thread 1, thread 0, thread 1）后完全停止。

**根因**：`tx_timer_interrupt.S` 在 `TX_TIMER_PROCESS_IN_ISR` 未定义时直接调用 `_tx_thread_system_resume(_tx_timer_thread)`，绕过了 `_tx_timer_expiration_process`。后者本应在调用前执行 `preempt_disable++`。缺少此递增导致 `preempt_disable` 下溢（0 → 0xFFFFFFFF），进而：
1. `tx_thread_sleep()` 检查 `preempt_disable != 0` 返回 `TX_CALLER_ERROR`，线程无法睡眠
2. `_tx_thread_system_suspend()` 末尾 `TX_THREAD_SYSTEM_RETURN_CHECK` 返回非零，`_tx_thread_system_return` 不被调用 → 上下文切换不发生 → 系统挂起

**修复**：移除 `#ifdef TX_TIMER_PROCESS_IN_ISR` 条件分支，始终调用 `_tx_timer_expiration_process`，与官方 risc-v32 参考移植一致。

---

## 演示程序

`src/Main.c` 包含两个演示线程：

| 线程 | 优先级 | 睡眠 | 周期 |
|------|--------|------|------|
| thread 0 | 2 | 100 tick | 1 秒 |
| thread 1 | 3 | 50 tick | 0.5 秒 |

预期输出：

```
ThreadX CH585 port start
ThreadX CH585: thread 0
ThreadX CH585: thread 1
ThreadX CH585: thread 0
ThreadX CH585: thread 1
ThreadX CH585: thread 0
...（持续输出）
```

UART1 配置：PA8=RXD, PA9=TXD，波特率跟随系统时钟。

---

## 使用方法

1. 在 WCH IDE (MounRiver Studio) 工程中，将 `Startup/startup_CH585_TX.S` 替换原厂 `startup_CH585.S`
2. 添加 `Middleware/threadx/common/src/` 下所有 `.c` 文件到编译
3. 添加 `Middleware/threadx/ports/QingkeV3C/src/` 下所有 `.S` 和 `.c` 文件到编译
4. 包含路径：`Middleware/threadx/common/inc/` 和 `Middleware/threadx/ports/QingkeV3C/inc/`
5. 链接脚本使用 `Ld/Link.ld`（已包含 `.highcode` 段定义）
6. 编译下载到 CH585M

---

## 许可证

ThreadX 遵循 MIT 许可证。移植代码遵循相同许可。
