#include "mod.h"

#include "../proc/method.h"
#include "../mem/method.h"

extern super_block_t sb;

/* 内存中的inode资源集合 */
static inode_t inode_cache[N_INODE];
static spinlock_t lk_inode_cache;

/* inode_cache初始化 */
void inode_init()
{
	spinlock_init(&lk_inode_cache, "inode_cache");
	for (int i = 0; i < N_INODE; i++) {
		inode_t *ip = &inode_cache[i];
		memset(&ip->disk_info, 0, sizeof(inode_disk_t));
		ip->valid_info = false;
		ip->inode_num = INVALID_INODE_NUM;
		ip->ref = 0;
		sleeplock_init(&ip->slk, "inode");
	}
} 

/*--------------------关于inode->index的增删查操作-----------------*/

/* 
	供free_data_blocks使用
	递归删除inode->index中的一个元素
	返回删除过程中是否遇到空的block_num (文件末尾)
*/
static bool __free_data_blocks(uint32 block_num, uint32 level)
{
	if (block_num == 0)
		return true;

	if (level == 0) {
		bitmap_free_block(block_num);
		return false;
	}

	buffer_t *buf = buffer_get(block_num);
	uint32 *idx = (uint32 *)buf->data;
	uint32 n = BLOCK_SIZE / sizeof(uint32);
	bool meet_empty = false;

	for (uint32 i = 0; i < n; i++) {
		meet_empty = __free_data_blocks(idx[i], level - 1);
		idx[i] = 0;
		if (meet_empty)
			break;
	}

	buffer_write(buf);
	buffer_put(buf);

	bitmap_free_block(block_num);
	return meet_empty;
}

/* 
	释放inode管理的blocks
*/
static void free_data_blocks(uint32 *inode_index)
{
	unsigned int i;
	bool meet_empty = false;

	/* step-1: 释放直接映射的block */
	for (i = 0; i < INODE_INDEX_1; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 0);
		if (meet_empty) return;
	}

	/* step-2: 释放一级间接映射的block */
	for (; i < INODE_INDEX_2; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 1);
		if (meet_empty) return;
	}

	/* step-3: 释放二级间接映射的block */
	for (; i < INODE_INDEX_3; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 2);
		if (meet_empty) return;		
	}

	panic("free_data_blocks: impossible!");
}

