#include "arch/mod.h"
#include "lib/mod.h"

int main()
{
    int cpuid = r_tp();
    
    // 初始化打印系统
    print_init();
    
    // 输出启动信息
    printf("cpu %d is booting!\n", cpuid);
    
    // 无限循环，保持系统运行
    while (1) {
        // 可以在这里添加其他功能
    }
    
    return 0;
}