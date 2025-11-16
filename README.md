# LAB-6: 单进程走向多进程——进程调度与生命周期

## 过程日志
1. 2025.11.10 更新lab6文件
2. 2025.11.10 张子扬初步完成lab6的测试以及README
3. 2025.11.16 王俊翔修改test02/03/04涉及到的bug并完善README


## 代码结构
```
OKOS
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── .gdbinit.tmp-riscv xv6自带的调试配置
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目
├── kernel.ld      定义了内核程序在链接时的布局
├── pictures       README使用的图片目录 (CHANGE, 日常更新)
├── lab-6-README.md实验指导书 (CHANGE, 日常更新)
├── README.md      实验报告 (CHANGE, 日常更新)
└── src            源码
    ├── kernel     内核源码
    │   ├── arch   RISC-V相关
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── boot   机器启动
    │   │   ├── entry.S
    │   │   └── start.c
    │   ├── lock   锁机制
    │   │   ├── spinlock.c
    │   │   ├── sleeplock.c (DONE, 实现睡眠锁)
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h (CHANGE, 增加头文件)
    │   │   └── type.h (CHANGE)
    │   ├── lib    常用库
    │   │   ├── cpu.c
    │   │   ├── print.c
    │   │   ├── uart.c
    │   │   ├── utils.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── mem    内存模块
    │   │   ├── pmem.c
    │   │   ├── kvm.c (DONE, kvm_init从单进程内核栈初始化到多进程内核栈初始化)
    │   │   ├── uvm.c
    │   │   ├── mmap.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── trap   陷阱模块
    │   │   ├── plic.c
    │   │   ├── timer.c (DONE, 新增timer_wait函数, 增加时钟中断调度逻辑)
    │   │   ├── trap_kernel.c (DONE, 增加时钟中断调度逻辑)
    │   │   ├── trap_user.c (DONE, 增加时钟中断调度逻辑)
    │   │   ├── trap.S
    │   │   ├── trampoline.S
    │   │   ├── method.h (CHANGE, 增加timer_wait函数声明)
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── proc   进程模块
    │   │   ├── proc.c (DONE, 核心工作)
    │   │   ├── swtch.S
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE)
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c (CHANGE, 支持新的系统调用)
    │   │   ├── sysfunc.c (DONE, 实现新的系统调用)
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE)
    │   └── main.c (CHANGE)
    └── user       用户程序
        ├── initcode.c (CHANGE)
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h (CHANGE)
```

## 实验过程

为了把“单进程内核”升级成“能在多核上稳定调度多个用户进程的系统”，整个过程自顶向下拆成三个层级：先明确想要的能力，再设计可验证的路径，最后用测试驱动收敛缺陷。

### 一、目标导向的能力建设
- 目标：内核需要在时钟中断驱动下，安全地在多核之间切换不同的用户进程，同时支持 `fork/wait/sleep` 等必要的生命周期接口。
- 方案：先在抽象层设计“时钟触发调度 → 保存/恢复 trapframe → 重新进入用户态”的闭环，再映射到对应模块。
- 落实：
    - `trap/timer.c` 引入 `timer_wait()` 并由 `trap_kernel.c` 驱动时钟中断，形成调度入口；`trap_user.c` 负责切换向量表与 `sstatus`，保证用户/内核态切换合法。
    - `proc.c` 补齐 `proc_init/proc_fork/proc_exit/proc_wait`、`proc_scheduler/proc_try_wakeup/proc_sleep` 等函数，实现进程结构体生命周期管理。
    - `syscall.c/sysfunc.c` 开放 `fork/wait/sleep/brk/mmap`，并把 `user/initcode.c` 设计成可选择不同测试场景的“脚本”，实现从抽象 API 到可执行代码的落地。

### 二、构建可重复的验证路径
- 初衷是把“想要的行为”具象成用户态程序，方便回归；因此 `initcode.c` 被划分为 fork 树、wait/mmap、sleep/wait 三个场景。
- 每次修改都运行 `make clean && make run`（首次）与 `make run`（增量），在多核 QEMU 下比对输出；截图统一落在 `picture/`，方便对照 README。
- 为了让输出一眼能看出差异，调度、睡眠、唤醒、wait 等关键路径都加上结构化日志：既是调试手段，也是交付物的一部分。

### 三、对照测试逐个收敛缺陷
虽然最终 `test.md` 会删除，但 README 中保留了每个测试的意图、问题和修复方式，便于助教直接复现。

#### Test-01：单进程基线自检
- 目标：确认 `proczero` 能在两核环境正常启动、打印 "hello world"，确保时钟/串口/基础 trap 流程无误，为后续多进程改造提供可回归的 baseline。
- 思路：保持 `initcode.c` 的 test-01 场景（仅执行 `proczero: hello world!`），重点观察 `cpu 0/1 is booting!` 与打印顺序，排查多核启动和基础中断初始化问题。
- 结果：串口输出稳定为 `cpu 0 is booting!`, `cpu 1 is booting!`, `proczero: hello world!`，说明调度骨架搭建前的内核最小闭环健全，可作为之后每次重构的 sanity check。
![test-01 output](picture/测试一.png)

