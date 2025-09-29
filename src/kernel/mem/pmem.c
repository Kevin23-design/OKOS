#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    // 初始化内核区域
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end = kern_region.begin + KERN_PAGES * PGSIZE;
    kern_region.allocable = KERN_PAGES;
    spinlock_init(&kern_region.lk, "kern_region");
    kern_region.list_head.next = NULL;

    // 初始化用户区域  
    user_region.begin = kern_region.end;
    user_region.end = (uint64)ALLOC_END;
    user_region.allocable = (user_region.end - user_region.begin) / PGSIZE;
    spinlock_init(&user_region.lk, "user_region");
    user_region.list_head.next = NULL;

    // 初始化内核区域空闲链表
    uint64 current = kern_region.begin;
    page_node_t *prev = &kern_region.list_head;
    while (current < kern_region.end) {
        page_node_t *node = (page_node_t *)current;
        node->next = NULL;
        prev->next = node;
        prev = node;
        current += PGSIZE;
    }

    // 初始化用户区域空闲链表
    current = user_region.begin;
    prev = &user_region.list_head;
    while (current < user_region.end) {
        page_node_t *node = (page_node_t *)current;
        node->next = NULL;
        prev->next = node;
        prev = node;
        current += PGSIZE;
    }
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    alloc_region_t *ar = in_kernel ? &kern_region : &user_region;
    page_node_t *page;

    spinlock_acquire(&ar->lk);
    
    if (ar->allocable == 0) {
        spinlock_release(&ar->lk);
        panic("pmem_alloc: out of memory");
    }

    page = ar->list_head.next;
    if (page == NULL) {
        spinlock_release(&ar->lk);
        panic("pmem_alloc: free list corrupted");
    }

    ar->list_head.next = page->next;
    ar->allocable--;
    
    spinlock_release(&ar->lk);

    // 清零物理页内容
    memset((void *)page, 0, PGSIZE);
    return (void *)page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *ar = in_kernel ? &kern_region : &user_region;
    
    // 检查页面是否在正确区域内
    if (page < ar->begin || page >= ar->end) {
        panic("pmem_free: page out of region bounds");
    }
    
    if ((page % PGSIZE) != 0) {
        panic("pmem_free: page not aligned");
    }

    spinlock_acquire(&ar->lk);
    
    page_node_t *node = (page_node_t *)page;
    node->next = ar->list_head.next;
    ar->list_head.next = node;
    ar->allocable++;
    
    spinlock_release(&ar->lk);
}
