# ThreadX QingKe V3C 移植 (CH585M)

Azure RTOS ThreadX 在 WCH QingKe V3C (CH585M, RV32IMAC) 上的移植实现。

## 概述

本移植针对 CH585M MCU 的 **QingKe V3C** RISC-V 内核，采用**全软件上下文保存**模型（与官方 ThreadX risc-v32/risc-v64 移植一致）。

> **重要**：本移植**关闭 HPE 硬件压栈**（启动文件 CSR `0x804 = 0`）。原因见[已解决的问题 #3](#3-随机崩溃hpe-弹栈状态依赖)。

### 硬件平台

| 项目 | 说明 |
|------|------|
| MCU | CH585M (WCH) |
| 内核 | QingKe V3C (RV32IMAC, 32 位 RISC-V) |
| Flash | 448 KB (0x00000000) |
| SRAM | 128 KB (0x20000000) |
| 系统时钟 | 62.4 MHz (HSE PLL) |
| 中断控制器 | PFIC |
| 系统定时器 | SysTick (0xE000F000) |

### 移植参考

- **官方 ThreadX risc-v32/risc-v64 移植**：上下文切换逻辑、调度器结构、128B 全量软件帧
- **WCH 原厂 FreeRTOS/RT-Thread 移植**：确认 HPE 不可用于线程上下文恢复、流水线延时处理、CSR 配置

---

## 目录结构

```
QingkeV3C/
├── inc/
│   └── tx_port.h                  # 移植层头文件：类型、栈帧、寄存器、构建选项
├── src/
│   ├── qingke_hpe_isr.S           # 统一中断包装（TX_ISR_PROLOGUE/EPILOGUE 宏，全软件上下文）
│   ├── tx_initialize_low_level.c  # 底层初始化：系统栈、SysTick、PFIC
│   ├── tx_thread_interrupt_control.S  # 中断开关（mstatus.MIE + 流水线延时）
│   ├── tx_thread_schedule.S       # 调度器：查找就绪线程并恢复上下文
│   ├── tx_thread_stack_build.S    # 构建新线程初始栈帧
│   ├── tx_thread_system_return.S  # 线程主动让出 CPU
│   └── tx_timer_interrupt.S       # SysTick tick 处理
└── README.md                      # 本文件
```

---

## 上下文模型与栈帧布局

### 全软件上下文（HPE 已关闭）

中断入口由软件保存全部易失寄存器，`mret` 仅负责返回（跳转 mepc、恢复 MIE/MPP），**不依赖任何硬件弹栈行为**：

- HPE 的 `mret` 硬件弹栈（恢复 16 个调用者保存寄存器 + sp 偏移）受**内部状态位**控制：中断进入时置位、弹栈后清除
- 调度器从"线程主动让出"（`_tx_thread_system_return`）或首次调度路径用 `mret` 恢复中断帧时，该状态位**未置位，弹栈不会发生**
- 若依赖弹栈：恢复的线程 sp 少加 64 字节、a0-a7/t0-t6/ra 全为垃圾值 → 栈错位破坏 → 随机崩溃
- WCH 原厂 FreeRTOS/RT-Thread 移植同样不依赖 HPE 弹栈恢复线程（均手动保存全部寄存器，SW_Handler 中还用 `csrs 0x804, 0x20` 显式禁止弹栈）

### 中断栈帧（128 字节，32 字）

所有中断入口由 `TX_ISR_PROLOGUE` 构建的软件帧：

```
偏移    内容                  说明
+0x00   帧类型 = 1           中断帧标识
+0x04   ra                  返回地址
+0x08   t0                  调用者保存寄存器
+0x0C   t1
+0x10   t2
+0x14   t3
+0x18   t4
+0x1C   t5
+0x20   t6
+0x24   a0
+0x28   a1
+0x2C   a2
+0x30   a3
+0x34   a4
+0x38   a5
+0x3C   a6
+0x40   a7
+0x44   s0                  被调用者保存寄存器
+0x48   s1
+0x4C   s2
+0x50   s3
+0x54   s4
+0x58   s5
+0x5C   s6
+0x60   s7
+0x64   s8
+0x68   s9
+0x6C   s10
+0x70   s11
+0x74   mstatus             机器状态寄存器
+0x78   mepc                被打断处的 PC
+0x7C   保留
```

### 主动栈帧（64 字节）

`_tx_thread_system_return`（线程主动让出）构建的帧。由 C 函数调用进入，按 RISC-V ABI 调用者保存寄存器本就无需保存：

```
偏移    内容                  说明
+0x00   帧类型 = 0           主动帧标识
+0x04   ra                  返回地址
+0x08   s0                  被调用者保存寄存器
...     s1..s11             +0x0C..+0x34
+0x38   mstatus             机器状态寄存器
```

### 新线程初始栈帧

`tx_thread_stack_build.S` 为新线程构建**单个 128 字节中断帧**（与 ISR 帧布局完全一致）：
类型 = 1，全部寄存器 = 0，mstatus = `0x1880`（MPP=Machine, MPIE=1, MIE=0），mepc = `_tx_thread_shell_entry`。

调度器按中断帧恢复后 `mret`：MIE←MPIE=1（开中断），跳转到线程入口，全部寄存器从 0 开始（线程参数由 TCB 提供，不依赖寄存器）。

---

## 中断管理

### 统一中断包装

`qingke_hpe_isr.S` 定义了两个宏，所有中断共用：

#### TX_ISR_PROLOGUE（中断入口）

1. 软件保存全部易失寄存器构成 128B 中断帧（类型 = 1，ra/t0-t6/a0-a7/s0-s11/mstatus/mepc）
2. `_tx_thread_system_state++`（内核据此识别 ISR 上下文）
3. 首层中断（state == 1）且有线程被打断时：保存 sp 到 TCB 偏移 8，切到系统栈
4. 嵌套中断或初始化/空闲期间：不换栈，直接在当前栈上处理

#### TX_ISR_EPILOGUE（中断出口）

1. `_tx_thread_system_state--`
2. 若仍在中断嵌套/初始化/空闲：恢复全量软件帧后 `mret` 返回
3. 检查抢占条件：
   - `preempt_disable > 0`：不抢占，恢复被打断线程
   - `execute_ptr == current_ptr`：无更高优先级就绪，恢复被打断线程
   - 需要抢占：保存剩余时间片，清 `current_ptr`，切系统栈，跳转 `_tx_thread_schedule`（上下文已在入口完整保存）

### 中断向量表

`startup_CH585_TX.S` 定义了两张表：

| 表 | 用途 |
|----|------|
| `_vector_base` | 硬件向量表，IRQn 16-39 指向 `unified_interrupt_entry`（LLE 除外，直接入口） |
| `_real_user_vector_base` | 用户 ISR 函数指针表，`unified_interrupt_entry` 按 `mcause` 查表分发 |

外部中断统一经 `unified_interrupt_entry` 分发，该入口负责 ThreadX 上下文保存/抢占判定，然后按 `mcause` 索引 `_real_user_vector_base` 调用用户 ISR。

**LLE (IRQn 21)** 保持直接入口，兼容 BLE 库的时序要求。`_BB_IRQHandler_base` 和 `_LLE_IRQHandler_base` 全局标签供 BLE 库运行时替换。

### 中断嵌套控制

- PFIC 层面：SysTick/SWI 的 IPRIOR bit7 = 1（非抢占优先级）
- 软件层面：`_tx_thread_system_state` 计数器管理，中断期间全程关闭 MIE
- CSR `0xbc1` = `0x0`（无硬件嵌套，与原厂移植一致）

### QingKe 3 级流水线延时

`csrci mstatus, 8`（关中断）后需补 3 个 `nop`，等待 3 级流水线确认 MIE 生效。这一做法与 WCH 官方 FreeRTOS/RT-Thread 移植一致，见 `tx_thread_interrupt_control.S` 和 `tx_thread_schedule.S`。

---

## 上下文切换

本移植不使用软件中断（SWI）触发上下文切换，与官方 ThreadX risc-v 移植一致。

### 主动让出 — `_tx_thread_system_return`

线程调用阻塞服务（如 `tx_thread_sleep`）时由内核调用：

1. 使用 `csrrci t0, mstatus, 0x08` 原子保存调用前的 mstatus 并关闭 MIE
2. 补 3 个 NOP，等待 QingKe 三级流水线确认 MIE 生效
3. 构建 64B 主动帧（类型 = 0，ra，s0-s11，保存的原始 mstatus）
4. 保存 sp 到 TCB 偏移 8，保存剩余时间片
5. 清 `current_ptr`，切系统栈

主动帧必须保存**调用前**的 mstatus。如果先关闭 MIE 再读取 mstatus，调度器恢复线程时会把 MIE=0 写回，导致线程恢复后中断保持关闭，进而出现 tick 和调度时序异常。
6. `j _tx_thread_schedule`

### 调度器 — `_tx_thread_schedule`

1. `csrsi mstatus, 0x08` 开中断，等待就绪线程
2. 轮询 `execute_ptr`，为空则 `wfi` 睡眠
3. 找到就绪线程后 `csrci` + 3 NOP 关中断
4. 设 `current_ptr = execute_ptr`，`run_count++`，设时间片
5. 切到线程栈（TCB 偏移 8）
6. 判断帧类型：
   - **中断帧**（type=1，128B）：恢复 mepc/mstatus/ra/t0-t6/a0-a7/s0-s11，sp 跳过软帧，`mret`
   - **主动帧**（type=0，64B）：恢复 mstatus/ra/s0-s11，sp 跳过软帧，`ret`

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
  ├── TX_ISR_PROLOGUE          # 保存全量上下文, system_state++
  ├── 清 STK_SR (写 0 到 0xE000F004)
  ├── call _tx_timer_interrupt # tx_timer_interrupt.S
  │     ├── _tx_timer_system_clock++
  │     ├── 时间片递减, 到期调 _tx_thread_time_slice
  │     └── 定时器链表推进, 到期调 _tx_timer_expiration_process
  │           └── preempt_disable++ → 唤醒 _tx_timer_thread
  └── TX_ISR_EPILOGUE          # system_state--, 抢占判定
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

向量表（`mtvec`，绝对地址模式）由启动文件 `startup_CH585_TX.S` 配置；HPE 硬件压栈在该文件中显式关闭（`0x804 = 0`）。

---

## 启动流程

```
上电
  │
  ▼
_start → handle_reset (startup_CH585_TX.S)
  ├── 加载 .highcode_init / .highcode / .data 从 Flash 到 RAM
  ├── 清零 .bss
  ├── CSR 0xbc0 = 0x25  (Prefetch Enable)
  ├── CSR 0x804 = 0x0   (HPE 关闭, 全软件上下文)
  ├── CSR 0xbc1 = 0x0   (无硬件中断嵌套)
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
| `TX_MINIMUM_STACK` | 1024 | 最小线程栈大小（字节） |
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
| `0x804` | INTSYSCR | `0x0` | **HPE 关闭**（全软件上下文） |
| `0xbc0` | - | `0x25` | 预取使能 |
| `0xbc1` | - | `0x0` | 无硬件中断嵌套 |
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

### 3. 随机崩溃（HPE 弹栈状态依赖）

**现象**：多线程 + SysTick 抢占 + 睡眠切换场景下随机崩溃/挂死。

**根因**：移植层原依赖 HPE 硬件压栈（CSR `0x804 = 0x3`）保存 16 个调用者保存寄存器，`_tx_thread_schedule` 恢复中断帧时 `addi sp, sp, 64; mret` 假设硬件弹栈必然发生。

但 HPE 的 `mret` 硬件弹栈受**内部状态位**控制（中断进入时置位、弹栈后清除）：

| 进入调度器的路径 | 弹栈状态位 | 后果 |
|---|---|---|
| 中断退出抢占（EPILOGUE） | 已置位 | 弹栈正确 |
| 线程主动让出（`_tx_thread_system_return`）/ 首次调度 | **未置位** | **sp 少加 64 字节 + a0-a7/t0-t6/ra 全为垃圾 → 栈错位破坏 → 随机崩溃** |

只要系统中存在两个先后被中断抢占的线程，第二个被恢复时必踩此坑。

**佐证**：WCH 原厂 FreeRTOS/RT-Thread CH585 移植均不依赖 HPE 弹栈恢复线程——上下文切换全部采用完整软件帧（手动保存全部寄存器），且 SW_Handler 在 `mret` 前用 `csrs 0x804, 0x20` 显式禁止弹栈。

**修复**（关闭 HPE，全软件上下文，与官方 ThreadX risc-v32 移植结构一致）：
1. `qingke_hpe_isr.S`：中断帧改为 128 字节全量软件帧（ra/t0-t6/a0-a7/s0-s11/mstatus/mepc），宏更名为 `TX_ISR_PROLOGUE`/`TX_ISR_EPILOGUE`
2. `tx_thread_schedule.S`：中断帧路径全量软件恢复后 `mret`，不依赖硬件弹栈
3. `tx_thread_stack_build.S`：初始帧改为单个 128 字节中断帧，删除伪 HPE 帧
4. `startup_CH585_TX.S`：`0x804 = 0`（关 HPE）、`0xbc1 = 0`（与原厂一致）

---

## 测试程序

`src/Main.c` 包含 21 组板级综合测试（test_thread，优先级 15，TX_AUTO_START）。这些测试借鉴官方 `Examples/threadx/test/tx/regression` 的测试分类，但不是官方完整回归套件的替代品。

官方套件包含约 100 个独立测试，覆盖更多错误参数、对象删除、挂起超时、线程终止、优先级排序、信息查询和多种 ISR 组合。当前测试重点验证 CH585 移植最关键的上下文切换、中断分发、同步对象和定时器路径。

| # | 测试项 | # | 测试项 |
|---|--------|---|--------|
| 1 | 线程创建与基本调度 | 12 | 系统时间 |
| 2 | 线程抢占调度 | 13 | 中断控制 |
| 3 | 时间片轮转 | 14 | 集成测试（4 线程 + 队列/信号量/互斥量/事件/内存池） |
| 4 | 线程挂起/恢复 | 15 | **真实外设中断**（TMR0 → unified_interrupt_entry 全链路 + ISR 内内核 API + EPILOGUE 抢占） |
| 5 | 消息队列 | 16 | **互斥量优先级继承**（双锁嵌套提升 14→12→13→14 逐级恢复） |
| 6 | 计数信号量 | 17 | **抢占阈值**（阈值内不抢占 + tx_thread_preemption_change 放宽后立即抢占） |
| 7 | 互斥量（含递归） | 18 | **线程高级 API**（wait_abort / relinquish 轮转 / priority_change / TX_COMPLETED / 挂起睡眠线程） |
| 8 | 事件标志组（AND/OR） | 19 | **阻塞中止与终止清理**（wait_abort 中止队列阻塞 / terminate 清理互斥量挂起链） |
| 9 | 字节内存池 | 20 | **多字消息队列**（TX_4_ULONG / TX_16_ULONG 数据完整性） |
| 10 | 块内存池 | 21 | **定时器精度**（单次/周期到期 tick 精度 + 停止后不再到期） |
| 11 | 软件定时器 |  |  |

测试完成后输出 PASS/FAIL 汇总，21 组测试会按顺序全部执行并报告统计。

### 测试同步原则

测试代码不再使用固定 tick 推断线程是否已经运行或进入等待状态：

- 线程入口通过信号量报告“已开始执行”；
- relinquish、抢占阈值和恢复测试通过完成信号量报告实际进度；
- wait-abort、队列阻塞和延迟挂起使用明确的对象状态或握手确认；
- `sleep(5/20/25/35)` 等固定 tick 仅用于真实的时间流逝、时间片和定时器精度测试。

因此，`sleep(1)` 在这些测试中表示真实的一个系统 tick，不再被当作通用的线程启动等待机制。

**用户 ISR 编写要点**（重要）：经 `unified_interrupt_entry` 分发的外设 ISR 必须是**普通 C 函数 + `__HIGH_CODE`**，**不可加 `__INTERRUPT` 属性**——该属性（WCH-Interrupt-fast）依赖 HPE 且以 `mret` 返回，仅适用于直接入口向量（如 LLE）。统一入口已保存全量上下文并以 `jalr` 普通调用进入用户 ISR。参考 `src/Main.c` 的 `TMR0_IRQHandler`。

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
