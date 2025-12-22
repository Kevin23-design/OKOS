# LAB-8: 文件系统 之 数据组织与层次结构

## 过程日志
1. 2025.12.14 更新lab8文件

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
├── picture        README使用的图片目录 (CHANGE)
├── lab-8-README.md 实验指导书 (CHANGE)
├── README.md      实验报告 (TODO)
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
    │   │   ├── utils.c (CHANGE, 新增strlen函数)
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── mem    内存模块
    │   │   ├── pmem.c
    │   │   ├── kvm.c
    │   │   ├── uvm.c
    │   │   ├── mmap.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── trap   陷阱模块
    │   │   ├── plic.c
    │   │   ├── timer.c
    │   │   ├── trap_kernel.c
    │   │   ├── trap_user.c
    │   │   ├── trap.S
    │   │   ├── trampoline.S
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── proc   进程模块
    │   │   ├── proc.c
    │   │   ├── swtch.S
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c
    │   │   ├── sysfunc.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── fs     文件系统模块
    │   │   ├── bitmap.c
    │   │   ├── buffer.c
    │   │   ├── inode.c (DONE, 核心工作)
    │   │   ├── dentry.c (DONE, 核心工作)
    │   │   ├── fs.c (DONE, 增加inode初始化逻辑和测试用例)
    │   │   ├── virtio.c
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE)
    │   └── main.c
    ├── mkfs       磁盘映像初始化
    │   ├── mkfs.c (CHANGE, 更复杂的文件系统初始化)
    │   └── mkfs.h (CHANGE)
    └── user       用户程序
        ├── initcode.c
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h
```


## 核心思考与架构设计

### 缓存一致性与持久化
文件系统的核心职责之一是数据的持久化存储。在引入 Buffer Cache 后，内存中的数据副本与磁盘上的数据可能存在短暂的不一致。为了验证系统的可靠性，我们需要确保：
1. **写穿策略 (Write-Through)**: 本实验中 `buffer_write` 采用同步写穿策略，即每次写入缓冲区都会立即触发磁盘 I/O。
2. **缓存失效验证**: 为了证明数据是从磁盘读取而非残留的内存缓存，我们在测试中引入了 `buffer_flush_all` 机制，强制废弃所有缓存块，模拟系统重启后的冷启动状态。

## 实验过程详解

---

## 测试分析

### **测试1: inode的访问 + 创建 + 删除**

### **测试2: 写入和读取inode管理的数据**

### **测试3: 目录项的增加、删除、查找操作**

### **测试4: 文件路径的解析**

### **新增测试5: 数据持久化与缓存一致性测试**
- **目的**: 验证文件写入操作是否真正持久化到了磁盘，排除内存缓存的干扰。
- **设计思路**:
  1. **写入阶段**: 创建文件，写入特定特征字符串（如 "PERSISTENCE_TEST..."），然后关闭文件释放 inode。
  2. **干扰阶段**: 调用 `buffer_flush_all()`。该函数遍历缓冲区链表，将所有引用计数为 0 的缓存块标记为无效 (`BLOCK_NUM_UNUSED`) 并移入空闲链表。这相当于清空了文件系统的读缓存。
  3. **验证阶段**: 重新通过 inode 编号获取文件，读取数据。此时 `buffer_get` 无法在缓存中找到对应块，必须发起 `virtio_disk_rw` 从磁盘读取。
  4. **比对**: 比较读取内容与原字符串，若一致则证明数据已成功持久化。

### **关键代码实现**

#### 1. 强制刷新缓存 (`kernel/fs/buf.c`)
为了模拟缓存失效，我们实现了 `buffer_flush_all` 函数。它遍历活跃链表，将所有引用计数为 0（即当前未被任何进程使用）的缓冲区标记为无效 (`BLOCK_NUM_UNUSED`) 并移入空闲链表。

```c
void buffer_flush_all()
{
    buffer_node_t *node;
    spinlock_acquire(&lk_buf_cache);
    
    node = buf_head_active.next;
    while (node != &buf_head_active) {
        buffer_node_t *next = node->next;
        // 仅回收未被引用的缓冲区
        if (node->buf.ref == 0) {
            node->buf.block_num = BLOCK_NUM_UNUSED;
            insert_node(node, false, true); // 移入 inactive 链表
        }
        node = next;
    }
    
    spinlock_release(&lk_buf_cache);
}
```

#### 2. 测试逻辑 (`kernel/fs/fs.c`)
测试流程严格遵循 "Write -> Flush -> Read" 的顺序。

```c
    /* 1. 写入数据并释放 Inode */
    ip = inode_create(INODE_TYPE_DATA, ...);
    inode_lock(ip);
    inode_write_data(ip, 0, len, test_str, false); // 写入 "PERSISTENCE_TEST..."
    uint32 inum = ip->inode_num;
    inode_unlock(ip);
    inode_put(ip); // 关键：释放引用，使 buffer.ref 降为 0，允许被 flush 回收

    /* 2. 模拟缓存失效 */
    buffer_flush_all(); 

    /* 3. 重新读取验证 */
    ip = inode_get(inum);
    inode_lock(ip);
    inode_read_data(ip, 0, len, buf, false); // 此时必须从磁盘加载
    
    if (strncmp(test_str, buf, len) == 0) {
        printf("   [PASS] Data matches!\n");
    }
```

![alt text](picture/测试5.png)

---

## 实验反思