/*
	获取inode第logical_block_num个block的物理序号block_num
	调用者保证输入的logical_block_num只有两种情况:
	1. 属于已经分配的区域 (返回block_num)
	2. 将已经分配出去的区域往外扩展1个block (申请block并返回block_num) 
	成功返回block_num, 失败返回-1
*/
static uint32 locate_or_add_block(uint32 *inode_index, uint32 logical_block_num)
{
	if (logical_block_num >= INODE_BLOCK_INDEX_3)
		return (uint32)-1;

	// 1) direct blocks
	if (logical_block_num < INODE_BLOCK_INDEX_1) {
		uint32 *p = &inode_index[logical_block_num];
		if (*p == 0) {
			uint32 b = bitmap_alloc_block();
			if (b == BLOCK_NUM_UNUSED)
				return (uint32)-1;
			*p = b;
			buffer_t *buf = buffer_get(b);
			memset(buf->data, 0, BLOCK_SIZE);
			buffer_write(buf);
			buffer_put(buf);
		}
		return *p;
	}

	// 2) single indirect blocks
	if (logical_block_num < INODE_BLOCK_INDEX_2) {
		uint32 off = logical_block_num - INODE_BLOCK_INDEX_1; // [0, 2048)
		uint32 which = off / (BLOCK_SIZE / sizeof(uint32));    // 0 or 1
		uint32 slot = off % (BLOCK_SIZE / sizeof(uint32));
		uint32 *indirect_p = &inode_index[INODE_INDEX_1 + which];
		if (*indirect_p == 0) {
			uint32 ib = bitmap_alloc_block();
			if (ib == BLOCK_NUM_UNUSED)
				return (uint32)-1;
			*indirect_p = ib;
			buffer_t *z = buffer_get(ib);
			memset(z->data, 0, BLOCK_SIZE);
			buffer_write(z);
			buffer_put(z);
		}

		buffer_t *buf = buffer_get(*indirect_p);
		uint32 *idx = (uint32 *)buf->data;
		if (idx[slot] == 0) {
			uint32 b = bitmap_alloc_block();
			if (b == BLOCK_NUM_UNUSED) {
				buffer_put(buf);
				return (uint32)-1;
			}
			idx[slot] = b;
			buffer_write(buf);
			buffer_t *z = buffer_get(b);
			memset(z->data, 0, BLOCK_SIZE);
			buffer_write(z);
			buffer_put(z);
		}
		uint32 ret = idx[slot];
		buffer_put(buf);
		return ret;
	}

	// 3) double indirect blocks
	uint32 off = logical_block_num - INODE_BLOCK_INDEX_2; // [0, 1024*1024)
	uint32 first = off / (BLOCK_SIZE / sizeof(uint32));
	uint32 second = off % (BLOCK_SIZE / sizeof(uint32));

	uint32 *dbl_p = &inode_index[INODE_INDEX_2];
	if (*dbl_p == 0) {
		uint32 iib = bitmap_alloc_block();
		if (iib == BLOCK_NUM_UNUSED)
			return (uint32)-1;
		*dbl_p = iib;
		buffer_t *z = buffer_get(iib);
		memset(z->data, 0, BLOCK_SIZE);
		buffer_write(z);
		buffer_put(z);
	}

	buffer_t *iibuf = buffer_get(*dbl_p);
	uint32 *lvl2 = (uint32 *)iibuf->data;
	if (lvl2[first] == 0) {
		uint32 ib = bitmap_alloc_block();
		if (ib == BLOCK_NUM_UNUSED) {
			buffer_put(iibuf);
			return (uint32)-1;
		}
		lvl2[first] = ib;
		buffer_write(iibuf);
		buffer_t *z = buffer_get(ib);
		memset(z->data, 0, BLOCK_SIZE);
		buffer_write(z);
		buffer_put(z);
	}
	uint32 ib = lvl2[first];
	buffer_put(iibuf);

	buffer_t *ibuf = buffer_get(ib);
	uint32 *lvl1 = (uint32 *)ibuf->data;
	if (lvl1[second] == 0) {
		uint32 b = bitmap_alloc_block();
		if (b == BLOCK_NUM_UNUSED) {
			buffer_put(ibuf);
			return (uint32)-1;
		}
		lvl1[second] = b;
		buffer_write(ibuf);
		buffer_t *z = buffer_get(b);
		memset(z->data, 0, BLOCK_SIZE);
		buffer_write(z);
		buffer_put(z);
	}
	uint32 ret = lvl1[second];
	buffer_put(ibuf);
	return ret;
}

static uint32 locate_block(uint32 *inode_index, uint32 logical_block_num)
{
	if (logical_block_num >= INODE_BLOCK_INDEX_3)
		return 0;

	if (logical_block_num < INODE_BLOCK_INDEX_1)
		return inode_index[logical_block_num];

	if (logical_block_num < INODE_BLOCK_INDEX_2) {
		uint32 off = logical_block_num - INODE_BLOCK_INDEX_1;
		uint32 which = off / (BLOCK_SIZE / sizeof(uint32));
		uint32 slot = off % (BLOCK_SIZE / sizeof(uint32));
		uint32 ib = inode_index[INODE_INDEX_1 + which];
		if (ib == 0)
			return 0;
		buffer_t *buf = buffer_get(ib);
		uint32 ret = ((uint32 *)buf->data)[slot];
		buffer_put(buf);
		return ret;
	}

	uint32 off = logical_block_num - INODE_BLOCK_INDEX_2;
	uint32 first = off / (BLOCK_SIZE / sizeof(uint32));
	uint32 second = off % (BLOCK_SIZE / sizeof(uint32));
	uint32 iib = inode_index[INODE_INDEX_2];
	if (iib == 0)
		return 0;
	buffer_t *iibuf = buffer_get(iib);
	uint32 ib = ((uint32 *)iibuf->data)[first];
	buffer_put(iibuf);
	if (ib == 0)
		return 0;
	buffer_t *ibuf = buffer_get(ib);
	uint32 ret = ((uint32 *)ibuf->data)[second];
	buffer_put(ibuf);
	return ret;
}

