## test-01
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
cpu 1 is booting!

proczero: hello world!
```
此测试输出正确。

## test-02
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
cpu 1 is booting!
level-1!
level-2!
level-2!

unexpected exception: Instruction page fault
trap_id = 0, sepc = 0x00000000803b2000
unexpected exception: Instruction page fault
trap_id = 12, sepc = 0x00000000803bd000, stval = 0x00000000803bd000
panic! trap_kernel_handler
```
输出错误。

正确输出如下：(可能有所省略只贴出前几行输出)
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
proc 1 is running...
cpu 1 is booting!
level-1!
proc 2 is running...
level-2!
level-2!
level-3!
level-3!
proc 3 is running...
level-3!
proc 4 is running...
level-3!
proc 1 is running...
proc 2 is running...
proc 3 is running...
proc 4 is running...
proc 1 is running...
proc 2 is running...
proc 3 is running...
proc 4 is running...
proc 1 is running...
proc 2 is running...
proc 3 is running...
proc 4 is running...
proc 1 is running...
proc 2 is running...
proc 3 is running...
proc 4 is running...
```


## test-03
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
cpu 1 is booting!
sys_mmap: allocated region [0x0000003ffaffe000, 0x0000003ffafff000)

alloced mmap_space:
alloced mmap_region: 0x0000003ffaffe000 ~ 0x0000003ffafff000
level-2 pgtbl: pa = 0x00000000803c2000
.. level-1 pgtbl 0: pa = 0x00000000803bf000
.. .. level-0 pgtbl 0: pa = 0x00000000803be000
.. .. .. physical page 1: pa = 0x0000000087fde000 flags = 95
.. level-1 pgtbl 255: pa = 0x00000000803c1000
.. .. level-0 pgtbl 471: pa = 0x00000000803bd000
.. .. .. physical page 510: pa = 0x0000000087fdd000 flags = 23
.. .. level-0 pgtbl 511: pa = 0x00000000803c0000
.. .. .. physical page 509: pa = 0x0000000087fdf000 flags = 23
.. .. .. physical page 510: pa = 0x00000000803c3000 flags = 199
.. .. .. physical page 511: pa = 0x0000000080004000 flags = 75

look event: ret_heap_top = 0x0000000000002000
level-2 pgtbl: pa = 0x00000000803c2000
.. level-1 pgtbl 0: pa = 0x00000000803bf000
.. .. level-0 pgtbl 0: pa = 0x00000000803be000
.. .. .. physical page 1: pa = 0x0000000087fde000 flags = 95
.. level-1 pgtbl 255: pa = 0x00000000803c1000
.. .. level-0 pgtbl 471: pa = 0x00000000803bd000
.. .. .. physical page 510: pa = 0x0000000087fdd000 flags = 215
.. .. level-0 pgtbl 511: pa = 0x00000000803c0000
.. .. .. physical page 509: pa = 0x0000000087fdf000 flags = 23
.. .. .. physical page 510: pa = 0x00000000803c3000 flags = 199
.. .. .. physical page 511: pa = 0x0000000080004000 flags = 75
grow event: ret_heap_top = 0x0000000000003000
level-2 pgtbl: pa = 0x00000000803c2000
.. level-1 pgtbl 0: pa = 0x00000000803bf000
.. .. level-0 pgtbl 0: pa = 0x00000000803be000
.. .. .. physical page 1: pa = 0x0000000087fde000 flags = 95
.. .. .. physical page 2: pa = 0x0000000087fdc000 flags = 23
.. level-1 pgtbl 255: pa = 0x00000000803c1000
.. .. level-0 pgtbl 471: pa = 0x00000000803bd000
.. .. .. physical page 510: pa = 0x0000000087fdd000 flags = 215
.. .. level-0 pgtbl 511: pa = 0x00000000803c0000
.. .. .. physical page 509: pa = 0x0000000087fdf000 flags = 23
.. .. .. physical page 510: pa = 0x00000000803c3000 flags = 199
.. .. .. physical page 511: pa = 0x0000000080004000 flags = 75

--------test begin--------
child proc: hello!
MMAP_REGION
HEAP_REGION
STACK_REGION

panic! acquire
```
输出错误。

正确输出：
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
cpu 1 is booting!

--------test begin--------
child proc: hello!
MMAP_REGION
HEAP_REGION
STACK_REGION

parent proc: hello!
num = 2
good boy!
--------test end----------
```


## test-04  
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
cpu 1 is booting!
panic! uvm_copyin_str: invalid address
```
输出错误。

正确输出：
```bash
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu 0 is booting!
cpu 1 is booting!
Ready to sleep!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is sleeping!
proc 2 is wakeup!
Ready to exit!
proc l is wakeup!
proc l is wakeup!
Child exit!
```