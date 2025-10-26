# LAB-3 实验报告

## 过程日志
1. 2025.10.13 更新lab3文件
2. 2025.10.14 王俊翔完成实验要求 1&2
3. 2025.10.16 张子扬完成实验要求 3&4

## 实现思路  
  
- 抽象目标：构建“被打断 → 处理中断 → 返回”这条闭环。
- 实践落点：
  1. `start.c` 在 M-mode 设置委托、激活计时器；
  2. `main.c` + `trap_kernel_init` 准备页表与 PLIC；
  3. `trap.S` 保存/恢复寄存器；
  4. `trap_kernel.c` 根据 `scause` 分发；
  5. `plic.c` / `timer.c` / `uart.c` 处理硬件细节；
  6. 通过滴答日志、串口回显验证链路是否正常。


## 串口中断旅程（UART）
```
┌────────────┐        ┌─────────────┐        ┌──────────────┐
│  UART RX   │ ─────► │  PLIC IRQ   │ ─────► │ kernel_vector│
│ (字符到达)  │        │  聚合仲裁   │        │ 保存32个寄存器 │
└────────────┘        └──────┬──────┘        └──────────────┘
                              │
                              ▼
                       ┌──────────────┐
                       │trap_kernel   │ 读取 scause，高位=1? 低位=9?
                       │_handler      │─────┬───────────────┐
                       └──────┬───────┘     │ yes           │ no → panic
                              │             ▼
                              │      ┌──────────────────────────┐
                              │      │external_interrupt_handler│
                              │      └──────┬───────────────────┘
                              │             │
                              │       plic_claim()
                              │             │← IRQ==UART?
                              │             ▼
                              │      ┌─────────────┐
                              │      │ uart_intr() │ 处理CR/LF、退格、打印字符
                              │      └──────┬──────┘
                              │             │
                              │       plic_complete()
                              ▼
                    ┌─────────────────┐
                    │ kernel_vector   │ 恢复寄存器
                    └─────────────────┘
                              │
                              ▼
                           sret → 回到被中断的指令
```

在进入 `main()` 前用 `start()` 委托 S 级处理器接手中断（start.c 中 `w_mideleg`、`w_medeleg`、`w_mcounteren`）。在 `uart_intr()` 里支持回车、换行和退格（uart.c 的 CR/LF 归一化与 `"\b \b"` 逻辑）。通过 PLIC 识别 UART IRQ 并调用正确的处理中断流程（plic.c + `trap_kernel_handler()`→`external_interrupt_handler()`）。  

### 怎么做
>`main()` 里 CPU0 先跑 `trap_kernel_init()`（初始化 PLIC、创建系统时钟），每核再跑 `trap_kernel_inithart()`（`w_stvec(kernel_vector)`、`w_sie(...)`、`intr_on()`），对应任务 3/4 的 “识别并进入 `uart_intr()`”。  

- **M 态委托与准备（start.c）**  
  - `w_mideleg((1<<1)|(1<<5)|(1<<9))`：硬件默认只把中断送到 M 态，这里手工把软件/时钟/外部三类转给 S 态，等于告诉硬件“以后直接找内核（S）”。  
  - `w_medeleg(...)`：异常也交给 S 态统一处理，否则一旦 U 态出错还得回 M 态，流程被打断。  
  - `w_mcounteren(0x7)`：S 态需要读 `time`/`cycle`/`instret` 做调度或打印日志，不授权就会触发异常。  
  - `mstatus.MPP = S`, `mepc = main`, `mret`：把返回目标改成 S 态 `main()`，实现“导演 (M) 布好场 → 把麦递给经理 (S)”。

- **S 态入口与开关（main.c, `trap_kernel_inithart`, `trap_kernel_init`）**  
  - `w_stvec(kernel_vector)`：装好门，任何 S 态 trap 统一进 `kernel_vector`，后面才能展开 C 层。  
  - `w_sie(...)`：对外部/软件/时钟单独开闸；`intr_on()` 打开总开关，否则门虽装好但始终不响。  
  - `plic_init()`/`plic_inithart()`：给 UART IRQ 配优先级、在每个 hart 上启用；缺它就收不到 PLIC 报告。

