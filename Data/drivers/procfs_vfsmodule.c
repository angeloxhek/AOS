#include <stdint.h>
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

// --- 1. ТИПЫ ДВИЖКА (ENGINE) ---

typedef enum {
    PROC_FILE,              // Файл (вызывает read_cb)
    PROC_SYMLINK,           // Статичный симлинк (вызывает read_cb для генерации пути)
    PROC_STATIC_DIR,        // Обычная папка
    PROC_DYNAMIC_DIR,       // Папка, генерирующая ID (внутри лежат папки)
    PROC_DYNAMIC_SYMLINK    // Папка, генерирующая ID (внутри лежат симлинки)
} proc_type_t;

typedef struct proc_entry {
    const char* name;
    proc_type_t type;
    
    struct proc_entry* children; // Указатель на содержимое (для папок)
    
    int (*read_cb)(uint32_t id1, uint32_t id2, void* buf, uint64_t size, uint64_t offset);
    int (*get_list_cb)(uint32_t parent_id, uint32_t* out_arr, int max);
} proc_entry_t;

typedef struct {
    proc_entry_t* entry;
    uint32_t id1;           // PID или TID
    uint32_t id2;           // Дочерний TID или FD
    int is_dynamic_root;    // 1 = мы открыли саму папку (напр. /p), 0 = открыли ID (напр. /p/0)
    char full_path[128];    // Сохраняем путь для универсального openat!
} procfs_handle_t;

// --- 2. БИЗНЕС-ЛОГИКА (ФУНКЦИИ ДЛЯ ЧТЕНИЯ) ---

static int write_text(const char* text, void* buf, uint64_t size, uint64_t offset) {
    uint64_t len = strlen(text);
    if (offset >= len) return 0;
    uint64_t to_copy = size;
    if (offset + to_copy > len) to_copy = len - offset;
    memcpy(buf, text + offset, to_copy);
    return to_copy;
}

// Функции файлов
static int read_pid_name(uint32_t pid, uint32_t none, void* buf, uint64_t size, uint64_t offset) {
    proc_info_user_t pinfo; if (get_proc_info(pid, &pinfo) != SYS_RES_OK) return -1;
    char text[64]; snprintf(text, sizeof(text), "%s\n", pinfo.name);
    return write_text(text, buf, size, offset);
}

static int read_tid_status(uint32_t tid, uint32_t none, void* buf, uint64_t size, uint64_t offset) {
    thread_info_user_t tinfo; if (get_thread_info(tid, &tinfo) != SYS_RES_OK) return -1;
    char text[128]; snprintf(text, sizeof(text), "State: %d\n", tinfo.state);
    return write_text(text, buf, size, offset);
}

// Функции симлинков
static int symlink_tid_to_proc(uint32_t tid, uint32_t none, void* buf, uint64_t size, uint64_t offset) {
    thread_info_user_t tinfo; if (get_thread_info(tid, &tinfo) != SYS_RES_OK) return -1;
    char path[64]; snprintf(path, sizeof(path), "/tasks/p/%u/", tinfo.parent_pid);
    return write_text(path, buf, size, offset);
}

static int read_thread_symlink(uint32_t pid, uint32_t tid, void* buf, uint64_t size, uint64_t offset) {
    char path[64]; snprintf(path, sizeof(path), "/tasks/t/%u/", tid);
    return write_text(path, buf, size, offset);
}

// Функции списков
static int get_all_pids(uint32_t none, uint32_t* arr, int max) { return get_pid_list(arr, max); }
static int get_all_tids(uint32_t none, uint32_t* arr, int max) { return get_tid_list(0xFFFFFFFF, arr, max); }
static int get_threads_for_pid(uint32_t pid, uint32_t* arr, int max) { return get_tid_list(pid, arr, max); }

// ЗАГЛУШКА: Для списка FD
static int get_thread_fds(uint32_t tid, uint32_t* arr, int max) {
    if (max < 3) return 0;
    arr[0] = 0; arr[1] = 1; arr[2] = 2; // Возвращаем фейковые 0, 1, 2
    return 3; 
}


// --- 3. ОПИСАНИЕ ДЕРЕВА (ПРОСТО И КРАСИВО) ---

extern proc_entry_t tid_dir_template[];

// Шаблон: Что внутри папки дескриптора (/tasks/t/<tid>/fd/<fd_num>/)
proc_entry_t fd_dir_template[] = {
    // Пока пусто, добавим позже. Но чтобы папка не была пустой, кинем заглушку:
    {"info", PROC_FILE, NULL, NULL, NULL}, 
    {NULL, 0, NULL, NULL, NULL}
};

// Шаблон: Что внутри папки потока (/tasks/t/<tid>/)
proc_entry_t tid_dir_template[] = {
    {"status", PROC_FILE, NULL, read_tid_status, NULL},
    {"proc",   PROC_SYMLINK, NULL, symlink_tid_to_proc, NULL},     // Симлинк на процесс
    {"fd",     PROC_DYNAMIC_DIR, fd_dir_template, NULL, get_thread_fds}, // Папка дескрипторов
    {NULL, 0, NULL, NULL, NULL}
};

