#include <stdint.h>
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

typedef enum {
    PROCFS_TYPE_ROOT = 1,
    PROCFS_TYPE_P_DIR,
    PROCFS_TYPE_T_DIR,
    PROCFS_TYPE_PID_DIR,
    PROCFS_TYPE_PID_NAME,
    PROCFS_TYPE_PID_STATE,
    PROCFS_TYPE_PID_MEM
} procfs_file_type_t;

typedef struct {
    procfs_file_type_t type;
    uint32_t target_id;
} procfs_file_t;

int proc_read_text(const char* text, void* buf, uint64_t size, uint64_t offset) {
    uint64_t len = strlen(text);
    if (offset >= len) return 0;
    
    uint64_t to_copy = size;
    if (offset + to_copy > len) {
        to_copy = len - offset;
    }
    
    memcpy(buf, text + offset, to_copy);
    return to_copy;
}

fs_instance_t procfs_mount(block_dev_t* dev) {
	UNUSED(dev);
    return (fs_instance_t)1; 
}

void procfs_umount(fs_instance_t fs) {
	UNUSED(fs);
}

fs_file_handle_t procfs_open(fs_instance_t fs, const char* path, uint32_t flags) {
    if (!path) return 0;

    procfs_file_t* handle = malloc(sizeof(procfs_file_t));
    if (!handle) return 0;
    memset(handle, 0, sizeof(procfs_file_t));

    char path_copy[256];
    strlcpy(path_copy, path, sizeof(path_copy));
    
    char* saveptr;
    char* token = strtok_r(path_copy, "/", &saveptr);

    if (!token) {
        handle->type = PROCFS_TYPE_ROOT;
        return (fs_file_handle_t)handle;
    }

    if (strcmp(token, "p") == 0) {
        char* pid_str = strtok_r(0, "/", &saveptr);
        
        if (!pid_str) {
            handle->type = PROCFS_TYPE_P_DIR;
            return (fs_file_handle_t)handle;
        }

        uint32_t pid = atoi(pid_str);
        
        proc_info_user_t pinfo;
        if (get_proc_info(pid, &pinfo) != SYS_RES_OK) {
            free(handle);
            return 0;
        }

        handle->target_id = pid;
        
        char* file_str = strtok_r(0, "/", &saveptr);
        
        if (!file_str) {
            handle->type = PROCFS_TYPE_PID_DIR;
            return (fs_file_handle_t)handle;
        }

        if (strcmp(file_str, "name") == 0) {
            handle->type = PROCFS_TYPE_PID_NAME;
            return (fs_file_handle_t)handle;
        } else if (strcmp(file_str, "state") == 0) {
            handle->type = PROCFS_TYPE_PID_STATE;
            return (fs_file_handle_t)handle;
        } else if (strcmp(file_str, "mem") == 0) {
            handle->type = PROCFS_TYPE_PID_MEM;
            return (fs_file_handle_t)handle;
        }
    }
    else if (strcmp(token, "t") == 0) {
        if (!strtok_r(0, "/", &saveptr)) {
            handle->type = PROCFS_TYPE_T_DIR;
            return (fs_file_handle_t)handle;
        }
        // ... Логика для потоков ...
    }

    free(handle);
    return 0;
}

fs_file_handle_t procfs_openat(fs_instance_t fs, fs_file_handle_t dir_handle, const char* path, uint32_t flags) {
    // В виртуальных ФС обычно проще собрать полный путь и вызвать open, 
    // но для простоты пока вернем 0. Полная реализация опциональна.
    return 0; 
}

int procfs_read(fs_instance_t fs, fs_file_handle_t f, void* buf, uint64_t size, uint64_t offset) {
    procfs_file_t* handle = (procfs_file_t*)f;
    char text[256];
    memset(text, 0, sizeof(text));

    if (handle->type == PROCFS_TYPE_ROOT || handle->type == PROCFS_TYPE_P_DIR || handle->type == PROCFS_TYPE_PID_DIR) {
        return -1;
    }

    if (handle->type == PROCFS_TYPE_PID_NAME) {
        proc_info_user_t pinfo;
        if (get_proc_info(handle->target_id, &pinfo) == SYS_RES_OK) {
            snprintf(text, sizeof(text), "%s\n", pinfo.name);
            return proc_read_text(text, buf, size, offset);
        }
    } 
    else if (handle->type == PROCFS_TYPE_PID_STATE) {
        proc_info_user_t pinfo;
        if (get_proc_info(handle->target_id, &pinfo) == SYS_RES_OK) {
            snprintf(text, sizeof(text), "%d\n", pinfo.state);
            return proc_read_text(text, buf, size, offset);
        }
    }
    else if (handle->type == PROCFS_TYPE_PID_MEM) {
        proc_info_user_t pinfo;
        if (get_proc_info(handle->target_id, &pinfo) == SYS_RES_OK) {
            snprintf(text, sizeof(text), "Heap Limit: %llu bytes\nThreads: %llu\n", 
                     (unsigned long long)pinfo.heap_limit, 
                     (unsigned long long)pinfo.threads_count);
            return proc_read_text(text, buf, size, offset);
        }
    }

    return 0;
}

