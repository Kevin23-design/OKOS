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
    return -1;
}

/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    return 0;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    return 0;
}