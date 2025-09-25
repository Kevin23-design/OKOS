#include "arch/mod.h"
#include "lib/mod.h"

// CPU1 需等待 CPU0 完成早期初始化（如 UART、printf 锁等）
volatile static int started = 0;

int main()
{
    int cpuid = r_tp();
    
    if (cpuid == 0) {
        // CPU 0 负责初始化
        print_init();
        // 发布初始化完成的可见性
        __sync_synchronize();
        started = 1;
        printf("cpu %d is booting!\n", cpuid);
    } else {
        // CPU 1 等待初始化完成
        while (started == 0)
            ;
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
    }
    
    while (1) {
        // 保持运行状态
    }
}