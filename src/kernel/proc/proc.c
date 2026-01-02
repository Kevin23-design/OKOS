// (TODO, 增加open_file和cwd的初始化、设置、销毁逻辑)

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


#define SCHED_DEBUG 0

// 全局pid + 保护它的锁
static int global_pid;
static spinlock_t pid_lk;
// wait/exit同步锁
static spinlock_t wait_lk;

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
    spinlock_release(&myproc()->lk);
    fs_init();
    proc_t *p = myproc();
    if (p == proczero && p->cwd == NULL) {
        p->cwd = inode_get(ROOT_INODE);
        for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
            p->open_file[i] = NULL;
        p->open_file[0] = file_open("/dev/stdin", FILE_OPEN_READ);
        p->open_file[1] = file_open("/dev/stdout", FILE_OPEN_WRITE);
        p->open_file[2] = file_open("/dev/stderr", FILE_OPEN_WRITE);
    }
    trap_user_return();
}

/* 进程模块初始化 */
void proc_init()
{    
    // 初始化 PID 分配器锁
    spinlock_init(&pid_lk, "pid");
    spinlock_init(&wait_lk, "wait");
    global_pid = 1;  // PID 从 1 开始
    
    // 初始化进程数组中的每个进程
    for (int i = 0; i < N_PROC; i++) {
        spinlock_init(&proc_list[i].lk, "proc");
        proc_list[i].state = UNUSED;
        proc_list[i].pid = 0;
        proc_list[i].parent = NULL;
        proc_list[i].exit_code = 0;
        proc_list[i].sleep_space = NULL;
        proc_list[i].pgtbl = NULL;
        proc_list[i].heap_top = 0;
        proc_list[i].ustack_npage = 0;
        proc_list[i].mmap = NULL;
        proc_list[i].tf = NULL;
        proc_list[i].kstack = 0;
        proc_list[i].cwd = NULL;
        for (int j = 0; j < N_OPEN_FILE_PER_PROC; j++)
            proc_list[i].open_file[j] = NULL;
        memset(proc_list[i].name, 0, sizeof(proc_list[i].name));
        memset(&proc_list[i].ctx, 0, sizeof(proc_list[i].ctx));
    }
}

/* 
    申请一个UNUSED进程结构体(返回时带锁)
    并执行通用的初始化逻辑
*/
proc_t *proc_alloc()
{
    proc_t *p = NULL;
    
    // 扫描进程数组，寻找第一个 UNUSED 的进程
    for (int i = 0; i < N_PROC; i++) {
        spinlock_acquire(&proc_list[i].lk);
        if (proc_list[i].state == UNUSED) {
            p = &proc_list[i];
            break;
        }
        spinlock_release(&proc_list[i].lk);
    }
    
    if (p == NULL) {
        return NULL;  // 没有可用进程
    }
    
    // 分配 PID
    p->pid = alloc_pid();
    
    // 分配 trapframe
    p->tf = (trapframe_t *)pmem_alloc(true);
    if (p->tf == NULL) {
        p->state = UNUSED;
        spinlock_release(&p->lk);
        return NULL;
    }
    memset(p->tf, 0, PGSIZE);
    
    // 创建用户页表（包括 trapframe 和 trampoline 映射）
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (p->pgtbl == NULL) {
        pmem_free((uint64)p->tf, true);
        p->tf = NULL;
        p->state = UNUSED;
        spinlock_release(&p->lk);
        return NULL;
    }
    
    // 初始化其他字段
    p->parent = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    p->mmap = NULL;
    p->cwd = NULL;
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
        p->open_file[i] = NULL;
    memset(p->name, 0, sizeof(p->name));
    
    // 计算进程在数组中的索引，设置内核栈
    int proc_id = p - proc_list;
    p->kstack = KSTACK(proc_id);
    
    // 初始化内核上下文
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->ctx.sp = p->kstack + PGSIZE;      // 内核栈顶
    p->ctx.ra = (uint64)proc_return;     // 返回地址设为 proc_return
    
    // 状态设置为 RUNNABLE
    p->state = RUNNABLE;
    
    // 返回时持有进程锁
    return p;
}