/*---------------------关于inode的管理: get dup lock unlock put----------------------*/

/* 
	磁盘里的inode <-> 内存里的inode
	调用者需要持有ip->slk并设置合理的inode_num
*/
void inode_rw(inode_t *ip, bool write)
{
	assert(sleeplock_holding(&ip->slk), "inode_rw: slk");
	assert(ip->inode_num != INVALID_INODE_NUM, "inode_rw: invalid inode_num");

	uint32 block_num = sb.inode_firstblock + ip->inode_num / INODE_PER_BLOCK;
	uint32 byte_offset = (ip->inode_num % INODE_PER_BLOCK) * sizeof(inode_disk_t);

	buffer_t *buf = buffer_get(block_num);
	if (write) {
		memmove(buf->data + byte_offset, &ip->disk_info, sizeof(inode_disk_t));
		buffer_write(buf);
	} else {
		memmove(&ip->disk_info, buf->data + byte_offset, sizeof(inode_disk_t));
		ip->valid_info = true;
	}
	buffer_put(buf);
}

/*
	尝试在inode_cache里寻找是否存在目标inode
	如果不存在则申请一个空闲的inode
	如果没有空闲位置直接panic
	核心逻辑: ref++
*/
inode_t *inode_get(uint32 inode_num)
{
	spinlock_acquire(&lk_inode_cache);

	// cache hit
	for (int i = 0; i < N_INODE; i++) {
		inode_t *ip = &inode_cache[i];
		if (ip->ref > 0 && ip->inode_num == inode_num) {
			ip->ref++;
			spinlock_release(&lk_inode_cache);
			return ip;
		}
	}

	// cache miss: allocate empty slot
	for (int i = 0; i < N_INODE; i++) {
		inode_t *ip = &inode_cache[i];
		if (ip->ref == 0) {
			ip->ref = 1;
			ip->inode_num = inode_num;
			ip->valid_info = false;
			spinlock_release(&lk_inode_cache);
			return ip;
		}
	}

	spinlock_release(&lk_inode_cache);
	panic("inode_get: no free inode");
	return NULL;
}

/*
	在磁盘里创建1个新的inode
	1. 查询和修改inode_bitmap
	2. 填充inode_region对应位置的inode
	注意: 返回的inode未上锁
*/
inode_t *inode_create(uint16 type, uint16 major, uint16 minor)
{
	uint32 inode_num = bitmap_alloc_inode();
	if (inode_num == (uint32)-1)
		panic("inode_create: no free inode");

	inode_t *ip = inode_get(inode_num);
	sleeplock_acquire(&ip->slk);

	memset(&ip->disk_info, 0, sizeof(inode_disk_t));
	ip->disk_info.type = type;
	ip->disk_info.major = major;
	ip->disk_info.minor = minor;
	ip->disk_info.nlink = 1;
	ip->disk_info.size = 0;
	for (int i = 0; i < INODE_INDEX_3; i++)
		ip->disk_info.index[i] = 0;

	// For directory inode, allocate one data block for dentries.
	if (type == INODE_TYPE_DIR) {
		uint32 b = bitmap_alloc_block();
		if (b == BLOCK_NUM_UNUSED)
			panic("inode_create: no free data block for dir");
		ip->disk_info.index[0] = b;
		buffer_t *buf = buffer_get(b);
		memset(buf->data, 0, BLOCK_SIZE);
		buffer_write(buf);
		buffer_put(buf);
	}

	ip->valid_info = true;
	inode_rw(ip, true);
	sleeplock_release(&ip->slk);
	return ip;
}

