#include "mod.h"

#include "../mem/method.h"

super_block_t sb; /* 超级块 */

file_t file_table[N_FILE]; // 文件资源池
spinlock_t lk_file_table; // 保护它的锁

/* 初始化file_table */
void file_init()
{
	spinlock_init(&lk_file_table, "file_table");
	for (int i = 0; i < N_FILE; i++) {
		file_table[i].ip = NULL;
		file_table[i].readable = false;
		file_table[i].writbale = false;
		file_table[i].offset = 0;
		file_table[i].ref = 0;
	}
}

/* 从file_table中获取1个空闲file */
file_t* file_alloc()
{
	spinlock_acquire(&lk_file_table);
	for (int i = 0; i < N_FILE; i++) {
		if (file_table[i].ref == 0) {
			file_table[i].ref = 1;
			file_table[i].ip = NULL;
			file_table[i].readable = false;
			file_table[i].writbale = false;
			file_table[i].offset = 0;
			spinlock_release(&lk_file_table);
			return &file_table[i];
		}
	}
	spinlock_release(&lk_file_table);
	return NULL;
}

/*
	根据路径打开文件 (指定打开模式)
	成功返回file, 失败返回NULL
*/
file_t* file_open(char *path, uint32 open_mode)
{
	inode_t *ip = path_to_inode(path);
	if (ip == NULL) {
		if (!(open_mode & FILE_OPEN_CREATE))
			return NULL;
		ip = path_create_inode(path, INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
		if (ip == NULL)
			return NULL;
	}

	inode_lock(ip);
	if (ip->disk_info.type == INODE_TYPE_DIVICE) {
		if (!device_open_check(ip->disk_info.major, open_mode)) {
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}
	}
	inode_unlock(ip);

	file_t *f = file_alloc();
	if (f == NULL) {
		inode_put(ip);
		return NULL;
	}
	f->ip = ip;
	f->readable = (open_mode & FILE_OPEN_READ) != 0;
	f->writbale = (open_mode & FILE_OPEN_WRITE) != 0;
	f->offset = 0;
	return f;
}

/* 关闭文件 */
void file_close(file_t *file)
{
	spinlock_acquire(&lk_file_table);
	if (file->ref == 0)
		panic("file_close: ref underflow");
	file->ref--;
	if (file->ref > 0) {
		spinlock_release(&lk_file_table);
		return;
	}
	spinlock_release(&lk_file_table);

	if (file->ip)
		inode_put(file->ip);
	file->ip = NULL;
	file->readable = false;
	file->writbale = false;
	file->offset = 0;
}

/* 读取文件内容, 返回读到的字节数量 */
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool is_user_dst)
{
	if (!file->readable)
		return 0;
	if (file->ip == NULL)
		return 0;

	inode_t *ip = file->ip;
	uint32 ret = 0;

	switch (ip->disk_info.type) {
	case INODE_TYPE_DATA:
		inode_lock(ip);
		ret = inode_read_data(ip, file->offset, len, (void*)dst, is_user_dst);
		file->offset += ret;
		inode_unlock(ip);
		break;
	case INODE_TYPE_DIR:
		inode_lock(ip);
		ret = dentry_transmit(ip, dst, len, is_user_dst);
		inode_unlock(ip);
		break;
	case INODE_TYPE_DIVICE:
		ret = device_read_data(ip->disk_info.major, len, dst, is_user_dst);
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

/* 读取文件内容, 返回读到的字节数量 */
uint32 file_write(file_t* file, uint32 len, uint64 src, bool is_user_src)
{
	if (!file->writbale)
		return 0;
	if (file->ip == NULL)
		return 0;

	inode_t *ip = file->ip;
	uint32 ret = 0;

	switch (ip->disk_info.type) {
	case INODE_TYPE_DATA:
		inode_lock(ip);
		ret = inode_write_data(ip, file->offset, len, (void*)src, is_user_src);
		file->offset += ret;
		inode_unlock(ip);
		break;
	case INODE_TYPE_DIR:
		ret = 0;
		break;
	case INODE_TYPE_DIVICE:
		ret = device_write_data(ip->disk_info.major, len, src, is_user_src);
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

/* 
	读/写指针的移动
	对于不合理的lseek_offset, 只做尽力而为的移动
	返回新的file->offset
*/
uint32 file_lseek(file_t *file, uint32 lseek_offset, uint32 lseek_flag)
{
	if (file->ip == NULL)
		return (uint32)-1;

	inode_t *ip = file->ip;
	uint32 new_off = file->offset;

	switch (lseek_flag) {
	case FILE_LSEEK_SET:
		new_off = lseek_offset;
		break;
	case FILE_LSEEK_ADD:
		new_off = file->offset + lseek_offset;
		break;
	case FILE_LSEEK_SUB:
		new_off = (file->offset > lseek_offset) ? (file->offset - lseek_offset) : 0;
		break;
	default:
		break;
	}

	if (ip->disk_info.type != INODE_TYPE_DIVICE) {
		inode_lock(ip);
		if (new_off > ip->disk_info.size)
			new_off = ip->disk_info.size;
		inode_unlock(ip);
	}

	file->offset = new_off;
	return new_off;
}

/* file->ref++ with lock protect */
file_t* file_dup(file_t* file)
{
	spinlock_acquire(&lk_file_table);
	if (file->ref == 0)
		panic("file_dup: invalid ref");
	file->ref++;
	spinlock_release(&lk_file_table);
	return file;
}

/* 获取文件参数, 成功返回0, 失败返回-1 */
uint32 file_get_stat(file_t* file, uint64 user_dst)
{
	if (file->ip == NULL)
		return (uint32)-1;

	file_stat_t stat;
	inode_t *ip = file->ip;

	inode_lock(ip);
	stat.type = ip->disk_info.type;
	stat.nlink = ip->disk_info.nlink;
	stat.size = ip->disk_info.size;
	stat.inode_num = ip->inode_num;
	inode_unlock(ip);
	stat.offset = file->offset;

	proc_t *p = myproc();
	uvm_copyout(p->pgtbl, user_dst, (uint64)&stat, sizeof(stat));
	return 0;
}

// fs_init may touch disk and sleep (virtio/buffer), so it must NOT hold a spinlock.
// Use a simple state machine for one-time initialization.
static volatile int fs_state = 0; // 0=uninit, 1=initializing, 2=ready

#define FS_TEST_ID 0

/* 基于superblock输出磁盘布局信息 (for debug) */
static void sb_print()
{
	printf("\ndisk layout information:\n");
	printf("1. super block:  block[0]\n");
	printf("2. inode bitmap: block[%d - %d]\n", sb.inode_bitmap_firstblock,
		sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1);
	printf("3. inode region: block[%d - %d]\n", sb.inode_firstblock,
		sb.inode_firstblock + sb.inode_blocks - 1);
	printf("4. data bitmap:  block[%d - %d]\n", sb.data_bitmap_firstblock,
		sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1);
	printf("5. data region:  block[%d - %d]\n", sb.data_firstblock,
		sb.data_firstblock + sb.data_blocks - 1);
	printf("block size = %d Byte, total size = %d MB, total inode = %d\n\n", sb.block_size,
		(int)((unsigned long long)(sb.total_blocks) * sb.block_size / 1024 / 1024), sb.total_inodes);
}

static void fs_read_superblock()
{
	buffer_t *buf = buffer_get(FS_SB_BLOCK);
	memmove(&sb, buf->data, sizeof(super_block_t));
	buffer_put(buf);
	assert(sb.magic_num == FS_MAGIC, "fs_read_superblock: invalid magic");
}

/* 文件系统初始化 */
void fs_init()
{
	if (fs_state == 2)
		return;

	// Become the one-time initializer.
	if (!__sync_bool_compare_and_swap(&fs_state, 0, 1)) {
		// Someone else is initializing; wait.
		while (fs_state != 2)
			;
		return;
	}

	buffer_init();
	fs_read_superblock();
	sb_print();
	inode_init();
	file_init();
	device_init();
	__sync_synchronize();
	fs_state = 2;

#if FS_TEST_ID == 1
	/* 测试1: inode的访问 + 创建 + 删除 */
	printf("============= test begin =============\n\n");

	inode_t *rooti, *ip_1, *ip_2;
	
	rooti = inode_get(ROOT_INODE);
	inode_lock(rooti);
	inode_print(rooti, "root");
	inode_unlock(rooti);

	/* 第一次查看bitmap */
	bitmap_print(false);

	ip_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	ip_2 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip_1);
	inode_lock(ip_2);
	inode_dup(ip_2);

	inode_print(ip_1, "dir");
	inode_print(ip_2, "data");
	
	/* 第二次查看bitmap */
	bitmap_print(false);

	ip_1->disk_info.nlink = 0;
	ip_2->disk_info.nlink = 0;
	inode_unlock(ip_1);
	inode_unlock(ip_2);
	inode_put(ip_1);
	inode_put(ip_2);

	/* 第三次查看bitmap */
	bitmap_print(false);

	inode_put(ip_2);
	
	/* 第四次查看bitmap */
	bitmap_print(false);

	printf("============= test end =============\n\n");

	intr_off();
	for (;;)
		asm volatile("wfi");
#elif FS_TEST_ID == 2
	/* 测试2: 写入和读取inode管理的数据 */
	printf("============= test begin =============\n\n");

	inode_t *ip_1, *ip_2;
	uint32 len, cut_len;

	/* 小批量读写测试 */

	int small_src[10], small_dst[10];
	for (int i = 0; i < 10; i++)
		small_src[i] = i;
	
	ip_1 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip_1);
	inode_print(ip_1, "small_data");

	printf("writing data...\n\n");
	cut_len = 10 * sizeof(int);
	for (uint32 offset = 0; offset < 400 * cut_len; offset += cut_len) {
		len = inode_write_data(ip_1, offset, cut_len, small_src, false);
		assert(len == cut_len, "write fail 1!");
	}
	inode_print(ip_1, "small_data");

	len = inode_read_data(ip_1, 120 * cut_len + 4, cut_len, small_dst, false);
	assert(len == cut_len, "read fail 1!");
	printf("read data:");
	for (int i = 0; i < 10; i++)
		printf(" %d", small_dst[i]);
	printf("\n\n");

	ip_1->disk_info.nlink = 0;
	inode_unlock(ip_1);
	inode_put(ip_1);

	/* 大批量读写测试 */

	char *big_src, big_dst[9];
	big_dst[8] = 0;

	// pmem_alloc() 不保证连续物理页；这里改为单页缓冲区，写多页以覆盖 direct->indirect。
	big_src = pmem_alloc(true);
	for (uint32 i = 0; i < PGSIZE; i++)
		big_src[i] = 'A' + (i % 8);

	ip_2 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip_2);
	inode_print(ip_2, "big_data");

	printf("writing data...\n\n");
	cut_len = PGSIZE;
	uint32 rounds = INODE_BLOCK_INDEX_1 + 20; // 覆盖 direct(10块) + 少量 single-indirect
	for (uint32 offset = 0; offset < rounds * cut_len; offset += cut_len) {
		len = inode_write_data(ip_2, offset, cut_len, big_src, false);
		assert(len == cut_len, "write fail 2!");
	}
	inode_print(ip_2, "big_data");

	len = inode_read_data(ip_2, rounds * cut_len - 8, 8, big_dst, false);
	assert(len == 8, "read fail 2");
	printf("read data: %s\n", big_dst);


	ip_2->disk_info.nlink = 0;
	inode_unlock(ip_2);
	inode_put(ip_2);

	pmem_free((uint64)big_src, true);


	printf("============= test end =============\n");

	intr_off();
	for (;;)
		asm volatile("wfi");
#elif FS_TEST_ID == 3
	/* 测试3: 目录项的增加、删除、查找操作 */
	printf("============= test begin =============\n\n");

	inode_t *rooti, *ip_1, *ip_2, *ip_3;
	uint32 inode_num_1, inode_num_2, inode_num_3;
	uint32 len, cutlen, offset;
	char tmp[10];

	tmp[9] = 0;
	cutlen = 9;
	rooti = inode_get(ROOT_INODE);

	/* 搜索预置的dentry */

	inode_lock(rooti);
	inode_num_1 = dentry_search(rooti, "ABCD.txt");
	inode_num_2 = dentry_search(rooti, "abcd.txt");
	inode_num_3 = dentry_search(rooti, ".");
	if (inode_num_1 == INVALID_INODE_NUM || 
		inode_num_2 == INVALID_INODE_NUM || 
		inode_num_3 == INVALID_INODE_NUM) {
		panic("invalid inode num!");
	}
	dentry_print(rooti);
	inode_unlock(rooti);

	ip_1 = inode_get(inode_num_1);
	inode_lock(ip_1);
	ip_2 = inode_get(inode_num_2);
	inode_lock(ip_2);
	ip_3 = inode_get(inode_num_3);
	inode_lock(ip_3);

	inode_print(ip_1, "ABCD.txt");
	inode_print(ip_2, "abcd.txt");
	inode_print(ip_3, "root");

	len = inode_read_data(ip_1, 0, cutlen, tmp, false);
	assert(len == cutlen, "read fail 1!");
	printf("\nread data: %s\n", tmp);

	len = inode_read_data(ip_2, 0, cutlen, tmp, false);
	assert(len == cutlen, "read fail 2!");
	printf("read data: %s\n\n", tmp);

	inode_unlock(ip_1);
	inode_unlock(ip_2);
	inode_unlock(ip_3);
	inode_put(ip_1);
	inode_put(ip_2);
	inode_put(ip_3);

	/* 创建和删除dentry */
	inode_lock(rooti);

	ip_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);	
	offset = dentry_create(rooti, ip_1->inode_num, "new_dir");
	inode_num_1 = dentry_search(rooti, "new_dir");
	printf("new dentry offset = %d\n", offset);
	printf("new dentry inode_num = %d\n\n", inode_num_1);
	
	dentry_print(rooti);

	inode_num_2 = dentry_delete(rooti, "new_dir");
	assert(inode_num_1 == inode_num_2, "inode num is not equal!");

	dentry_print(rooti);

	inode_unlock(rooti);
	inode_put(rooti);

	printf("============= test end =============\n");

	intr_off();
	for (;;)
		asm volatile("wfi");
#elif FS_TEST_ID == 4
	/* 测试4: 文件路径的解析 */
	printf("============= test begin =============\n\n");

	inode_t *rooti, *ip_1, *ip_2, *ip_3, *ip_4, *ip_5;
	
	/* 准备测试环境 */

	rooti = inode_get(ROOT_INODE);
	ip_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	ip_2 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	ip_3 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	
	inode_lock(rooti);
	inode_lock(ip_1);
	inode_lock(ip_2);
	inode_lock(ip_3);

	if (dentry_create(rooti, ip_1->inode_num, "AABBC") == (uint32)-1)
		panic("dentry_create fail 1!");
	if (dentry_create(ip_1, ip_2->inode_num, "aaabb") == (uint32)-1)
		panic("dentry_create fail 2!");
	if (dentry_create(ip_2, ip_3->inode_num, "file.txt") == (uint32)-1)
		panic("dentry_create fail 3!");

	char tmp1[] = "This is file context!";
	char tmp2[32];
	inode_write_data(ip_3, 0, sizeof(tmp1), tmp1, false);

	inode_rw(rooti, true);
	inode_rw(ip_1, true);
	inode_rw(ip_2, true);

	inode_unlock(rooti);
	inode_unlock(ip_1);
	inode_unlock(ip_2);
	inode_unlock(ip_3);
	inode_put(rooti);
	inode_put(ip_1);
	inode_put(ip_2);
	inode_put(ip_3);

	char *path = "///AABBC///aaabb/file.txt";
	char name[MAXLEN_FILENAME];

	ip_4 = path_to_inode(path);
	if (ip_4 == NULL)
		panic("invalid ip_4");

	ip_5 = path_to_parent_inode(path, name);
	if (ip_5 == NULL)
		panic("invalid ip_5");
	
	printf("get a name = %s\n\n", name);

	inode_lock(ip_4);
	inode_lock(ip_5);

	inode_print(ip_4, "file.txt");
	inode_print(ip_5, "aaabb");

	inode_read_data(ip_4, 0, 32, tmp2, false);
	printf("read data: %s\n\n", tmp2);

	inode_unlock(ip_4);
	inode_unlock(ip_5);
	inode_put(ip_4);
	inode_put(ip_5);

	printf("============= test end =============\n");

	intr_off();
	for (;;)
		asm volatile("wfi");
#elif FS_TEST_ID == 5
	/* 测试5: 持久化测试 (Write -> Drop Cache -> Read) */
	printf("============= test 5 begin =============\n\n");

	inode_t *ip;
	char *test_str = "PERSISTENCE_TEST_DATA_1234567890";
	char buf[100];
	int len = strlen(test_str);

	/* 1. Create and Write */
	printf("1. Create file and write data...\n");
	ip = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip);
	inode_write_data(ip, 0, len, test_str, false);
	uint32 inum = ip->inode_num;
	inode_unlock(ip);
	inode_put(ip); // Release inode and its buffers (ref counts drop)
	printf("   Write done. Inode %d released.\n", inum);

	/* 2. Drop Cache */
	printf("2. Flushing buffer cache...\n");
	buffer_flush_all();
	printf("   Cache flushed.\n");

	/* 3. Read back */
	printf("3. Read back data...\n");
	ip = inode_get(inum);
	inode_lock(ip);
	memset(buf, 0, sizeof(buf));
	inode_read_data(ip, 0, len, buf, false);
	printf("   Read data: %s\n", buf);

	if (strncmp(test_str, buf, len) == 0) {
		printf("   [PASS] Data matches!\n");
	} else {
		printf("   [FAIL] Data mismatch! Expected: %s, Got: %s\n", test_str, buf);
		panic("Persistence test failed");
	}

	inode_unlock(ip);
	inode_put(ip);

	printf("============= test 5 end =============\n\n");
	
	intr_off();
	while(1) {
		asm volatile("wfi");
	}
