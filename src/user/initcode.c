// test-4: cache thrashing
#include "sys.h"

#define N_BUFFER 8
#define TEST_COUNT 16 // 2 * N_BUFFER，确保发生大规模置换
#define BLOCK_BASE 6000
#define PGSIZE 4096

int main()
{
    char data[PGSIZE];
    char read_buf[PGSIZE];
    unsigned long long buf_ids[TEST_COUNT];

    syscall(SYS_print_str, "Test-4: Cache Thrashing Start\n");

    /* 
       阶段1: 连续写入 16 个块
       由于 N_BUFFER=8，写入第 9 个块时，必然会踢出第 1 个块。
       如果 Buffer Cache 实现了正确的“脏回写”策略，第 1 个块的数据应该被自动保存到磁盘。
    */
    syscall(SYS_print_str, "Step 1: Writing 16 blocks (Force Eviction)...\n");
    for (int i = 0; i < TEST_COUNT; i++) {
        // 构造标记数据: "Block-A", "Block-B", ...
        for(int j=0; j<PGSIZE; j++) data[j] = 0;
        data[0] = 'B'; data[1] = 'l'; data[2] = 'o'; data[3] = 'c'; data[4] = 'k'; data[5] = '-';
        data[6] = 'A' + i; 
        
        buf_ids[i] = syscall(SYS_get_block, BLOCK_BASE + i);
        syscall(SYS_write_block, buf_ids[i], data);
        syscall(SYS_put_block, buf_ids[i]); // 引用计数归零，允许被置换
    }

    /* 
       阶段2: 彻底清空缓存
       这会强制剩余的 8 个块也写回磁盘，并释放所有物理页。
       接下来的读取操作将全部触发 Cache Miss，必须从磁盘读数据。
    */
    syscall(SYS_print_str, "Step 2: Flush all buffers.\n");
    syscall(SYS_flush_buffer, N_BUFFER); 

    /* 
       阶段3: 验证数据回读
       检查之前被“踢出”的数据是否真的持久化到了磁盘上。
    */
    syscall(SYS_print_str, "Step 3: Verify data reload...\n");
    int pass = 1;
    for (int i = 0; i < TEST_COUNT; i++) {
        unsigned long long bid = syscall(SYS_get_block, BLOCK_BASE + i);
        syscall(SYS_read_block, bid, read_buf);
        syscall(SYS_put_block, bid);

        // 验证标记位
        if (read_buf[6] != 'A' + i) {
            pass = 0;
            syscall(SYS_print_str, "Fail at index: ");
            // 简单的错误提示 (假设没有 printf %d)
            char c[2] = {'0'+i/10, '0'+i%10}; // 简易 hex/decimal 打印
            if(i<10) { c[0] = '0'+i; c[1]='\0'; }
            syscall(SYS_print_str, c); 
            syscall(SYS_print_str, "\n");
        }
    }

    if (pass) {
        syscall(SYS_print_str, "Test-4 PASSED: All blocks persisted and reloaded.\n");
    } else {
        syscall(SYS_print_str, "Test-4 FAILED: Data lost during eviction.\n");
    }

    while(1);
}