/*
	ip->ref++ with lock proctect
*/
inode_t* inode_dup(inode_t* ip)
{
	spinlock_acquire(&lk_inode_cache);
	assert(ip->ref > 0, "inode_dup: invalid ref");
	ip->ref++;
	spinlock_release(&lk_inode_cache);
	return ip;
}

/*
	锁住inode
	如果inode->disk_info无效则更新一波
*/
void inode_lock(inode_t* ip)
{
	sleeplock_acquire(&ip->slk);
	if (!ip->valid_info) {
		inode_rw(ip, false);
		ip->valid_info = true;
	}
}

/*
	解锁inode
*/
void inode_unlock(inode_t *ip)
{
	sleeplock_release(&ip->slk);
}

/*
	与inode_get相对应, 调用者释放inode资源
	如果达成某些条件, 可能触发彻底删除
*/
void inode_put(inode_t* ip)
{
	bool do_delete = false;

	spinlock_acquire(&lk_inode_cache);
	if (ip->ref == 0)
		panic("inode_put: ref underflow");
	ip->ref--;
	if (ip->ref == 0 && ip->valid_info && ip->disk_info.nlink == 0)
		do_delete = true;
	spinlock_release(&lk_inode_cache);

	if (do_delete) {
		inode_lock(ip);
		inode_delete(ip);
		ip->valid_info = false;
		ip->inode_num = INVALID_INODE_NUM;
		memset(&ip->disk_info, 0, sizeof(inode_disk_t));
		inode_unlock(ip);
	}
}

/*
	在磁盘里删除1个inode
	1. 修改inode_bitmap释放inode_region资源
	2. 修改block_bitmap释放block_region资源
	注意: 调用者需要持有ip->slk
*/
void inode_delete(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "inode_delete: slk");
	assert(ip->inode_num != INVALID_INODE_NUM, "inode_delete: invalid inode_num");

	// free all data/index blocks managed by this inode
	free_data_blocks(ip->disk_info.index);

	// free inode bitmap
	bitmap_free_inode(ip->inode_num);

	// clear on-disk inode region (best-effort)
	memset(&ip->disk_info, 0, sizeof(inode_disk_t));
	ip->valid_info = true;
	inode_rw(ip, true);
}

/*----------------------基于inode的数据读写操作--------------------*/

/*
	基于inode的数据读取
	inode管理的数据空间逻辑上是一个连续的数组data
	需要拷贝data[offset,offset+len)到dst(用户态地址/内核态地址)
	返回读取的数据量(字节)
*/
uint32 inode_read_data(inode_t *ip, uint32 offset, uint32 len, void *dst, bool is_user_dst)
{
	assert(sleeplock_holding(&ip->slk), "inode_read_data: slk");

	if (offset >= ip->disk_info.size)
		return 0;
	uint32 maxlen = ip->disk_info.size - offset;
	if (len > maxlen)
		len = maxlen;

	uint8 *kdst = (uint8 *)dst;
	uint32 total = 0;
	proc_t *p = myproc();

	while (len > 0) {
		uint32 logical = offset / BLOCK_SIZE;
		uint32 boff = offset % BLOCK_SIZE;
		uint32 cut = MIN(len, BLOCK_SIZE - boff);
		uint32 block_num = locate_block(ip->disk_info.index, logical);
		if (block_num == 0)
			panic("inode_read_data: missing block");
		buffer_t *buf = buffer_get(block_num);
		if (is_user_dst) {
			uvm_copyout(p->pgtbl, (uint64)kdst, (uint64)(buf->data + boff), cut);
		} else {
			memmove(kdst, buf->data + boff, cut);
		}
		buffer_put(buf);

		offset += cut;
		kdst += cut;
		total += cut;
		len -= cut;
	}
	return total;
}

