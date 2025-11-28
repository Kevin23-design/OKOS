#include "mod.h"
#include "../mem/method.h"

super_block_t sb;

static bool fs_ready = false;
static bool fs_lock_inited = false;
static spinlock_t fs_init_lk;

static void fs_read_superblock()
{
    buffer_t *buf = buffer_get(FS_SB_BLOCK);
    memmove(&sb, buf->data, sizeof(super_block_t));
    buffer_put(buf);

    assert(sb.magic_num == FS_MAGIC, "fs_read_superblock: invalid magic");
}

static void fs_print_superblock()
{
    uint32 inode_bitmap_last = sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1;
    uint32 inode_region_last = sb.inode_firstblock + sb.inode_blocks - 1;
    uint32 data_bitmap_last = sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1;
    uint32 data_region_last = sb.data_firstblock + sb.data_blocks - 1;
    uint64 total_bytes = (uint64)sb.block_size * (uint64)sb.total_blocks;
    uint32 total_mb = (uint32)(total_bytes / (1024 * 1024));

    printf("disk layout information:\n");
    printf("1. super block:  block[%d]\n", FS_SB_BLOCK);
    printf("2. inode bitmap: block[%d", sb.inode_bitmap_firstblock);
    if (sb.inode_bitmap_blocks > 1)
        printf(" - %d", inode_bitmap_last);
    printf("]\n");
    printf("3. inode region: block[%d", sb.inode_firstblock);
    if (sb.inode_blocks > 1)
        printf(" - %d", inode_region_last);
    printf("]\n");
    printf("4. data bitmap:  block[%d", sb.data_bitmap_firstblock);
    if (sb.data_bitmap_blocks > 1)
        printf(" - %d", data_bitmap_last);
    printf("]\n");
    printf("5. data region:  block[%d", sb.data_firstblock);
    if (sb.data_blocks > 1)
        printf(" - %d", data_region_last);
    printf("]\n");
    printf("block size = %d Byte, total size = %d MB, total inode = %d\n",
           sb.block_size, total_mb, sb.total_inodes);
}

void fs_init()
{
    if (fs_ready)
        return;

    if (!fs_lock_inited) {
        spinlock_init(&fs_init_lk, "fs_init");
        fs_lock_inited = true;
    }

    spinlock_acquire(&fs_init_lk);
    if (!fs_ready) {
        buffer_init();
        fs_read_superblock();
        fs_print_superblock();
        fs_ready = true;
    }
    spinlock_release(&fs_init_lk);
}
