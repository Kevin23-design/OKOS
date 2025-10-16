# LAB-3 实验报告
---

## 实现思路

思考切入点：中断是“打断→处理→返回”。我们需要打通整条链：

硬件(UART) → PLIC → S 态入口(kernel_vector) → C 处理(trap_kernel_handler) → 判断来源 → 调用 uart_intr → 回显 → 返回

### 1. 在 M 态完成委托与基础准备（start.c）

为什么要委托？因为我们的内核主要跑在 S 态。若不委托，S 态收不到中断。关键寄存器：
- `medeleg`/`mideleg`：把异常/中断从 M 态委托给 S 态（本实验需要 SSIP/SEIP/STIP 等）
- `mcounteren`：允许 S 态读计数器（后续计时器会用到）
- 设置 `mstatus.MPP = S`，`mepc = main`，`mret` 进入 S 态执行 `main()`
- 初始化计时器（`timer_init()`）留待下一步实验用

对应代码：`src/kernel/boot/start.c`（已实现）。

### 2. 在 S 态设置入口并开启中断（trap_kernel.c + main.c）

- 用 `w_stvec(kernel_vector)` 指定 S 态 trap 向量入口
- 置位 `SIE` 的 `SEIE/SSIE/STIE` 分开关，并打开 `sstatus.SIE`（`intr_on()`）
- 每核初始化 `plic_inithart()`；CPU0 先做全局初始化 `plic_init()`、内存、页表等

对应代码：
- `trap_kernel_inithart()` 设置 stvec、SIE、intr_on
- `trap_kernel_init()` 调用 `plic_init()` 并创建系统时钟对象
- `main()` 在合适的位置调用上述两个初始化函数

### 3. 识别 UART 外设中断（plic.c + trap_kernel.c）

- PLIC 侧：
  - `plic_init()` 给 UART IRQ 设置优先级
  - `plic_inithart()` 在当前 CPU 的 SENABLE 中打开 UART IRQ，并设置阈值
- S 态处理：
  - `trap_kernel_handler()` 里根据 `scause` 判断是中断还是异常；当为中断且小类为 9（S 态外部中断）时，进入 `external_interrupt_handler()`
  - `external_interrupt_handler()` → `plic_claim()` 得到 `irq` → 若等于 `UART_IRQ` 调用 `uart_intr()` → 最后 `plic_complete(irq)` 归还中断

这一步完成“找到并识别是 UART 引发的中断”。

### 4. 完善 UART 中断回显（uart.c）

在 `uart_intr()` 中对键盘输入进行处理并回显：
- 将 `\r` 或 `\n` 统一回显为 `\r\n`
- 处理退格（`'\b'` 与 `0x7f'`）：回显 `"\b \b"`
- 仅回显可打印字符（ASCII 32~126）和 `\t`，避免控制字符造成噪声

这就实现了“只用回显字符”的功能需求。

---

## 关键代码片段

- S 态入口设置（`trap_kernel_inithart`）：
  - `w_stvec(kernel_vector);`
  - `w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);`
  - `intr_on();`
- 中断识别（`trap_kernel_handler`）：
  - `if (is_interrupt && cause==9) external_interrupt_handler();`
- PLIC 驱动：
  - `plic_claim()`/`plic_complete(irq)`
- UART 回显（`uart_intr`）：
  - `\r`/`\n` → `\r\n`；`\b`/`0x7f` → `"\b \b"`；其它过滤

---

## 测试样例
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

### UART输入测试
![alt text](pictures/lab3串口中断.png)  

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