- **Trap 入口保存（trap.S）**  
  - `kernel_vector` 保存 32 个寄存器再调用 `trap_kernel_handler`：保证 C 代码能自由使用寄存器；没有这个“保存→处理→恢复”，回到原指令时寄存器会被破坏。

- **返回 C 语言（trap_kernel.c）**  
  - `trap_kernel_handler()` 解读 `scause`：高位=1 表示中断，低位=9 表示 S 态外部中断；这是抽象“判类型”的具体实现。  
  - `external_interrupt_handler()` 里 `plic_claim()`：向 PLIC 询问“是谁敲门”；判断 `irq==UART_IRQ` 后调用 `uart_intr()`，最后 `plic_complete()` 告诉 PLIC“处理完了，可以放下一个”。

- **设备逻辑（`uart_intr()`）**  
  - 循环 `uart_getc_sync()` 直到返 -1：清空 FIFO。  
  - CR/LF 合并输出 `"\r\n"`、退格回显 `"\b \b"`、过滤不可打印字符：让终端行为可预期，同时保持“只回显、不做复杂逻辑”的设计原则。



## 时钟中断旅程（CLINT）
```
┌────────────┐        ┌─────────────┐
│  CLINT     │        │ timer_init  │ 在 start.c:
│ (mtime到点)│◄──────►│ 设置mtvec    │  mtimecmp = mtime + INTERVAL
└────┬───────┘        │ mie |= MTIE │  mscratch 指向缓冲区
     │                └─────────────┘
     │ 触发 M 态时钟中断
     ▼
┌──────────────┐
│ timer_vector │ 保存 a1-a3，交换 mscratch
│ (M 态入口)   │ 更新 mtimecmp += INTERVAL
└────┬────────┘
     │
     ▼
csrs sip, SSIP        ──────► 触发 S 态软件中断
mret 返回 M 态上下文
     │
     ▼
┌──────────────┐
│ kernel_vector│ 保存32个寄存器
└────┬─────────┘
     │
     ▼
┌──────────────┐
│trap_kernel   │ 读取 scause：高位=1? 低位=1?
│_handler      │─────┬───────────────┐
└────┬─────────┘     │ yes           │ no → panic
     │              ▼
     │      ┌────────────────────────┐
     │      │timer_interrupt_handler │
     │      └──────┬─────────────────┘
     │             │
     │        mycpuid()==0?
     │             │ yes
     │             ▼
     │      ┌──────────────┐
     │      │timer_update()│ ticks++（自旋锁保护）
     │      └──────────────┘
     │
     └─ w_sip(r_sip() & ~SSIP) 清除软件中断标志
            ▼
┌─────────────────┐
│ kernel_vector   │ 恢复寄存器
└─────────────────┘
            │
            ▼
         sret → 回到被中断的指令
```

在切换到 S 态前，由 M 态完成时钟初始化和中断向量设置（timer_init() 写 mtimecmp、mtvec、mie）。把 M 态时钟中断转换成 S 态软件中断，再在 S 态更新 ticks 并清掉 SSIP（timer_vector→timer_interrupt_handler()→timer_update()→w_sip(r_sip() & ~2)）。

### 怎么做

>start() 调 timer_init() 后 mret；timer_vector 里先用 mscratch 保存寄存器，再让 mtimecmp += INTERVAL，然后 csrs sip, 2 触发软件中断；trap_kernel_handler() 识别 scause 低位 1，调 timer_interrupt_handler()，只有 CPU0 调 timer_update()（spinlock 保护），最后清 SSIP。

- **M 态定时器配置（`start.c::timer_init`）**  
  - `mtimecmp = mtime + INTERVAL`：先设下一次触发点，等价于启动硬件闹钟。  
  - `mscratch` 指向本地临时区：给 `timer_vector` 提供存储寄存器和常量 (`mtimecmp` 地址、`INTERVAL`) 的位置。  
  - `w_mtvec(timer_vector)`：指明“闹钟响时先找 `timer_vector`”；`w_mie(...|MIE_MTIE)` 打开时钟中断分开关，继续保留 `mstatus.MIE` 让 M 态能响应。

