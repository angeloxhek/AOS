#include <stdint.h>
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

extern fs_driver_t fat32_driver; 

typedef enum {
    VFS_TYPE_DIR,
    VFS_TYPE_DEVICE_FILE,
    VFS_TYPE_MOUNT_POINT,
    VFS_TYPE_SYMLINK
} vfs_node_type_t;

typedef struct vfs_node {
    char name[64];
    vfs_node_type_t type;
    
    struct vfs_node* parent;
    struct vfs_node* children;
    struct vfs_node* next;

    union {
        struct {
            int (*read)(void* param, void* buf, uint64_t size, uint64_t offset);
            int (*write)(void* param, void* buf, uint64_t size, uint64_t offset);
            void* param;
        } dev_ops;

        struct {
            fs_driver_t* driver;
            fs_instance_t fs_inst;
        } mount;

        char target_path[128];
    };
} vfs_node_t;

vfs_node_t* vfs_root = 0;
vfs_node_t* ptasks = 0;
vfs_node_t* ttasks = 0;

typedef struct {
    int id;
    uint64_t owner_tid;
    int used;
    uint64_t offset;
    vfs_node_type_t type; 
    union {
        struct {
            fs_driver_t* driver;
            fs_instance_t fs;
            fs_file_handle_t handle;
        } mounted_file;

        struct {
            int (*read)(void* param, void* buf, uint64_t size, uint64_t offset);
            int (*write)(void* param, void* buf, uint64_t size, uint64_t offset);
            void* param;
        } device_file;

        struct {
            vfs_node_t* node;
        } dir;
    };
} vfs_file_t;


#define MAX_OPEN_FILES 1024
vfs_file_t open_files[MAX_OPEN_FILES];

int vfs_alloc_fd(uint64_t tid) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].used) {
			memset(&open_files[i], 0, sizeof(vfs_file_t));
            open_files[i].used = 1;
            open_files[i].id = i + 1;
            open_files[i].owner_tid = tid;
            open_files[i].offset = 0;
            return i + 1;
        }
    }
    return -1;
}

vfs_file_t* vfs_get_file(int fd, uint64_t tid) {
    if (fd < 1 || fd > MAX_OPEN_FILES) return 0;
    vfs_file_t* f = &open_files[fd - 1];
    if (!f->used) return 0;
    if (f->owner_tid != tid) return 0;
    return f;
}

int dev_read_raw(void* param, void* buf, uint64_t size, uint64_t offset) {
    block_dev_t* dev = (block_dev_t*)param;
    uint64_t lba = offset / 512;
    uint64_t count = (size + 511) / 512;
    return block_read(dev, lba, count, buf); 
}

int dev_write_raw(void* param, void* buf, uint64_t size, uint64_t offset) {
    block_dev_t* dev = (block_dev_t*)param;
    uint64_t lba = offset / 512;
    uint64_t count = (size + 511) / 512;
    return block_write(dev, lba, count, buf); 
}

int dev_read_uptime(void* param, void* buf, uint64_t size, uint64_t offset) {
    system_info_t info;
    get_sysinfo(&info);

    char text[32];
    sprintf(text, "%d\n", (int)info.uptime);
    
    int len = strlen(text);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buf, text + offset, size);
    return size;
}

int dev_read_flags(void* param, void* buf, uint64_t size, uint64_t offset) {
    system_info_t info;
    get_sysinfo(&info);

    char text[32];
    sprintf(text, "%d\n", (int)info.flags);
    
    int len = strlen(text);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buf, text + offset, size);
    return size;
}

int dev_read_zero(void* param, void* buf, uint64_t size, uint64_t offset) {
    memset(buf, 0, size);
    return size;
}

int dev_read_null(void* param, void* buf, uint64_t size, uint64_t offset) {
    return 0;
}

int dev_write_null(void* param, void* buf, uint64_t size, uint64_t offset) {
    return size;
}

static uint64_t rand_seed = 123456789;

