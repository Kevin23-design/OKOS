#include "mod.h"

/*
    测试: 从用户空间传入一个int类型的数组
    uint64 addr 数组起始地址
    uint32 len  元素数量
    成功返回0
*/
uint64 sys_copyin()
{
    proc_t *p = myproc();
    uint64 addr;
    uint32 len;
    
    // 读取参数
    arg_uint64(0, &addr);
    arg_uint32(1, &len);
    
    // 分配内核缓冲区
    int *kernel_buf = (int *)pmem_alloc(true);
    if (kernel_buf == NULL) {
        return -1;
    }
    
    // 从用户空间拷贝数据到内核空间
    uvm_copyin(p->pgtbl, (uint64)kernel_buf, addr, len * sizeof(int));
    
    // 打印数据验证（按照实验要求的格式）
    for (uint32 i = 0; i < len; i++) {
        printf("get a number from user: %d\n", kernel_buf[i]);
    }
    
    // 释放内核缓冲区
    pmem_free((uint64)kernel_buf, true);
    
    return 0;
}

/*
    测试: 向用户空间传出一个int类型的数组
    uint64 addr 数组起始地址
    成功返回拷贝的元素数量
*/
uint64 sys_copyout()
{
    proc_t *p = myproc();
    uint64 addr;
    
    // 读取参数
    arg_uint64(0, &addr);
    
    // 准备要发送的数据
    int kernel_data[5] = {1, 2, 3, 4, 5};
    uint32 len = 5;
    
    // 从内核空间拷贝数据到用户空间
    uvm_copyout(p->pgtbl, addr, (uint64)kernel_data, len * sizeof(int));
    
    return len;
}

/*
    测试: 从用户空间传入一个字符串
    uint64 addr 字符串起始地址
    成功返回0
*/
uint64 sys_copyinstr()
{
    proc_t *p = myproc();
    uint64 addr;
    
    // 读取参数
    arg_uint64(0, &addr);
    
    // 分配内核缓冲区
    char *kernel_buf = (char *)pmem_alloc(true);
    if (kernel_buf == NULL) {
        return -1;
    }
    
    // 从用户空间拷贝字符串到内核空间
    uvm_copyin_str(p->pgtbl, (uint64)kernel_buf, addr, PGSIZE);
    
    // 打印字符串验证（按照实验要求的格式）
    printf("get string for user: %s\n", kernel_buf);
    
    // 释放内核缓冲区
    pmem_free((uint64)kernel_buf, true);
    
    return 0;
}

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
    测试页表复制与销毁
    成功返回0, 失败返回-1
*/
uint64 sys_test_pgtbl()
{
    proc_t *p = myproc();
    
    printf("\n=== [Kernel] Testing page table copy and destroy ===\n");
    
    // 1. 创建新页表
    printf("\n[1] Creating new page table...\n");
    pgtbl_t new_pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (new_pgtbl == NULL) {
        printf("Failed to create new page table\n");
        return -1;
    }
    printf("New page table created at %p\n", new_pgtbl);
    
    // 2. 复制页表
    printf("\n[2] Copying page table content...\n");
    printf("Source page table: %p\n", p->pgtbl);
    printf("  - heap_top: %p\n", p->heap_top);
    printf("  - ustack_npage: %d\n", p->ustack_npage);
    printf("  - mmap regions: ");
    if (p->mmap == NULL) {
        printf("none\n");
    } else {
        printf("\n");
        uvm_show_mmaplist(p->mmap);
    }
    
    uvm_copy_pgtbl(p->pgtbl, new_pgtbl, p->heap_top, p->ustack_npage, p->mmap);
    printf("Page table copied successfully\n");
    
    // 3. 验证复制是否正确（通过打印新页表的内容）
    printf("\n[3] Verifying copied page table...\n");
    printf("New page table content:\n");
    vm_print(new_pgtbl);
    
    // 4. 销毁新页表
    printf("\n[4] Destroying copied page table...\n");
    uvm_destroy_pgtbl(new_pgtbl);
    printf("Page table destroyed successfully\n");
    
    printf("\n=== [Kernel] Page table test completed ===\n\n");
    
    return 0;
}