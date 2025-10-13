
#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
struct alloc_region kern_region, user_region;

// 初始化物理内存分区和空闲链表
void pmem_init(void)
{
    uint64 alloc_begin = (uint64)ALLOC_BEGIN;
    uint64 alloc_end = (uint64)ALLOC_END;
    uint64 kern_end = alloc_begin + KERN_PAGES * PGSIZE;
    
    // 初始化内核区域
    kern_region.begin = alloc_begin;
    kern_region.end = kern_end;
    spinlock_init(&kern_region.lk, "kern_region");
    kern_region.allocable = KERN_PAGES;
    kern_region.list_head.next = NULL;
    
    // 将内核区域的所有物理页加入空闲链表
    page_node_t *current = &kern_region.list_head;
    for (uint64 pa = alloc_begin; pa < kern_end; pa += PGSIZE) {
        page_node_t *node = (page_node_t *)pa;
        node->next = NULL;
        current->next = node;
        current = node;
    }
    
    // 初始化用户区域
    user_region.begin = kern_end;
    user_region.end = alloc_end;
    spinlock_init(&user_region.lk, "user_region");
    user_region.allocable = (alloc_end - kern_end) / PGSIZE;
    user_region.list_head.next = NULL;
    
    // 将用户区域的所有物理页加入空闲链表
    current = &user_region.list_head;
    for (uint64 pa = kern_end; pa < alloc_end; pa += PGSIZE) {
        page_node_t *node = (page_node_t *)pa;
        node->next = NULL;
        current->next = node;
        current = node;
    }
}

// 分配一个物理页，分区由 in_kernel 决定
void* pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    page_node_t *page = NULL;
    
    spinlock_acquire(&region->lk);
    
    if (region->list_head.next != NULL) {
        // 从链表头取出一个页面
        page = region->list_head.next;
        region->list_head.next = page->next;
        region->allocable--;
    } else {
        // 没有可用页面
        spinlock_release(&region->lk);
        panic("pmem_alloc: out of memory");
    }
    
    spinlock_release(&region->lk);
    
    // 将分配的页面清零
    memset((void *)page, 0, PGSIZE);
    
    return (void *)page;
}

// 释放一个物理页
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    
    // 检查页面地址是否对齐
    if (page % PGSIZE != 0) {
        panic("pmem_free: page address not aligned");
    }
    
    // 检查页面是否在正确的区域内
    if (page < region->begin || page >= region->end) {
        panic("pmem_free: page address out of region bounds");
    }
    
    // 将页面清零
    memset((void *)page, 0, PGSIZE);
    
    spinlock_acquire(&region->lk);
    
    // 将页面插入到链表头
    page_node_t *node = (page_node_t *)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    
    spinlock_release(&region->lk);
}
