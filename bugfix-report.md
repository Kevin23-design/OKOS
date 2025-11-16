# LAB-6 Bugfix Report

> *说明：每个测试阶段按“问题-分析-修复-验证”结构呈现。*

## Test-02：多叉 fork 调度异常

### 初始症状
- 运行 `initcode.c` 的 fork 树程序时，在 `level-2!` 之后触发 `Instruction page fault` 并 panic。
- `result.md` 日志确认 fault 地址位于 `proc_return` 周边，怀疑是 trapframe / 内核栈错乱。

### 根因分析
1. `proc_fork()` 仅浅拷贝父进程的 trapframe：`child->tf = parent->tf;`，导致 `user_to_kern_sp` 仍指向父进程的内核栈。
2. 当内核切回子进程时，陷入 `proc_return()`，在同一栈上释放锁 / 切换，进而覆盖父进程上下文，最终触发 page fault。

### 关键修复
- 文件：`src/kernel/proc/proc.c`
- 修改位置：`proc_fork()` 在复制 trapframe 后，加入对子进程内核栈指针的重定向。

```c
// 子进程应当使用自己分配的内核栈
child->tf->user_to_kern_sp = child->kstack + PGSIZE;
```

### 额外改进
- 同时按照测试要求，在 `proc_scheduler()` 中增加调度日志，便于肉眼确认调度顺序：

```c
printf("proc %d is running...\n", p->pid);
```

### 验证步骤
1. 将 `initcode.c` 切换到测试 2 的 fork 树代码。
2. 执行 `make run`。
3. 观察串口输出（图见 `image.png`）。

### 测试结论
- `proc 1..4 is running...` 与 `level-1/2/3!` 交替打印，与 README 期望一致。
- 无 page fault / panic；系统进入调度循环稳定运行。
- 判定：**test-02 PASS**。

![test-02 output](picture/测试二.png)

---

## Test-03：wait/sleep 死锁与杂噪输出

### 初始症状
- 子进程打印 `MMAP_REGION / HEAP_REGION / STACK_REGION` 后，内核 panic：`panic! acquire`。
- 日志中充斥 `sys_mmap/sys_brk` 的页表打印以及调度器的 `proc x is running...`，干扰测试判读。

### 根因分析
1. `proc_wait()` 在遍历子进程后持有 `parent->lk` 调用 `proc_sleep(parent, &parent->lk)`；子进程 `proc_exit()` 也需要先获取 `parent->lk` 才能唤醒父进程 → 形成死锁。
2. `proc_try_wakeup()` 通过 `proc_wakeup()` 全局扫描，无法保证在持有父进程锁的前提下修改状态，存在竞态风险。
3. `sys_brk / sys_mmap / sys_munmap` 内的调试打印与 `proc_scheduler()` 的提示输出在测试阶段不再需要。

### 关键修复
- `src/kernel/proc/proc.c`
	- 新增 `wait_lk`，`proc_wait()`/`proc_exit()` 统一通过它保护父子同步；睡眠改为 `proc_sleep(parent, &wait_lk)`。
	- `proc_try_wakeup()` 改为直接获取父进程锁并检查 `sleep_space==parent`，避免遍历。
	- 调度日志由 `SCHED_DEBUG` 开关控制，默认关闭噪声输出。
- `src/kernel/syscall/sysfunc.c`
	- 去掉 `sys_brk / sys_mmap / sys_munmap` 的事件与页表打印，保持终端干净。

### 验证步骤
1. 将 `initcode.c` 切换到 README 中的 test-03 程序。
2. `make clean && make run`。
3. 观察串口输出，确认依次出现 `child proc`、`parent proc`、`good boy!`、`--------test end----------`，且无 panic。

### 测试结论
- 父子进程能够正确同步，父进程 wait 返回后打印 `parent proc: hello! 2good boy!`。
- `panic! acquire` 与多余调试日志消失，输出与期望截图一致。
- 判定：**test-03 PASS**。

![test-03 output](picture/测试三.png)

---

## Test-04：sleep 日志 & "kernel trap from user?"

### 初始症状
- 参考输出要求 30 条 `proc 2 is sleeping!`，随后 `proc 2 is wakeup!`、`Ready to exit!`、双行 `proc 1 is wakeup!` 与 “Child exit!”。
- 实际运行：
  - 早期完全没有睡眠提示；
  - 添加日志后，多核环境下经常 panic：`kernel trap from user? scause=0xa/0xc`，`sepc` 指向 `pop_off/spinlock`，随后 `Instruction page fault`。

### 根因分析
1. `timer_wait()` 没有输出调试信息，需要在睡眠/唤醒前后打印。
2. `proc_wait()` 只在 `proc_try_wakeup()` 中打印一次唤醒日志，导致 “Ready to exit!” 与 “Child exit!” 中间只有一行。
3. **最关键**：`trap_user_return()` 在恢复用户态前就把 `SSTATUS_SPP` 清零并设置 `stvec → user_vector`，但没有关闭中断。如果此时 M→S 时钟中断到来，`kernel_vector` 会看到 `SPP=0`，误以为 trap 来自用户，进而 panic。多核下概率显著。

### 关键修复
- `src/kernel/trap/timer.c`
	- 每次循环 `proc_sleep()` 前打印 `proc %d is sleeping!`，唤醒后打印 `proc %d is wakeup!`，完全还原 README。
- `src/kernel/proc/proc.c`
	- 在 `proc_wait()` 真正收尸之前再次输出 `proc %d is wakeup!`，实现两行父进程提示。
- `src/kernel/trap/trap_user.c`
	- `trap_user_handler()` 增加 `SPP==0` 断言，快速暴露异常路径；
	- `trap_user_return()` 开头调用 `intr_off()`，再依次写 `stvec/sscratch/sstatus/sepc`，最后由 `sret` 恢复中断，消除竞态窗口。

### 验证步骤
1. `initcode.c` 使用 README 的 test-04 程序。
2. 连续执行 `make clean && make run`、`make run` 多次。
3. 输出固定为：`Ready to sleep!` → 30×`proc 2 is sleeping!` → `proc 2 is wakeup!` → `Ready to exit!` → `proc 1 is wakeup!`（两行） → `Child exit!` → QEMU 正常退出。

### 测试结论
- 日志与 README 完全一致，睡眠/唤醒过程直观。
- 多核上再无 “kernel trap from user?” / instruction page fault。
- 判定：**test-04 PASS**。

![test-04 output](picture/测试四.png)

---

> 四个测试全部通过。 
