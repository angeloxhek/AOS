#include "include/kernel_internal.h"

static uint64_t vfs_driver_tid = 0;

#define ensure_vfs_init() { if (vfs_driver_tid == 0) vfs_driver_tid = get_driver_tid(DT_VFS); }

void vfs_init() {
    ensure_vfs_init();
}

static int vfs_rpc_call(message_t* req, message_t* resp_out) {
    ensure_vfs_init();

    req->type = MSG_TYPE_VFS;

    ipc_send(vfs_driver_tid, req);

    ipc_receive_ex(
        vfs_driver_tid,
        MSG_TYPE_VFS,
        MSG_SUBTYPE_NONE,
        resp_out
    );

    if (resp_out->param1 == VFS_ERR_OK) {
        return 0;
    }
    
    return -1;
}

int vfs_open(const char* path, uint32_t flags) {
    if (!path) return -1;
	int len = kernel_strnlen(path, 64);
	
    message_t req;
    message_t resp;

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_OPEN;
	req.param2 = flags;
    kernel_memcpy(req.data, path, len + 1);

    if (vfs_rpc_call(&req, &resp) == 0) {
        return (int)resp.param2;
    }
    
    return -1;
}

int vfs_openat(int dir_fd, const char* name, uint32_t flags) {
    if (dir_fd < 0 || !name) return -1;
    int len = kernel_strnlen(name, 64);
    
    message_t req;
    message_t resp;

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_OPENAT;
	req.param2 = flags;
    req.param3 = dir_fd;
    kernel_memcpy(req.data, name, len + 1);

    if (vfs_rpc_call(&req, &resp) == 0) {
        return (int)resp.param2;
    }
    
    return -1;
}

int vfs_close(int fd) {
    if (fd < 0) return -1;

    message_t req;
    message_t resp;

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_CLOSE;
    req.param2 = fd;

    if (vfs_rpc_call(&req, &resp) == 0) {
        return 0;
    }
    
    return -1;
}

int vfs_read(int fd, void* buf, int count) {
    if (fd < 0 || !buf || count == 0) return 0;
    
    ensure_vfs_init();

    uint64_t shm_vaddr = 0;
    uint64_t shm_id = shm_alloc((uint64_t)count, &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, vfs_driver_tid);

    message_t req;
    message_t resp;
    kernel_memset(&req, 0, sizeof(message_t));
	
	req.subtype = MSG_SUBTYPE_PING;
    req.param1 = VFS_CMD_READ;
    req.param2 = fd;
    req.param3 = count;
    
    *(uint64_t*)(req.data) = shm_id;
	
    int bytes_read = -1;

    if (vfs_rpc_call(&req, &resp) == 0) {
        bytes_read = (int)resp.param2;
        
        if (bytes_read > 0) {
            kernel_memcpy(buf, (void*)shm_vaddr, bytes_read);
        }
    }

    shm_free(shm_id);

    return bytes_read;
}

int vfs_write(int fd, const void* buf, int count) {
    if (fd < 0 || !buf || count == 0) return 0;

    ensure_vfs_init();

    uint64_t shm_vaddr = 0;
    uint64_t shm_id = shm_alloc((uint64_t)count, &shm_vaddr);
    if (!shm_id) return -1;

    kernel_memcpy((void*)shm_vaddr, buf, count);

    shm_allow(shm_id, vfs_driver_tid);

    message_t req;
    message_t resp;
    kernel_memset(&req, 0, sizeof(message_t));

	req.subtype = MSG_SUBTYPE_PING;
    req.param1 = VFS_CMD_WRITE;
    req.param2 = fd;
    req.param3 = count;
    *(uint64_t*)(req.data) = shm_id;

    int bytes_written = -1;

    if (vfs_rpc_call(&req, &resp) == 0) {
        bytes_written = (int)resp.param2;
    }
    
    shm_free(shm_id);

    return bytes_written;
}