// Шаблон: Что внутри папки процесса (/tasks/p/<pid>/)
proc_entry_t pid_dir_template[] = {
    {"name",    PROC_FILE, NULL, read_pid_name, NULL},
    // threads - это папка, внутри которой динамически создаются СИМЛИНКИ!
    {"threads", PROC_DYNAMIC_SYMLINK, NULL, read_thread_symlink, get_threads_for_pid},
    {NULL, 0, NULL, NULL, NULL}
};

// Корень: /tasks/
proc_entry_t root_dir_template[] = {
    {"p", PROC_DYNAMIC_DIR, pid_dir_template, NULL, get_all_pids},
    {"t", PROC_DYNAMIC_DIR, tid_dir_template, NULL, get_all_tids},
    {NULL, 0, NULL, NULL, NULL}
};


// --- 4. ДВИЖОК ПАРСИНГА И VFS ---

static procfs_handle_t* resolve_path(const char* path) {
    procfs_handle_t* handle = calloc(1, sizeof(procfs_handle_t));
    if (!handle) return 0;

    // Очистка пути
    char clean_path[128] = {0}; int cp_idx = 0; const char* p = path;
    while(*p == '/') p++;
    while(*p && cp_idx < 127) { if (*p == '/' && *(p+1) == '/') { p++; continue; } clean_path[cp_idx++] = *p++; }
    if (cp_idx > 0 && clean_path[cp_idx-1] == '/') clean_path[cp_idx-1] = '\0';
    strcpy(handle->full_path, clean_path);

    if (clean_path[0] == '\0') { handle->entry = NULL; return handle; } // Корень

    proc_entry_t* current_entries = root_dir_template;
    proc_entry_t* last_matched = NULL;
    int is_dyn_root = 1;
    const char* curr = clean_path;
    char token[64];

    // Ручной безопасный парсер токенов
    while (*curr != '\0') {
        int i = 0; while (*curr && *curr != '/' && i < 63) token[i++] = *curr++;
        token[i] = '\0'; while (*curr == '/') curr++;

        int found = 0;
        for (int j = 0; current_entries[j].name != NULL; j++) {
            if (strcmp(current_entries[j].name, token) == 0) {
                last_matched = &current_entries[j];
                is_dyn_root = 1; found = 1;

                if (last_matched->type == PROC_STATIC_DIR) {
                    current_entries = last_matched->children;
                } 
                else if (last_matched->type == PROC_DYNAMIC_DIR || last_matched->type == PROC_DYNAMIC_SYMLINK) {
                    if (*curr != '\0') { // Есть еще один токен (ID)
                        i = 0; while (*curr && *curr != '/' && i < 63) token[i++] = *curr++;
                        token[i] = '\0'; while (*curr == '/') curr++;

                        uint32_t id = atoi(token);
                        if (handle->id1 == 0) handle->id1 = id;
                        else handle->id2 = id;

                        is_dyn_root = 0;
                        if (last_matched->type == PROC_DYNAMIC_DIR) {
                            current_entries = last_matched->children;
                        } else {
                            goto parse_done; // Симлинки — это листья дерева
                        }
                    }
                }
                break;
            }
        }
        if (!found) { free(handle); return 0; }
    }

parse_done:
    handle->entry = last_matched;
    handle->is_dynamic_root = is_dyn_root;
    return handle;
}

fs_instance_t procfs_mount(block_dev_t* dev) { UNUSED(dev); return (fs_instance_t)1; }
void procfs_umount(fs_instance_t fs) { UNUSED(fs); }

fs_file_handle_t procfs_open(fs_instance_t fs, const char* path, uint32_t flags) {
    UNUSED(fs); UNUSED(flags); return (fs_file_handle_t)resolve_path(path);
}

// Теперь openat абсолютно универсальный, он просто клеит сохраненный путь!
fs_file_handle_t procfs_openat(fs_instance_t fs, fs_file_handle_t dir_handle, const char* path, uint32_t flags) {
    if (!dir_handle || !path) return 0;
    procfs_handle_t* dir = (procfs_handle_t*)dir_handle;
    
    char new_path[256];
    while (*path == '/') path++;
    
    if (dir->full_path[0] == '\0') snprintf(new_path, sizeof(new_path), "%s", path);
    else snprintf(new_path, sizeof(new_path), "%s/%s", dir->full_path, path);
    
    return procfs_open(fs, new_path, flags);
}

