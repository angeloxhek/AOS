#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../include/aoslib.h"

int fstat(int fd, struct stat *buf) {
    if (fd < 0 || !buf) return -1;

    vfs_stat_info_t info;
    if (vfs_stat(fd, &info) != 0) {
        return -1;
    }

    memset(buf, 0, sizeof(struct stat));
    
    buf->st_size = info.size_bytes;
    buf->st_ino  = info.inode_id;
    
    if (info.attributes & 0x10) {
        buf->st_mode = S_IFDIR | 0755;
    } else {
        buf->st_mode = S_IFREG | 0644;
    }

    buf->st_atime = time(NULL);
    buf->st_mtime = time(NULL);
    buf->st_ctime = time(NULL);

    return 0;
}

int stat(const char *path, struct stat *buf) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    int res = fstat(fd, buf);
    close(fd);
    
    return res;
}