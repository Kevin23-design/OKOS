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

这个任务要给用户进程实现动态内存管理能力,分为两部分:

### 堆的手动管理 - sys_brk系统调用

**堆 (HEAP)** 从低地址向高地址增长,用户通过 `sys_brk` 系统调用来手动管理堆空间。

**sys_brk 的四种功能:**

1. **查询堆顶**: `sys_brk(0)` → 返回当前 `heap_top`
2. **扩展堆**: `sys_brk(new_top)` 且 `new_top > heap_top` → 分配新页面
3. **收缩堆**: `sys_brk(new_top)` 且 `new_top < heap_top` → 释放页面
4. **不变**: `sys_brk(heap_top)` → 什么都不做

**实现要点 (在 `uvm.c` 和 `sysfunc.c` 中):**

1. **uvm_heap_grow()**: 堆扩展
   - 检查边界: 新堆顶不能超过 `MMAP_BEGIN`
   - 页对齐: 用 `PGROUNDUP` 计算需要多少页
   - 分配物理页: `pmem_alloc()` + `memset()` 清零
   - 映射页表: `vm_mappages()` 设置 `PTE_R|PTE_W|PTE_U` 权限
   - 调试输出: 调用 `vm_print()` 显示"look event"和"grow event"

2. **uvm_heap_ungrow()**: 堆收缩
   - 检查下限: 不能收缩到代码段以下 (`USER_BASE + PGSIZE`)
   - 页对齐: 用 `PGROUNDDOWN` 计算要释放多少页
   - 释放页面: `vm_unmappages()` 解除映射并释放物理内存
   - 调试输出: 显示"equal event"(无变化)或"ungrow event"(释放了页)

3. **sys_brk()**: 系统调用处理
   - 参数为0: 返回当前 `heap_top`
   - 扩展/收缩: 调用 `uvm_heap_grow/ungrow` 并更新 `proc->heap_top`
   - 打印日志: 每次操作都输出 "sys_brk: heap grow/shrink/unchanged to 0x..."

### 栈的自动增长 - Page Fault处理

**栈 (STACK)** 从高地址向低地址增长,当访问未分配的栈空间时触发 **Page Fault**,内核自动扩展栈。

**实现要点 (在 `trap_user.c` 和 `uvm.c` 中):**

1. **trap_user_handler()**: 捕获异常
   - 监听 `trap_id = 13` (Load Page Fault) 和 `trap_id = 15` (Store/AMO Page Fault)
   - 读取故障地址: `fault_addr = r_stval()`
   - 调用 `uvm_ustack_grow()` 自动扩展栈
   - 更新进程栈页数: `proc->ustack_npage`

2. **uvm_ustack_grow()**: 栈扩展
   - 计算当前栈范围: `[TRAPFRAME - PGSIZE - ustack_npage*PGSIZE, TRAPFRAME - PGSIZE]`
   - 边界检查: 
     - `fault_addr` 必须在当前栈底下方 (否则不需要扩展)
     - `fault_addr` 不能低于 `MMAP_END` (否则栈溢出)
   - 计算需要扩展的页数: 从 `fault_addr` 向下对齐到页面边界
   - 分配并映射新页面: `pmem_alloc()` + `vm_mappages()` 设置 `PTE_R|PTE_W|PTE_U`
   - 返回新的栈页数

**核心设计理念:**
- 堆是"显式管理": 用户主动调用 `sys_brk`
- 栈是"隐式管理": 触发 Page Fault 自动扩展
- 都使用页对齐宏: `PGROUNDUP` (向上对齐) 和 `PGROUNDDOWN` (向下对齐)

**踩过的坑:**
- 用户代码页的权限问题: 必须加上 `PTE_W` 写权限才能正常工作
  - 修改 `proc.c` 中的映射: `PTE_R|PTE_W|PTE_X|PTE_U`
- 调试输出格式: 需要在关键时刻调用 `vm_print()` 显示页表状态
- 页面对齐计算: 堆扩展用 `PGROUNDUP`,堆收缩和栈扩展都要注意对齐

## 测试2
![alt](pictures/测试2-1.png)
![alt](pictures/测试2-2.png)



## 实验感想
