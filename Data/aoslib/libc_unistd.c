#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <aos/vfs.h>
#include <aos/syscalls.h>
#include <string.h>

int open(const char *pathname, int flags, ...) {
    uint32_t aos_flags = 0;
    
    if ((flags & O_ACCMODE) == O_WRONLY) {
        aos_flags |= VFS_FWRITE;
    } else if ((flags & O_ACCMODE) == O_RDWR) {
        aos_flags |= VFS_FRW;
    } else {
        aos_flags |= VFS_FREAD;
    }
    
    if (flags & O_CREAT)  aos_flags |= VFS_FCREATE;
    if (flags & O_TRUNC)  aos_flags |= VFS_FTRUNC;
    if (flags & O_APPEND) aos_flags |= VFS_FAPPEND;

    return vfs_open(pathname, aos_flags);
}

ssize_t read(int fd, void *buf, size_t count) {
    if (fd < 0 || buf == NULL) return -1;
    if (count == 0) return 0;

    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        return -1;
    }

    return (ssize_t)vfs_read(fd, buf, (int)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (fd < 0 || buf == NULL) return -1;
    if (count == 0) return 0;

    if (fd == STDIN_FILENO) {
        return -1;
    }

    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        
        if (count < 256) {
            char temp_buf[256];
            memcpy(temp_buf, buf, count);
            temp_buf[count] = '\0';
            sysprint(temp_buf);
            return (ssize_t)count;
        }

        char *dyn_buf = (char *)malloc(count + 1);
        if (dyn_buf != NULL) {
            memcpy(dyn_buf, buf, count);
            dyn_buf[count] = '\0';
            
            sysprint(dyn_buf);
            
            free(dyn_buf);
            return (ssize_t)count;
        }

        size_t written = 0;
        const char* ptr = (const char*)buf;
        char temp_buf[256];

        while (written < count) {
            size_t chunk = (count - written < 255) ? (count - written) : 255;
            memcpy(temp_buf, ptr + written, chunk);
            temp_buf[chunk] = '\0';
            
            sysprint(temp_buf);
            written += chunk;
        }
        return (ssize_t)written;
    }

    return (ssize_t)vfs_write(fd, buf, (int)count);
}

int close(int fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        return 0;
    }
    return vfs_close(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        return -1; 
    }
    
    return (off_t)vfs_seek(fd, (int64_t)offset, (vfs_seek_t)whence);
}