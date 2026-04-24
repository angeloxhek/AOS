#include <stdint.h>
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

extern fs_driver_t fat32_driver;
extern fs_driver_t procfs_driver;

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

typedef struct {
    int id;
    uint64_t owner_tid;
    int used;
    uint64_t offset;
    vfs_node_type_t type;
	vfs_node_t* mount_node;
	uint64_t inode_id;
	uint32_t flags;
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

#define MAX_LOCKED_FILES 256

typedef struct {
    int used;
    vfs_node_t* mount_node;
    uint64_t inode_id;
    int type;
    uint64_t owner_tid;
} vfs_lock_t;

vfs_lock_t active_locks[MAX_LOCKED_FILES];

int vfs_check_lock(vfs_node_t* mount_node, uint64_t inode_id, uint64_t tid, int is_write) {
    if (!mount_node) return 0;

    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (active_locks[i].used && 
            active_locks[i].mount_node == mount_node && 
            active_locks[i].inode_id == inode_id) 
        {
            if (active_locks[i].type == VFS_LOCK_EX) {
                if (active_locks[i].owner_tid != tid) return VFS_ERR_BUSY;
            } 
            else if (active_locks[i].type == VFS_LOCK_SH && is_write) {
                if (active_locks[i].owner_tid != tid) return VFS_ERR_BUSY;
            }
        }
    }
    return 0;
}

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
    snprintf(text, sizeof(text), "%d\n", (int)info.uptime);

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
    snprintf(text, sizeof(text), "%d\n", (int)info.flags);
    
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
    cmd[size < 31 ? size : 31] = '\0';

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

