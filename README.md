# LAB-5: 系统调用流程建立 + 用户态虚拟内存管理

## 过程日志
1. 2025.11.01 更新lab5文件
2. 2025.11.03 张子扬完成测试一
3. 2025.11.03 王俊翔完成测试二、三
4. 2025.11.03 张子扬完善测试二、三，完成测试四
5. 2025.11.10 张子扬完成测试五
6. 2025.11.10 王俊翔、张子扬完成README编写
7. 2025.12.08 张子扬增加示意图


## 代码结构
```
OKOS
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── .gdbinit.tmp-riscv xv6自带的调试配置
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目 (CHANGE, 新增目录syscall)
├── kernel.ld      定义了内核程序在链接时的布局
├── pictures       README使用的图片目录 (CHANGE, 日常更新)
├── README.md      实验报告 (DONE)
├── lab-5-README.md实验指导书 (CHANGE, 日常更新)
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
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
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
    │   │   ├── kvm.c
    │   │   ├── uvm.c (DONE, 用户态虚拟内存管理主体)
    │   │   ├── mmap.c (DONE, mmap节点资源仓库)
    │   │   ├── method.h (CHANGE, 日常更新)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE, 日常更新)
    │   ├── trap   陷阱模块
    │   │   ├── plic.c
    │   │   ├── timer.c
    │   │   ├── trap_kernel.c
    │   │   ├── trap_user.c (DONE, 系统调用处理 + pagefault处理)
    │   │   ├── trap.S
    │   │   ├── trampoline.S
    │   │   ├── method.h
    │   │   ├── mod.h (CHANGE, 日常更新)
    │   │   └── type.h
    │   ├── proc   进程模块
    │   │   ├── proc.c (DONE, proczero->mmap初始化)
    │   │   ├── swtch.S
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE, 进程结构体里新增mmap字段)
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c (NEW, 系统调用通用逻辑)
    │   │   ├── sysfunc.c (DONE, 各个系统调用的处理逻辑) 
    │   │   ├── method.h (NEW)
    │   │   ├── mod.h (NEW)
    │   │   └── type.h (NEW)
    │   └── main.c
    └── user       用户程序
        ├── initcode.c (CHANGE, 按照测试需求来设置)
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h (CHANGE, 日常更新)
```


## 实现思路 

## 任务1：用户态和内核态的数据迁移

这个任务的核心问题是：用户传的地址是基于用户页表的，但内核用的是内核页表，咋办？

**解决方案很简单：查页表！**

1. **uvm_copyin/uvm_copyout/uvm_copyin_str** (在 `uvm.c` 中)
   - 拿到用户的虚拟地址，通过 `vm_getpte()` 查用户页表
   - 找到对应的物理地址（PTE_TO_PA）
   - 按页拷贝数据，注意处理跨页的情况
   - 字符串拷贝还得找 `\0` 结束符

2. **系统调用处理** (在 `trap_user.c` 中)
   - 用户执行 `ecall` 触发异常 (trap_id=8)
   - `trap_user_handler()` 里调用 `syscall()` 分发到具体的系统调用
   - 系统调用号在 `a7` 寄存器里

3. **具体的系统调用** (在 `sysfunc.c` 中)
   - `sys_copyin()`: 从用户拿数组，逐个打印
   - `sys_copyout()`: 给用户发 [1,2,3,4,5]
   - `sys_copyinstr()`: 从用户拿字符串并打印

**踩过的坑：**
- 用户程序的系统调用宏有递归定义问题，直接删掉那几行
- 入口点偏移 `INITCODE_ENTRY_OFFSET` 要改成 0

## 测试1
![alt](pictures/测试1.png)


## 任务2：堆的手动管理与栈的自动增长

这个任务要给用户进程实现动态内存管理能力，分为两部分：堆的手动管理和栈的自动扩展。

```mermaid
block-beta
    columns 1
    stack["User Stack (Grows Down)"]
    space1["⬇️ Auto Grow (Page Fault)"]
    free["Free Space"]
    space2["⬆️ Manual Grow (sys_brk)"]
    heap["User Heap"]
    data["Data / Code"]
```

### 堆的手动管理 — sys_brk 系统调用

堆（HEAP）从低地址向高地址增长，用户通过 `sys_brk` 手动管理堆空间。

