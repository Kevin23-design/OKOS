// in initcode.c
#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - (16 * 256 + 2) * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 64 * 256 * PGSIZE)

int main()
{
    // —— 功能性测试：验证跨页读写与 copyin/copyout ——
    // 1) 申请两页匿名映射（自动分配），返回起始地址
    unsigned long long mm = (unsigned long long)syscall(SYS_mmap, 0, 2 * PGSIZE);

    // 2) 在跨页边界处写入 8 个 int，随后让内核使用 sys_copyin 打印出来
    //    基址选择在第1页末尾，使得这8个元素跨越两页
    volatile int *cross = (int *)(mm + PGSIZE - 8 * sizeof(int));
    for (int i = 0; i < 8; i++) {
        cross[i] = 1000 + i; // 1000..1007
    }
    syscall(SYS_copyin, (unsigned long long)cross, (unsigned long)8);

    // 3) 让内核往该地址写入 5 个整数（1..5），再用 sys_copyin 读回并打印验证
    syscall(SYS_copyout, (unsigned long long)cross);
    syscall(SYS_copyin, (unsigned long long)cross, (unsigned long)5);

    // 4) 解除第二页映射，验证页表变化（仅通过内核侧 vm_print/mmaplist 打印）
    syscall(SYS_munmap, mm + PGSIZE, PGSIZE);

    // 5) 再次在原位置映射一页，随后用 sys_copyout 写入并用 sys_copyin 打印，验证恢复可用
    syscall(SYS_mmap, mm + PGSIZE, PGSIZE);
    volatile int *pg2 = (int *)(mm + PGSIZE);
    syscall(SYS_copyout, (unsigned long long)pg2);
    syscall(SYS_copyin, (unsigned long long)pg2, (unsigned long)5);

    while(1);
    return 0;
}