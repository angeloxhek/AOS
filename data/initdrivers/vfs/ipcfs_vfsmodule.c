#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <aos/types.h>
#include <aos/ipc.h>
#include <aos/syscalls.h>
#include <aos/vfs.h>

#include <vfs/disk_interface.h>
#include <vfs/fs_interface.h>

typedef struct ipcfs_context {
	apid_t driver_pid;
    char name[64];
    struct ipcfs_context* next;
} ipcfs_context_t;

typedef struct {
    ipcfs_context_t context;
    block_dev_t dev;
    fs_instance_t instance;
} ipcfs_instance_t;

typedef struct ipcfs_instance_node {
	ipcfs_instance_t inst;
	struct ipcfs_instance_node* next;
} ipcfs_instance_node_t;

typedef struct {
    fs_file_handle_t remote_fd;
} ipcfs_file_t;

ipcfs_instance_node_t* inst_head = NULL;

static ipcfs_instance_t* new_inst() {
	ipcfs_instance_node_t* inst = (ipcfs_instance_node_t*)malloc(sizeof(ipcfs_instance_node_t));
    if (!inst) return 0;
	memset(inst, 0, sizeof(ipcfs_instance_node_t));
	inst->next = inst_head;
	inst_head = inst;
	return &inst->inst;
}

static ipcfs_instance_node_t* get_inst_node(ipcfs_instance_t* inst) {
	if (!inst) return 0;
	ipcfs_instance_node_t* curr = inst_head;
	while (curr) {
		if (&curr->inst == inst) return curr;
		curr = curr->next;
	}
	return 0;
}

#define valid_inst(inst) (get_inst_node(inst) != NULL)

static int remove_inst(ipcfs_instance_t* inst) {
	if (!inst) return 0;
	ipcfs_instance_node_t* curr = inst_head;
	ipcfs_instance_node_t* prev = NULL;
	while (curr) {
		if (&curr->inst == inst) {
			if (prev == NULL) {
				inst_head = curr->next;
			} else {
				prev->next = curr->next;
			}
			
			free(curr);
			return 1;
		}
		prev = curr;
		curr = curr->next;
	}
	return 0;
}

fs_instance_t ipcfs_mount(block_dev_t* dev, fs_context_t context) {
    if (!context) return 0;
    
    ipcfs_instance_t* inst = new_inst();
    if (!inst) return 0;
    
    memcpy(&inst->context, context, sizeof(ipcfs_context_t));
    if (dev) memcpy(&inst->dev, dev, sizeof(block_dev_t));
    
    void* shm_data;
    uint64_t shm_id = shm_alloc(sizeof(block_dev_t), &shm_data);
    if (!shm_id) {
        free(inst);
        return 0;
    }
    
    if (dev) memcpy(shm_data, dev, sizeof(block_dev_t));
    shm_allow(shm_id, inst->context.driver_pid);
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_MOUNT_FS;
    req.param2 = shm_id;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) {
        free(inst);
        shm_free(shm_id);
        return 0;
    }
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    shm_free(shm_id);
    
    if (resp.param1 == VFS_ERR_OK) {
        inst->instance = (fs_instance_t)resp.param2;
        return (fs_instance_t)inst;
    }
    
    free(inst);
    return 0;
}

void ipcfs_umount(fs_instance_t fs) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
    if (!valid_inst(inst)) return;
    
    message_t req;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_SEND;
    req.param1 = VFS_CMD_UMOUNT_FS;
    req.param2 = (uint64_t)inst->instance;
    
    ipc_send(inst->context.driver_pid, &req);
    remove_inst(inst);
}

fs_file_handle_t ipcfs_open(fs_instance_t fs, const char* path, uint32_t flags) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return 0;
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_OPEN;
    req.param2 = flags;
    req.param3 = (uint64_t)inst->instance;
    
    strlcpy((char*)req.data, path, 55);
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) return 0;
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    if (resp.param1 == VFS_ERR_OK) {
        ipcfs_file_t* file = malloc(sizeof(ipcfs_file_t));
        if (!file) return 0;
        file->remote_fd = (fs_file_handle_t)resp.param2;
        return (fs_file_handle_t)file;
    }
    return 0;
}

fs_file_handle_t ipcfs_openat(fs_instance_t fs, fs_file_handle_t dir_handle, const char* path, uint32_t flags) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return 0;
    ipcfs_file_t* dir = (ipcfs_file_t*)dir_handle;
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_OPENAT;
    req.param2 = flags;
    req.param3 = (uint64_t)dir->remote_fd;
    
    strlcpy((char*)req.data, path, 55);
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) return 0;
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    if (resp.param1 == VFS_ERR_OK) {
        ipcfs_file_t* file = malloc(sizeof(ipcfs_file_t));
        if (!file) return 0;
        file->remote_fd = (fs_file_handle_t)resp.param2;
        return (fs_file_handle_t)file;
    }
    return 0;
}