- **S 态入口与开关（同串口）**  
  - 同样需要 `w_stvec`、`w_sie`、`intr_on()`，否则 `timer_vector` 把软件中断交给 S 态后没人接。

- **M 态中转（`timer_vector`）**  
  - 交换 `mscratch` 与 `a0`、暂存 `a1-3`：腾出寄存器空间，说明必须在 M 态自己保存现场。  
  - `mtimecmp += INTERVAL`：更新下一次闹钟，不做就只响一次。  
  - `csrs sip, SIP_SSIP`：手工拉起 S 态软件中断标志，把事件“转发”给内核。  
  - 恢复寄存器后 `mret`：返回原来的 M 态上下文（其实是回到被打断的 M 态指令），但因为设置了软件中断，接下来会立即跳到 `kernel_vector`。

- **返回 C 语言（`trap_kernel_handler` → `timer_interrupt_handler`）**  
  - `trap_kernel_handler()` 读 `scause` 高位=1、低位=1：识别是软件中断。  
  - `timer_interrupt_handler()`：只让 CPU0 执行 `timer_update()`（配合自旋锁避免多核竞争），随后 `w_sip(r_sip() & ~2)` 清除软件中断位，不然立刻再次触发。

- **定时逻辑（`timer_update` 等）**  
  - `spinlock_acquire`/`release` 保护 `ticks`，防止并发写；`ticks++` 就是抽象里的“系统时钟加一”。后续可以在这里加调度、打印等行为。

### 为什么
- **M/S 协作不可或缺**：CLINT 只能在 M 态写配置，因此用 `timer_vector` 把硬件中断转换成 S 态软件中断。  
- **软件中断自清**：若不清除 `SSIP`，内核会立即再次陷入同一处理而无法返回。  
- **单核更新与锁**：限制为 CPU0 更新 `ticks`，配合自旋锁避免多核竞争及输出混乱。  
- **统一框架**：再次利用 `kernel_vector` + `trap_kernel_handler` 骨架，复用已有保存/恢复与分发逻辑。

---

## 测试样例

### UART输入测试
![alt text](pictures/lab3串口中断.png)  

### 嘀嗒测试
在 `trap_kernel.c` 中的 `timer_interrupt_handle` 修改如下：
```c
// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler(void) {
    // 清除 SSIP，避免反复进入
    w_sip(r_sip() & ~2);

    // 在合适的位置添加一行“滴答”输出
    printf("cpu %d:di da\n", mycpuid());
}
```

![alt text](pictures/lab3嘀嗒.png)

并且，“di da”来自定时器触发的 S 态软件中断，其出现时间取决于两件事：每核何时开中断、各核 mtimecmp 何时到期。两核之间没有先后保证，所以可能先看到“cpu 1:di da”，再看到“cpu 0:di da”，所以与题干中的结果又一定出入。

### 时钟快慢测试
在 `trap_kernel.c` 中的 `timer_interrupt_handle` 修改如下：
```c
void timer_interrupt_handler(void) {
    // 清除 SSIP，避免反复进入
    w_sip(r_sip() & ~2);

    // 递增全局ticks（原子加），仅CPU0打印
    uint64 t = __sync_add_and_fetch(&g_ticks, 1);
    if (mycpuid() == 0) {
        printf("ticks=%d\n", (int)t);
    }
}
```

![alt text](pictures/lab3快慢.png)



## 五、设计取舍与反思

- 为什么把 CR 与 LF 统一回显为 CRLF？
  - 许多终端需要 CRLF 才能将光标移到下一行开头；只输出 `\n` 常会导致“斜着走”。
- 为什么 Backspace 回显要输出 `"\b \b"`？
  - 先回退一格，再用空格覆盖，再回退一格，让光标停在被删字符的位置，实现“可视化删除”。
- 为什么中断识别要通过 PLIC claim？
  - RISC-V 平台级中断控制器（PLIC）负责聚合外设中断；只有 claim 才知道是哪一个外设触发中断，并且必须 complete 归还。
- 安全性考虑：
  - 中断处理里尽量保持短小，避免复杂状态；`uart_intr()` 只做回显，不做行缓冲与上层协议解析。

---
