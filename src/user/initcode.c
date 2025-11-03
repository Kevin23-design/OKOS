// in initcode.c
#include "sys.h"

int main()
{
    int L[5];
    char* s = "hello, world"; 
    syscall(SYS_copyout, (long)L);
    syscall(SYS_copyin, (long)L, 5);
    syscall(SYS_copyinstr, (long)s);
    while(1);
    return 0;
}