/* 
    回收一个进程结构体并释放它包含的资源
    tips: 调用者需要持有进程锁
*/
void proc_free(proc_t *p)
{
    // 释放 trapframe
    if (p->tf) {
        pmem_free((uint64)p->tf, true);
        p->tf = NULL;
    }
    
    // 释放用户页表及其管理的所有物理页
    if (p->pgtbl) {
        // 释放用户代码页
        vm_unmappages(p->pgtbl, USER_BASE, PGSIZE, true);
        
        // 释放堆空间
        if (p->heap_top > USER_BASE + PGSIZE) {
            uint64 heap_size = p->heap_top - (USER_BASE + PGSIZE);
            vm_unmappages(p->pgtbl, USER_BASE + PGSIZE, heap_size, true);
        }
        
        // 释放所有 mmap 区域
        mmap_region_t *tmp = p->mmap;
        while (tmp != NULL) {
            vm_unmappages(p->pgtbl, tmp->begin, tmp->npages * PGSIZE, true);
            mmap_region_t *next = tmp->next;
            mmap_region_free(tmp);
            tmp = next;
        }
        p->mmap = NULL;
        
        // 释放用户栈
        if (p->ustack_npage > 0) {
            uint64 ustack_bottom = TRAPFRAME - p->ustack_npage * PGSIZE;
            vm_unmappages(p->pgtbl, ustack_bottom, p->ustack_npage * PGSIZE, true);
        }
        
        // 解除 trapframe 和 trampoline 的映射（不释放物理页）
        vm_unmappages(p->pgtbl, TRAPFRAME, PGSIZE, false);
        vm_unmappages(p->pgtbl, TRAMPOLINE, PGSIZE, false);
        
        // 释放页表本身占用的物理页
        pmem_free((uint64)p->pgtbl, true);
        p->pgtbl = NULL;
    }

    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (p->open_file[i] != NULL) {
            file_close(p->open_file[i]);
            p->open_file[i] = NULL;
        }
    }
    if (p->cwd != NULL) {
        inode_put(p->cwd);
        p->cwd = NULL;
    }
    
    // 清空其他字段
    p->pid = 0;
    p->parent = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    p->kstack = 0;
    memset(p->name, 0, sizeof(p->name));
    memset(&p->ctx, 0, sizeof(p->ctx));
    
    // 状态设置为 UNUSED
    p->state = UNUSED;
    
    // 释放进程锁
    spinlock_release(&p->lk);
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

    // 使用 proc_alloc 申请第一个进程
    proc_t *p = proc_alloc();
    if (p == NULL) panic("proc_make_first: proc_alloc failed");
    
    // 设置进程名称
    const char *name = "proczero";
    for (int i = 0; i < sizeof(p->name) - 1 && name[i]; i++) {
        p->name[i] = name[i];
    }
    
    // 保存 proczero 指针
    proczero = p;

    // 分配用户栈
    void *ustack_pa = pmem_alloc(false);
    if (ustack_pa == NULL) panic("proc_make_first: alloc ustack failed");
    vm_mappages(p->pgtbl, USTACK, (uint64)ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    p->ustack_npage = 1;

    // 分配用户代码页放在 USER_BASE
    void *ucode_pa = pmem_alloc(false);
    if (ucode_pa == NULL) panic("proc_make_first: alloc ucode failed");
    memset(ucode_pa, 0, PGSIZE);
    memmove(ucode_pa, initcode, MIN((uint32)initcode_len, (uint32)PGSIZE));
    vm_mappages(p->pgtbl, USER_BASE, (uint64)ucode_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    p->heap_top = USER_BASE + PGSIZE;

    // 填写 trapframe 关键字段
    p->tf->user_to_kern_satp = r_satp();
    int proc_id = p - proc_list;
    p->tf->user_to_kern_sp = KSTACK(proc_id) + PGSIZE;  // 内核栈顶
    extern void trap_user_handler();
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    p->tf->user_to_kern_epc = USER_BASE;
    p->tf->user_to_kern_hartid = r_tp();
    p->tf->sp = USTACK + PGSIZE;
    
    // 状态设置为 RUNNABLE（等待调度器调度）
    p->state = RUNNABLE;

    // 释放进程锁(proc_alloc 返回时持有锁)
    spinlock_release(&p->lk);
}


/*
    父进程产生子进程
    UNUSED -> RUNNABLE
*/
int proc_fork()
{
    proc_t *parent = myproc();
    
    // 1. 通过proc_alloc申请一个空闲的进程结构体
    proc_t *child = proc_alloc();
    if (child == NULL) {
        return -1; // 进程数组已满,无法分配
    }
    
    // 2. 复制父进程的内存映射
    // 复制用户页表和所有内存内容(代码段、堆、用户栈、mmap区域)
    uvm_copy_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top, 
                   parent->ustack_npage, parent->mmap);
    
    // 3. 复制父进程的其他字段
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;
    child->cwd = (parent->cwd != NULL) ? inode_dup(parent->cwd) : NULL;
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (parent->open_file[i] != NULL)
            child->open_file[i] = file_dup(parent->open_file[i]);
        else
            child->open_file[i] = NULL;
    }
    
    // 复制mmap链表
    child->mmap = NULL;
    mmap_region_t *p_mmap = parent->mmap;
    mmap_region_t **c_mmap_ptr = &child->mmap;
    while (p_mmap != NULL) {
        mmap_region_t *new_region = mmap_region_alloc();
        if (new_region == NULL) {
            // 分配失败,需要清理已分配的资源
            proc_free(child);
            return -1;
        }
        new_region->begin = p_mmap->begin;
        new_region->npages = p_mmap->npages;
        new_region->next = NULL;
        *c_mmap_ptr = new_region;
        c_mmap_ptr = &new_region->next;
        p_mmap = p_mmap->next;
    }
    
    // 4. 复制trapframe
    *child->tf = *parent->tf;
    
    // 5. 记录父子关系
    child->parent = parent;
    
    // 6. 设置子进程的返回值为0
    // 通过设置a0寄存器(系统调用返回值),让子进程能区分自己
    child->tf->a0 = 0;

    // 子进程内核栈与父进程不同, 需要更新trapframe中的kernel sp
    child->tf->user_to_kern_sp = child->kstack + PGSIZE;
    
    // 7. 复制进程名称(调试用)
    for (int i = 0; i < 16 && parent->name[i] != '\0'; i++) {
        child->name[i] = parent->name[i];
    }
    
    // 8. 释放子进程的锁(proc_alloc返回时持有锁)
    spinlock_release(&child->lk);
    
    // 9. 返回子进程的pid给父进程
    return child->pid;
}

