#include "mod.h"
#include "../mem/method.h"

static buffer_node_t buf_cache[N_BUFFER];
static buffer_node_t buf_head_active, buf_head_inactive;
static spinlock_t lk_buf_cache;

/* 
	将一个节点拿出来并插入
	1. 活跃链表的头部 buf_head_active->next
	2. 活跃链表的尾部 buf_head_active->prev
	3. 不活跃链表的头部 buf_head_inactive->next
	4. 不活跃链表的尾部 buf_head_inactive->prev
*/
static void insert_node(buffer_node_t *node, bool insert_active, bool insert_next)
{
	/* 如果有需要, 让node先离开当前位置 */
	if (node->next != NULL && node->prev != NULL) {
		node->next->prev = node->prev;
		node->prev->next = node->next;
	}

	/* 选择目标双向循环链表 */
	buffer_node_t *head = &buf_head_inactive;
	if (insert_active)
		head = &buf_head_active;

	/* 然后将node插入head->next or head->prev */	
	if (insert_next) {
		node->next = head->next;
		node->next->prev = node;
		node->prev = head;
		head->next = node;
	} else {
		node->prev = head->prev;
		node->prev->next = node;
		node->next = head;
		head->prev = node;
	}
}

/* 
	buffer系统初始化：
	1. 初始化全局的lk_buf_cache + buf_head_active + buf_head_inactive
	2. 初始化buf_cache中的所有node, 并将他们放在不活跃链表中
*/
void buffer_init()
{
	spinlock_init(&lk_buf_cache, "buf_cache");

	buf_head_active.next = &buf_head_active;
	buf_head_active.prev = &buf_head_active;
	buf_head_inactive.next = &buf_head_inactive;
	buf_head_inactive.prev = &buf_head_inactive;

	for (int i = N_BUFFER - 1; i >= 0; i--) {
		buffer_node_t *node = &buf_cache[i];
		memset(&node->buf, 0, sizeof(buffer_t));
		node->buf.block_num = BLOCK_NUM_UNUSED;
		node->buf.data = NULL;
		node->buf.ref = 0;
		node->buf.disk = false;
		sleeplock_init(&node->buf.slk, "buffer");
		node->next = NULL;
		node->prev = NULL;
		insert_node(node, false, true);
	}
}

/* 磁盘读取: block -> buf */
static void buffer_read(buffer_t *buf)
{
	assert(sleeplock_holding(&buf->slk), "buffer_read: sleeplock not held");
	assert(buf->data != NULL, "buffer_read: no data page");
	virtio_disk_rw(buf, false);
}

/* 磁盘写入: buf -> block */
void buffer_write(buffer_t *buf)
{
	assert(sleeplock_holding(&buf->slk), "buffer_write: sleeplock not held");
	assert(buf->data != NULL, "buffer_write: no data page");
	virtio_disk_rw(buf, true);
}

/* 
	Invalidate all buffers in the cache that are not currently in use.
	This forces subsequent reads to fetch from disk.
*/
void buffer_flush_all()
{
	buffer_node_t *node;
	spinlock_acquire(&lk_buf_cache);
	
	node = buf_head_active.next;
	while (node != &buf_head_active) {
		buffer_node_t *next = node->next;
		if (node->buf.ref == 0) {
			node->buf.block_num = BLOCK_NUM_UNUSED;
			insert_node(node, false, true);
		}
		node = next;
	}
	
	spinlock_release(&lk_buf_cache);
}

/* 从buf_cache中获取一个buf */
buffer_t* buffer_get(uint32 block_num)
{
	buffer_node_t *node;
	bool need_read = false;

	spinlock_acquire(&lk_buf_cache);

	/* 1. active list */
	for (node = buf_head_active.next; node != &buf_head_active; node = node->next) {
		if (node->buf.block_num == block_num) {
			node->buf.ref++;
			insert_node(node, true, true);
			spinlock_release(&lk_buf_cache);
			sleeplock_acquire(&node->buf.slk);
			return &node->buf;
		}
	}

	/* 2. inactive list */
	for (node = buf_head_inactive.next; node != &buf_head_inactive; node = node->next) {
		if (node->buf.block_num == block_num) {
			node->buf.ref++;
			insert_node(node, true, true);
			spinlock_release(&lk_buf_cache);
			sleeplock_acquire(&node->buf.slk);
			return &node->buf;
		}
	}

	/* 3. cache miss: 复用最不活跃的buffer */
	node = buf_head_inactive.prev;
	if (node == &buf_head_inactive)
		panic("buffer_get: no free buffer");
	if (node->buf.ref != 0)
		panic("buffer_get: inactive ref");

	if (node->buf.data == NULL)
		node->buf.data = (uint8*)pmem_alloc(true);

	node->buf.block_num = block_num;
	node->buf.ref = 1;
	node->buf.disk = false;
	need_read = true;

	insert_node(node, true, false);
	spinlock_release(&lk_buf_cache);

	sleeplock_acquire(&node->buf.slk);
	if (need_read)
		buffer_read(&node->buf);
	return &node->buf;
}

/* 向buf_cache归还一个buf */
void buffer_put(buffer_t *buf)
{
	if (!sleeplock_holding(&buf->slk))
		panic("buffer_put: sleeplock not held");

	sleeplock_release(&buf->slk);

	spinlock_acquire(&lk_buf_cache);
	if (buf->ref == 0)
		panic("buffer_put: ref underflow");
	buf->ref--;
	if (buf->ref == 0)
		insert_node((buffer_node_t*)buf, false, true);
	spinlock_release(&lk_buf_cache);
}

/*
	从后向前遍历非活跃链表, 尝试释放buffer_count个buffer持有的物理内存(data)
	返回成功释放资源的buffer数量
*/
uint32 buffer_freemem(uint32 buffer_count)
{
	uint32 freed = 0;

	spinlock_acquire(&lk_buf_cache);
	for (buffer_node_t *node = buf_head_inactive.prev;
		 node != &buf_head_inactive && freed < buffer_count;
		 node = node->prev) {
		buffer_t *buf = &node->buf;
		if (buf->ref != 0 || buf->data == NULL)
			continue;
		pmem_free((uint64)buf->data, true);
		buf->data = NULL;
		buf->block_num = BLOCK_NUM_UNUSED;
		freed++;
	}
	spinlock_release(&lk_buf_cache);

	return freed;
}

/* 输出buffer_cache的信息 (for test) */
void buffer_print_info()
{
	buffer_node_t *node;

	assert(N_BUFFER == N_BUFFER_TEST, "buffer_print_info: invalid N_BUFFER");

	spinlock_acquire(&lk_buf_cache);

	printf("buffer_cache information:\n");
	
	printf("1.active list:\n");
	for (node = buf_head_active.next; node != &buf_head_active; node = node->next) {
		printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
			(int)(node - buf_cache), node->buf.ref, (uint64)node->buf.data, node->buf.block_num);
	}
	printf("over!\n");

	printf("2.inactive list:\n");
	for (node = buf_head_inactive.next; node != &buf_head_inactive; node = node->next) {
		printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
			(int)(node - buf_cache), node->buf.ref, (uint64)node->buf.data, node->buf.block_num);
	}
	printf("over!\n");

	spinlock_release(&lk_buf_cache);
}
