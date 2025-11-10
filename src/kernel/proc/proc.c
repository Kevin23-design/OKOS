#include "mod.h"
#include "../arch/mod.h"
#include "../mem/mod.h"
#include "../lib/mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

/* ------------本地变量----------- */

// 进程结构体数组 + 第一个用户进程的指针
static proc_t proc_list[N_PROC];
static proc_t *proczero;


// 全局pid + 保护它的锁
static int global_pid;
static spinlock_t pid_lk;

/* 获取一个pid */
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&pid_lk);
    assert(global_pid > 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&pid_lk);
    return tmp;
}

/* 释放进程锁 + trap_user_return */
static void proc_return()
{

}

/* 进程模块初始化 */
void proc_init()
{    

}

/* 
    申请一个UNUSED进程结构体(返回时带锁)
    并执行通用的初始化逻辑
*/
proc_t *proc_alloc()
{

}

/* 
    回收一个进程结构体并释放它包含的资源
    tips: 调用者需要持有进程锁
*/
void proc_free(proc_t *p)
{

}

/* 
    获得一个初始化过的用户页表
    完成trapframe和trampoline的映射
*/
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t upgtbl = (pgtbl_t)pmem_alloc(true);
    if (upgtbl == NULL) panic("proc_pgtbl_init: alloc pgtbl failed");
    memset(upgtbl, 0, PGSIZE);

    extern char trampoline[];
    // 映射 trampoline（仅S态执行，不置U）
    vm_mappages(upgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    // 映射 trapframe（S态读写，不置U）
    vm_mappages(upgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W);

    return upgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_first()
{
    // 用户栈地址定义
    #define USTACK (TRAPFRAME - PGSIZE)

    proc_t *p = &proczero;
    p->pid = 0;

    // 分配 trapframe
    p->tf = (trapframe_t *)pmem_alloc(true);
    if (p->tf == NULL) panic("proc_make_first: alloc tf failed");
    memset(p->tf, 0, PGSIZE);

    // 创建用户页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);

    // 用户栈
    void *ustack_pa = pmem_alloc(false);
    if (ustack_pa == NULL) panic("proc_make_first: alloc ustack failed");
    vm_mappages(p->pgtbl, USTACK, (uint64)ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    p->ustack_npage = 1;

    // 用户代码页放在 USER_BASE
    void *ucode_pa = pmem_alloc(false);
    if (ucode_pa == NULL) panic("proc_make_first: alloc ucode failed");
    memset(ucode_pa, 0, PGSIZE);
    memmove(ucode_pa, initcode, MIN((uint32)initcode_len, (uint32)PGSIZE));
    vm_mappages(p->pgtbl, USER_BASE, (uint64)ucode_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    p->heap_top = USER_BASE + PGSIZE;

    // 填写 trapframe 关键字段
    p->tf->user_to_kern_satp = r_satp();
    p->tf->user_to_kern_sp = KSTACK(0) + PGSIZE;  // 内核栈顶
    extern void trap_user_handler();
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    p->tf->user_to_kern_epc = USER_BASE;
    p->tf->user_to_kern_hartid = r_tp();
    p->tf->sp = USTACK + PGSIZE;

    // 设置进程的内核栈和上下文
    p->kstack = KSTACK(0);
    p->ctx.sp = KSTACK(0) + PGSIZE;  // 内核栈顶
    p->ctx.ra = (uint64)trap_user_return;  // 返回地址

    // 当前CPU绑定该进程
    cpu_t *c = mycpu();
    c->proc = p;

    trap_user_return();
}


/*
    父进程产生子进程
    UNUSED -> RUNNABLE
*/
int proc_fork()
{

}

/*
    进程主动放弃CPU控制权
    RUNNING->RUNNABLE
*/
void proc_yield()
{

}

/*
    当父进程退出时, 让它的所有子进程认proczero为父
    因为proczero永不退出, 可以回收子进程的资源
*/
static void proc_reparent(proc_t *parent)
{

}

/*
    唤醒等待呼叫的进程
    由proc_exit调用
    tips: 调用者需要持有p的进程锁
*/
static void proc_try_wakeup(proc_t *p)
{

}

/*
    进程退出
    RUNNING -> ZOMBIE
*/
void proc_exit(int exit_code)
{

}

/*
    父进程等待一个子进程进入ZOMBIE状态
    1. 如果等到: 释放子进程, 返回子进程的pid, 将子进程的退出状态传出到user_addr
    2. 如果发现没孩子: 返回-1
    3. 如果没等到: 父进程进入睡眠状态 
*/
int proc_wait(uint64 user_addr)
{

}

/*
    进程等待sleep_space对应的资源, 进入睡眠状态
    RUNNING -> SLEEPING
*/
void proc_sleep(void *sleep_space, spinlock_t *lock)
{

}

/*
    唤醒所有等待sleep_space的进程
    SLEEPING -> RUNNABLE
*/
void proc_wakeup(void *sleep_space)
{

}

/* 
    用户进程切换到调度器
    tips: 调用者保证持有当前进程的锁
*/
void proc_sched()
{

}

/* 
    调度器
    RUNNABLE->RUNNING
*/
void proc_scheduler()
{

}