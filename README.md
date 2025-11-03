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


## 任务3：mmap_region 资源仓库管理

这个任务要解决的问题是：**如何让多个进程安全高效地共享 `mmap_region_t` 结构体资源？**

### 为什么需要离散内存管理？

应用程序除了堆和栈，还需要临时申请一些离散的内存块：
- **栈**：无法手动释放，函数返回后自动回收
- **堆**：适合管理大片连续空间，频繁申请释放会产生碎片化

因此需要在堆和栈之间划分一个 **mmap区域** (`MMAP_BEGIN` ~ `MMAP_END`)，用链表管理离散的内存块。

### 数据结构设计

**1. mmap_region_t**: 描述一块已分配的连续地址空间
```c
typedef struct mmap_region {
    uint64 begin;             // 起始地址
    uint32 npages;            // 页面数量
    struct mmap_region *next; // 链表指针
} mmap_region_t;
```

**2. mmap_region_node_t**: 资源仓库中的包装节点
```c
typedef struct mmap_region_node {
    mmap_region_t mmap;       // 内嵌 mmap_region_t
    struct mmap_region_node *next; // 仓库链表指针
} mmap_region_node_t;
```

**3. 全局资源仓库** (在 `mmap.c` 中):
- `node_list[N_MMAP]`: 256个节点的静态数组
- `list_head`: 空闲链表头节点（不可分配）
- `list_lk`: 自旋锁，保证多核并发安全

### 实现要点

**1. mmap_init()**: 初始化资源仓库
```c
void mmap_init() {
    spinlock_init(&list_lk, "mmap_node_list");
    spinlock_acquire(&list_lk);
    
    // 将 node_list 串成空闲链
    for (int i = 0; i < N_MMAP - 1; i++) {
        node_list[i].next = &node_list[i + 1];
    }
    node_list[N_MMAP - 1].next = NULL;
    list_head.next = &node_list[0];
    
    spinlock_release(&list_lk);
}
```
- 将256个节点串成空闲链表
- 初始化自旋锁
- `list_head.next` 指向第一个可用节点

**2. mmap_region_alloc()**: 从仓库申请节点
```c
mmap_region_t *mmap_region_alloc() {
    spinlock_acquire(&list_lk);
    
    mmap_region_node_t *node = list_head.next;
    if (node == NULL) {
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: no free node");
    }
    list_head.next = node->next;
    
    spinlock_release(&list_lk);
    
    // 清空并返回
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;
    return &node->mmap;
}
```
- 加锁 → 从链表头取节点 → 解锁
- 清空字段并返回指针
- 仓库为空则 `panic`

**3. mmap_region_free()**: 归还节点到仓库
```c
void mmap_region_free(mmap_region_t *mmap) {
    if (mmap == NULL) return;
    
    mmap_region_node_t *node = (mmap_region_node_t *)mmap;
    
    // 清理字段（可选）
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;
    
    spinlock_acquire(&list_lk);
    node->next = list_head.next;
    list_head.next = node;
    spinlock_release(&list_lk);
}
```
- 清空 mmap 字段
- 加锁 → 头插法插入链表 → 解锁
- O(1) 复杂度

### 核心设计理念

- **对象池模式**: 预分配固定数量的节点，避免频繁的物理页分配
- **空闲链表**: 已释放的节点不归还物理内存，加入空闲链表等待复用
- **自旋锁保护**: 多核环境下保证资源申请/释放的原子性
- **结构体嵌套技巧**: `mmap_region_t` 是 `mmap_region_node_t` 的首成员，地址相同，可直接指针转换

### 踩过的坑

- **指针转换**: `mmap_region_free()` 接收 `mmap_region_t*`，需转换为 `mmap_region_node_t*`
  - 因为 `mmap` 是首成员，地址相同，可以直接强制转换：`(mmap_region_node_t *)mmap`
- **锁的粒度**: 必须在访问 `list_head.next` 前后正确加锁/解锁
- **边界检查**: `mmap_region_alloc()` 必须检查仓库是否为空

## 测试3
测试逻辑：双核并发申请和释放256个节点

**测试代码** (在 `main.c` 中):
- CPU0 申请节点 0~127，CPU1 申请节点 128~255
- 同步屏障后再并发释放
- 检查初始和最终状态的节点链表完整性