/*
    进程主动放弃CPU控制权
    RUNNING->RUNNABLE
*/
void proc_yield()
{
    proc_t *p = myproc();
    
    // 获取当前进程的锁
    spinlock_acquire(&p->lk);
    
    // 状态转换: RUNNING -> RUNNABLE
    // 当前进程主动放弃CPU,回到就绪队列
    p->state = RUNNABLE;
    
    // 切换到调度器
    // 注意: proc_sched要求调用者持有进程的锁
    // proc_sched返回时,进程已经被重新调度,锁也已经被释放
    proc_sched();
    
    // swtch返回后,释放锁
    spinlock_release(&p->lk);
}

/*
    唤醒等待呼叫的进程
    由proc_exit调用
    tips: 调用者需要持有p的进程锁
*/
static void proc_try_wakeup(proc_t *p)
{
    proc_t *parent = p->parent;
    if (parent == NULL)
        return;

    spinlock_acquire(&parent->lk);
    if (parent->state == SLEEPING && parent->sleep_space == parent) {
        parent->state = RUNNABLE;
    }
    spinlock_release(&parent->lk);
}

/*
    当父进程退出时, 让它的所有子进程认proczero为父
    因为proczero永不退出, 可以回收子进程的资源
*/
static void proc_reparent(proc_t *parent)
{
    // 遍历进程数组,找到所有以parent为父的进程
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        
        // 需要持有子进程的锁来访问parent字段
        spinlock_acquire(&p->lk);
        
        // 如果这个进程的父进程是当前要退出的进程
        if (p->parent == parent) {
            // 将其过继给proczero
            p->parent = proczero;
            
            // 如果子进程已经是ZOMBIE状态,需要唤醒proczero来回收
            if (p->state == ZOMBIE) {
                proc_try_wakeup(p);
            }
        }
        
        spinlock_release(&p->lk);
    }
}

