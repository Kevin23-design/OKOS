#include "mod.h"

// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    // 初始化链表头和自旋锁
    spinlock_init(&list_lk, "mmap_node_list");

    spinlock_acquire(&list_lk);

    // 将 node_list 串成空闲链
    for (int i = 0; i < N_MMAP - 1; i++) {
        node_list[i].next = &node_list[i + 1];
    }
    node_list[N_MMAP - 1].next = NULL;

    list_head.next = &node_list[0];

    spinlock_release(&list_lk);
}

// 从仓库申请一个 mmap_region_t
// 若仓库空了则 panic
mmap_region_t *mmap_region_alloc()
{
    // TODO: 任务3实现
    spinlock_acquire(&list_lk);

    mmap_region_node_t *node = list_head.next;
    if (node == NULL) {
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: no free node");
    }
    list_head.next = node->next;

    spinlock_release(&list_lk);

    // 清理并返回 mmap 区域指针
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;
    return &node->mmap;
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t *mmap)
{
    if (mmap == NULL) return;

    mmap_region_node_t *node = (mmap_region_node_t *)mmap; // mmap 是首成员，可直接转换

    // 可选：清理字段
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;

    spinlock_acquire(&list_lk);
    node->next = list_head.next;
    list_head.next = node;
    spinlock_release(&list_lk);
}

// 输出可用的 mmap_region_node_t 链
// for debug
void mmap_show_nodelist()
{
    spinlock_acquire(&list_lk);

    mmap_region_node_t *tmp = list_head.next;
    int node = 0, index = 0;
    while (tmp)
    {
        index = tmp - &(node_list[0]);
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}