#include "mod.h"

/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n = 0;
    while (n < len) {
        // 获取当前用户虚拟地址对应的页表项
        uint64 va = src + n;
        pte_t *pte = vm_getpte(pgtbl, va, false);
        
        // 检查页表项是否有效且可读
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_R) || !(*pte & PTE_U)) {
            panic("uvm_copyin: invalid address");
        }
        
        // 获取物理地址
        uint64 pa = PTE_TO_PA(*pte);
        
        // 计算当前页内偏移
        uint64 offset = va % PGSIZE;
        
        // 计算本次能拷贝的字节数（不能跨页）
        uint64 copy_len = PGSIZE - offset;
        if (copy_len > len - n) {
            copy_len = len - n;
        }
        
        // 从用户物理地址拷贝到内核虚拟地址
        memmove((void *)(dst + n), (void *)(pa + offset), copy_len);
        
        n += copy_len;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n = 0;
    while (n < len) {
        // 获取当前用户虚拟地址对应的页表项
        uint64 va = dst + n;
        pte_t *pte = vm_getpte(pgtbl, va, false);
        
        // 检查页表项是否有效且可写
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_W) || !(*pte & PTE_U)) {
            panic("uvm_copyout: invalid address");
        }
        
        // 获取物理地址
        uint64 pa = PTE_TO_PA(*pte);
        
        // 计算当前页内偏移
        uint64 offset = va % PGSIZE;
        
        // 计算本次能拷贝的字节数（不能跨页）
        uint64 copy_len = PGSIZE - offset;
        if (copy_len > len - n) {
            copy_len = len - n;
        }
        
        // 从内核虚拟地址拷贝到用户物理地址
        memmove((void *)(pa + offset), (void *)(src + n), copy_len);
        
        n += copy_len;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 n = 0;
    bool found_end = false;
    
    while (n < maxlen && !found_end) {
        // 获取当前用户虚拟地址对应的页表项
        uint64 va = src + n;
        pte_t *pte = vm_getpte(pgtbl, va, false);
        
        // 检查页表项是否有效且可读
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_R) || !(*pte & PTE_U)) {
            panic("uvm_copyin_str: invalid address");
        }
        
        // 获取物理地址
        uint64 pa = PTE_TO_PA(*pte);
        
        // 计算当前页内偏移
        uint64 offset = va % PGSIZE;
        
        // 逐字节拷贝，直到遇到 '\0' 或页面结束或达到最大长度
        while (offset < PGSIZE && n < maxlen) {
            char c = *((char *)(pa + offset));
            *((char *)(dst + n)) = c;
            
            if (c == '\0') {
                found_end = true;
                break;
            }
            
            n++;
            offset++;
        }
    }
    
    // 确保字符串以 '\0' 结尾
    if (n >= maxlen) {
        *((char *)(dst + maxlen - 1)) = '\0';
    }
}

/*--------------------part-2: mmap_region相关--------------------*/