int procfs_write(fs_instance_t fs, fs_file_handle_t f, const void* buf, uint64_t size, uint64_t offset) {
    return -1;
}

int procfs_readdir(fs_instance_t fs, fs_file_handle_t dir_handle, uint64_t* offset, fs_dirent_t* out_array, int max_entries) {
    procfs_file_t* handle = (procfs_file_t*)dir_handle;
    int count = 0;

    if (*offset == (uint64_t)-1) return 0;

    if (handle->type == PROCFS_TYPE_ROOT) {
        if (*offset == 0 && count < max_entries) {
            strcpy(out_array[count].name, "p");
            out_array[count].type = VFS_FILE_TYPE_DIR;
            count++; (*offset)++;
        }
        if (*offset == 1 && count < max_entries) {
            strcpy(out_array[count].name, "t");
            out_array[count].type = VFS_FILE_TYPE_DIR;
            count++; (*offset)++;
        }
        *offset = (uint64_t)-1;
        return count;
    }

    if (handle->type == PROCFS_TYPE_P_DIR) {
        uint32_t current_pid = (*offset == 0) ? 1 : (uint32_t)(*offset);
        
        #define MAX_SYSTEM_PIDS 1024 

        while (count < max_entries && current_pid < MAX_SYSTEM_PIDS) {
            proc_info_user_t pinfo;
            if (get_proc_info(current_pid, &pinfo) == SYS_RES_OK) {
                utoa(current_pid, out_array[count].name, 10);
                out_array[count].type = VFS_FILE_TYPE_DIR;
                out_array[count].size = 0;
                count++;
            }
            current_pid++;
        }

        if (current_pid >= MAX_SYSTEM_PIDS) *offset = (uint64_t)-1;
        else *offset = (uint64_t)current_pid;
        
        return count;
    }

    if (handle->type == PROCFS_TYPE_PID_DIR) {
        const char* files[] = {"name", "state", "mem"};
        int num_files = 3;

        while (count < max_entries && *offset < num_files) {
            strcpy(out_array[count].name, files[*offset]);
            out_array[count].type = VFS_FILE_TYPE_REGULAR;
            out_array[count].size = 0;
            count++;
            (*offset)++;
        }

        if (*offset >= num_files) *offset = (uint64_t)-1;
        return count;
    }

    return 0;
}

int procfs_stat(fs_instance_t fs, fs_file_handle_t f, fs_stat_t* out_info) {
    procfs_file_t* handle = (procfs_file_t*)f;
    memset(out_info, 0, sizeof(fs_stat_t));
    
    out_info->inode_id = ((uint64_t)handle->target_id << 32) | handle->type;
    out_info->size_bytes = 0;

    if (handle->type == PROCFS_TYPE_ROOT || handle->type == PROCFS_TYPE_P_DIR || handle->type == PROCFS_TYPE_PID_DIR) {
        // Если у вас в fs_stat_t есть поле attributes, можно поставить FAT_ATTR_DIRECTORY или аналог
        // out_info->attributes = FAT_ATTR_DIRECTORY;
    }
    
    return 0;
}

void procfs_get_label(fs_instance_t fs, char* out_label) {
    strcpy(out_label, "PROCFS");
}

void procfs_close(fs_instance_t fs, fs_file_handle_t f) {
    free(f);
}

fs_driver_t procfs_driver = {
    .mount = procfs_mount,
    .umount = procfs_umount,
    .open = procfs_open,
    .openat = procfs_openat,
    .read = procfs_read,
    .write = procfs_write,
    .readdir = procfs_readdir,
    .stat = procfs_stat,
    .get_label = procfs_get_label,
    .close = procfs_close
};