#endif
}


// fs.c in lab-7 
// #include "mod.h"
// #include "../mem/method.h"

// super_block_t sb;

// static bool fs_ready = false;
// static bool fs_lock_inited = false;
// static spinlock_t fs_init_lk;

// static void fs_read_superblock()
// {
//     buffer_t *buf = buffer_get(FS_SB_BLOCK);
//     memmove(&sb, buf->data, sizeof(super_block_t));
//     buffer_put(buf);

//     assert(sb.magic_num == FS_MAGIC, "fs_read_superblock: invalid magic");
// }

// static void fs_print_superblock()
// {
//     uint32 inode_bitmap_last = sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1;
//     uint32 inode_region_last = sb.inode_firstblock + sb.inode_blocks - 1;
//     uint32 data_bitmap_last = sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1;
//     uint32 data_region_last = sb.data_firstblock + sb.data_blocks - 1;
//     uint64 total_bytes = (uint64)sb.block_size * (uint64)sb.total_blocks;
//     uint32 total_mb = (uint32)(total_bytes / (1024 * 1024));

//     printf("disk layout information:\n");
//     printf("1. super block:  block[%d]\n", FS_SB_BLOCK);
//     printf("2. inode bitmap: block[%d", sb.inode_bitmap_firstblock);
//     if (sb.inode_bitmap_blocks > 1)
//         printf(" - %d", inode_bitmap_last);
//     printf("]\n");
//     printf("3. inode region: block[%d", sb.inode_firstblock);
//     if (sb.inode_blocks > 1)
//         printf(" - %d", inode_region_last);
//     printf("]\n");
//     printf("4. data bitmap:  block[%d", sb.data_bitmap_firstblock);
//     if (sb.data_bitmap_blocks > 1)
//         printf(" - %d", data_bitmap_last);
//     printf("]\n");
//     printf("5. data region:  block[%d", sb.data_firstblock);
//     if (sb.data_blocks > 1)
//         printf(" - %d", data_region_last);
//     printf("]\n");
//     printf("block size = %d Byte, total size = %d MB, total inode = %d\n",
//            sb.block_size, total_mb, sb.total_inodes);
// }

// void fs_init()
// {
//     if (fs_ready)
//         return;

//     if (!fs_lock_inited) {
//         spinlock_init(&fs_init_lk, "fs_init");
//         fs_lock_inited = true;
//     }

//     spinlock_acquire(&fs_init_lk);
//     if (!fs_ready) {
//         buffer_init();
//         fs_read_superblock();
//         fs_print_superblock();
//         fs_ready = true;
//     }
//     spinlock_release(&fs_init_lk);
// }
