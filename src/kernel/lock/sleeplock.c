#include "mod.h"
#include "../proc/mod.h"

// 睡眠锁初始化
void sleeplock_init(sleeplock_t *lk, char *name)
{
    // 初始化保护睡眠锁的自旋锁
    spinlock_init(&lk->lock, "sleeplock");
    
    // 初始化睡眠锁的状态
    lk->locked = 0;
    lk->name = name;
    lk->pid = 0;
}

// 检查当前进程是否持有睡眠锁
bool sleeplock_holding(sleeplock_t *lk)
{
    int holding;
    
    spinlock_acquire(&lk->lock);
    holding = (lk->locked && lk->pid == myproc()->pid);
    spinlock_release(&lk->lock);
    
    return holding;
}

// 当前进程尝试获取睡眠锁, 失败进入睡眠状态
void sleeplock_acquire(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    
    // 如果锁已被持有,进入睡眠状态等待
    while (lk->locked) {
        // 以睡眠锁本身为sleep_space
        // proc_sleep会释放lk->lock,睡眠,被唤醒后重新获取lk->lock
        proc_sleep(lk, &lk->lock);
    }
    
    // 获取锁
    lk->locked = 1;
    lk->pid = myproc()->pid;
    
    spinlock_release(&lk->lock);
}

// 释放睡眠锁, 唤醒其他等待睡眠锁的进程
void sleeplock_release(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    
    // 释放锁
    lk->locked = 0;
    lk->pid = 0;
    
    // 唤醒所有等待这个锁的进程
    proc_wakeup(lk);
    
    spinlock_release(&lk->lock);
}