// 打印以mmap为首的mmap链
// for debug
void uvm_show_mmaplist(mmap_region_t *mmap)
{
    mmap_region_t *tmp = mmap;
    printf("\nalloced mmap_space:\n");
    if (tmp == NULL)
        printf("empty\n");
    while (tmp != NULL)
    {
        printf("alloced mmap_region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 两个 mmap_region 区域合并
// 注意: 保留一个 释放一个 不操作 next 指针
// 由uvm_mmap调用
__attribute__((unused))
static void mmap_merge(mmap_region_t *mmap_1, mmap_region_t *mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if (keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 寻找一块足够大的区域(len), 作为 mmap_region
// 由uvm_mmap调用(处理begin==0的情况)
// 成功返回begin, 失败返回0
__attribute__((unused))
static uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{
    mmap_region_t *tmp = head_mmap;
    uint64 search_begin = MMAP_BEGIN;
    
    // 从 MMAP_BEGIN 开始扫描，寻找第一个足够大的空隙
    while (tmp != NULL) {
        // 检查 [search_begin, tmp->begin) 是否足够大
        if (tmp->begin >= search_begin + len) {
            // 找到了足够的空间
            *p_tmp_mmap = tmp;
            if (tmp == head_mmap) {
                *p_last_mmap = NULL;
            } else {
                // 需要找到tmp的前一个节点
                mmap_region_t *prev = head_mmap;
                while (prev->next != tmp) {
                    prev = prev->next;
                }
                *p_last_mmap = prev;
            }
            return search_begin;
        }
        
        // 更新搜索起始位置到当前区域的结束位置
        search_begin = tmp->begin + tmp->npages * PGSIZE;
        tmp = tmp->next;
    }
    
    // 检查最后一个区域之后是否有足够空间
    if (search_begin + len <= MMAP_END) {
        *p_last_mmap = NULL;
        // 需要找到链表的最后一个节点
        if (head_mmap != NULL) {
            tmp = head_mmap;
            while (tmp->next != NULL) {
                tmp = tmp->next;
            }
            *p_last_mmap = tmp;
        }
        *p_tmp_mmap = NULL;
        return search_begin;
    }
    
    // 没有找到足够的空间
    return 0;
}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    proc_t *p = myproc();
    uint64 len = npages * PGSIZE;
    
    mmap_region_t *last_mmap = NULL;
    mmap_region_t *tmp_mmap = NULL;
    
    // 如果 begin == 0，自动寻找空间
    if (begin == 0) {
        begin = uvm_mmap_find(p->mmap, len, &last_mmap, &tmp_mmap);
        if (begin == 0) {
            panic("uvm_mmap: no space found");
        }
    } else {
        // 检查边界
        if (begin < MMAP_BEGIN || begin + len > MMAP_END) {
            panic("uvm_mmap: address out of range");
        }
        
        // 找到插入位置
        if (p->mmap == NULL || begin < p->mmap->begin) {
            // 插入到链表头部
            last_mmap = NULL;
            tmp_mmap = p->mmap;
        } else {
            // 找到插入位置
            last_mmap = p->mmap;
            while (last_mmap->next != NULL && last_mmap->next->begin < begin) {
                last_mmap = last_mmap->next;
            }
            tmp_mmap = last_mmap->next;
        }
    }
    
    // 创建新的 mmap_region
    mmap_region_t *new_mmap = mmap_region_alloc();
    new_mmap->begin = begin;
    new_mmap->npages = npages;
    new_mmap->next = tmp_mmap;
    
    // 插入链表
    if (last_mmap == NULL) {
        p->mmap = new_mmap;
    } else {
        last_mmap->next = new_mmap;
    }
    
    // 尝试与前面的节点合并
    if (last_mmap != NULL && last_mmap->begin + last_mmap->npages * PGSIZE == new_mmap->begin) {
        // 合并：扩展 last_mmap，释放 new_mmap
        last_mmap->npages += new_mmap->npages;
        last_mmap->next = new_mmap->next;  // 重要：更新链表指针
        mmap_region_free(new_mmap);
        new_mmap = last_mmap;  // 更新 new_mmap 指向合并后的节点
    }
    
    // 尝试与后面的节点合并
    if (new_mmap->next != NULL && new_mmap->begin + new_mmap->npages * PGSIZE == new_mmap->next->begin) {
        // 合并：扩展 new_mmap，释放 next
        mmap_region_t *next_mmap = new_mmap->next;
        new_mmap->npages += next_mmap->npages;
        new_mmap->next = next_mmap->next;  // 重要：更新链表指针
        mmap_region_free(next_mmap);
    }
    
    // 分配物理页并映射
    for (uint64 va = begin; va < begin + len; va += PGSIZE) {
        void *pa = pmem_alloc(false);
        if (pa == NULL) {
            panic("uvm_mmap: pmem_alloc failed");
        }
        memset(pa, 0, PGSIZE);
        vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE, perm);
    }
}


// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{
    proc_t *p = myproc();
    uint64 end = begin + npages * PGSIZE;
    
    // 检查边界
    if (begin < MMAP_BEGIN || end > MMAP_END) {
        panic("uvm_munmap: address out of range");
    }
    
    mmap_region_t *prev = NULL;
    mmap_region_t *curr = p->mmap;
    
    while (curr != NULL && curr->begin < end) {
        uint64 curr_end = curr->begin + curr->npages * PGSIZE;
        
        // 检查是否有交集
        if (curr_end > begin) {
            // 有交集，需要处理
            uint64 unmap_begin = (curr->begin > begin) ? curr->begin : begin;
            uint64 unmap_end = (curr_end < end) ? curr_end : end;
            
            // 情况1: 完全包含当前节点
            if (begin <= curr->begin && end >= curr_end) {
                // 解除映射并释放物理页
                vm_unmappages(p->pgtbl, curr->begin, curr->npages * PGSIZE, true);
                
                // 从链表中删除
                mmap_region_t *to_free = curr;
                if (prev == NULL) {
                    p->mmap = curr->next;
                    curr = p->mmap;
                } else {
                    prev->next = curr->next;
                    curr = curr->next;
                }
                mmap_region_free(to_free);
                continue;
            }
            // 情况2: 只覆盖前半部分
            else if (begin <= curr->begin && end < curr_end) {
                uint32 unmap_npages = (unmap_end - unmap_begin) / PGSIZE;
                vm_unmappages(p->pgtbl, unmap_begin, unmap_npages * PGSIZE, true);
                
                // 修改当前节点
                curr->npages -= unmap_npages;
                curr->begin = unmap_end;
            }
            // 情况3: 只覆盖后半部分
            else if (begin > curr->begin && end >= curr_end) {
                uint32 unmap_npages = (unmap_end - unmap_begin) / PGSIZE;
                vm_unmappages(p->pgtbl, unmap_begin, unmap_npages * PGSIZE, true);
                
                // 修改当前节点
                curr->npages -= unmap_npages;
            }
            // 情况4: 中间打洞
            else if (begin > curr->begin && end < curr_end) {
                // 需要分裂成两个节点
                uint32 unmap_npages = (unmap_end - unmap_begin) / PGSIZE;
                vm_unmappages(p->pgtbl, unmap_begin, unmap_npages * PGSIZE, true);
                
                // 创建新节点表示后半部分
                mmap_region_t *new_mmap = mmap_region_alloc();
                new_mmap->begin = end;
                new_mmap->npages = (curr_end - end) / PGSIZE;
                new_mmap->next = curr->next;
                
                // 修改当前节点表示前半部分
                curr->npages = (begin - curr->begin) / PGSIZE;
                curr->next = new_mmap;
                
                prev = new_mmap;
                curr = new_mmap->next;
                continue;
            }
        }
        
        prev = curr;
        curr = curr->next;
    }
}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

// '''(TODO, 修改uvm_heap_grow以支持flag的输入)'''

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len, int flag) 
{
    if (len == 0) return cur_heap_top;

    uint64 new_top = cur_heap_top + (uint64)len;

    // 边界检查：堆不应越过 MMAP_BEGIN
    if (new_top > MMAP_BEGIN) {
        return (uint64)-1;
    }

    // 仅按页粒度分配新页面
    uint64 page_start = (cur_heap_top + PGSIZE - 1) & ~(PGSIZE - 1); // 向上取整
    uint64 page_end   = (new_top      + PGSIZE - 1) & ~(PGSIZE - 1); // 向上取整

    int perm = flag | PTE_U;
    for (uint64 va = page_start; va < page_end; va += PGSIZE) {
        void *pa = pmem_alloc(false);
        if (pa == NULL) {
            // 回滚已分配部分
            for (uint64 unmap = page_start; unmap < va; unmap += PGSIZE) {
                vm_unmappages(pgtbl, unmap, PGSIZE, true);
            }
            return (uint64)-1;
        }
        vm_mappages(pgtbl, va, (uint64)pa, PGSIZE, perm);
    }

    return new_top;
}

// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    if (len == 0) return cur_heap_top;

    uint64 new_top = (cur_heap_top > len) ? (cur_heap_top - (uint64)len) : 0;

    // 计算要释放的整页区间 [free_start, free_end)
    uint64 free_start = (new_top + PGSIZE - 1) & ~(PGSIZE - 1); // 向上取整
    uint64 free_end   = (cur_heap_top + PGSIZE - 1) & ~(PGSIZE - 1);

    // 下界：不释放代码页（保护 USER_BASE 处的代码页）
    uint64 min_heap_base = USER_BASE + PGSIZE; // 代码页结束位置
    if (free_start < min_heap_base) free_start = min_heap_base;

    for (uint64 va = free_start; va < free_end; va += PGSIZE) {
        vm_unmappages(pgtbl, va, PGSIZE, true);
    }

    return new_top;
}

// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr)
{
    // 合法性检查：栈向低地址增长；不得越过 MMAP_END；不得超过 TRAPFRAME
    if (fault_addr >= TRAPFRAME || fault_addr < MMAP_END) {
        return (uint64)-1;
    }

    // 当前已映射的栈区间：[stack_low, stack_top)
    uint64 stack_top = TRAPFRAME; // 固定
    uint64 stack_low = stack_top - old_ustack_npage * PGSIZE;

    // 如果 fault 在已映射区间内，无需扩展
    if (fault_addr >= stack_low && fault_addr < stack_top) {
        return old_ustack_npage;
    }

    // 需要扩展到包含 fault 所在页
    uint64 need_low = fault_addr & ~(PGSIZE - 1); // 向下取整到页

    // 不得越过 MMAP_END
    if (need_low < MMAP_END) {
        need_low = MMAP_END;
    }

    // 计算扩展后的页数
    uint64 new_npage = (stack_top - need_low + PGSIZE - 1) / PGSIZE;
    if (new_npage <= old_ustack_npage) {
        return old_ustack_npage;
    }

    // 为 [stack_low - add_size, stack_low) 区间分配页面
    uint64 add_pages = new_npage - old_ustack_npage;
    uint64 map_begin = stack_low - add_pages * PGSIZE;

    if (map_begin < MMAP_END) {
        return (uint64)-1;
    }

    for (uint64 va = map_begin; va < stack_low; va += PGSIZE) {
        void *pa = pmem_alloc(false);
        if (pa == NULL) {
            // 回滚
            for (uint64 unmap = map_begin; unmap < va; unmap += PGSIZE) {
                vm_unmappages(pgtbl, unmap, PGSIZE, true);
            }
            return (uint64)-1;
        }
        vm_mappages(pgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    }

    return new_npage;
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    // 遍历当前级页表的所有页表项
    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        
        // 如果页表项有效
        if (pte & PTE_V) {
            uint64 child_pa = PTE_TO_PA(pte);
            
            // 如果不是叶子页表项（level > 1），递归释放下一级页表
            if (level > 1) {
                destroy_pgtbl((pgtbl_t)child_pa, level - 1);
            } else {
                // level == 1, 这是叶子节点,需要释放它指向的物理页
                // 但要注意 trampoline 不能释放(flags & PTE_U == 0)
                if (pte & PTE_U) {
                    // 用户页面,可以释放
                    pmem_free(child_pa, false);
                }
                // 内核页面(如 trampoline)不释放,因为是共享的
            }
        }
    }
    
    // 释放当前页表本身占用的物理页
    pmem_free((uint64)pgtbl, true);
}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    // 注意: 在测试场景中,trapframe 可能是共享的,所以不释放物理页
    // 只解除映射即可
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, false);  // 不释放物理页
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false); // 不释放,因为所有进程共用
    destroy_pgtbl(pgtbl, 3);
}

