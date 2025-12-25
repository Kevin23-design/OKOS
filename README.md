# LAB-9: 文件系统 之 文件管理与全系统整合

## 过程日志
1. 2025.12.25 更新lab-9文件


## 代码组织结构

```
ECNU-OSLAB-2025-TASK
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── .gdbinit.tmp-riscv xv6自带的调试配置
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目 (CHANGE)
├── picture        README使用的图片目录 (CHANGE)
├── README.md      实验指导书 (CHANGE)
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
    │   │   ├── console.c (NEW, 行缓冲的输入输出)
    │   │   ├── print.c (CHANGE, 在print_init中调用console_init进行初始化)
    │   │   ├── uart.c (CHANGE, 将uart_intr中的switch-case逻辑换成cons_edit)
    │   │   ├── utils.c
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE)
    │   ├── mem    内存模块
    │   │   ├── pmem.c (TODO, 增加函数pmem_stat用于获取剩余页面数量信息)
    │   │   ├── kvm.c
    │   │   ├── uvm.c (TODO, 修改uvm_heap_grow以支持flag的输入)
    │   │   ├── mmap.c
    │   │   ├── method.h (CHANGE)
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
    │   │   ├── proc.c (TODO, 增加open_file和cwd的初始化、设置、销毁逻辑)
    │   │   ├── exec.c (TODO, 操作ELF文件以填充新的进程)
    │   │   ├── swtch.S
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE)
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c (TODO, 新的系统调用)
    │   │   ├── sysfunc.c (TODO, 新的系统调用)
    │   │   ├── method.h (TODO, 新的系统调用)
    │   │   ├── mod.h
    │   │   └── type.h (TODO, 新的系统调用)
    │   ├── fs     文件系统模块
    │   │   ├── bitmap.c
    │   │   ├── buffer.c
    │   │   ├── inode.c
    │   │   ├── device.c (TODO, 增加设备文件操作逻辑)
    │   │   ├── dentry.c (TODO, 增加目录和路径的功能)
    │   │   ├── fs.c (TODO, 增加文件操作逻辑)
    │   │   ├── virtio.c
    │   │   ├── method.h (CHANGE)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE)
    │   └── main.c
    ├── mkfs       磁盘映像初始化
    │   ├── mkfs.c (CHANGE, 增加输入参数的支持)
    │   └── mkfs.h (CHANGE)
    ├── loader     存放链接脚本
    │   ├── kernel.ld (CHANGE, 移动了位置)
    │   └── user.ld (NEW, 定义了用户态ELF程序的链接规则)
    └── user       用户程序
        ├── initcode.c (CHANGE, 启动测试程序)
        ├── syscall.c (NEW, 封装了系统调用)
        ├── help.c (NEW, 其他公共库函数)
        ├── test_1.c (NEW, 测试点)
        ├── test_2.c (NEW, 测试点)
        ├── test_3.c (NEW, 测试点)
        ├── test_4.c (NEW, 测试点)
        ├── help.h (NEW, 库函数和重要定义)
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h (CHANGE, 新的系统调用)
```

**标记说明**

**NEW**: 新增源文件, 直接拷贝即可, 无需修改

**CHANGE**: 旧的源文件发生了更新, 直接拷贝即可, 无需修改

**TODO**: 你需要实现新功能 / 你需要完善旧功能

---  

## 核心思考

---  

## 实验过程详解


---  

## 测试分析

### 测试1: 

### 测试2: 

### 测试3: 

### 测试4: 


---  

## 实验反思

