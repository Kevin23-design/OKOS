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

## 测试样例

## 实验感想