int ipcfs_read(fs_instance_t fs, fs_file_handle_t file, void* buf, uint64_t size, uint64_t offset) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return -1;
    ipcfs_file_t* f = (ipcfs_file_t*)file;
    if (size == 0) return 0;
    
    void* shm_vaddr;
    uint64_t shm_id = shm_alloc(size, &shm_vaddr);
    if (!shm_id) return -1;
    shm_allow(shm_id, inst->context.driver_pid);
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_READ;
    req.param2 = (uint64_t)f->remote_fd;
    req.param3 = size;
    
    *(uint64_t*)(req.data) = shm_id;
    *(uint64_t*)(req.data + 8) = offset;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) { shm_free(shm_id); return -1; }
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    int bytes_read = -1;
    if (resp.param1 == VFS_ERR_OK) {
        bytes_read = (int)resp.param2;
        memcpy(buf, shm_vaddr, bytes_read);
    }
    
    shm_free(shm_id);
    return bytes_read;
}

int ipcfs_write(fs_instance_t fs, fs_file_handle_t file, const void* buf, uint64_t size, uint64_t offset) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return -1;
    ipcfs_file_t* f = (ipcfs_file_t*)file;
    if (size == 0) return 0;
    
    void* shm_vaddr;
    uint64_t shm_id = shm_alloc(size, &shm_vaddr);
    if (!shm_id) return -1;
    
    memcpy(shm_vaddr, buf, size);
    shm_allow(shm_id, inst->context.driver_pid);
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_WRITE;
    req.param2 = (uint64_t)f->remote_fd;
    req.param3 = size;
    
    *(uint64_t*)(req.data) = shm_id;
    *(uint64_t*)(req.data + 8) = offset;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) { shm_free(shm_id); return -1; }
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    int bytes_written = -1;
    if (resp.param1 == VFS_ERR_OK) {
        bytes_written = (int)resp.param2;
    }
    
    shm_free(shm_id);
    return bytes_written;
}

void ipcfs_close(fs_instance_t fs, fs_file_handle_t file) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return;
    ipcfs_file_t* f = (ipcfs_file_t*)file;
    if (!f) return;
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_CLOSE;
    req.param2 = (uint64_t)f->remote_fd;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id >= 0) {
        ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    }
    
    free(f);
}

int ipcfs_readdir(fs_instance_t fs, fs_file_handle_t dir, uint64_t* offset, fs_dirent_t* out_array, int max_entries) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return 0;
    ipcfs_file_t* f = (ipcfs_file_t*)dir;
    if (max_entries <= 0) return 0;
    
    uint64_t shm_size = max_entries * sizeof(fs_dirent_t);
    void* shm_vaddr;
    uint64_t shm_id = shm_alloc(shm_size, &shm_vaddr);
    if (!shm_id) return 0;
    shm_allow(shm_id, inst->context.driver_pid);
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_LIST;
    req.param2 = (uint64_t)f->remote_fd;
    req.param3 = max_entries;
    
    *(uint64_t*)(req.data) = shm_id;
    *(uint64_t*)(req.data + 8) = *offset;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) { shm_free(shm_id); return 0; }
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    int entries_read = 0;
    if (resp.param1 == VFS_ERR_OK) {
        entries_read = (int)resp.param2;
        *offset = *(uint64_t*)(resp.data); 
        if (entries_read > 0) {
            memcpy(out_array, shm_vaddr, entries_read * sizeof(fs_dirent_t));
        }
    }
    
    shm_free(shm_id);
    return entries_read;
}

int ipcfs_stat(fs_instance_t fs, fs_file_handle_t file, fs_stat_t* out_info) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return -1;
    ipcfs_file_t* f = (ipcfs_file_t*)file;
    
    void* shm_vaddr;
    uint64_t shm_id = shm_alloc(sizeof(fs_stat_t), &shm_vaddr);
    if (!shm_id) return -1;
    shm_allow(shm_id, inst->context.driver_pid);
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_STAT;
    req.param2 = (uint64_t)f->remote_fd;
    *(uint64_t*)(req.data) = shm_id;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) { shm_free(shm_id); return -1; }
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    int res = -1;
    if (resp.param1 == VFS_ERR_OK) {
        memcpy(out_info, shm_vaddr, sizeof(fs_stat_t));
        res = 0;
    }
    
    shm_free(shm_id);
    return res;
}

void ipcfs_get_label(fs_instance_t fs, char* out_label) {
    ipcfs_instance_t* inst = (ipcfs_instance_t*)fs;
	if (!valid_inst(inst)) return;
    
    strlcpy(out_label, "NO_NAME", 12);
    
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_VFS;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VFS_CMD_GET_LABEL_FS;
    req.param2 = (uint64_t)inst->instance;
    
    int64_t tx_id = ipc_send(inst->context.driver_pid, &req);
    if (tx_id < 0) return;
    
    ipc_recv_ex(inst->context.driver_pid, MSG_TYPE_VFS, MSG_SUBTYPE_RESPONSE, (uint64_t)tx_id, MSG_FLAG_NONE, &resp);
    
    if (resp.param1 == VFS_ERR_OK) {
        strlcpy(out_label, (char*)resp.data, 12);
    }
}

fs_driver_t ipcfs_driver = {
    .name = "ipcfs",
    .mount = ipcfs_mount,
    .umount = ipcfs_umount,
    .open = ipcfs_open,
    .openat = ipcfs_openat,
    .read = ipcfs_read,
    .write = ipcfs_write,
    .close = ipcfs_close,
    .readdir = ipcfs_readdir,
    .stat = ipcfs_stat,
    .get_label = ipcfs_get_label
};