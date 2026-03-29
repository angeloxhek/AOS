#include "../include/aoslib.h"

int64_t block_read(block_dev_t* dev, uint64_t lba, uint64_t count, void* buffer) {
    return (int64_t)syscall(SYS_BLOCK_READ, dev->disk_id, dev->partition_offset_lba + lba, count, (uint64_t)buffer, 0);
}

int64_t block_write(block_dev_t* dev, uint64_t lba, uint64_t count, void* buffer) {
    return (int64_t)syscall(SYS_BLOCK_WRITE, dev->disk_id, dev->partition_offset_lba + lba, count, (uint64_t)buffer, 0);
}

static uint64_t vfs_server_tid = 0;

static void ensure_vfs_init() {
    if (vfs_server_tid == 0) {
        vfs_server_tid = get_driver_tid(DT_VFS);
    }
}

void vfs_init() {
    ensure_vfs_init();
}

static int vfs_rpc_call(message_t* req, message_t* resp_out) {
    ensure_vfs_init();

    req->type = MSG_TYPE_VFS;
    req->subtype = MSG_SUBTYPE_QUERY;

    ipc_send(vfs_server_tid, req);

    ipc_recv_ex(
        vfs_server_tid,
        MSG_TYPE_VFS,
        MSG_SUBTYPE_RESPONSE,
        resp_out
    );

    if (resp_out->param1 == VFS_ERR_OK) {
        return 0;
    }
    
    return -1;
}

int vfs_open(const char* path) {
    if (!path) return -1;
	int len = strlen(path);
    if (len >= 64) return -1;
	
    message_t req;
    message_t resp;

    req.param1 = VFS_CMD_OPEN;
    memcpy(req.data, path, len + 1);

    if (vfs_rpc_call(&req, &resp) == 0) {
        return (int)resp.param2;
    }
    
    return -1;
}

int vfs_close(int fd) {
    if (fd < 0) return -1;

    message_t req;
    message_t resp;

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

    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc((uint64_t)count, &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, vfs_server_tid);

    message_t req;
    message_t resp;
    memset(&req, 0, sizeof(message_t));
	
    req.param1 = VFS_CMD_READ;
    req.param2 = fd;
    req.param3 = count;
    
    *(uint64_t*)(req.data) = shm_id;
	
    int bytes_read = -1;

    if (vfs_rpc_call(&req, &resp) == 0) {
        bytes_read = (int)resp.param2;
        
        if (bytes_read > 0) {
            memcpy(buf, shm_vaddr, bytes_read);
        }
    }

    shm_free(shm_id);

    return bytes_read;
}

int vfs_write(int fd, const void* buf, int count) {
    if (fd < 0 || !buf || count == 0) return 0;

    ensure_vfs_init();

    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc((uint64_t)count, &shm_vaddr);
    if (!shm_id) return -1;

    memcpy(shm_vaddr, buf, count);

    shm_allow(shm_id, vfs_server_tid);

    message_t req;
    message_t resp;
    memset(&req, 0, sizeof(message_t));

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

    void* shm_vaddr = 0;
    uint64_t size = max_entries * sizeof(vfs_dirent_t);
    uint64_t shm_id = shm_alloc(size, &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, vfs_server_tid);

    message_t req;
    message_t resp;
    memset(&req, 0, sizeof(message_t));

    req.param1 = VFS_CMD_LIST;
    req.param2 = fd;
    req.param3 = max_entries;
    *(uint64_t*)(req.data) = shm_id;

    int entries_read = 0;

    if (vfs_rpc_call(&req, &resp) == 0) {
        entries_read = (int)resp.param2;
        
        if (entries_read > 0) {
            // Данные уже лежат в SHM. Копируем их в массив пользователя.
            memcpy(out_entries, shm_vaddr, entries_read * sizeof(vfs_dirent_t));
        }
    } else {
        entries_read = -1;
    }

    // 4. Уничтожаем
    shm_free(shm_id);

    return entries_read;
}