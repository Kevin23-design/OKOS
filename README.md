# LAB-7: 文件系统 之 磁盘管理

## 过程日志
1. 2025.11.27 更新lab7文件
2. 2025.11.28 王俊翔完成三个test
3. 2025.12.1  张子扬优化readme内容，设计新的测试样例


## 代码结构
```
OKOS
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── .gdbinit.tmp-riscv xv6自带的调试配置
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目 (CHANGE)
├── kernel.ld      定义了内核程序在链接时的布局
├── pictures       README使用的图片目录 (CHANGE, 日常更新)
├── README.md      实验指导书 (CHANGE, 日常更新)
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
    │   │   ├── sleeplock.c
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
    │   │   ├── kvm.c (DONE, 内核页表增加磁盘相关映射 + vm_getpte处理pgtbl为NULL的情况)
    │   │   ├── uvm.c
    │   │   ├── mmap.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── trap   陷阱模块
    │   │   ├── plic.c (DONE, 增加磁盘中断相关支持)
    │   │   ├── timer.c
    │   │   ├── trap_kernel.c (DONE, 在外设处理函数中识别和响应磁盘中断)
    │   │   ├── trap_user.c
    │   │   ├── trap.S
    │   │   ├── trampoline.S
    │   │   ├── method.h
    │   │   ├── mod.h (CHANGE, include 文件系统模块)
    │   │   └── type.h
    │   ├── proc   进程模块
    │   │   ├── proc.c (在proc_return中调用文件系统初始化函数)
    │   │   ├── swtch.S
    │   │   ├── method.h
    │   │   ├── mod.h (CHANGE, include 文件系统模块)
    │   │   └── type.h
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c (DONE, 新增系统调用)
    │   │   ├── sysfunc.c (DONE, 新增系统调用)
    │   │   ├── method.h (CHANGE, 新增系统调用)
    │   │   ├── mod.h (CHANGE, include文件系统模块)
    │   │   └── type.h (CHANGE, 新增系统调用)
    │   ├── fs     文件系统模块
    │   │   ├── bitmap.c (DONE, bitmap相关操作)
    │   │   ├── buffer.c (DONE, 内存中的block缓冲区管理)
    │   │   ├── fs.c (DONE, 文件系统相关)
    │   │   ├── virtio.c (NEW, 虚拟磁盘的驱动)
    │   │   ├── method.h (NEW)
    │   │   ├── mod.h (NEW)
    │   │   └── type.h (NEW)
    │   └── main.c (CHANGE, 增加virtio_init)
    ├── mkfs       磁盘映像初始化
    │   ├── mkfs.c (NEW)
    │   └── mkfs.h (NEW)
    └── user       用户程序
        ├── initcode.c (CHANGE, 日常更新)
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h (CHANGE, 日常更新)
```


## 核心思考与架构设计

本实验的目标是构建一个从用户态到磁盘驱动的完整 I/O 链路。不同于之前的内存管理或进程调度，文件系统涉及持久化存储，需要解决“内存-磁盘”速度不匹配和“用户-内核”空间隔离两大挑战。

### 1. 系统分层架构
为了降低复杂度，我们将系统划分为清晰的四层。每一层只负责特定的职责，通过标准接口交互。

```text
+-------------------+
|   User Space      |  用户程序 (initcode)
| (open, read, ...) |  调用系统调用
+---------+---------+
          | syscall (sys_read, sys_write, ...)
          v
+---------+---------+
|   File System     |  文件系统逻辑层
| (inode, bitmap)   |  管理文件元数据、空闲块分配
+---------+---------+
          | bread / bwrite
          v
+---------+---------+
|   Buffer Cache    |  缓冲层 (buf.c)
| (LRU, sleeplock)  |  缓存热点块，合并磁盘 I/O，提供同步机制
+---------+---------+
          | virtio_disk_rw
          v
+---------+---------+
|   Device Driver   |  驱动层 (virtio.c)
| (virtio-blk)      |  操作 MMIO 寄存器，通过 DMA 与磁盘交互
+-------------------+
```

### 2. 缓冲层设计的核心逻辑 (Buffer Cache)
Buffer Cache 是文件系统的“心脏”。它不仅是缓存，更是同步点。我们采用了 **LRU (Least Recently Used)** 策略来管理有限的内存块。

**双向链表状态机设计：**
我们维护了两个链表：`active_list` (活跃链表) 和 `inactive_list` (空闲/非活跃链表)。

```text
      [ Active List ]                 [ Inactive List ]
      (ref_cnt > 0)                   (ref_cnt == 0)
      正在被使用的块                   可被回收/复用的块
    
     +---+    +---+                  +---+    +---+
     | B1 |<->| B2 |                 | B3 |<->| B4 |
     +---+    +---+                  +---+    +---+
       ^        ^                      ^        ^
       |        |                      |        |
       |        +-------(bput)---------+        |
       |      引用计数归零，移入 inactive 头部      |
       |                                        |
       +--------(bget / cache hit)--------------+
      再次被引用，从 inactive 移回 active
```