int vfs_readdir(int fd, vfs_dirent_t* out_entries, int max_entries) {
    if (fd < 0 || !out_entries || max_entries <= 0) return 0;
    
    ensure_vfs_init();

    uint64_t shm_vaddr = 0;
    uint64_t size = max_entries * sizeof(vfs_dirent_t);
    uint64_t shm_id = shm_alloc(size, &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, vfs_driver_tid);

    message_t req;
    message_t resp;
    kernel_memset(&req, 0, sizeof(message_t));

	req.subtype = MSG_SUBTYPE_PING;
    req.param1 = VFS_CMD_LIST;
    req.param2 = fd;
    req.param3 = max_entries;
    *(uint64_t*)(req.data) = shm_id;

    int entries_read = 0;

    if (vfs_rpc_call(&req, &resp) == 0) {
        entries_read = (int)resp.param2;
        
        if (entries_read > 0) {
            kernel_memcpy(out_entries, (void*)shm_vaddr, entries_read * sizeof(vfs_dirent_t));
        }
    } else {
        entries_read = -1;
    }

    shm_free(shm_id);

    return entries_read;
}

int vfs_flock(int fd, vfs_lock_type_t lock_type) {
    if (fd < 0) return -1;

    ensure_vfs_init();

    message_t req;
    message_t resp;
    kernel_memset(&req, 0, sizeof(message_t));

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_FLOCK;
    req.param2 = fd;
    req.param3 = lock_type;

    if (vfs_rpc_call(&req, &resp) == 0) {
        return 0;
    }
    
    return -1;
}

int64_t vfs_seek(int fd, int64_t offset, vfs_seek_t whence) {
    if (fd < 0) return -1;

    ensure_vfs_init();

    message_t req;
    message_t resp;
    kernel_memset(&req, 0, sizeof(message_t));

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_SEEK;
    req.param2 = fd;
    req.param3 = whence;
    
    *(int64_t*)(req.data) = offset;

    if (vfs_rpc_call(&req, &resp) == 0) {
        return *(int64_t*)(resp.data);
    }
    
    return -1;
}

int vfs_stat(int fd, vfs_stat_info_t* out_stat) {
    if (fd < 0 || !out_stat) return -1;

    ensure_vfs_init();

    uint64_t shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(vfs_stat_info_t), &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, vfs_driver_tid);

    message_t req;
    message_t resp;
    kernel_memset(&req, 0, sizeof(message_t));

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_STAT;
    req.param2 = fd;
    *(uint64_t*)(req.data) = shm_id;

    int result = -1;
    if (vfs_rpc_call(&req, &resp) == 0) {
        kernel_memcpy(out_stat, (void*)shm_vaddr, sizeof(vfs_stat_info_t));
        result = 0;
    }

    shm_free(shm_id);
    return result;
}

int vfs_read_from_path(const char* user_path, uint8_t* data, char* name, uint64_t* size) {
	int fd = vfs_open(user_path, VFS_FREAD);
	if (fd < 0) {
		return -1;
	}
	
	vfs_stat_info_t* stat = (vfs_stat_info_t*)kernel_malloc(sizeof(vfs_stat_info_t));
	kernel_memset(stat, 0, sizeof(vfs_stat_info_t));
	if (vfs_stat(fd, stat)) {
		vfs_close(fd);
		return -1;
	}
	
	if (stat->size_bytes == 0 || stat->size_bytes == -1) {
		vfs_close(fd);
		return -1;
	}
	
	if (name) kernel_memcpy(name, stat->name, 256);
	if (size) *size = stat->size_bytes;
	
	data = (uint8_t*)kernel_malloc(stat->size_bytes);
	if (!data) {
		return -2;
	}
	
	uint64_t total_read = 0;
	while (total_read < stat->size_bytes) {
		int res = vfs_read(fd, (void*)(data + total_read), (int)(stat->size_bytes - total_read));
		if (res <= 0) break;
		total_read += res;
	}
	vfs_close(fd);
	
	if (total_read != stat->size_bytes) {
		kernel_free(data);
		return -1;
	}
	
	return 0;
}