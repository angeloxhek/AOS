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

    message_t req;
    message_t resp;

    req.param1 = VFS_CMD_OPEN;
    req.payload_ptr = (uint8_t*)path;
    req.payload_size = strlen(path) + 1;

    resp.payload_ptr = 0;
    resp.payload_size = 0;

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
    
    req.payload_ptr = 0;
    req.payload_size = 0;
    resp.payload_ptr = 0;
    resp.payload_size = 0;

    if (vfs_rpc_call(&req, &resp) == 0) {
        return 0; // Успех
    }
    
    return -1;
}

int vfs_read(int fd, void* buf, int count) {
    if (fd < 0 || !buf || count == 0) return 0;
    message_t req;
    message_t resp;
	
    req.param1 = VFS_CMD_READ;
    req.param2 = fd;
    req.param3 = count;
    req.payload_ptr = 0;
    req.payload_size = 0;
	
    resp.payload_ptr = 0;
    resp.payload_size = 0;
	
    if (vfs_rpc_call(&req, &resp) == 0) {
        if (resp.payload_ptr) {
            uint64_t copy_size = resp.payload_size;
            if (copy_size > (uint64_t)count) {
                copy_size = count;
            }
			
            memcpy(buf, resp.payload_ptr, copy_size);
            free(resp.payload_ptr);
            return (int)copy_size; 
        }
        return 0;
    }
    return -1;
}

int vfs_write(int fd, const void* buf, int count) {
    if (fd < 0 || !buf || count == 0) return 0;

    message_t req;
    message_t resp;

    req.param1 = VFS_CMD_WRITE;
    req.param2 = fd;
    
    req.payload_ptr = (uint8_t*)buf;
    req.payload_size = count;

    resp.payload_ptr = 0;
    resp.payload_size = 0;

    if (vfs_rpc_call(&req, &resp) == 0) {
        return (int)resp.payload_size;
    }
    
    return -1;
}

int vfs_readdir(int fd, int index, vfs_dirent_t* out_entry) {
    if (fd < 0 || !out_entry) return 0;
    message_t req;
    message_t resp;

    req.param1 = VFS_CMD_LIST;
    req.param2 = fd;
    req.param3 = index;
    req.payload_ptr = 0;
    req.payload_size = 0;

    resp.payload_ptr = 0;
    resp.payload_size = 0;

    if (vfs_rpc_call(&req, &resp) == 0) {
        if (resp.payload_ptr) {
            uint64_t copy_size = resp.payload_size;
            if (copy_size > sizeof(vfs_dirent_t)) {
                copy_size = sizeof(vfs_dirent_t);
            }
            memcpy(out_entry, resp.payload_ptr, copy_size);
            
            free(resp.payload_ptr);
        }
        return 1;
    }
    return 0;
}