*   **分配策略 (bget)**:
    1.  **Cache Hit**: 如果请求的 block 已经在 active 或 inactive 链表中，直接增加引用计数，移入 active 链表。
    2.  **Cache Miss**: 扫描 inactive 链表（通常从尾部开始，即最久未使用的块），复用该节点，重置元数据。
*   **同步机制**: 每个 Buffer 拥有一把 `sleeplock`。当一个进程正在读写某个块时，其他进程对该块的访问必须等待，保证了数据的一致性。

## 实验过程详解

### 1. 磁盘链路打通：页表与中断协同
- **内核页表补丁**：在 `kvm.c` 里让 `vm_getpte(NULL, ...)` 能回落到内核页表，再把 `VIRTIO_BASE` 相关寄存器映射进 `kernel_pgtbl`，保证驱动访问寄存器时不会触发缺页。
- **驱动初始化顺序**：`main.c` 在 `mmap_init` 之后调用 `virtio_disk_init`，同时 `proc_return` 进入用户态前会触发一次 `fs_init`，确保 proczero 一醒来就能看到磁盘。
- **PLIC 配置**：补齐 `plic.c` 的磁盘中断优先级和 `trap_kernel.c` 的外设分支，让 `virtio_disk_intr` 真正被调度器唤醒。

### 2. 缓冲系统：LRU 双链的自洽状态机
- **资源组织**：`buf.c` 把 8 个 buffer node 分成 active/inactive 两条循环链；ref>0 的在 active，ref=0 的在 inactive，符合 README 要求的 LRU 策略。
- **锁语义**：全局 `lk_buf_cache` 保护 block/ref 元数据，单个 buffer 的 `sleeplock` 保护数据区和磁盘标识，读写路径始终先持有睡眠锁再调 `virtio_disk_rw`。
- **生命周期**：`buffer_get` 负责 miss 时自动分配物理页并从磁盘读入，`buffer_put` 在引用计数清零后把节点挪回 inactive，`buffer_freemem` 提供按需回收通道。

### 3. bitmap 与 superblock：从磁盘找到资源池
- **fs_init**：首次进入 proczero 时通过 buffer 子系统读出 superblock，并按“范围+总大小”的形式打印磁盘布局，方便和 mkfs 结果对照。
- **bitmap_alloc/free**：遍历每个 bitmap block，先按字节、再按 bit 查找空位；分配成功后立即写回磁盘并返回全局块号/ inode 号，释放时对齐 block 位置做清零。
- **一致性输出**：根据 superblock 计算总块数、数据区范围，给后续 `show_bitmap` 的输出做基准。

### 4. 系统调用与用户程序联调
- **内核接口**：新增 11 个 syscall，把 bitmap 分配/释放、buffer get/put/read/write 以及缓存状态、flush 能力暴露给用户态；参数读取遵循 `arg_uint*`/`uvm_copyin` 规范。
- **initcode 梯度**：按 README 指示在 `src/user/initcode.c` 中轮流填入 test-1/2/3，用不同的输出去验证 superblock、bitmap、buffer_cache 的正确性。
- **运行策略**：每次切换测试都先 `make clean && make run`，确保磁盘镜像和内核镜像同步，日志截图记录在 `picture/` 目录供复现。

---

## 测试分析

### test-1：打印超级块信息
- **目标**：验证初始化路径能正确映射 virtio 寄存器，并在用户态首次运行时读出 superblock。
- **过程**：保留最小 initcode，让 proczero 只打印字符串；`fs_init` 在输出前把 superblock 的起止 block、容量统计全部展示。
- **结果**：终端出现 `disk layout information` 及 5 段 block 范围，数据与 mkfs 设定完全吻合，`hello, world!` 说明用户态还在循环等待下一步测试。
![alt text](picture/test-1-result.png)

### test-2：bitmap 申请与释放
- **目标**：同时覆盖 data/inode 两种 bitmap 的分配、释放与可视化接口。
- **过程**：test-2 依次申请 20 个 data block、分段释放、调用 `SYS_show_bitmap` 三次观测变化；同样逻辑应用在 inode bitmap 上。
- **结果**：输出显示 data bitmap 先连号占用 1067~1086，再按步骤逐渐清空；inode 部分打印 0~19 的分配轨迹，完全符合 README 给出的理想序列。
![alt text](picture/test-2-result.png)
### test-3：buffer_cache 行为
- **目标**：验证 `SYS_get_block/put_block/read_block/write_block/flush_buffer` 组合是否遵循 LRU 规则，并确保磁盘写回成功。
- **过程**：测试分两阶段：先写入 “ABCDEFGH” 并读回对比，再依次 get/put 不同 block，最后触发 flush 观察 inactive list 的物理页回收。
- **结果**：state-1~state-6 的 active/inactive 列表满足“ref>0 在 active、ref=0 在 inactive”的 LRU 约束；由于 `pmem_alloc` 分配顺序不同，节点顺序和 `page(pa=…)` 地址可能与 README 截图略有差异，但各节点记录的 `block[id]` 与操作顺序一致，`write/read data` 均打印 ABCDEFGH，最终 flush 后 inactive list 清零，验证了缓冲管理的正确性。
![alt text](picture/test-3(1)-result.png)
![alt text](picture/test-3(2)-result.png)

