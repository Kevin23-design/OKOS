# OK Kernel
## lab1
### 课程作业
对于**子任务一：进图main函数**，理解程序从entry.S->start.c->main.c的启动结构
- entry.S负责汇编层面的双核启动和栈设置
- start.c完成早期硬件初始化
- main.c作为系统主入口点

对于**子任务2：通过printf输出信息**，基于uart_putc_sync()实现字符输出并解析格式化字符串，支持基本的数据类型转换（%d, %s, %x等），同时使用自旋锁，实现对于串口作为共享资源，多核同时调用printf会导致输出混乱的问题

最后的实验结果截图如下：
![alt text](pictures/lab1实验结果截图01.png)

### 课后作业
#### 并行加法
这个问题是由于两个CPU核心在没有同步机制的情况下同时对共享变量 sum 进行写操作，导致了数据竞争（Race Condition）。

为了解决这个问题，引入锁机制来保护临界区（Critical Section），也就是 sum++ 这个操作。当一个CPU核心进入临界区时，它会先获取锁，此时其他核心如果也想进入临界区，就必须等待，直到该核心释放锁。
修改后的代码如下：
```c
volatile static int started = 0;

volatile static int sum = 0;
// 定义一个锁来保护sum
static spinlock_t sum_lock;

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0) {
        print_init();
        // 初始化锁
        spinlock_init(&sum_lock, "sum_lock");
        printf("cpu %d is booting!\n", cpuid);        
        __sync_synchronize();
        started = 1;
        for(int i = 0; i < 1000000; i++){
            // 在修改sum前获取锁
            spinlock_acquire(&sum_lock);
            sum++;
            // 修改sum后释放锁
            spinlock_release(&sum_lock);
        }
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    } else {
        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        for(int i = 0; i < 1000000; i++){
            // 在修改sum前获取锁
            spinlock_acquire(&sum_lock);
            sum++;
            // 修改sum后释放锁
            spinlock_release(&sum_lock);
        }
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }   
    while (1);    
}  
```
实验结果如下：
![alt text](pictures/lab1实验结果截图02.png)

**锁的粒度过细**：如果像现在这样，在 for 循环内部，每次 sum++ 操作都进行加锁和解锁（acquire -> sum++ -> release）。

优点：能确保 sum 的值正确累加，因为保护了最小的临界区。

缺点：加锁和解锁本身是有开销的。在循环中频繁地获取和释放锁会导致大量的性能损耗，降低了并行计算的效率。两个 CPU 大部分时间可能都在等待对方释放锁。

**锁的粒度过粗**：如果将整个 for 循环作为临界区，在循环开始前加锁，循环结束后解锁（acquire -> for 循环 -> release）。

优点：减少了加锁和解锁的次数，开销较小。
缺点：一个 CPU 会持有锁并执行完整个百万次的循环，而另一个 CPU 在此期间会一直自旋等待。这完全失去了并行计算的意义，程序退化成了串行执行，总耗时会更长。

### 并行输出
首先移除 printf 中的锁，然后在main.c中设计并发打印场景：
```c

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0) {
        print_init();
        // 初始化锁
        // spinlock_init(&sum_lock, "sum_lock");
        // printf("cpu %d is booting!\n", cpuid);        
        __sync_synchronize();
        started = 1;
        for(int i = 0; i < 100; i++){
            printf("aaaaaaaaaa");
        }
    } else {
        while(started == 0);
        __sync_synchronize();
        for(int i = 0; i < 100; i++){
            printf("bbbbbbbbbb");
        }
    }   
    while (1);    
}  
```
实验结果如下图：
![alt text](pictures/lab1实验结果截图03.png)

---