int dev_read_urandom(void* param, void* buf, uint64_t size, uint64_t offset) {
    uint8_t* b = (uint8_t*)buf;
    
    if (rand_seed == 123456789) {
        system_info_t info;
        if (get_sysinfo(&info) == 0) {
            rand_seed ^= info.uptime;
        }
    }

    for (uint64_t i = 0; i < size; i++) {
        rand_seed = rand_seed * 6364136223846793005ULL + 1442695040888963407ULL;
        b[i] = (uint8_t)(rand_seed >> 56);
    }
    return size;
}

int dev_write_ctl(void* param, void* buf, uint64_t size, uint64_t offset) {
    block_dev_t* dev = (block_dev_t*)param;
    char cmd[32];
    strncpy(cmd, (char*)buf, size < 31 ? size : 31);
    
    if (strcmp(cmd, "eject") == 0) {
        // syscall(SYS_EJECT_DISK, dev->disk_id...);
        printf("VFS: Ejecting disk %d\n", dev->disk_id);
    }
    return size;
}

vfs_node_t* vfs_mkdir(vfs_node_t* parent, const char* name) {
	if (!parent || !name) return 0;
    vfs_node_t* node = calloc(1, sizeof(vfs_node_t));
	if (!node || node == parent) return 0;
    strlcpy(node->name, name, sizeof(node->name));
    node->type = VFS_TYPE_DIR;
    node->parent = parent;
    
    node->next = parent->children;
    parent->children = node;
    return node;
}

void vfs_mkdev(vfs_node_t* parent, const char* name, void* read_func, void* write_func, void* param) {
	if (!name) return;
    vfs_node_t* node = vfs_mkdir(parent, name);
	if (!node) return;
    node->type = VFS_TYPE_DEVICE_FILE;
    node->dev_ops.read = read_func;
	node->dev_ops.write = write_func;
    node->dev_ops.param = param;
}

void vfs_symlink(vfs_node_t* parent, const char* name, const char* target) {
	if (!name || !target) return;
    vfs_node_t* node = vfs_mkdir(parent, name);
	if (!node) return; 
    node->type = VFS_TYPE_SYMLINK;
    strlcpy(node->target_path, target, sizeof(node->target_path));
}

vfs_node_t* find_child(vfs_node_t* parent, const char* name) {
    if (!parent || !name) return 0;
    
    vfs_node_t* child = parent->children;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next;
    }
    return 0;
}

void vfs_add_proc(uint64_t pid, const char* proc_name) {
    char pid_str[21];
    ulltoa((unsigned long long)pid, pid_str, 10); 
    
    vfs_node_t* proc_node = vfs_mkdir(ptasks, pid_str);
    if (!proc_node) return;
	
    vfs_mkdev(proc_node, "name",  0, 0, (void*)pid);
    vfs_mkdev(proc_node, "state", 0, 0, (void*)pid);
    vfs_mkdev(proc_node, "mem",   0, 0, (void*)pid);
    vfs_mkdev(proc_node, "ctl",   0, 0, (void*)pid);
	
    vfs_mkdir(proc_node, "threads");
}

void vfs_proc_add_thread(uint64_t tid, uint64_t parent_pid) {
    char tid_str[21];
    char pid_str[21];
    
    ulltoa((unsigned long long)tid, tid_str, 10);
    ulltoa((unsigned long long)parent_pid, pid_str, 10);
	
    vfs_node_t* thread_node = vfs_mkdir(ttasks, tid_str);
    if (!thread_node) return;

    vfs_mkdev(thread_node, "status",  0, 0, (void*)tid);
    vfs_mkdev(thread_node, "drvinfo", 0, 0, (void*)tid);
    
    vfs_mkdir(thread_node, "fd");

    char proc_target[64];
    sprintf(proc_target, "/tasks/p/%s", pid_str);
    vfs_symlink(thread_node, "proc", proc_target);

    vfs_node_t* parent_proc_node = find_child(ptasks, pid_str);
    if (parent_proc_node) {
        vfs_node_t* threads_dir = find_child(parent_proc_node, "threads");
        if (threads_dir) {
            char thread_target[64];
            sprintf(thread_target, "/tasks/t/%s", tid_str);
            vfs_symlink(threads_dir, tid_str, thread_target);
        }
    }
}

