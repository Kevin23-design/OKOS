# LAB-5: 系统调用流程建立 + 用户态虚拟内存管理

## 过程日志
1. 2025.11.1 2025.10.13 更新lab4文件



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


## 实验整体框架分析

### 实验目标
从**单进程(proczero)**走向**多进程系统**，解决两大核心问题：
1. 进程调度：多个进程如何竞争 CPU 资源
2. 进程生命周期：进程的创建、运行、睡眠、退出和回收

## 实验逻辑结构（分层实现）

### **第一层：基础设施准备**

#### 1. 进程数组与资源管理
目标：从单个 proczero 扩展到多进程数组

**新增数据结构：**
- `proc_list[N_PROC]` - 进程资源池（类似 lab-5 的 mmap_region 资源池）
- `global_pid` - 全局进程 ID 分配器
- 进程结构体新增字段：
  - `char name[16]` - 进程名称（调试用）
  - `spinlock_t lk` - 保护共享字段的锁
  - `enum proc_state state` - 5 种进程状态
  - `struct proc *parent` - 父进程指针
  - `int exit_code` - 退出状态码
  - `void *sleep_space` - 睡眠等待的资源

**需要实现的函数：**
1. `proc_init()` - 初始化进程数组和 global_pid
2. `proc_alloc()` - 从资源池申请空闲进程（设置 ctx.ra = proc_return）
3. `proc_free()` - 释放进程及其资源
4. `proc_make_first()` - 改写，使用 proc_alloc 创建 proczero

**内核栈扩展：**
- 修改 `kvm_init()`：从单个内核栈映射扩展到 N_PROC 个

---

### **第二层：进程调度机制**

#### 2. 基于循环扫描的调度器

```
核心思想：原生进程（CPU-0/CPU-1）变成调度器，用户进程变成被调度对象
```

**执行流转换：**
```
启动阶段：
entry.S → start.c → main.c 
  ↓
CPU-0: 系统初始化 + proczero 创建
CPU-1: 核心初始化
  ↓
两个 CPU 都进入 proc_scheduler() 死循环（永不返回）
```

**调度逻辑（双层 swtch）：**
```
原生进程（调度器）
  ↓ swtch(原生ctx, 用户进程A ctx)
用户进程 A 运行
  ↓ proc_sched() 主动/被动释放 CPU
  ↓ swtch(用户进程A ctx, 原生ctx)
原生进程（调度器）
  ↓ 扫描找到用户进程 B
  ↓ swtch(原生ctx, 用户进程B ctx)
用户进程 B 运行
  ...
```

**关键设计点：**
- 原生进程执行时：`CPU->proc = NULL`
- 用户进程执行时：`CPU->proc = 用户进程指针`
- 调度器只调度 `state == RUNNABLE` 的进程

**需要实现的函数：**
1. `proc_scheduler()` - 调度器主循环（扫描 + swtch）
2. `proc_sched()` - 用户进程释放 CPU（swtch 回调度器）

---

#### 3. 基于时钟的抢占式调度

```
问题：没有强制手段打断长进程 → 短进程饿死
解决：时钟中断 + 强制让出 CPU
```

**实现机制：**
- 每个时钟中断后，强制当前进程调用 `proc_yield()`
- `proc_yield()` 将进程状态从 `RUNNING` → `RUNNABLE`，然后调用 `proc_sched()`

**需要修改的位置：**
1. `trap_user.c` - 用户态时钟中断处理后调用 `proc_yield()`
2. `trap_kernel.c` - 内核态时钟中断处理后调用 `proc_yield()`
3. 实现 `proc_yield()` 函数

**效果：**
每个进程持有 1 个时间片（1 个时钟周期），用完就交出 CPU

---

### **第三层：进程生命周期管理**

#### 4. 五种进程状态

```
unused → runnable → running → zombie → unused
         ↑           ↓
         ← sleeping ←
```

**状态转换逻辑：**

| 状态 | 含义 | 可调度？ |
|------|------|---------|
| **unused** | 未初始化/已回收 | ❌ |
| **zombie** | 濒死，等待父进程回收 | ❌ |
| **sleeping** | 睡眠，等待资源 | ❌ |
| **runnable** | 就绪，等待调度 | ✅ |
| **running** | 正在运行 | N/A |

---

#### 5. fork-exit-wait 三剑客（蓝色路径）

**（1）proc_fork() - 进程复制**

```c
工作流程：
1. proc_alloc() 申请空闲进程
2. 复制父进程的页表（uvm_copy_pgtbl）
3. 复制父进程的 trapframe
4. 复制父进程的 mmap 链表
5. 记录父子关系：child->parent = parent
6. 设置子进程返回值为 0（区分父子）
7. 设置子进程状态为 RUNNABLE
```

**关键点：**
- 所有用户进程构成树形结构，proczero 是根节点
- fork 只搭骨架，lab-9 的 exec 会填充血肉

**（2）proc_exit() - 进程退出**

```c
问题：进程不能自己杀死自己（谁来执行回收逻辑？）
解决：进入 ZOMBIE 状态，等待父进程回收
```

