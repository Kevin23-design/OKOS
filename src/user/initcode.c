// test-1: 多层fork树测试
#include "sys.h"

int main()
{
    syscall(SYS_print_str, "\n=== Fork Tree Test ===\n");
    
    int pid = syscall(SYS_getpid);
    syscall(SYS_print_str, "Root process PID: ");
    syscall(SYS_print_int, pid);
    syscall(SYS_print_str, "\n");
    
    // 创建第一个子进程
    int child1 = syscall(SYS_fork);
    if (child1 == 0) {
        // 第一个子进程
        pid = syscall(SYS_getpid);
        syscall(SYS_print_str, "Child1 PID: ");
        syscall(SYS_print_int, pid);
        syscall(SYS_print_str, "\n");
        
        // 子进程再fork,创建孙子进程
        int grandchild = syscall(SYS_fork);
        if (grandchild == 0) {
            // 孙子进程
            pid = syscall(SYS_getpid);
            syscall(SYS_print_str, "Grandchild PID: ");
            syscall(SYS_print_int, pid);
            syscall(SYS_print_str, "\n");
            syscall(SYS_exit, 10);
        } else {
            // Child1等待孙子进程
            int exit_code = 0;
            int waited_pid = syscall(SYS_wait, &exit_code);
            syscall(SYS_print_str, "Child1 reaped PID: ");
            syscall(SYS_print_int, waited_pid);
            syscall(SYS_print_str, " with exit code: ");
            syscall(SYS_print_int, exit_code);
            syscall(SYS_print_str, "\n");
            syscall(SYS_exit, 20);
        }
    }
    
    // 创建第二个子进程
    int child2 = syscall(SYS_fork);
    if (child2 == 0) {
        // 第二个子进程
        pid = syscall(SYS_getpid);
        syscall(SYS_print_str, "Child2 PID: ");
        syscall(SYS_print_int, pid);
        syscall(SYS_print_str, "\n");
        syscall(SYS_exit, 30);
    }
    
    // 根进程等待所有子进程
    syscall(SYS_print_str, "Root waiting for children...\n");
    
    int exit_code1 = 0;
    int waited_pid1 = syscall(SYS_wait, &exit_code1);
    syscall(SYS_print_str, "Root reaped PID: ");
    syscall(SYS_print_int, waited_pid1);
    syscall(SYS_print_str, " with exit code: ");
    syscall(SYS_print_int, exit_code1);
    syscall(SYS_print_str, "\n");
    
    int exit_code2 = 0;
    int waited_pid2 = syscall(SYS_wait, &exit_code2);
    syscall(SYS_print_str, "Root reaped PID: ");
    syscall(SYS_print_int, waited_pid2);
    syscall(SYS_print_str, " with exit code: ");
    syscall(SYS_print_int, exit_code2);
    syscall(SYS_print_str, "\n");
    
    syscall(SYS_print_str, "=== Fork Tree Test Complete ===\n");
    
    while (1);
}