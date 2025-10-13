#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"

// 简单的SMP启动屏障：CPU0完成全局初始化后，其他CPU再继续
volatile static int started = 0;

int main()
{
    int id = mycpuid();

    if (id == 0) {
        // CPU0：执行一次性的全局初始化
        print_init();         // 串口与printf
        pmem_init();          // 物理内存
        kvm_init();           // 内核页表填充
        trap_kernel_init();   // PLIC与系统时钟创建（时钟对象）
        // printf("cpu %d: init done.\n", id);

        // 发布启动完成信号，让其他CPU继续
        __sync_synchronize();
        started = 1;
    } else {
        // 其他CPU：等待CPU0初始化完成
        while (started == 0) { /* spin */ }
        __sync_synchronize();
    }

    // 每个CPU都需要切到内核页表并开启自己的S态trap入口
    kvm_inithart();           // 切换到内核页表
    trap_kernel_inithart();   // 使能S态trap入口与SIE分开关

    printf("cpu %d is booting!\n", id);

    // 简单驻留
    while (1) { }
}