#### Test-02：多叉 fork 调度异常
- 现象：fork 树输出停在 `level-2!`，随后在 `proc_return` 附近触发 `Instruction page fault`。
- 解决思路：从“内核栈归属”这一抽象问题入手，确认子进程复制 trapframe 时沿用了父栈地址；因此在 `proc_fork()` 中把 `user_to_kern_sp` 改指向 `child->kstack + PGSIZE`，并在调度器里打印 `proc %d is running...` 方便核对调度序。
- 结果：`proc 1..4` 与 `level-1/2/3!` 交替出现，系统可以无限循环。
![test-02 output](picture/测试二.png)

#### Test-03：wait/sleep 死锁与杂噪输出
- 现象：`mmap/brk` 日志后 panic `panic! acquire`，且终端被页表打印与调度日志淹没。
- 解决思路：从“父子同步协议”切入，发现 `proc_wait()` 持有 `parent->lk` 休眠，而 `proc_exit()` 唤醒时也要拿同一把锁，形成死锁；于是新增 `wait_lk`，让 `proc_sleep/proc_wakeup` 在这把专用锁上配合，同时清理 `sys_brk/sys_mmap/sys_munmap` 的噪声打印。
- 结果：`child proc → parent proc → good boy! → --------test end----------` 顺序稳定，终端干净。
![test-03 output](picture/测试三.png)

#### Test-04：sleep 日志 & "kernel trap from user?"
- 现象：没有睡眠日志时难以判读；加上日志后又在多核环境频繁 panic，提示 `kernel trap from user? scause=0xa/0xc`。
- 解决思路：先在 `timer_wait()` 精准打印 30 条 `proc 2 is sleeping!`，再让 `proc_wait()` 输出两次 `proc 1 is wakeup!`，让用户可见日志与 README 对齐；随后审视“trap 返回顺序”，确认 `trap_user_return()` 没有在写 `stvec` 之前关闭中断，才导致中断抢占后 `SPP=0` 被误判为“来自用户”。修复方案是在函数开头 `intr_off()`，收尾由 `sret` 自动恢复。
- 结果：输出严格满足 README 要求，连续运行不再触发 panic。
![test-04 output](picture/测试四.png)

## 实验反思

### Test-01：baseline 验证
- **根因**：虽然 test-01 没有显式缺陷，但在多核 bring-up 中非常容易因为中断、串口或 `proc_alloc` 的初始化顺序出错，导致 `proczero` 看似启动却没有产出。通过保留这个测试，我们能在每轮大改前后快速验证“最简单路径”是否依旧成立。
- **修复心得**：将 `test-01` 纳入自动回归，无论修改 trap、调度或内存模块，都先跑一次 baseline，保证后续问题定位不被基础设施噪音干扰。

### Test-02：栈指针复制缺陷
- **根因**：对子进程 trapframe 的复制是“按值”操作，`user_to_kern_sp` 这种指针字段被不小心共享。只要子进程触发 trap，就会踩到父栈，引发 page fault。
- **修复过程**：确定 `child->kstack` 已在 `proc_alloc()` 中分配，于是只需在 `proc_fork()` 完成 `trapframe_copy` 后重写 `user_to_kern_sp`；同时把调度日志临时打开，确认每个 PID 的运行顺序与多叉树期望一致，确保“同栈”问题彻底消除。
- **收获**：复制结构体时绝不能忽略“指针语义”；今后会在类似函数中列清单校验每个指针成员，减少隐性共享。

### Test-03：wait/sleep 死锁
- **根因**：父进程在 `proc_wait()` 中拿着 `parent->lk` 进 `proc_sleep()`，而子进程 `proc_exit()` 也必须拿这把锁才能唤醒父进程，导致双方互等；同时 `proc_try_wakeup()` 全局扫描增加了竞态窗口，调试日志又让问题更难定位。
- **修复过程**：抽象出“父子同步只需要一把专用锁”的原则，引入 `wait_lk` 并在 `proc_wait/proc_exit` 中统一使用；`proc_try_wakeup()` 改成直接拿父进程锁查看 `sleep_space`，彻底避免遍历；最后删掉 `sys_brk/sys_mmap/sys_munmap` 的冗余输出，保证复现结果干净。
- **收获**：理解了“睡眠时必须持有与唤醒方相同的锁”这条铁律；今后编写同步代码会优先画出锁图，避免把状态锁和同步锁混用。

### Test-04：trap 返回竞态
- **根因**：在 `trap_user_return()` 中设置 `stvec→user_vector` 与清理 `SSTATUS_SPP` 时没有屏蔽中断；若恰好有时钟中断到来，`kernel_vector` 看到 `SPP=0` 就会以为 trap 来自用户，触发 `kernel trap from user?`，继而因上下文不一致导致 Instruction page fault。
- **修复过程**：先让输出与 README 对齐，保证“现象可视化”；接着分析 `sstatus` 位含义，确认只需要在写 CSR 前 `intr_off()` 即可避免竞态；函数末尾交给 `sret` 恢复中断。为防止未来回归难，`trap_user_handler()` 还加了 `SPP==0` 断言，第一时间暴露异常路径。
- **收获**：对 RISC-V `SPP/SPIE`、`intr_off/on` 的语义更扎实；任何涉及 CSR 写序列的代码必须结合硬件时序推敲。也学会了“先用用户可见日志固定预期，再去定位竞态”的调试套路。