/*
	基于inode的数据写入
	inode管理的数据空间逻辑上是一个连续的数组data
	需要拷贝src(用户态地址/内核态地址)到data[offset,offset+len)
	返回写入的数据量(字节)
*/
uint32 inode_write_data(inode_t *ip, uint32 offset, uint32 len, void *src, bool is_user_src)
{
	assert(sleeplock_holding(&ip->slk), "inode_write_data: slk");

	// For stream data inode, do not allow holes.
	if (ip->disk_info.type == INODE_TYPE_DATA && offset > ip->disk_info.size)
		return 0;

	uint64 end = (uint64)offset + (uint64)len;
	if (end > INODE_MAX_SIZE)
		return 0;

	uint32 old_blocks = COUNT_BLOCKS(ip->disk_info.size, BLOCK_SIZE);
	uint32 new_blocks = COUNT_BLOCKS((uint32)end, BLOCK_SIZE);
	if (new_blocks > INODE_BLOCK_INDEX_3)
		return 0;

	// allocate new blocks one-by-one to satisfy locate_or_add_block's contract
	for (uint32 lb = old_blocks; lb < new_blocks; lb++) {
		uint32 b = locate_or_add_block(ip->disk_info.index, lb);
		if (b == (uint32)-1)
			return 0;
	}

	uint8 *ksrc = (uint8 *)src;
	uint32 total = 0;
	proc_t *p = myproc();

	while (len > 0) {
		uint32 logical = offset / BLOCK_SIZE;
		uint32 boff = offset % BLOCK_SIZE;
		uint32 cut = MIN(len, BLOCK_SIZE - boff);
		uint32 block_num = locate_block(ip->disk_info.index, logical);
		if (block_num == 0)
			panic("inode_write_data: missing block");
		buffer_t *buf = buffer_get(block_num);
		if (is_user_src) {
			uvm_copyin(p->pgtbl, (uint64)(buf->data + boff), (uint64)ksrc, cut);
		} else {
			memmove(buf->data + boff, ksrc, cut);
		}
		buffer_write(buf);
		buffer_put(buf);

		offset += cut;
		ksrc += cut;
		total += cut;
		len -= cut;
	}

	if ((uint32)end > ip->disk_info.size)
		ip->disk_info.size = (uint32)end;
	inode_rw(ip, true);
	return total;
}

static char *inode_type_list[] = {"DATA", "DIR", "DEVICE"};

/* 输出inode信息(for debug) */
void inode_print(inode_t *ip, char* name)
{
	assert(sleeplock_holding(&ip->slk), "inode_print: slk");

	spinlock_acquire(&lk_inode_cache);

	printf("inode %s:\n", name);
	printf("ref = %d, inode_num = %d, valid_info = %d\n", ip->ref, ip->inode_num, ip->valid_info);
	printf("type = %s, major = %d, minor = %d, nlink = %d, size = %d\n", inode_type_list[ip->disk_info.type],
		ip->disk_info.major, ip->disk_info.minor, ip->disk_info.nlink, ip->disk_info.size);

	printf("index_list = [ ");
	for (int i = 0; i < INODE_INDEX_1; i++)
		printf("%d ", ip->disk_info.index[i]);
	printf("] [ ");
	for (int i = INODE_INDEX_1; i < INODE_INDEX_2; i++)
		printf("%d ", ip->disk_info.index[i]);
	printf("] [ ");
	for (int i = INODE_INDEX_2; i < INODE_INDEX_3; i++)
		printf("%d ", ip->disk_info.index[i]);
	printf("]\n\n");

	spinlock_release(&lk_inode_cache);
}
