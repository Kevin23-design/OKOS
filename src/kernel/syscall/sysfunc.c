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
        return cur;
    }

    if (new_top == cur) {
        return cur;
    }

    if (new_top > cur) {
        // 增长
        uint64 ret = uvm_heap_grow(p->pgtbl, cur, (uint32)(new_top - cur));
        if (ret == (uint64)-1) {
            return (uint64)-1;
        }
        p->heap_top = ret;
        return ret;
    } else {
        // 收缩
        uint64 ret = uvm_heap_ungrow(p->pgtbl, cur, (uint32)(cur - new_top));
        if (ret == (uint64)-1) {
            return (uint64)-1;
        }
        p->heap_top = ret;
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
    
    return 0;
}


/*
    打印一个字符串
    char *str
    成功返回0
*/
uint64 sys_print_str()
{
    uint64 user_str;
    arg_uint64(0, &user_str);
    
    // 从用户空间拷贝字符串到内核空间
    char buf[256];
    uvm_copyin_str(myproc()->pgtbl, (uint64)buf, user_str, 256);
    
    printf("%s", buf);
    return 0;
}

/*
    打印一个32位整数
    int num
    成功返回0
*/
uint64 sys_print_int()
{
    uint32 num;
    arg_uint32(0, &num);
    
    printf("%d", num);
    return 0;
}

/*
    进程复制
    返回子进程的pid
*/
uint64 sys_fork()
{
    return proc_fork();
}

/*
    等待子进程退出
    uint64 addr_exit_state
*/
uint64 sys_wait()
{
    uint64 user_addr;
    arg_uint64(0, &user_addr);
    
    return proc_wait(user_addr);
}

/*
    进程退出
    int exit_code
    不返回
*/
uint64 sys_exit()
{
    uint32 exit_code;
    arg_uint32(0, &exit_code);
    
    proc_exit((int)exit_code);
    
    // 永远不会执行到这里
    return 0;
}

/*
    进程睡眠
    uint32 ntick (1个tick大约0.1秒)
    成功返回0
*/
uint64 sys_sleep()
{
    uint32 ntick;
    arg_uint32(0, &ntick);
    
    timer_wait(ntick);
    return 0;
}

/*
    返回当前进程的pid
*/
uint64 sys_getpid()
{
    return myproc()->pid;
}

uint64 sys_alloc_block()
{
    uint32 block = bitmap_alloc_block();
    if (block == BLOCK_NUM_UNUSED)
        return (uint64)-1;
    return block;
}

uint64 sys_free_block()
{
    uint32 block_num;
    arg_uint32(0, &block_num);
    bitmap_free_block(block_num);
    return 0;
}

uint64 sys_alloc_inode()
{
    uint32 inode = bitmap_alloc_inode();
    if (inode == (uint32)-1)
        return (uint64)-1;
    return inode;
}

uint64 sys_free_inode()
{
    uint32 inode_num;
    arg_uint32(0, &inode_num);
    bitmap_free_inode(inode_num);
    return 0;
}

uint64 sys_show_bitmap()
{
    uint32 which;
    arg_uint32(0, &which);
    bitmap_print(which == 0);
    return 0;
}

static inline buffer_t *buffer_from_handle(uint64 handle, const char *who)
{
    buffer_t *buf = (buffer_t *)handle;
    assert(buf != NULL, who);
    return buf;
}

uint64 sys_get_block()
{
    uint32 block_num;
    arg_uint32(0, &block_num);
    buffer_t *buf = buffer_get(block_num);
    return (uint64)buf;
}

uint64 sys_put_block()
{
    uint64 handle;
    arg_uint64(0, &handle);
    buffer_t *buf = buffer_from_handle(handle, "sys_put_block: invalid handle");
    buffer_put(buf);
    return 0;
}

uint64 sys_read_block()
{
    uint64 handle;
    uint64 user_dst;
    arg_uint64(0, &handle);
    arg_uint64(1, &user_dst);

    buffer_t *buf = buffer_from_handle(handle, "sys_read_block: invalid handle");
    assert(sleeplock_holding(&buf->slk), "sys_read_block: buffer unlocked");

    proc_t *p = myproc();
    uvm_copyout(p->pgtbl, user_dst, (uint64)buf->data, BLOCK_SIZE);
    return 0;
}

uint64 sys_write_block()
{
    uint64 handle;
    uint64 user_src;
    arg_uint64(0, &handle);
    arg_uint64(1, &user_src);

    buffer_t *buf = buffer_from_handle(handle, "sys_write_block: invalid handle");
    assert(sleeplock_holding(&buf->slk), "sys_write_block: buffer unlocked");

    proc_t *p = myproc();
    uvm_copyin(p->pgtbl, (uint64)buf->data, user_src, BLOCK_SIZE);
    buffer_write(buf);
    return 0;
}

uint64 sys_show_buffer()
{
    buffer_print_info();
    return 0;
}

uint64 sys_flush_buffer()
{
    uint32 count;
    arg_uint32(0, &count);
    return buffer_freemem(count);
}

/* 构建fd->file的映射, 返回fd */
static uint32 alloc_fd(file_t *file)
{
    proc_t *p = myproc();
    for (uint32 i = 0; i < N_OPEN_FILE_PER_PROC; i++)
    {
        if (p->open_file[i] == NULL) {
            p->open_file[i] = file;
            return i;
        }
    }
    return -1;
}

/*
    执行ELF文件以替换当前进程的内容
    char *path
    char **argv
    成功返回argc, 失败返回-1
*/
uint64 sys_exec()
{

}

/*
    打开或创建文件
    char *path
    uint32 open_mode
    成功返回fd, 失败返回-1
*/
uint64 sys_open()
{

}

/*
    关闭文件
    uint32 fd
    成功返回0, 失败返回-1
*/
uint64 sys_close()
{

}

/*
    读取文件内容
    uint32 fd
    uint32 len
    uint64 addr
    成功返回读到的字节数, 失败返回0
*/
uint64 sys_read()
{

}

/*
    写入文件内容
    uint32 fd
    uint32 len
    uint64 addr
    成功返回写入的字节数, 失败返回0
*/
uint64 sys_write()
{

}

/*
    调整读写指针位置
    uint32 fd
    uint32 offset
    uint32 flag
    成功返回新的偏移量, 失败返回-1
*/
uint64 sys_lseek()
{

}

/*
    复制文件控制权
    uinr32 fd
    成功返回new_fd, 失败返回-1
*/
uint64 sys_dup()
{

}

/*
    获取文件信息
    uint32 fd
    uint64 addr
    成功返回0, 失败返回-1
*/
uint64 sys_fstat()
{

}

/*
    获取目录中的所有目录项信息
    uint32 fd
    uint64 addr
    uint32 buffer_len
    成功返回读到的字节数, 失败返回-1
*/
uint64 sys_get_dentries()
{

}

/*
    创建目录
    char *path
    成功返回0, 失败返回-1
*/
uint64 sys_mkdir()
{

}

/*
    修改当前工作目录
    char *new_path
    成功返回0, 失败返回-1
*/
uint64 sys_chdir()
{

}

/*
    打印当前工作目录的绝对路径
    成功返回0, 失败返回-1
*/
uint64 sys_print_cwd()
{

}

/*
    新建链接
    char *old_path
    char *new_path
    成功返回0, 失败返回-1
*/
uint64 sys_link()
{

}

/*
    删除链接 (可能触发删除文件)
    char *path
    成功返回0, 失败返回-1
*/
uint64 sys_unlink()
{

}