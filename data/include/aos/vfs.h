#ifndef AOS_VFS_H
#define AOS_VFS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VFS_ERR_OK                 DRV_ERR_OK
#define VFS_ERR_PERM                 -1
#define VFS_ERR_ISDIR                -2
#define VFS_ERR_BUSY                 -3
#define VFS_ERR_SYMLINKLOOP          -4
#define VFS_ERR_NOCOMM             DRV_ERR_NOCOMM
#define VFS_ERR_NOTFOUND           DRV_ERR_NOTFOUND
#define VFS_ERR_UNKNOWN            DRV_ERR_UNKNOWN

#define VFS_FREAD   (1 << 0)
#define VFS_FWRITE  (1 << 1)
#define VFS_FRW     (VFS_FREAD | VFS_FWRITE)
#define VFS_FCREATE (1 << 2)
#define VFS_FAPPEND (1 << 3)
#define VFS_FTRUNC  (1 << 4)

typedef enum {
    VFS_CMD_OPEN  = 1, VFS_CMD_OPENAT, VFS_CMD_CLOSE,
    VFS_CMD_READ, VFS_CMD_WRITE, VFS_CMD_STAT,
    VFS_CMD_LIST, VFS_CMD_FLOCK, VFS_CMD_SEEK
} vfs_cmd_t;

typedef enum { VFS_LOCK_UN = 1, VFS_LOCK_SH, VFS_LOCK_EX } vfs_lock_type_t;
typedef enum { VFS_SEEK_SET = 1, VFS_SEEK_CUR, VFS_SEEK_END } vfs_seek_t;

typedef enum {
    VFS_FILE_TYPE_UNKNOWN = 0, VFS_FILE_TYPE_REGULAR,
    VFS_FILE_TYPE_DIR, VFS_FILE_TYPE_SYMLINK, VFS_FILE_TYPE_DEVICE
} vfs_file_type_t;

typedef struct {
    char name[256];
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
} vfs_dirent_t;

typedef struct {
    char name[256];
    uint64_t inode_id;
    uint64_t size_bytes;
    uint32_t attributes;
} vfs_stat_info_t;

#ifdef AOSLIB_VFS
void vfs_init();
int vfs_open(const char* path, uint32_t flags);
int vfs_openat(int dir_fd, const char* name, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void* buf, int count);
int vfs_write(int fd, const void* buf, int count);
int vfs_readdir(int fd, vfs_dirent_t* out_entries, int max_entries);
int vfs_flock(int fd, vfs_lock_type_t lock_type);
int64_t vfs_seek(int fd, int64_t offset, vfs_seek_t whence);
int vfs_stat(int fd, vfs_stat_info_t* out_stat);
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_VFS_H