void vfs_init_tree() {
    vfs_root = calloc(1, sizeof(vfs_node_t));
    vfs_root->type = VFS_TYPE_DIR;

    vfs_node_t* hw   = vfs_mkdir(vfs_root, "hw");
	vfs_mkdev(hw, "null", dev_read_null, dev_write_null, 0);
	vfs_mkdev(hw, "zero", dev_read_zero, dev_write_null, 0);
	vfs_mkdev(hw, "urandom", dev_read_urandom, dev_write_null, 0);

    vfs_node_t* sys  = vfs_mkdir(vfs_root, "sys");
    vfs_node_t* tasks = vfs_mkdir(vfs_root, "tasks");
	ptasks = vfs_mkdir(tasks, "p");
	ttasks = vfs_mkdir(tasks, "t");
    vfs_node_t* mnt  = vfs_mkdir(vfs_root, "mnt");
    vfs_node_t* mnt_id = vfs_mkdir(mnt, "id");

    vfs_node_t* sysstat  = vfs_mkdir(sys, "stat");
	vfs_mkdev(sysstat, "uptime", dev_read_uptime, 0, 0);
	vfs_mkdev(sysstat, "flags", dev_read_flags, 0, 0);
	

    uint64_t disk_count = get_disk_count();
    for (int i = 0; i < disk_count; i++) {
        char name[16];
        sprintf(name, "ide%d", i);
        vfs_node_t* disk_node = vfs_mkdir(hw, name);
        block_dev_t* raw_disk = malloc(sizeof(block_dev_t));
		if (!raw_disk) return;
		memset(raw_disk, 0, sizeof(block_dev_t));
        raw_disk->disk_id = i;
        raw_disk->partition_offset_lba = 0;
		
        vfs_mkdev(disk_node, "raw", dev_read_raw, dev_write_raw, raw_disk);
        vfs_mkdev(disk_node, "ctl", 0, 0, raw_disk);
    }

    uint64_t part_count = get_partition_count();
    for (int i = 0; i < part_count; i++) {
        partition_info_t pinfo;
        get_partition_info(i, &pinfo);
		
        char disk_name[16];
        sprintf(disk_name, "ide%d", (int)pinfo.parent_disk_id);
        vfs_node_t* disk_node = find_child(hw, disk_name); 
		
        char part_name[16];
        sprintf(part_name, "v%d", (int)pinfo.id);
		
        vfs_node_t* p_node = vfs_mkdir(disk_node, part_name);
        block_dev_t* part_dev = malloc(sizeof(block_dev_t));
		if (!part_dev) return;
		memset(part_dev, 0, sizeof(block_dev_t));
        part_dev->disk_id = pinfo.parent_disk_id;
        part_dev->partition_offset_lba = pinfo.start_lba;
        part_dev->size_sectors = pinfo.size_sectors;
        
        vfs_mkdev(p_node, "raw", dev_read_raw, dev_write_raw, part_dev);

        fs_instance_t fs_inst = fat32_driver.mount(part_dev);
        if (fs_inst) {
            vfs_node_t* fs_node = vfs_mkdir(p_node, "fs");
            fs_node->type = VFS_TYPE_MOUNT_POINT;
            fs_node->mount.driver = &fat32_driver;
            fs_node->mount.fs_inst = fs_inst;
            
            char target[64];
            sprintf(target, "/hw/%s/%s/fs", disk_name, part_name);
            
            char link_name[16];
            sprintf(link_name, "v%d", i);
            
            vfs_symlink(mnt_id, link_name, target);
			
            char label[12];
            memset(label, 0, 12);
            strcpy(label, "NO_NAME"); 
            
            if (fs_node->mount.driver->get_label) {
                fs_node->mount.driver->get_label(fs_inst, label);
            }
            
            if (strcmp(label, "NO_NAME") != 0) {
                 char id_link[32];
                 sprintf(id_link, "/mnt/id/v%d", i);
                 vfs_symlink(mnt, label, id_link);
            }
        }
    }
}

