/*
 * 测试五 硬链接生命周期与 nlink 变化
 * 1) 创建基础文件并写入内容
 * 2) 建立两个硬链接, 检查 nlink 计数
 * 3) 通过不同路径读数据验证一致性
 * 4) 逐步 unlink, 确认访问与 nlink 变化, 最终删除
 */

#include "help.h"

static void die(const char *msg)
{
    fprintf(STDERR, "%s", msg);
    sys_exit(1);
}

static void expect_ok(bool bad, const char *msg)
{
    if (bad)
        die(msg);
}

static void check_nlink(uint32 fd, uint16 expect, const char *tag)
{
    file_stat_t st;
    expect_ok(sys_fstat(fd, &st) == (uint32)-1, "fstat fail\n");
    if (st.nlink != expect) {
        fprintf(STDERR, "%s nlink expect %d got %d\n", tag, expect, st.nlink);
        sys_exit(1);
    }
    fprintf(STDOUT, "%s nlink=%d size=%d offset=%d\n", tag, st.nlink, st.size, st.offset);
}

static void check_read(uint32 fd, const char *tag, const char *data, uint32 len)
{
    char buf[64];
    expect_ok(sys_lseek(fd, 0, LSEEK_SET) == (uint32)-1, "lseek fail\n");
    uint32 n = sys_read(fd, len, buf);
    if (n != len || strncmp(buf, data, len) != 0) {
        fprintf(STDERR, "%s read mismatch\n", tag);
        sys_exit(1);
    }
    fprintf(STDOUT, "%s read ok\n", tag);
}

void main(int argc, char *argv[])
{
    char base[] = "/hl_origin.txt";
    char link_a[] = "/hl_a.txt";
    char link_b[] = "hl_b.txt"; // 相对路径, 默认 cwd 是根
    char payload[] = "hardlink-payload";
    uint32 fd_base, fd_a, fd_b;

    // 清理旧残留
    sys_unlink(base);
    sys_unlink(link_a);
    sys_unlink(link_b);

    fd_base = sys_open(base, OPEN_READ | OPEN_WRITE | OPEN_CREATE);
    expect_ok(fd_base == (uint32)-1, "create base fail\n");

    expect_ok(sys_write(fd_base, sizeof(payload), (void*)payload) != sizeof(payload), "write payload fail\n");
    check_nlink(fd_base, 1, "after create");

    expect_ok(sys_link(base, link_a) == (uint32)-1, "link a fail\n");
    check_nlink(fd_base, 2, "after link a");

    expect_ok(sys_link(base, link_b) == (uint32)-1, "link b fail\n");
    check_nlink(fd_base, 3, "after link b");

    fd_a = sys_open(link_a, OPEN_READ | OPEN_WRITE);
    fd_b = sys_open(link_b, OPEN_READ | OPEN_WRITE);
    expect_ok(fd_a == (uint32)-1 || fd_b == (uint32)-1, "open links fail\n");

    check_read(fd_a, "read via a", payload, sizeof(payload));
    check_read(fd_b, "read via b", payload, sizeof(payload));

    expect_ok(sys_unlink(link_a) == (uint32)-1, "unlink a fail\n");
    check_nlink(fd_base, 2, "after unlink a");

    expect_ok(sys_unlink(base) == (uint32)-1, "unlink base fail\n");
    check_nlink(fd_b, 1, "after unlink base");

    sys_close(fd_a);
    sys_close(fd_base);

    expect_ok(sys_unlink(link_b) == (uint32)-1, "unlink b fail\n");

    uint32 reopen = sys_open(base, OPEN_READ);
    if (reopen != (uint32)-1) {
        fprintf(STDERR, "open removed base succeed unexpectedly\n");
        sys_exit(1);
    }
    reopen = sys_open(link_a, OPEN_READ);
    if (reopen != (uint32)-1) {
        fprintf(STDERR, "open removed link_a succeed unexpectedly\n");
        sys_exit(1);
    }
    reopen = sys_open(link_b, OPEN_READ);
    if (reopen != (uint32)-1) {
        fprintf(STDERR, "open removed link_b succeed unexpectedly\n");
        sys_exit(1);
    }

    sys_close(fd_b);
    fprintf(STDOUT, "hardlink lifecycle test done\n");
    sys_exit(0);
}