/*
    进程退出
    RUNNING -> ZOMBIE
*/
void proc_exit(int exit_code)
{
    proc_t *p = myproc();
    
    // proczero不能退出
    assert(p != proczero, "proc_exit: proczero cannot exit");

    // 1. 持有wait锁, 将当前进程的所有子进程过继给proczero
    spinlock_acquire(&wait_lk);
    proc_reparent(p);
    
    // 2. 获取当前进程的锁
    spinlock_acquire(&p->lk);
    
    // 3. 设置退出状态
    p->exit_code = exit_code;
    
    // 4. 状态转换: RUNNING -> ZOMBIE
    p->state = ZOMBIE;
    
    // 5. 唤醒父进程(如果父进程在wait中睡眠)
    proc_try_wakeup(p);
    spinlock_release(&wait_lk);
    
    // 6. 切换到调度器,永不返回
    // 注意: 此时进程还持有自己的锁
    // 父进程在proc_wait中回收时会释放这个锁
    proc_sched();
    
    // 永远不会执行到这里
    panic("proc_exit: zombie process returned");
}

/*
    父进程等待一个子进程进入ZOMBIE状态
    1. 如果等到: 释放子进程, 返回子进程的pid, 将子进程的退出状态传出到user_addr
    2. 如果发现没孩子: 返回-1
    3. 如果没等到: 父进程进入睡眠状态 
*/
int proc_wait(uint64 user_addr)
{
    proc_t *parent = myproc();
    spinlock_acquire(&wait_lk);
    
    while (1) {
        int has_children = 0; // 是否有子进程
        
        // 遍历进程数组,寻找子进程
        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];
            
            spinlock_acquire(&p->lk);
            
            // 检查是否是当前进程的子进程
            if (p->parent == parent) {
                has_children = 1;
                
                // 找到一个ZOMBIE状态的子进程
                if (p->state == ZOMBIE) {
                    // 保存pid和exit_code
                    int pid = p->pid;
                    int exit_code = p->exit_code;
                    
                    // 如果user_addr不为0,将退出状态传出到用户空间
                    if (user_addr != 0) {
                        uvm_copyout(parent->pgtbl, user_addr, 
                                   (uint64)&exit_code, sizeof(int));
                    }
                    
                    // 释放子进程的资源
                    // 注意: proc_free会释放p->lk
                    proc_free(p);
                    
                    // 返回子进程的pid
                    spinlock_release(&wait_lk);
                    return pid;
                }
            }
            
            spinlock_release(&p->lk);
        }
        
        // 如果没有子进程,返回-1
        if (!has_children) {
            spinlock_release(&wait_lk);
            return -1;
        }
        
        // 有子进程但都还没退出,进入睡眠状态
        // 使用wait锁与子进程exit保持同步
        proc_sleep(parent, &wait_lk);
    }
}

