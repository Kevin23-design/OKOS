#include "mod.h"

/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    proc_t *p = myproc();
    uint64 new_top;

    // 读取参数：new_heap_top（0 表示查询）
    arg_uint64(0, &new_top);

    uint64 cur = p->heap_top;

    if (new_top == 0) {
        // 查询当前堆顶
        printf("look event: ret_heap_top = %p\n", cur);
        vm_print(p->pgtbl);
        return cur;
    }

    if (new_top == cur) {
        // 不变
        printf("equal event: ret_heap_top = %p\n", cur);
        vm_print(p->pgtbl);
        return cur;
    }

    if (new_top > cur) {
        // 增长
        uint64 ret = uvm_heap_grow(p->pgtbl, cur, (uint32)(new_top - cur));
        if (ret == (uint64)-1) {
            printf("grow event: ret_heap_top = %p\n", cur);
            vm_print(p->pgtbl);
            return (uint64)-1;
        }
        p->heap_top = ret;
        printf("grow event: ret_heap_top = %p\n", ret);
        vm_print(p->pgtbl);
        return ret;
    } else {
        // 收缩
        uint64 ret = uvm_heap_ungrow(p->pgtbl, cur, (uint32)(cur - new_top));
        if (ret == (uint64)-1) {
            printf("ungrow event: ret_heap_top = %p\n", cur);
            vm_print(p->pgtbl);
            return (uint64)-1;
        }
        p->heap_top = ret;
        printf("ungrow event: ret_heap_top = %p\n", ret);
        vm_print(p->pgtbl);
        return ret;
    }
}

/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    proc_t *p = myproc();
    uint64 begin;
    uint64 len;
    
    // 读取参数
    arg_uint64(0, &begin);
    arg_uint64(1, &len);
    
    // 检查 len 是否是页对齐的
    if (len == 0 || len % PGSIZE != 0) {
        printf("sys_mmap: len is not page-aligned or zero\n");
        return -1;
    }
    
    // 检查 begin 是否是页对齐的（如果不为0）
    if (begin != 0 && begin % PGSIZE != 0) {
        printf("sys_mmap: begin is not page-aligned\n");
        return -1;
    }
    
    uint32 npages = len / PGSIZE;
    
    // 调用 uvm_mmap
    uvm_mmap(begin, npages, PTE_R | PTE_W | PTE_U);
    
    // 如果 begin == 0，需要找到实际分配的地址
    if (begin == 0) {
        // 遍历 mmap 链表找到最新分配的区域
        mmap_region_t *tmp = p->mmap;
        while (tmp != NULL && tmp->npages != npages) {
            tmp = tmp->next;
        }
        if (tmp != NULL) {
            begin = tmp->begin;
        }
    }
    
    printf("sys_mmap: allocated region [%p, %p)\n", begin, begin + len);
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
    
    return begin;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    proc_t *p = myproc();
    uint64 begin;
    uint64 len;
    
    // 读取参数
    arg_uint64(0, &begin);
    arg_uint64(1, &len);
    
    // 检查 len 是否是页对齐的
    if (len == 0 || len % PGSIZE != 0) {
        printf("sys_munmap: len is not page-aligned or zero\n");
        return -1;
    }
    
    // 检查 begin 是否是页对齐的
    if (begin % PGSIZE != 0) {
        printf("sys_munmap: begin is not page-aligned\n");
        return -1;
    }
    
    uint32 npages = len / PGSIZE;
    
    // 调用 uvm_munmap
    uvm_munmap(begin, npages);
    
    printf("sys_munmap: unmapped region [%p, %p)\n", begin, begin + len);
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
    
    return 0;
}


/*
    打印一个字符串
    char *str
    成功返回0
*/
uint64 sys_print_str()
{

}

/*
    打印一个32位整数
    int num
    成功返回0
*/
uint64 sys_print_int()
{

}

/*
    进程复制
    返回子进程的pid
*/
uint64 sys_fork()
{

}

/*
    等待子进程退出
    uint64 addr_exit_state
*/
uint64 sys_wait()
{

}

/*
    进程退出
    int exit_code
    不返回
*/
uint64 sys_exit()
{

}

/*
    让进程睡眠一段时间
    uint32 ntick (1个tick大约0.1秒)
    成功返回0
*/
uint64 sys_sleep()
{

}

/*
    返回当前进程的pid
*/
uint64 sys_getpid()
{

}