int procfs_read(fs_instance_t fs, fs_file_handle_t f, void* buf, uint64_t size, uint64_t offset) {
    UNUSED(fs); procfs_handle_t* h = (procfs_handle_t*)f;
    if (!h || !h->entry) return -1;

    // Чтение файлов или симлинков
    if (h->entry->type == PROC_FILE || h->entry->type == PROC_SYMLINK || 
       (h->entry->type == PROC_DYNAMIC_SYMLINK && !h->is_dynamic_root)) {
        if (h->entry->read_cb) return h->entry->read_cb(h->id1, h->id2, buf, size, offset);
    }
    return -1;
}

int procfs_write(fs_instance_t fs, fs_file_handle_t f, const void* buf, uint64_t size, uint64_t offset) {
    UNUSED(fs); UNUSED(f); UNUSED(buf); UNUSED(size); UNUSED(offset); return -1;
}

int procfs_readdir(fs_instance_t fs, fs_file_handle_t dir_handle, uint64_t* offset, fs_dirent_t* out_array, int max_entries) {
    UNUSED(fs); procfs_handle_t* h = (procfs_handle_t*)dir_handle;
    if (*offset == (uint64_t)-1) return 0;
    int count = 0;

    // 1. Чтение корня
    if (h->entry == NULL) {
        while (count < max_entries && root_dir_template[*offset].name != NULL) {
            strcpy(out_array[count].name, root_dir_template[*offset].name);
            out_array[count].type = VFS_FILE_TYPE_DIR; out_array[count].size = 0;
            count++; (*offset)++;
        }
        if (root_dir_template[*offset].name == NULL) *offset = (uint64_t)-1;
        return count;
    }

    // 2. Чтение динамических списков (например /tasks/p/ или /tasks/p/0/threads/)
    if ((h->entry->type == PROC_DYNAMIC_DIR || h->entry->type == PROC_DYNAMIC_SYMLINK) && h->is_dynamic_root) {
        if (!h->entry->get_list_cb) { *offset = (uint64_t)-1; return 0; }
        
        uint32_t ids[512];
        int total = h->entry->get_list_cb(h->id1, ids, 512); 
        
        while (count < max_entries && *offset < (uint64_t)total) {
            utoa(ids[*offset], out_array[count].name, 10);
            
            // Если мы в папке threads/, мы говорим VFS, что внутри лежат СИМЛИНКИ, а не папки!
            if (h->entry->type == PROC_DYNAMIC_SYMLINK) out_array[count].type = VFS_FILE_TYPE_SYMLINK;
            else out_array[count].type = VFS_FILE_TYPE_DIR;
            
            out_array[count].size = 0; count++; (*offset)++;
        }
        if (*offset >= (uint64_t)total) *offset = (uint64_t)-1;
        return count;
    }

    // 3. Чтение статических шаблонов (внутри папки процесса, потока или fd)
    proc_entry_t* template = h->entry->children;
    if (template) {
        while (count < max_entries && template[*offset].name != NULL) {
            strcpy(out_array[count].name, template[*offset].name);
            
            if (template[*offset].type == PROC_FILE) out_array[count].type = VFS_FILE_TYPE_REGULAR;
            else if (template[*offset].type == PROC_SYMLINK) out_array[count].type = VFS_FILE_TYPE_SYMLINK;
            else out_array[count].type = VFS_FILE_TYPE_DIR;
            
            out_array[count].size = 0; count++; (*offset)++;
        }
        if (template[*offset].name == NULL) *offset = (uint64_t)-1;
        return count;
    }

    return 0;
}

int procfs_stat(fs_instance_t fs, fs_file_handle_t f, fs_stat_t* out_info) {
    UNUSED(fs); procfs_handle_t* h = (procfs_handle_t*)f;
    memset(out_info, 0, sizeof(fs_stat_t));
    
    out_info->inode_id = ((uint64_t)h->id1 << 32) | (uint32_t)((uint64_t)h->entry & 0xFFFFFFFF);
    out_info->size_bytes = 0;

    // Задаем тип файла для stat
    if (h->entry == NULL || h->entry->type == PROC_STATIC_DIR || 
       (h->entry->type == PROC_DYNAMIC_DIR && h->is_dynamic_root) || 
       (h->entry->type == PROC_DYNAMIC_DIR && !h->is_dynamic_root) ||
       (h->entry->type == PROC_DYNAMIC_SYMLINK && h->is_dynamic_root)) {
        out_info->attributes = 0x10; // DIR
    } else {
        out_info->attributes = 0; // REGULAR / SYMLINK
    }
    
    return 0;
}

void procfs_get_label(fs_instance_t fs, char* out_label) { UNUSED(fs); strcpy(out_label, "PROCFS"); }
void procfs_close(fs_instance_t fs, fs_file_handle_t f) { UNUSED(fs); free(f); }

fs_driver_t procfs_driver = {
    .mount = procfs_mount, .umount = procfs_umount,
    .open = procfs_open, .openat = procfs_openat,
    .read = procfs_read, .write = procfs_write,
    .readdir = procfs_readdir, .stat = procfs_stat,
    .get_label = procfs_get_label, .close = procfs_close
};