**预期输出**:
- 初始状态: `node 0 index = 0`, `node 1 index = 1`, ..., `node 255 index = 255`
- 最终状态: 两股输出交替，node从0到255，一股index从255减到128，另一股index从127减到0

![alt](pictures/测试3-1.png)
![alt](pictures/测试3-2.png)


## 任务4：mmap 与 munmap 系统调用

这个任务实现用户态的离散内存管理，允许用户动态申请和释放 mmap 区域的内存。

### 核心功能

**sys_mmap(begin, len)**: 申请一块连续的内存空间
- `begin=0`: 内核自动寻找第一个足够大的空闲区域
- `begin!=0`: 在指定地址申请（需要页对齐）
- `len`: 申请的字节数（必须是 PGSIZE 的倍数）

**sys_munmap(begin, len)**: 释放指定地址范围的内存
- 支持部分释放、完全释放、中间打洞等复杂情况
- 自动处理链表节点的分裂和删除

### 实现要点

**1. uvm_mmap_find()**: 自动寻找空闲空间
- 从 `MMAP_BEGIN` 开始扫描已分配区域之间的空隙
- 返回第一个足够大的空闲区域起始地址
- 同时返回插入位置的前后节点指针

**2. uvm_mmap()**: 分配 mmap 区域

核心步骤：
- **查找位置**: `begin=0` 时调用 `uvm_mmap_find()`，否则在链表中找到插入位置
- **创建节点**: 从资源仓库分配 `mmap_region_t`，设置 `begin`、`npages`
- **插入链表**: 保持链表按地址有序
- **合并相邻节点**: 
  - 与前一个节点相邻 → 扩展前节点，释放新节点，**更新链表**
  - 与后一个节点相邻 → 扩展新节点，释放后节点，**更新链表**
- **分配物理页**: 调用 `pmem_alloc()` 并用 `vm_mappages()` 建立映射

**关键点 - 合并时必须更新链表指针：**
```c
// 与前面合并
last_mmap->npages += new_mmap->npages;
last_mmap->next = new_mmap->next;  // ← 重要！
mmap_region_free(new_mmap);

// 与后面合并  
new_mmap->npages += next_mmap->npages;
new_mmap->next = next_mmap->next;  // ← 重要！
mmap_region_free(next_mmap);
```

**3. uvm_munmap()**: 释放 mmap 区域

需要处理4种情况：

- **情况1 - 完全包含**: 解除整个节点的映射，从链表删除并释放节点
- **情况2 - 覆盖前半**: 解除前半映射，修改节点 `begin` 和 `npages`
- **情况3 - 覆盖后半**: 解除后半映射，修改节点 `npages`
- **情况4 - 中间打洞**: 解除中间映射，分裂成两个节点

**4. 系统调用实现**

**sys_mmap()**:
- 参数检查：`len` 和 `begin` 必须页对齐
- 调用 `uvm_mmap(begin, npages, PTE_R|PTE_W|PTE_U)`
- 打印调试信息和页表状态

**sys_munmap()**:
- 参数检查：`len` 和 `begin` 必须页对齐
- 调用 `uvm_munmap(begin, npages)`
- 打印调试信息和页表状态

### 核心设计理念

- **链表有序性**: mmap 链表始终按地址从低到高排序
- **节点合并**: 相邻的节点自动合并，减少资源仓库的消耗
- **资源复用**: 释放的 `mmap_region_t` 归还到资源仓库而非物理内存
- **灵活释放**: 支持任意范围的 munmap，自动处理节点分裂

### 踩过的坑

- **合并后忘记更新链表指针**: 合并节点后如果不更新 `next` 指针，会导致链表结构破坏
  - 症状：所有 mmap 区域显示相同的地址
  - 解决：每次 `mmap_region_free()` 后必须更新前一个节点的 `next`
  
- **munmap 的边界情况**: 必须仔细处理 4 种覆盖情况，特别是中间打洞
  - 需要分配新节点表示后半部分
  - 需要正确连接链表

- **物理页映射**: mmap 时必须分配物理页并建立映射，munmap 时必须解除映射并释放物理页

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

![alt](pictures/测试4-1.png)


## 实验感想