/*
    进程等待sleep_space对应的资源, 进入睡眠状态
    RUNNING -> SLEEPING
*/
void proc_sleep(void *sleep_space, spinlock_t *lock)
{
    proc_t *p = myproc();
    
    // 必须持有进程的锁才能调用proc_sched
    // 但是调用者可能持有的是其他的锁(比如某个资源的锁)
    // 需要先释放外部锁,再获取进程锁,避免死锁
    
    // 先获取进程锁
    spinlock_acquire(&p->lk);
    
    // 释放外部传入的锁
    // 这样其他进程可以修改资源状态并调用wakeup
    spinlock_release(lock);
    
    // 记录等待的资源
    p->sleep_space = sleep_space;
    
    // 状态转换: RUNNING -> SLEEPING
    p->state = SLEEPING;
    
    // 切换到调度器
    // 当被唤醒并重新调度时,会从这里返回
    proc_sched();
    
    // 被唤醒后,清空sleep_space
    p->sleep_space = NULL;
    
    // 释放进程锁
    spinlock_release(&p->lk);
    
    // 重新获取外部锁
    // 保证调用者在返回后仍然持有这个锁
    spinlock_acquire(lock);
}

/*
    唤醒所有等待sleep_space的进程
    SLEEPING -> RUNNABLE
*/
void proc_wakeup(void *sleep_space)
{
    // 遍历进程数组,找到所有等待该资源的进程
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        
        // 需要持有进程锁才能访问state和sleep_space字段
        spinlock_acquire(&p->lk);
        
        // 如果进程在睡眠状态,且等待的就是这个资源
        if (p->state == SLEEPING && p->sleep_space == sleep_space) {
            // 状态转换: SLEEPING -> RUNNABLE
            p->state = RUNNABLE;
        }
        
        spinlock_release(&p->lk);
    }
}

/* 
    用户进程切换到调度器
    tips: 调用者保证持有当前进程的锁
*/
void proc_sched()
{
    proc_t *p = myproc();
    
    // 确保调用者持有进程的锁
    assert(spinlock_holding(&p->lk), "proc_sched: not holding lock");
    
    // 确保当前进程不是调度器(即有有效进程在运行)
    assert(p != NULL, "proc_sched: no process running");
    
    // 保存当前进程的上下文,切换到调度器的上下文
    // 这里会保存用户进程的ctx,然后跳转到cpu->ctx继续执行
    // 注意:从用户进程的角度看,swtch返回时已经是下一次被调度的时候了
    swtch(&p->ctx, &mycpu()->ctx);
}

/* 
    调度器
    RUNNABLE->RUNNING
*/
void proc_scheduler()
{
    cpu_t *c = mycpu();
    
    // 调度器死循环,永不返回
    while (1) {
        // 设置当前CPU上没有进程在运行(调度器不算用户进程)
        c->proc = NULL;
        
        // 开中断,使得在调度器运行时可以响应中断
        // (比如串口中断、时钟中断等)
        intr_on();
        
        // 循环扫描进程数组,寻找RUNNABLE状态的进程
        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];
            
            // 获取进程的锁(需要检查和修改state字段)
            spinlock_acquire(&p->lk);
            
            // 找到一个可运行的进程
            if (p->state == RUNNABLE) {
                // 状态转换: RUNNABLE -> RUNNING
                p->state = RUNNING;
                
                // 将当前CPU绑定到这个进程
                c->proc = p;

                if (SCHED_DEBUG) {
                    printf("proc %d is running...\n", p->pid);
                }
                
                // 切换到用户进程的上下文
                // 保存调度器的上下文到cpu->ctx
                // 恢复用户进程的上下文从p->ctx
                // 注意:swtch返回时,可能是很久以后另一个进程调用proc_sched切换回来的
                swtch(&c->ctx, &p->ctx);
                
                // swtch返回后,说明用户进程又切换回调度器了
                // 此时该进程已经不再运行
                // 注意:不要在这里设置c->proc=NULL,因为进程可能还需要用到myproc()
                // 在下一次循环开始时再清空c->proc
            }
            
            // 释放进程的锁
            spinlock_release(&p->lk);
        }
    }
}