typedef struct {
    vfs_node_t* node;
    char* fs_path;
    int error;
} vfs_path_result_t;

void vfs_resolve(const char* path, vfs_path_result_t* out) {
    out->node = 0;
    out->fs_path = 0;
    out->error = VFS_ERR_NOFILE;

    if (!path || !out || path[0] != '/') return;

    vfs_node_t* current = vfs_root;
    
    char* path_copy = strdup(path); 
    char* cursor = path_copy + 1;
    
    while (1) {
        if (current->type == VFS_TYPE_MOUNT_POINT) {
            
            out->node = current;
            if (*cursor == 0) {
                out->fs_path = strdup("/");
            } else {
                out->fs_path = strdup(cursor);
            }
            out->error = 0;
            free(path_copy);
            return;
        }

        if (*cursor == 0) {
            out->node = current;
            out->error = 0;
            free(path_copy);
            return;
        }

        char* next_slash = strchr(cursor, '/');
        if (next_slash) {
            *next_slash = 0;
        }

        char* token = cursor;

        vfs_node_t* next_node = 0;
        vfs_node_t* child = current->children;
        while (child) {
            if (strcmp(child->name, token) == 0) {
                next_node = child;
                break;
            }
            child = child->next;
        }

        if (!next_node) {
            if (strcmp(current->name, "proc") == 0) {
                // Тут можно проверить, число ли 'token', и создать узел динамически
                // next_node = vfs_create_proc_node(token);
            }
        }

        if (!next_node) {
            out->error = VFS_ERR_NOFILE;
            free(path_copy);
            return;
        }

        if (next_node->type == VFS_TYPE_SYMLINK) {
            
            static int recursion_depth = 0;
            if (recursion_depth > 8) {
                out->error = VFS_ERR_SYMLINKLOOP; // Loop detected
                free(path_copy);
                return;
            }

            char* remainder = next_slash ? (next_slash + 1) : "";
            
            int new_len = strlen(next_node->target_path) + 1 + strlen(remainder) + 1;
            char* new_full_path = malloc(new_len);
			if (!new_full_path) return;
			memset(new_full_path, 0, new_len);            
            strlcpy(new_full_path, next_node->target_path, new_len);
            if (*remainder) {
                strlcat(new_full_path, "/", new_len);
                strlcat(new_full_path, remainder, new_len);
            }

            recursion_depth++;
            vfs_resolve(new_full_path, out);
            recursion_depth--;

            free(new_full_path);
            free(path_copy);
            return;
        }

        current = next_node;
        
        if (next_slash) {
            cursor = next_slash + 1;
        } else {
            cursor = cursor + strlen(cursor);
        }
    }
}