**工作流程：**
```
1. 设置 exit_code
2. 将自己的子进程过继给 proczero
3. 设置状态为 ZOMBIE
4. 唤醒父进程（proc_try_wakeup）
5. 调用 proc_sched()（永不返回）
```

**（3）proc_wait() - 父进程回收子进程**

```c
工作流程：
1. 循环扫描进程数组
2. 找到 state == ZOMBIE 且 parent == 自己的子进程
3. 调用 proc_free() 回收资源
4. 返回子进程的 exit_code
```

**典型使用模式：**
```c
int pid = fork();
if (pid == 0) {
    do_something();
    exit(0);  // 子进程退出
} else {
    int state;
    wait(&state);  // 父进程等待
    do_something_else();
}
```

---

#### 6. sleep-wakeup 机制（黑色路径）

```
问题：proc_wait() 中不断 yield 会导致父进程无效调度
解决：引入 SLEEPING 状态，只有资源就绪才唤醒
```

**核心思想：**
- `proc_sleep(资源, 锁)` - 等待资源，进入 SLEEPING
- `proc_wakeup(资源)` - 唤醒所有等待该资源的进程
- `proc_try_wakeup(进程)` - 只唤醒指定进程（优化版）

**改进的 proc_wait()：**
```c
while (1) {
    扫描子进程;
    if (找到 ZOMBIE 子进程) {
        回收并返回;
    }
    proc_sleep(自己, &自己的锁);  // 进入睡眠，等待子进程唤醒
}
```

**改进的 proc_exit()：**
```c
设置状态为 ZOMBIE;
proc_try_wakeup(parent);  // 唤醒父进程
proc_sched();
```

**为什么 proc_sleep 要传入锁？**
- 防止"lost wakeup"问题：
  ```
  父进程检查子进程（未找到 ZOMBIE）
    ↓ （此时子进程调用 exit 并 wakeup，但父进程还没 sleep）
  父进程调用 sleep（永远睡眠，因为 wakeup 已经发生）
  ```
- 解决方案：先持有锁，检查条件 → 进入睡眠 → 释放锁（原子操作）

---

### **第四层：睡眠锁**

```
基于 spinlock + sleep/wakeup 实现 sleeplock
```

**对比：**
| 锁类型 | 获取失败行为 | 适用场景 |
|--------|------------|---------|
| **spinlock** | 忙等（循环重试） | 短期持有的资源 |
| **sleeplock** | 睡眠（SLEEPING） | 长期持有的资源（如文件） |

**需要实现的函数：**
1. `sleeplock_init()`
2. `sleeplock_acquire()` - 获取失败则 sleep
3. `sleeplock_release()` - 释放后 wakeup
4. `sleeplock_holding()`

---

### **第五层：系统调用封装**

**新增系统调用：**
```c
SYS_print_str   // 打印字符串（方便测试）
SYS_print_int   // 打印整数
SYS_getpid      // 获取当前进程 PID
SYS_fork        // 进程复制
SYS_wait        // 等待子进程退出
SYS_exit        // 进程退出
SYS_sleep       // 睡眠 N 个时钟周期
```

**重点：sys_sleep 的实现**
```c
工作逻辑：
1. 记录目标时间 = 当前时间 + ntick
2. while (当前时间 < 目标时间) {
       proc_sleep(&sys_timer, &timer_lock);
   }
```

**配合实现：**
- `timer_update()` 中调用 `proc_wakeup(&sys_timer)`
- 实现 `timer_wait()` 函数供 sys_sleep 调用

---

## 🧪 测试用例分析

### 测试 1：基础进程运行
```c
验证：proczero 创建成功，能获取 PID=1
```

### 测试 2：进程树创建
```c
level-1 fork → level-2 fork → level-3
验证：fork 能正确复制进程，形成进程树
```

### 测试 3：父子进程通信
```c
验证：
1. 子进程能复制父进程的堆/栈/mmap 数据
2. exit-wait 机制能正确传递退出状态
```

### 测试 4：睡眠唤醒
```c
验证：
1. 子进程能正确睡眠 30 个时钟周期
2. 父进程能正确等待子进程退出
```

---

## 🎯 实现顺序建议

```
1. 准备工作：进程数组 + proc_init/alloc/free
   ↓
2. 进程调度：proc_scheduler + proc_sched
   ↓
3. 抢占调度：proc_yield + 时钟中断集成
   ↓
4. 生命周期：proc_fork + proc_exit + proc_wait
   ↓
5. 睡眠机制：proc_sleep + proc_wakeup
   ↓
6. 睡眠锁：sleeplock 实现
   ↓
7. 系统调用：封装所有功能
   ↓
8. 测试验证：逐个测试用例验证
```

---

## 💡 关键设计思想

1. **资源池模式**：进程数组类似 mmap_region 资源池
2. **双层 swtch**：原生进程作为调度缓冲层
3. **状态机设计**：5 种状态清晰描述进程生命周期
4. **父子关系树**：进程树结构，proczero 是根
5. **睡眠唤醒**：避免无效调度，提高效率
6. **锁的分层**：spinlock（短期）+ sleeplock（长期）

这个实验是从单进程到多进程系统的关键跨越，为后续的文件系统（lab-7~9）打下坚实基础！🚀

## 实验感想