sys_brk 的四种行为：
1. 查询堆顶：`sys_brk(0)` → 返回当前 `heap_top`
2. 扩展堆：`sys_brk(new_top)` 且 `new_top > heap_top` → 分配新页（实现见 [`uvm_heap_grow`](src/kernel/mem/uvm.c#L378)）
3. 收缩堆：`sys_brk(new_top)` 且 `new_top < heap_top` → 释放页面（实现见 [`uvm_heap_ungrow`](src/kernel/mem/uvm.c#L409)）
4. 不变：`sys_brk(heap_top)` → 什么都不做

实现要点（在 `uvm.c` 与 `sysfunc.c`）
- 堆扩展：检查边界（不能超过 `MMAP_BEGIN`）；按页向上对齐；`pmem_alloc()+memset` 清零；`vm_mappages` 映射为 `PTE_R|PTE_W|PTE_U`；必要时打印 `vm_print`（look/grow 事件）。
- 堆收缩：检查下限（不能低于 `USER_BASE+PGSIZE`）；按页向下对齐；`vm_unmappages(..., freeit=true)` 释放；必要时打印 equal/ungrow 事件。
- 系统调用处理：[`sys_brk`](src/kernel/syscall/sysfunc.c#L98) 读取参数，调用上述函数并更新 `proc->heap_top`，每次输出形如 “sys_brk: heap grow/shrink/unchanged to 0x...”。

### 栈的自动增长 — Page Fault 处理

栈（STACK）从高地址向低地址增长；当访问未分配的栈地址时触发 Page Fault，内核自动扩展栈。

实现要点（在 `trap_user.c` 与 `uvm.c`）
- 异常入口：[`trap_user_handler`](src/kernel/trap/trap_user.c#L21) 处理 `trap_id=13/15`，读取 `stval`，调用 [`uvm_ustack_grow`](src/kernel/mem/uvm.c#L432) 扩展，并回写 `proc->ustack_npage`。
- 栈扩展：计算当前栈区 `[TRAPFRAME - ustack_npage*PGSIZE, TRAPFRAME)`，确保 `fault_addr` 在其下方且不低于 `MMAP_END`；自下而上按页分配并 `vm_mappages` 为 `PTE_R|PTE_W|PTE_U`；返回新页数。

核心设计理念
- 堆是“显式管理”：用户主动 `sys_brk`
- 栈是“隐式管理”：Page Fault 自动扩展
- 对齐策略：扩展用“向上取整”，收缩/栈扩展注意页对齐

踩过的坑
- 用户代码页写权限：需要 `PTE_W` 才能在用户代码页写入。参见代码处的映射设置：[`proc_make_first`](src/kernel/proc/proc.c#L82) 使用 `PTE_R|PTE_W|PTE_X|PTE_U`。

## 测试2
![alt](pictures/测试2-1.png)
![alt](pictures/测试2-2.png)


## 任务3：mmap_region 资源仓库管理

这个任务的核心问题是：**多个进程如何安全高效地共享 `mmap_region_t` 结构体资源？**

解决方案：对象池 + 自旋锁

1. **数据结构设计** (在 [`type.h`](src/kernel/mem/type.h) 中)
   - `mmap_region_t`: 描述已分配的连续地址空间
   - `mmap_region_node_t`: 资源仓库中的包装节点
   - 全局仓库：`node_list[N_MMAP]` + `list_head` + `list_lk`

2. **仓库初始化**：[`mmap_init`](src/kernel/mem/mmap.c#L9)
   - 初始化自旋锁 `list_lk`
   - 将256个节点串成空闲链表
   - `list_head.next` 指向第一个可用节点

3. **节点申请**：[`mmap_region_alloc`](src/kernel/mem/mmap.c#L29)
   - 加锁 → 从链表头取节点 → 解锁
   - 清空字段并返回指针
   - 仓库为空则 `panic`

4. **节点归还**：[`mmap_region_free`](src/kernel/mem/mmap.c#L51)
   - 清空 mmap 字段
   - 加锁 → 头插法插入链表 → 解锁
   - O(1) 复杂度

**踩过的坑：**
- 指针转换：`mmap_region_free()` 接收 `mmap_region_t*`，需转换为 `mmap_region_node_t*`
- 锁的粒度：必须在访问 `list_head.next` 前后正确加锁/解锁

## 测试3
测试逻辑：双核并发申请和释放256个节点

**测试代码** (在 `main.c` 中):
- CPU0 申请节点 0 ~ 127，CPU1 申请节点 128 ~ 255
- 同步屏障后再并发释放
- 检查初始和最终状态的节点链表完整性

**预期输出**:
- 初始状态: `node 0 index = 0`, `node 1 index = 1`, ..., `node 255 index = 255`
- 最终状态: 两股输出交替，node从0到255，一股index从255减到128，另一股index从127减到0

![alt](pictures/测试3-1.png)
![alt](pictures/测试3-2.png)


## 任务4：mmap 与 munmap 系统调用

这个任务解决的是如何让用户程序动态申请和释放离散的内存块

```mermaid
graph LR
    subgraph Before["合并前"]
        N1[Node A: 0x1000-0x2000] --> N2[Node B: 0x2000-0x3000]
        N2 --> N3[Node C: 0x5000-0x6000]
    end
    subgraph After["合并后 (A+B)"]
        M1[Node AB: 0x1000-0x3000] --> M2[Node C: 0x5000-0x6000]
    end
```

**解决方案：有序链表 + 自动合并**

1. **sys_mmap(begin, len)** (在 [`sysfunc.c`](src/kernel/syscall/sysfunc.c#L155) 中)
   - `begin=0`: 内核自动寻找第一个足够大的空闲区域
   - `begin!=0`: 在指定地址申请（需要页对齐）
   - `len`: 申请的字节数（必须是 PGSIZE 的倍数）

2. **sys_munmap(begin, len)** (在 [`sysfunc.c`](src/kernel/syscall/sysfunc.c#L208) 中)
   - 支持部分释放、完全释放、中间打洞等复杂情况
   - 自动处理链表节点的分裂和删除

3. **核心实现** (在 [`uvm.c`](src/kernel/mem/uvm.c) 中)
   - [`uvm_mmap`](src/kernel/mem/uvm.c#L212): 分配 mmap 区域
     - 查找位置 → 创建节点 → 插入链表 → 合并相邻 → 分配物理页
   - [`uvm_munmap`](src/kernel/mem/uvm.c#L292): 释放 mmap 区域
     - 处理4种情况：完全包含、覆盖前半、覆盖后半、中间打洞

**关键点：**
- **链表有序性**: mmap 链表始终按地址从低到高排序
- **节点合并**: 相邻的节点自动合并，减少资源仓库的消耗
- **资源复用**: 释放的 `mmap_region_t` 归还到资源仓库而非物理内存

**踩过的坑：**
- 合并后忘记更新链表指针：导致链表结构破坏
- munmap 的边界情况：必须仔细处理4种覆盖情况
- 物理页映射：mmap 时必须分配物理页并建立映射

## 测试4
测试用例涵盖了 mmap/munmap 的各种复杂情况：
- 指定地址的 mmap
- 自动寻址的 mmap (`begin=0`)
- 相邻节点自动合并
- 部分释放、完全释放、中间打洞

每次操作后都会打印：
- 分配/释放的地址范围
- 当前 mmap 链表状态（所有已分配区域）
- 页表详细状态（三级页表结构）

![alt](pictures/测试4.png)


## 任务5：页表复制与销毁

这个任务的核心问题是：**如何为 fork 系统调用准备页表深拷贝和完整销毁功能？**

```mermaid
graph TD
    subgraph Parent["父进程"]
        PPT[页表] --> PP1[物理页 1]
    end
    subgraph Child["子进程 (Deep Copy)"]
        CPT[新页表] --> CP1[物理页 1' (副本)]
    end
    PP1 -.->|memcpy| CP1
```

**解决方案：逐页复制 + 递归释放！**

1. **页表复制**：[`uvm_copy_pgtbl`](src/kernel/mem/uvm.c#L554)
   - 复制用户代码段、堆、mmap 区域、用户栈
   - 为每个页面分配新的物理内存并复制数据
   - 不复制 trapframe 和 trampoline（内核管理）

2. **页表销毁**：[`uvm_destroy_pgtbl`](src/kernel/mem/uvm.c#L519)
   - 递归释放三级页表结构
   - 释放所有用户物理页面
   - 释放页表本身占用的物理页

3. **核心实现** (在 [`uvm.c`](src/kernel/mem/uvm.c) 中)
   - `copy_range`: 复制连续地址空间
   - `destroy_pgtbl`: 递归释放页表结构

**关键点：**
- **逐页复制**: 创建地址空间的完整副本，物理内存完全独立
- **递归释放**: 从顶级页表开始，自顶向下释放所有层级
- **PTE_U 检查**: 只释放用户页面，保留内核共享资源（trampoline）

**踩过的坑：**
- 堆顶页对齐问题：需要手动页对齐 `(heap_top + PGSIZE - 1) & ~(PGSIZE - 1)`
- Trapframe 释放问题：测试中新页表共享原进程的 trapframe，不能释放物理页
- 头文件包含问题：`sys_test_pgtbl` 中调用 `proc_pgtbl_init()` 未声明

## 测试5

测试用例验证了页表复制和销毁的正确性：

**测试步骤**：
1. **准备数据**：在堆、mmap、栈中写入测试数据
   - 堆：`0x12345678`, `0xABCDEF00`, `0x99887766`
   - Mmap 区域1：`0xDEADBEEF`, `0xCAFEBABE`
   - Mmap 区域2：`0x11223344`, `0x55667788`
   - 栈：字符 `'H'`, `'W'`, `'!'`

2. **触发栈扩展**：分配 2 页的栈数组触发 page fault

3. **页表复制**：
   - 创建新页表
   - 复制所有用户内存区域
   - 打印新页表内容验证

4. **页表销毁**：
   - 递归释放所有页表和物理页
   - 验证没有内存泄漏或 panic

**验证要点**：
- 复制后的页表应该有不同的物理地址
- 虚拟地址布局应该完全一致
- Trapframe 和 trampoline 的映射正确
- 销毁过程没有释放内核共享资源

**测试结果**：
![alt](pictures/测试5.png)

## 实验反思
1. 在本次实验中，通过实现 uvm 的 copyin/copyout/字符串拷贝、syscall 分发、堆的 brk 管理、栈的按需扩展和 mmap 节点池，深入理解了用户态与内核态地址转换、页表映射与缺页处理的实际机制；对多级页表、PTE 权限和物理页分配有了直观认识。
2. 同时，实现页表深拷贝与销毁的完整测试（为 fork 做准备）、在多核并发场景下验证 mmap 仓库的正确性、加入更完善的内存使用统计与泄漏检测工具。