### 新增测试：test-4: Cache Thrashing (压力测试)
- **目标**：验证当活跃块数量远超缓存容量时，系统是否能正确处理“置换-写回-重载”的循环。
- **过程**：连续写入 16 个不同的 Block (超过缓存大小 2 倍)，强制触发置换。随后清空缓存，重新读取这 16 个 Block 验证数据。
- **结果**：所有数据读取正确，证明“内存不足时的置换写回”和“缓存未命中时的磁盘重载”逻辑均正常。

```c
// test-4: cache thrashing
#include "sys.h"

#define N_BUFFER 8
#define TEST_COUNT 16 // 2 * N_BUFFER，确保发生大规模置换
#define BLOCK_BASE 6000

int main()
{
    char data[PGSIZE];
    char read_buf[PGSIZE];
    unsigned long long buf_ids[TEST_COUNT];

    syscall(SYS_print_str, "Test-4: Cache Thrashing Start\n");

    /* 
       阶段1: 连续写入 16 个块
       由于 N_BUFFER=8，写入第 9 个块时，必然会踢出第 1 个块。
       如果 Buffer Cache 实现了正确的“脏回写”策略，第 1 个块的数据应该被自动保存到磁盘。
    */
    syscall(SYS_print_str, "Step 1: Writing 16 blocks (Force Eviction)...\n");
    for (int i = 0; i < TEST_COUNT; i++) {
        // 构造标记数据: "Block-A", "Block-B", ...
        for(int j=0; j<PGSIZE; j++) data[j] = 0;
        data[0] = 'B'; data[1] = 'l'; data[2] = 'o'; data[3] = 'c'; data[4] = 'k'; data[5] = '-';
        data[6] = 'A' + i; 
        
        buf_ids[i] = syscall(SYS_get_block, BLOCK_BASE + i);
        syscall(SYS_write_block, buf_ids[i], data);
        syscall(SYS_put_block, buf_ids[i]); // 引用计数归零，允许被置换
    }

    /* 
       阶段2: 彻底清空缓存
       这会强制剩余的 8 个块也写回磁盘，并释放所有物理页。
       接下来的读取操作将全部触发 Cache Miss，必须从磁盘读数据。
    */
    syscall(SYS_print_str, "Step 2: Flush all buffers.\n");
    syscall(SYS_flush_buffer, N_BUFFER); 

    /* 
       阶段3: 验证数据回读
       检查之前被“踢出”的数据是否真的持久化到了磁盘上。
    */
    syscall(SYS_print_str, "Step 3: Verify data reload...\n");
    int pass = 1;
    for (int i = 0; i < TEST_COUNT; i++) {
        unsigned long long bid = syscall(SYS_get_block, BLOCK_BASE + i);
        syscall(SYS_read_block, bid, read_buf);
        syscall(SYS_put_block, bid);

        // 验证标记位
        if (read_buf[6] != 'A' + i) {
            pass = 0;
            syscall(SYS_print_str, "Fail at index: ");
            // 简单的错误提示 (假设没有 printf %d)
            char c[2] = {'0'+i/10, '0'+i%10}; // 简易 hex/decimal 打印
            if(i<10) { c[0] = '0'+i; c[1]='\0'; }
            syscall(SYS_print_str, c); 
            syscall(SYS_print_str, "\n");
        }
    }

    if (pass) {
        syscall(SYS_print_str, "Test-4 PASSED: All blocks persisted and reloaded.\n");
    } else {
        syscall(SYS_print_str, "Test-4 FAILED: Data lost during eviction.\n");
    }

    while(1);
}
```

![alt text](picture/test-4-result.png)

---

## 实验反思

### 磁盘与内核解耦
- **体会**：virtio 驱动和 PLIC 配置非常依赖正确的页表映射。只有在 `vm_getpte` 允许 NULL 代表“用内核页表”之后，驱动才能安全地发送描述符，否则任何一次 DMA 都会卡在缺页里。
- **改进**：后续若再引入新外设，可以复用同样的“驱动初始化 + PLIC + trap”三段式模版，避免把调试时间浪费在基本连通性上。

### 缓冲区并发模型
- **体会**：active/inactive 双链虽然概念简单，但实现时要同时考虑引用计数、LRU 顺位和物理页生命周期。一次漏锁或错误插入都会在高并发下导致 ref 异常。
- **改进**：调试过程中坚持先在获取到缓冲区后立刻上睡眠锁，把“磁盘操作必须在睡眠锁保护下”写进代码注释，后面排查问题就有据可依。

### Bitmap 语义与可视化
- **体会**：bitmap 分配逻辑如果少算了最后一个 block 的有效 bit，就会出现“明明有空间却返回 -1”的假阳性。把 valid_count 设计成“遍历时动态缩减”之后，才真正覆盖全盘。
- **改进**：保持 `SYS_show_bitmap` 这类可视化接口长期可用，后续引入 inode 层级时依旧可以借助它观察资源池是否被正确消费。
