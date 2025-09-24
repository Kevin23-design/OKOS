#include "arch/mod.h"
#include "lib/mod.h"
#include "lock/mod.h"

volatile static int started = 0;
volatile static int sum = 0;
static spinlock_t sum_lock;

int main()
{
    int cpuid = r_tp();
    
    if(cpuid == 0) {
        // 初始化打印系统
        print_init();
        // 初始化自旋锁
        spinlock_init(&sum_lock, "sum_lock");
        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;
        for(int i = 0; i < 1000000; i++) {
            spinlock_acquire(&sum_lock);
            sum++;
            spinlock_release(&sum_lock);
        }
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    } else {
        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        for(int i = 0; i < 1000000; i++) {
            spinlock_acquire(&sum_lock);
            sum++;
            spinlock_release(&sum_lock);
        }
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }
    
    // 无限循环，保持系统运行
    while (1) {
        // 可以在这里添加其他功能
    }
    
    return 0;
}