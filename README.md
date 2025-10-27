# lab2：内存管理初步
## 过程日志
1. 2025.9.29 更新lab2文件
2. 2025.9.29 张子扬完成修改pmem.c，完成lab2第一阶段
3. 2025.10.13 王俊翔完成lab2第二阶段

## 代码结构
```
OKOS
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目 (CHANGE, 增加trap和mem目录作为target)
├── kernel.ld      定义了内核程序在链接时的布局 (CHANGE, 增加一些关键位置的标记)
├── pictures       README使用的图片目录
├── lab2实验文档.md 实验指导书
├── README.md      实验报告 
└── src            源码
    └── kernel     内核源码
        ├── arch   RISC-V相关
        │   ├── method.h
        │   ├── mod.h
        │   └── type.h
        ├── boot   机器启动
        │   ├── entry.S
        │   └── start.c
        ├── lock   锁机制
        │   ├── spinlock.c
        │   ├── method.h
        │   ├── mod.h
        │   └── type.h
        ├── lib    常用库
        │   ├── cpu.c
        │   ├── print.c
        │   ├── uart.c
        │   ├── utils.c (NEW, 工具函数)
        │   ├── method.h (CHANGE, utils.c的函数声明)
        │   ├── mod.h
        │   └── type.h
        ├── mem    内存模块
        │   ├── pmem.c (DONE, 物理内存管理)
        │   ├── kvm.c (DONE, 内核态虚拟内存管理)
        │   ├── method.h (NEW)
        │   ├── mod.h (NEW)
        │   └── type.h (NEW)
        ├── trap   陷阱模块
        │   ├── method.h (NEW)
        │   ├── mod.h (NEW)
        │   └── type.h (NEW, 增加CLINT和PLIC寄存器定义)
        └── main.c (DONE)
```

## 实验分析
该实验的主题是内存管理初步，目标是为了实现物理内存管理和内核态虚拟内存管理。
### 第一阶段: 物理内存
该阶段是为了管理由 ALLOC_BEGIN ~ ALLOC_END 区域的物理页的相关内容，该内存布局由 kernel.ld 定义，其示意图如下：
```
物理地址空间划分：
┌────────────────────────────────────┐
│ 0x80000000 ~ KERNEL_DATA           │ ← 内核代码段（不可分配）
├────────────────────────────────────┤
│ KERNEL_DATA ~ ALLOC_BEGIN          │ ← 内核数据段（不可分配）
├────────────────────────────────────┤
│ ALLOC_BEGIN ~ (ALLOC_BEGIN+4MB)    │ ← 内核物理页池（1024页）
├────────────────────────────────────┤
│ (ALLOC_BEGIN+4MB) ~ ALLOC_END      │ ← 用户物理页池（剩余页）
└────────────────────────────────────┘
```

完成 pmem.c 的修改实现以下函数：
```
void pmem_init();    // 初始化系统, 只调用一次
void* pmem_alloc();  // 申请一个空闲的物理页
void pmem_free();    // 释放一个之前申请的物理页
```
#### test1
完成 test1 这个测试用例，它的作用是：
向cpu-0和cpu-1并行申请全部1024个内核页, 赋值并输出信息，待申请全部结束， 并行释放所有申请的物理内存，从而验证自旋锁保护的正确性，验证内存清零和数据写入。实验结果的截图如下：

![alt text](pictures/lab2截图01.png)

#### test2
test2测试用例的作用是：
1. 测试内存耗尽的`panic`是否正常触发
2. 测试用户空间物理页申请和释放的正确性

实验结果的截图如下：
![alt text](pictures/lab2截图02.png)

### 第二阶段: 内核态虚拟内存
修改 kvm.c ，实现页表项(PTE) 和 页表(pgtbl)。
#### test1
test1测试用例测试了两件事情:
1. 使用内核页表后你的OS内核是否还能正常执行
2. 使用映射和解映射操作修改你的页表, 使用vm_print输出它被修改前后的对比

实验结果截图如下：
![alt text](pictures/lab2截图03.png)

#### test2
这个测试用例主要关注映射和解映射是否正确执行，得到的实验结果截图如下：
![alt text](pictures/lab2截图04.png)

## 实验感想
1. 与 CSAPP 的内容有所呼应，CSAPP 中介绍的页表结构、地址翻译机制、内存分配策略等概念在此次实验中得到了具体实现。特别是对虚拟内存"为每个进程提供独立地址空间假象"这一核心思想的理解更加深刻，从理论层面的页表遍历算法到实际的RISC-V SV39规范实现，真正体验了从抽象概念到硬件实现的完整过程，从理论到实践。