// 连续虚拟空间的复制
// 在uvm_copy_pgtbl中使用
__attribute__((unused))
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t *pte;

    for (va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char *)page, (const char *)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 拷贝页表 (拷贝并不包括 trapframe 和 trampoline)
// 拷贝的页表管理的物理页是原来页表的复制品
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap)
{
    // 1. 复制用户代码页 (USER_BASE)
    copy_range(old, new, USER_BASE, USER_BASE + PGSIZE);
    
    // 2. 复制堆空间 [USER_BASE + PGSIZE, heap_top)
    if (heap_top > USER_BASE + PGSIZE) {
        uint64 heap_start = USER_BASE + PGSIZE;
        // 向上对齐到页边界
        uint64 heap_end = (heap_top + PGSIZE - 1) & ~(PGSIZE - 1);
        copy_range(old, new, heap_start, heap_end);
    }
    
    // 3. 复制 mmap 区域
    mmap_region_t *tmp = mmap;
    while (tmp != NULL) {
        uint64 mmap_start = tmp->begin;
        uint64 mmap_end = tmp->begin + tmp->npages * PGSIZE;
        copy_range(old, new, mmap_start, mmap_end);
        tmp = tmp->next;
    }
    
    // 4. 复制用户栈 [TRAPFRAME - ustack_npage * PGSIZE, TRAPFRAME)
    uint64 ustack_start = TRAPFRAME - ustack_npage * PGSIZE;
    copy_range(old, new, ustack_start, TRAPFRAME);
}