void handle_vfs_request(message_t* req) {
    message_t resp;
	memset(&resp, 0, sizeof(message_t));
    resp.type = MSG_TYPE_VFS;
    resp.subtype = MSG_SUBTYPE_RESPONSE;
    resp.sender_tid = req->sender_tid;
    resp.payload_ptr = 0;
    resp.payload_size = 0;

    switch (req->param1) {
        case VFS_CMD_OPEN: {
            char* path = (char*)req->payload_ptr;
            if (!path) { resp.param1 = VFS_ERR_UNKNOWN; break; }

            vfs_path_result_t res;
            vfs_resolve(path, &res);

            if (res.error != 0) {
                resp.param1 = res.error;
                break;
            }

            int fd = vfs_alloc_fd(req->sender_tid);
            if (fd < 0) {
                resp.param1 = VFS_ERR_UNKNOWN;
                if (res.fs_path) free(res.fs_path);
                break;
            }

            vfs_file_t* f = &open_files[fd - 1];
            f->offset = 0;

            if (res.node->type == VFS_TYPE_MOUNT_POINT) {
                f->type = VFS_TYPE_MOUNT_POINT;
                f->mounted_file.driver = res.node->mount.driver;
                f->mounted_file.fs = res.node->mount.fs_inst;
                
                char* p = (res.fs_path && *res.fs_path) ? res.fs_path : "/";
                
                if (f->mounted_file.driver && f->mounted_file.driver->open) {
                    f->mounted_file.handle = f->mounted_file.driver->open(f->mounted_file.fs, p);
                } else {
                    f->mounted_file.handle = 0;
                }
                
                if (!f->mounted_file.handle) {
                    f->used = 0; // Освобождаем слот
                    resp.param1 = VFS_ERR_NOFILE;
                } else {
                    resp.param1 = VFS_ERR_OK;
                    resp.param2 = fd;
                }
            } 
            else if (res.node->type == VFS_TYPE_DEVICE_FILE) {
                f->type = VFS_TYPE_DEVICE_FILE;
                f->device_file.read = res.node->dev_ops.read;
                f->device_file.write = res.node->dev_ops.write;
                f->device_file.param = res.node->dev_ops.param;
                
                resp.param1 = VFS_ERR_OK;
                resp.param2 = fd;
            }
            else if (res.node->type == VFS_TYPE_DIR) {
                f->type = VFS_TYPE_DIR;
                f->dir.node = res.node;
                
                resp.param1 = VFS_ERR_OK;
                resp.param2 = fd;
            }
            else {
                f->used = 0;
                resp.param1 = VFS_ERR_PERM;
            }

            if (res.fs_path) free(res.fs_path);
            break;
        }

        case VFS_CMD_READ: {
            int fd = req->param2;
            uint64_t size = req->param3;
            
            vfs_file_t* f = vfs_get_file(fd, req->sender_tid);
            if (!f) { resp.param1 = VFS_ERR_PERM; break; }

            if (f->type == VFS_TYPE_DIR) {
                resp.param1 = VFS_ERR_ISDIR;
                break;
            }

            void* buf = malloc(size);
			if (!buf) break;
			memset(buf, 0, size);
            int bytes = -1;

            if (f->type == VFS_TYPE_MOUNT_POINT) {
                if (f->mounted_file.driver && f->mounted_file.driver->read) {
                    bytes = f->mounted_file.driver->read(
                        f->mounted_file.fs, f->mounted_file.handle, buf, size, f->offset
                    );
                }
            } else if (f->type == VFS_TYPE_DEVICE_FILE && f->device_file.read) {
                bytes = f->device_file.read(f->device_file.param, buf, size, f->offset);
            }

            if (bytes >= 0) {
                f->offset += bytes;
                resp.param1 = VFS_ERR_OK;
                resp.payload_ptr = (uint8_t*)buf;
                resp.payload_size = bytes;
            } else {
                resp.param1 = VFS_ERR_UNKNOWN;
                resp.payload_size = 0;
            }
            
            ipc_send(req->sender_tid, &resp);
            free(buf);
            return;
        }
		
		case VFS_CMD_WRITE: {
            int fd = req->param2;
            uint64_t size = req->param3;
            
            vfs_file_t* f = vfs_get_file(fd, req->sender_tid);
            if (!f) { resp.param1 = VFS_ERR_PERM; break; }

            if (f->type == VFS_TYPE_DIR) {
                resp.param1 = VFS_ERR_ISDIR;
                break;
            }

            int bytes = -1;

            if (f->type == VFS_TYPE_MOUNT_POINT) {
                if (f->mounted_file.driver && f->mounted_file.driver->write) {
                    bytes = f->mounted_file.driver->write(
                        f->mounted_file.fs, f->mounted_file.handle, req->payload_ptr, size, f->offset
                    );
                }
            } else if (f->type == VFS_TYPE_DEVICE_FILE && f->device_file.write) {
                bytes = f->device_file.write(f->device_file.param, req->payload_ptr, size, f->offset);
            }

            if (bytes >= 0) {
                f->offset += bytes;
                resp.param1 = VFS_ERR_OK;
                resp.payload_size = bytes;
            } else {
                resp.param1 = VFS_ERR_UNKNOWN;
                resp.payload_size = 0;
            }
            
            ipc_send(req->sender_tid, &resp);
            return;
        }
        
        case VFS_CMD_LIST: {
            int fd = req->param2;
            int index = req->param3;
            
            vfs_file_t* f = vfs_get_file(fd, req->sender_tid);
            if (!f) { resp.param1 = VFS_ERR_PERM; break; }

            vfs_dirent_t dirent;
            memset(&dirent, 0, sizeof(vfs_dirent_t));
            int res = 0;

            if (f->type == VFS_TYPE_DIR) {
                vfs_node_t* child = f->dir.node->children;
                int i = 0;
                while (child && i < index) {
                    child = child->next;
                    i++;
                }
                
                if (child) {
					if (child == f->dir.node || strcmp(child->name, f->dir.node->name) == 0){
						printf("VFS: GRAPH LOOP DETECTED!\n");
                        resp.param1 = VFS_ERR_UNKNOWN;
                        break; 
					}
                    strlcpy(dirent.name, child->name, sizeof(dirent.name));
                    dirent.size = 0;
                    
                    if (child->type == VFS_TYPE_DIR || child->type == VFS_TYPE_MOUNT_POINT) {
                        dirent.type = VFS_FILE_TYPE_DIR;
                    } else if (child->type == VFS_TYPE_SYMLINK) {
                        dirent.type = VFS_FILE_TYPE_SYMLINK;
                    } else if (child->type == VFS_TYPE_DEVICE_FILE) {
                        dirent.type = VFS_FILE_TYPE_DEVICE;
                    } else {
                        dirent.type = VFS_FILE_TYPE_REGULAR;
                    }
                    
                    res = 1;
                }
            } 
            else if (f->type == VFS_TYPE_MOUNT_POINT) {
                if (f->mounted_file.driver->readdir) {
                    fs_dirent_t fs_ent;
					memset(&fs_ent, 0, sizeof(fs_dirent_t));
                    res = f->mounted_file.driver->readdir(
                        f->mounted_file.fs, f->mounted_file.handle, index, &fs_ent
                    );
                    if (res) {
                        strlcpy(dirent.name, fs_ent.name, sizeof(dirent.name));
                        dirent.size = fs_ent.size;
                        dirent.type = fs_ent.type;
                    }
                }
            }

            if (res) {
                resp.param1 = VFS_ERR_OK;
                resp.payload_ptr = (uint8_t*)&dirent;
                resp.payload_size = sizeof(vfs_dirent_t);
            } else {
                resp.param1 = VFS_ERR_UNKNOWN;
            }
            
            break;
        }
		case VFS_CMD_CLOSE: {
            int fd = req->param2;
            vfs_file_t* f = vfs_get_file(fd, req->sender_tid);
            if (f) {
                f->used = 0;
                resp.param1 = VFS_ERR_OK;
            } else {
                resp.param1 = VFS_ERR_PERM;
            }
            break;
        }
		default: {
			resp.param1 = VFS_ERR_NOCOMM;
			break;
		}
    }
    
    ipc_send(req->sender_tid, &resp);
}

int driver_main(void* reserved1, void* reserved2) {
	
    vfs_init_tree();
    printf("VFS: Tree initialized.\n");
	
	register_driver(DT_VFS, 0);
	
	message_t msg;
    while (1) {
        ipc_recv_ex(0, MSG_TYPE_VFS, MSG_SUBTYPE_NONE, &msg);
		handle_vfs_request(&msg);
		if (msg.payload_ptr != (void*)0 && msg.payload_size > 0) {
            free(msg.payload_ptr);
            msg.payload_ptr = (void*)0;
            msg.payload_size = 0;
        }
    }
}