void vfs_init_tree() {
    vfs_root = calloc(1, sizeof(vfs_node_t));
    vfs_root->type = VFS_TYPE_DIR;

    vfs_node_t* hw   = vfs_mkdir(vfs_root, "hw");
	vfs_mkdev(hw, "null", dev_read_null, dev_write_null, 0);
	vfs_mkdev(hw, "zero", dev_read_zero, dev_write_null, 0);
	vfs_mkdev(hw, "urandom", dev_read_urandom, dev_write_null, 0);

    vfs_node_t* sys  = vfs_mkdir(vfs_root, "sys");
	
    vfs_node_t* tasks = vfs_mkdir(vfs_root, "tasks");
	tasks->type = VFS_TYPE_MOUNT_POINT;
    tasks->mount.driver = &procfs_driver;
    tasks->mount.fs_inst = procfs_driver.mount(0);
	
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
            snprintf(target, sizeof(target), "/hw/%s/%s/fs", disk_name, part_name);
            
            char link_name[16];
            snprintf(link_name, sizeof(link_name), "v%d", i);
            
            vfs_symlink(mnt_id, link_name, target);
			
            char label[12];
            memset(label, 0, 12);
            strcpy(label, "NO_NAME"); 
            
            if (fs_node->mount.driver->get_label) {
                fs_node->mount.driver->get_label(fs_inst, label);
            }
            
            if (strcmp(label, "NO_NAME") != 0) {
                 char id_link[32];
                 snprintf(id_link, sizeof(id_link), "/mnt/id/v%d", i);
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

void vfs_resolve_from(vfs_node_t* start_node, const char* path, vfs_path_result_t* out) {
    out->node = 0;
    out->fs_path = 0;
    out->error = VFS_ERR_NOTFOUND;

    if (!path || !out) return;

    vfs_node_t* current;
    
    char* path_copy = strdup(path); 
    char* cursor;
	if (path[0] == '/') {
        current = vfs_root;
        cursor = path_copy + 1;
    } else {
        if (!start_node) {
            free(path_copy);
            return;
        }
        current = start_node;
        cursor = path_copy;
    }
    
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
            out->error = VFS_ERR_NOTFOUND;
            free(path_copy);
            return;
        }

        if (next_node->type == VFS_TYPE_SYMLINK) {
            
            static int recursion_depth = 0;
            if (recursion_depth > 8) {
                out->error = VFS_ERR_SYMLINKLOOP;
                free(path_copy);
                return;
            }

            char* remainder = next_slash ? (next_slash + 1) : "";
            
            int new_len = strlen(next_node->target_path) + 1 + strlen(remainder) + 1;
            char* new_full_path = malloc(new_len);
			if (!new_full_path) {
				out->error = VFS_ERR_UNKNOWN;
				free(path_copy);
				return;
			}
			memset(new_full_path, 0, new_len);            
            strlcpy(new_full_path, next_node->target_path, new_len);
            if (*remainder) {
                strlcat(new_full_path, "/", new_len);
                strlcat(new_full_path, remainder, new_len);
            }

            recursion_depth++;
            vfs_resolve_from(current, new_full_path, out);
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

void vfs_resolve(const char* path, vfs_path_result_t* out) {
    vfs_resolve_from(vfs_root, path, out);
}

void handle_vfs_request(message_t* in) {
    message_t out;
    memset(&out, 0, sizeof(message_t));
    out.type = MSG_TYPE_VFS;
    out.subtype = MSG_SUBTYPE_RESPONSE;

    switch (in->param1) {
        case VFS_CMD_OPEN:
        case VFS_CMD_OPENAT: {
			uint32_t flags = in->param2;
            int dir_fd = (in->param1 == VFS_CMD_OPENAT) ? in->param3 : -1;
            char* path = (char*)in->data;

            if (!path || path[0] == '\0') {
                out.param1 = VFS_ERR_UNKNOWN;
                break;
            }

            vfs_path_result_t res;
            vfs_node_t* start_node = vfs_root;

            if (in->param1 == VFS_CMD_OPENAT && path[0] != '/') {
                vfs_file_t* parent_f = vfs_get_file(dir_fd, in->sender_tid);
                if (parent_f) {
                    if (parent_f->type == VFS_TYPE_DIR) {
                        start_node = parent_f->dir.node;
                    } else if (parent_f->type == VFS_TYPE_MOUNT_POINT) {
                        int new_fd = vfs_alloc_fd(in->sender_tid);
                        if (new_fd < 0) {
                            out.param1 = VFS_ERR_UNKNOWN; break;
                        }
                        vfs_file_t* new_f = &open_files[new_fd - 1];
                        new_f->type = VFS_TYPE_MOUNT_POINT;
                        new_f->mounted_file.driver = parent_f->mounted_file.driver;
                        new_f->mounted_file.fs = parent_f->mounted_file.fs;

                        if (new_f->mounted_file.driver->openat) {
                            new_f->mounted_file.handle = new_f->mounted_file.driver->openat(
                                new_f->mounted_file.fs,
                                parent_f->mounted_file.handle,
                                path,
                                flags
                            );
                        }

                        if (new_f->mounted_file.handle) {
							new_f->mount_node = parent_f->mount_node;
                            new_f->flags = flags;
                            if (new_f->mounted_file.driver->stat) {
                                fs_stat_t file_stat;
                                if (new_f->mounted_file.driver->stat(new_f->mounted_file.fs, new_f->mounted_file.handle, &file_stat) == 0) {
                                    new_f->inode_id = file_stat.inode_id;
									if (flags & VFS_FAPPEND) {
										new_f->offset = file_stat.size_bytes;
									}
                                }
                            }
                            out.param1 = VFS_ERR_OK;
                            out.param2 = new_fd;
                        } else {
                            new_f->used = 0;
                            out.param1 = VFS_ERR_NOTFOUND;
                        }
                        break;
                    }
                }
            }

            vfs_resolve_from(start_node, path, &res);

            if (res.error != 0) {
                out.param1 = res.error;
                break;
            }

            int new_fd = vfs_alloc_fd(in->sender_tid);
            if (new_fd < 0) {
                out.param1 = VFS_ERR_UNKNOWN;
                if (res.fs_path) free(res.fs_path);
                break;
            }

            vfs_file_t* f = &open_files[new_fd - 1];
            f->offset = 0;

            if (res.node->type == VFS_TYPE_MOUNT_POINT) {
                f->type = VFS_TYPE_MOUNT_POINT;
                f->mounted_file.driver = res.node->mount.driver;
                f->mounted_file.fs = res.node->mount.fs_inst;
				
				f->mount_node = res.node;
                
                char* p = (res.fs_path && *res.fs_path) ? res.fs_path : "/";
                
                if (f->mounted_file.driver && f->mounted_file.driver->open) {
                    f->mounted_file.handle = f->mounted_file.driver->open(f->mounted_file.fs, p, flags);
					f->flags = flags;
					if (f->mounted_file.handle && f->mounted_file.driver->stat) {
                        fs_stat_t file_stat;
                        if (f->mounted_file.driver->stat(f->mounted_file.fs, f->mounted_file.handle, &file_stat) == 0) {
                            f->inode_id = file_stat.inode_id;
							if (flags & VFS_FAPPEND) {
								f->offset = file_stat.size_bytes;
							}
                        }
					}
                }
                
                if (!f->mounted_file.handle) {
                    f->used = 0;
                    out.param1 = VFS_ERR_NOTFOUND;
                } else {
                    out.param1 = VFS_ERR_OK;
                    out.param2 = new_fd;
                }
            } 
            else if (res.node->type == VFS_TYPE_DEVICE_FILE) {
                f->type = VFS_TYPE_DEVICE_FILE;
                f->device_file.read = res.node->dev_ops.read;
                f->device_file.write = res.node->dev_ops.write;
                f->device_file.param = res.node->dev_ops.param;
                out.param1 = VFS_ERR_OK;
                out.param2 = new_fd;
            }
            else if (res.node->type == VFS_TYPE_DIR) {
                f->type = VFS_TYPE_DIR;
                f->dir.node = res.node;
                out.param1 = VFS_ERR_OK;
                out.param2 = new_fd;
            }
            else {
                f->used = 0;
                out.param1 = VFS_ERR_PERM;
            }

            if (res.fs_path) free(res.fs_path);
            break;
        }

        case VFS_CMD_READ: {
			out.subtype = MSG_SUBTYPE_PONG;
            int fd = in->param2;
            uint64_t size = in->param3;
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
            if (!f) { out.param1 = VFS_ERR_PERM; break; }
            if (f->type == VFS_TYPE_DIR) { out.param1 = VFS_ERR_ISDIR; break; }
			if (!(f->flags & VFS_FREAD)) { out.param1 = VFS_ERR_PERM; break; }

            void* buf = shm_map(shm_id);
            if (!buf) { out.param1 = VFS_ERR_UNKNOWN; break; }
            
            memset(buf, 0, size);
            int bytes = -1;

            if (f->type == VFS_TYPE_MOUNT_POINT) {
				if (vfs_check_lock(f->mount_node, f->inode_id, in->sender_tid, 0) == VFS_ERR_BUSY) {
					out.param1 = VFS_ERR_BUSY;
					break;
				}
                if (f->mounted_file.driver && f->mounted_file.driver->read) {
                    bytes = f->mounted_file.driver->read(f->mounted_file.fs, f->mounted_file.handle, buf, size, f->offset);
                }
            } else if (f->type == VFS_TYPE_DEVICE_FILE && f->device_file.read) {
                bytes = f->device_file.read(f->device_file.param, buf, size, f->offset);
            }

            shm_free(shm_id);

            if (bytes >= 0) {
                f->offset += bytes;
                out.param1 = VFS_ERR_OK;
                out.param2 = bytes;
            } else {
                out.param1 = VFS_ERR_UNKNOWN;
            }
            break;
        }
        
        case VFS_CMD_WRITE: {
			out.subtype = MSG_SUBTYPE_PONG;
            int fd = in->param2;
            uint64_t size = in->param3;
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
            if (!f) { out.param1 = VFS_ERR_PERM; break; }
            if (f->type == VFS_TYPE_DIR) { out.param1 = VFS_ERR_ISDIR; break; }
			if (!(f->flags & VFS_FWRITE)) { out.param1 = VFS_ERR_PERM; break; }

            void* buf = shm_map(shm_id);
            if (!buf) { out.param1 = VFS_ERR_UNKNOWN; break; }

            int bytes = -1;

            if (f->type == VFS_TYPE_MOUNT_POINT) {
				if (vfs_check_lock(f->mount_node, f->inode_id, in->sender_tid, 1) == VFS_ERR_BUSY) {
					out.param1 = VFS_ERR_BUSY;
					break;
				}
                if (f->mounted_file.driver && f->mounted_file.driver->write) {
                    bytes = f->mounted_file.driver->write(f->mounted_file.fs, f->mounted_file.handle, buf, size, f->offset);
                }
            } else if (f->type == VFS_TYPE_DEVICE_FILE && f->device_file.write) {
                bytes = f->device_file.write(f->device_file.param, buf, size, f->offset);
            }

            shm_free(shm_id);

            if (bytes >= 0) {
                f->offset += bytes;
                out.param1 = VFS_ERR_OK;
                out.param2 = bytes;
            } else {
                out.param1 = VFS_ERR_UNKNOWN;
            }
            break;
        }
        
        case VFS_CMD_LIST: {
			out.subtype = MSG_SUBTYPE_PONG;
            int fd = in->param2;
            int max_entries = in->param3;
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            if (max_entries <= 0 || max_entries > 128) max_entries = 32;
            
            vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
            if (!f) { out.param1 = VFS_ERR_PERM; break; }

            vfs_dirent_t* dirent_array = (vfs_dirent_t*)shm_map(shm_id);
            if (!dirent_array) { out.param1 = VFS_ERR_UNKNOWN; break; }
            
            memset(dirent_array, 0, sizeof(vfs_dirent_t) * max_entries);
            int entries_read = 0;

            if (f->type == VFS_TYPE_DIR) {
                while (entries_read < max_entries && f->offset != (uint64_t)-1) {
                    vfs_node_t* child = (f->offset == 0) ? f->dir.node->children : (vfs_node_t*)f->offset;
                    
                    if (child) {
                        if (child == f->dir.node || strcmp(child->name, f->dir.node->name) == 0){
                            printf("VFS: GRAPH LOOP DETECTED!\n");
                            break; 
                        }
                        
                        strlcpy(dirent_array[entries_read].name, child->name, 64);
                        dirent_array[entries_read].size = 0;
                        
                        if (child->type == VFS_TYPE_DIR || child->type == VFS_TYPE_MOUNT_POINT) {
                            dirent_array[entries_read].type = VFS_FILE_TYPE_DIR;
                        } else if (child->type == VFS_TYPE_SYMLINK) {
                            dirent_array[entries_read].type = VFS_FILE_TYPE_SYMLINK;
                        } else if (child->type == VFS_TYPE_DEVICE_FILE) {
                            dirent_array[entries_read].type = VFS_FILE_TYPE_DEVICE;
                        } else {
                            dirent_array[entries_read].type = VFS_FILE_TYPE_REGULAR;
                        }
                        
                        f->offset = (child->next != 0) ? (uint64_t)child->next : (uint64_t)-1;
                        entries_read++;
                    } else {
                        f->offset = (uint64_t)-1;
                    }
                }
            } 
            else if (f->type == VFS_TYPE_MOUNT_POINT) {
                if (f->mounted_file.driver->readdir) {
                    
                    fs_dirent_t* fs_array = malloc(sizeof(fs_dirent_t) * max_entries);
                    if (!fs_array) {
                        out.param1 = VFS_ERR_UNKNOWN;
                        break;
                    }
                    memset(fs_array, 0, sizeof(fs_dirent_t) * max_entries);

                    int fs_entries_read = f->mounted_file.driver->readdir(
                        f->mounted_file.fs, 
                        f->mounted_file.handle, 
                        &f->offset,
                        fs_array,
                        max_entries
                    );

                    if (fs_entries_read > 0) {
                        for (int i = 0; i < fs_entries_read; i++) {
                            strlcpy(dirent_array[i].name, fs_array[i].name, 64);
                            dirent_array[i].size = fs_array[i].size;
                            
                            dirent_array[i].type = fs_array[i].type;
                        }
                    }

                    entries_read = fs_entries_read;
                    free(fs_array);
                }
            }

            shm_free(shm_id);

            if (entries_read >= 0) {
                out.param1 = VFS_ERR_OK;
                out.param2 = entries_read;
            } else {
                out.param1 = VFS_ERR_UNKNOWN;
            }
            break;
        }
        
        case VFS_CMD_CLOSE: {
            int fd = in->param2;
            vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
            if (f) {
                if (f->type == VFS_TYPE_MOUNT_POINT && f->mounted_file.driver && f->mounted_file.driver->close) {
					for (int i = 0; i < MAX_LOCKED_FILES; i++) {
						if (active_locks[i].used && 
							active_locks[i].owner_tid == in->sender_tid &&
							active_locks[i].mount_node == f->mount_node &&
							active_locks[i].inode_id == f->inode_id) 
						{
							active_locks[i].used = 0;
						}
					}
                    f->mounted_file.driver->close(f->mounted_file.fs, f->mounted_file.handle);
                }
                f->used = 0;
                out.param1 = VFS_ERR_OK;
            } else {
                out.param1 = VFS_ERR_PERM;
            }
            break;
        }
		
		case VFS_CMD_FLOCK: {
            int fd = in->param2;
            vfs_lock_type_t lock_type = in->param3;
            
            vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
            if (!f) { out.param1 = VFS_ERR_PERM; break; }
            if (f->type != VFS_TYPE_MOUNT_POINT) { out.param1 = VFS_ERR_OK; break; }

            vfs_lock_t* existing_lock = 0;
            int free_slot = -1;

            for (int i = 0; i < MAX_LOCKED_FILES; i++) {
                if (active_locks[i].used && 
                    active_locks[i].mount_node == f->mount_node && 
                    active_locks[i].inode_id == f->inode_id) 
                {
                    existing_lock = &active_locks[i];
                    break;
                }
                if (!active_locks[i].used && free_slot == -1) free_slot = i;
            }

            if (lock_type == VFS_LOCK_UN) { 
                if (existing_lock && existing_lock->owner_tid == in->sender_tid) {
                    existing_lock->used = 0; 
                }
                out.param1 = VFS_ERR_OK;
            } 
            else { 
                if (existing_lock) {
                    if (existing_lock->owner_tid != in->sender_tid) {
                        out.param1 = VFS_ERR_BUSY;
                    } else {
                        existing_lock->type = lock_type;
                        out.param1 = VFS_ERR_OK;
                    }
                } else {
                    if (free_slot != -1) {
                        active_locks[free_slot].used = 1;
                        active_locks[free_slot].type = lock_type;
                        active_locks[free_slot].owner_tid = in->sender_tid;
                        active_locks[free_slot].mount_node = f->mount_node;
                        active_locks[free_slot].inode_id = f->inode_id;
                        out.param1 = VFS_ERR_OK;
                    } else {
                        out.param1 = VFS_ERR_UNKNOWN;
                    }
                }
            }
            break;
        }
		
		case VFS_CMD_SEEK: {
            int fd = in->param2;
            vfs_seek_t whence = in->param3;
            int64_t offset = *(int64_t*)(in->data);

            vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
            if (!f) { out.param1 = VFS_ERR_PERM; break; }
            if (f->type == VFS_TYPE_DIR) { out.param1 = VFS_ERR_ISDIR; break; }

            int64_t new_offset = f->offset;

            if (whence == VFS_SEEK_SET) {
                new_offset = offset;
            } 
            else if (whence == VFS_SEEK_CUR) {
                new_offset += offset;
            } 
            else if (whence == VFS_SEEK_END) {
                uint64_t file_size = 0;
                if (f->type == VFS_TYPE_MOUNT_POINT && f->mounted_file.driver->stat) {
                    fs_stat_t st;
                    if (f->mounted_file.driver->stat(f->mounted_file.fs, f->mounted_file.handle, &st) == 0) {
                        file_size = st.size_bytes;
                    }
                }
                new_offset = file_size + offset;
            }

            if (new_offset < 0) {
                out.param1 = VFS_ERR_UNKNOWN;
            } else {
                f->offset = new_offset;
                out.param1 = VFS_ERR_OK;
                *(int64_t*)(out.data) = f->offset; 
            }
            break;
        }

		case VFS_CMD_STAT: {
			int fd = in->param2;
			uint64_t shm_id = *(uint64_t*)(in->data);
			
			vfs_file_t* f = vfs_get_file(fd, in->sender_tid);
			if (!f) { out.param1 = VFS_ERR_PERM; break; }

			vfs_stat_info_t* user_stat = (vfs_stat_info_t*)shm_map(shm_id);
			
			fs_stat_t native_stat;
			if (f->type == VFS_TYPE_MOUNT_POINT && f->mounted_file.driver->stat) {
				if (f->mounted_file.driver->stat(f->mounted_file.fs, f->mounted_file.handle, &native_stat) == 0) {
					user_stat->inode_id = native_stat.inode_id;
					user_stat->size_bytes = native_stat.size_bytes;
					user_stat->attributes = native_stat.attributes;
					strlcpy(user_stat->name, f->mount_node->name, 256); 
					out.param1 = VFS_ERR_OK;
				} else {
					out.param1 = VFS_ERR_UNKNOWN;
				}
			} else {
				out.param1 = VFS_ERR_UNKNOWN;
			}

			shm_free(shm_id);
			break;
		}
        
        default: {
            out.param1 = VFS_ERR_NOCOMM;
            break;
        }
    }
    
    ipc_send(in->sender_tid, &out);
}

int driver_main(void* reserved1, void* reserved2) {
	
    vfs_init_tree();
    printf("VFS: Tree initialized.\n");
	
	register_driver(DT_VFS, 0);
	
	message_t msg;
    while (1) {
        ipc_recv_ex(0, MSG_TYPE_VFS, MSG_SUBTYPE_NONE, &msg);
		handle_vfs_request(&msg);
    }
}