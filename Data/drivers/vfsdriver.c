#include <stdint.h>
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

extern fs_driver_t fat32_driver; 

typedef enum {
    VFS_TYPE_DIR,           // Обычная папка (виртуальная)
    VFS_TYPE_DEVICE_FILE,   // Файл устройства (raw, memory, ctl) - VFS обрабатывает сама
    VFS_TYPE_MOUNT_POINT,   // Точка входа в драйвер ФС (папка fs/)
    VFS_TYPE_SYMLINK        // Символическая ссылка (soft link)
} vfs_node_type_t;

typedef struct vfs_node {
    char name[64];
    vfs_node_type_t type;
    
    struct vfs_node* parent;
    struct vfs_node* children;
    struct vfs_node* next;

    // Специфичные данные
    union {
        // Для VFS_TYPE_DEVICE_FILE
        struct {
            int (*read)(void* param, void* buf, uint64_t size, uint64_t offset);
            int (*write)(void* param, void* buf, uint64_t size, uint64_t offset);
            void* param; // Например, указатель на block_dev_t
        } dev_ops;

        // Для VFS_TYPE_MOUNT_POINT
        struct {
            fs_driver_t* driver;
            fs_instance_t fs_inst;
        } mount;

        // Для VFS_TYPE_SYMLINK
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
    union {
        // Если это файл внутри ФС (например FAT32)
        struct {
            fs_driver_t* driver;
            fs_instance_t fs;
            fs_file_handle_t handle;
        } mounted_file;

        // Если это файл устройства (например /hw/ide0/raw)
        struct {
            int (*read)(void* param, void* buf, uint64_t size, uint64_t offset);
            int (*write)(void* param, void* buf, uint64_t size, uint64_t offset);
            void* param;
        } device_file;

        // Если это открытая папка VFS (например /hw)
        struct {
            vfs_node_t* node;
        } dir;
    };
} vfs_file_t;

// === ГЛОБАЛЬНАЯ ТАБЛИЦА ФАЙЛОВ ===

#define MAX_OPEN_FILES 1024
vfs_file_t open_files[MAX_OPEN_FILES];

int vfs_alloc_fd(uint64_t tid) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].used) {
			memset(&open_files[i], 0, sizeof(vfs_file_t));
            open_files[i].used = 1;
            open_files[i].id = i + 1; // FD начинаются с 1
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

int dev_read_kmem(void* param, void* buf, uint64_t size, uint64_t offset) {
    return syscall(SYS_READ_KMEM, offset, size, (uint64_t)buf, 0, 0);
}

// Управление устройством (для 'ctl')
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

// Создание папки
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

// Создание спецфайла
void vfs_mkdev(vfs_node_t* parent, const char* name, void* read_func, void* param) {
	if (!name) return;
    vfs_node_t* node = vfs_mkdir(parent, name);
	if (!node) return;
    node->type = VFS_TYPE_DEVICE_FILE;
    node->dev_ops.read = read_func;
    node->dev_ops.param = param;
}

// Создание симлинка
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
    vfs_node_t* sys  = vfs_mkdir(vfs_root, "sys");
    vfs_node_t* proc = vfs_mkdir(vfs_root, "proc");
    vfs_node_t* mnt  = vfs_mkdir(vfs_root, "mnt");
    vfs_node_t* mnt_id = vfs_mkdir(mnt, "id");

    vfs_mkdev(sys, "kram", dev_read_kmem, 0);

    uint64_t disk_count = syscall(SYS_GET_DISK_COUNT, 0, 0, 0, 0, 0);
    for (int i = 0; i < disk_count; i++) {
        char name[16];
        sprintf(name, "ide%d", i);
        vfs_node_t* disk_node = vfs_mkdir(hw, name);
        block_dev_t* raw_disk = malloc(sizeof(block_dev_t));
		if (!raw_disk) return;
		memset(raw_disk, 0, sizeof(block_dev_t));
        raw_disk->disk_id = i;
        raw_disk->partition_offset_lba = 0;
		
        vfs_mkdev(disk_node, "raw", dev_read_raw, raw_disk);
        vfs_mkdev(disk_node, "ctl", 0, raw_disk);
    }

    uint64_t part_count = syscall(SYS_GET_PARTITION_COUNT, 0, 0, 0, 0, 0);
    for (int i = 0; i < part_count; i++) {
        partition_info_t pinfo;
        syscall(SYS_GET_PARTITION_INFO, i, (uint64_t)&pinfo, 0, 0, 0);
		
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
        
        vfs_mkdev(p_node, "raw", dev_read_raw, part_dev);

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
			
            char label[12] = "NO_NAME"; 
            // fat32_get_label(fs_inst, label);
            
            if (strcmp(label, "NO_NAME") != 0) {
                 char id_link[32];
                 sprintf(id_link, "/mnt/id/v%d", i);
                 vfs_symlink(mnt, label, id_link);
            }
        }
    }
}

typedef struct {
    vfs_node_t* node;       // Найденный узел (папка, файл устр-ва или точка монтирования)
    char* fs_path;          // Остаток пути для драйвера ФС (нужно делать free!)
    int error;              // 0 - OK, иначе код ошибки
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
        // --- Шаг 1: Проверка на Точку Монтирования ---
        if (current->type == VFS_TYPE_MOUNT_POINT) {
            // МЫ НАШЛИ ФС! 
            // Всё, что осталось в cursor - это путь внутри ФС.
            
            out->node = current;
            if (*cursor == 0) {
                out->fs_path = strdup("/"); // Корень ФС
            } else {
                out->fs_path = strdup(cursor); // "folder/file.txt"
            }
            out->error = 0;
            free(path_copy);
            return;
        }

        // --- Шаг 2: Конец строки ---
        if (*cursor == 0) {
            out->node = current;
            out->error = 0;
            free(path_copy);
            return;
        }

        // --- Шаг 3: Выделяем следующий токен ---
        char* next_slash = strchr(cursor, '/');
        if (next_slash) {
            *next_slash = 0;
        }

        char* token = cursor;

        // --- Шаг 4: Ищем ребенка ---
        vfs_node_t* next_node = 0;
        vfs_node_t* child = current->children;
        while (child) {
            if (strcmp(child->name, token) == 0) {
                next_node = child;
                break;
            }
            child = child->next;
        }

        // --- Шаг 5: Логика /proc (Динамическая) ---
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

        // --- Шаг 6: Обработка Symlink ---
        if (next_node->type == VFS_TYPE_SYMLINK) {
            
            // Ограничение рекурсии
            static int recursion_depth = 0;
            if (recursion_depth > 8) {
                out->error = VFS_ERR_SYMLINKLOOP; // Loop detected
                free(path_copy);
                return;
            }

            char* remainder = next_slash ? (next_slash + 1) : "";
            
            // target: "/hw/ide0/p0"
            // remainder: "windows/system32"
            int new_len = strlen(next_node->target_path) + 1 + strlen(remainder) + 1;
            char* new_full_path = malloc(new_len);
			if (!new_full_path) return;
			memset(new_full_path, 0, new_len);            
            strlcpy(new_full_path, next_node->target_path, new_len);
            if (*remainder) {
                strlcat(new_full_path, "/", new_len);
                strlcat(new_full_path, remainder, new_len);
            }

            // РЕКУРСИВНЫЙ ВЫЗОВ
            recursion_depth++;
            vfs_resolve(new_full_path, out);
            recursion_depth--;

            free(new_full_path);
            free(path_copy);
            return;
        }

        // Переходим дальше
        current = next_node;
        
        if (next_slash) {
            cursor = next_slash + 1;
        } else {
            // Слэша не было, значит это было последнее слово
            cursor = cursor + strlen(cursor);
        }
    }
}

void handle_vfs_request(message_t* req) {
    message_t resp;
	memset(&resp, 0, sizeof(message_t));
    // Копируем базовые поля для ответа
    resp.type = MSG_TYPE_VFS;
    resp.subtype = MSG_SUBTYPE_RESPONSE;
    resp.sender_tid = req->sender_tid; // Отвечаем тому, кто спросил
    resp.payload_ptr = 0;
    resp.payload_size = 0;

    switch (req->param1) {
        // === ОТКРЫТИЕ ФАЙЛА ===
        case VFS_CMD_OPEN: {
            char* path = (char*)req->payload_ptr;
            if (!path) { resp.param1 = VFS_ERR_UNKNOWN; break; }

            // 1. Резолвим путь
            vfs_path_result_t res;
            vfs_resolve(path, &res);

            if (res.error != 0) {
                resp.param1 = res.error;
                break;
            }

            // 2. Аллоцируем FD
            int fd = vfs_alloc_fd(req->sender_tid);
            if (fd < 0) {
                resp.param1 = VFS_ERR_UNKNOWN; // Limit reached
                if (res.fs_path) free(res.fs_path);
                break;
            }

            vfs_file_t* f = &open_files[fd - 1];
            f->offset = 0;

            // 3. Логика открытия в зависимости от типа узла
            if (res.node->type == VFS_TYPE_MOUNT_POINT) {
                // Это файл внутри ФС (FAT32)
                f->type = VFS_TYPE_MOUNT_POINT;
                f->mounted_file.driver = res.node->mount.driver;
                f->mounted_file.fs = res.node->mount.fs_inst;
                
                // Если путь пустой, значит открываем корень (для ls)
                char* p = (res.fs_path && *res.fs_path) ? res.fs_path : "/";
                
                if (f->mounted_file.driver && f->mounted_file.driver->open) {
                    f->mounted_file.handle = f->mounted_file.driver->open(f->mounted_file.fs, p);
                } else {
                    f->mounted_file.handle = 0; // Нет реализации open
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
                // Это файл устройства (например /hw/ide0/raw)
                f->type = VFS_TYPE_DEVICE_FILE;
                f->device_file.read = res.node->dev_ops.read;
                f->device_file.write = res.node->dev_ops.write;
                f->device_file.param = res.node->dev_ops.param;
                
                resp.param1 = VFS_ERR_OK;
                resp.param2 = fd;
            }
            else if (res.node->type == VFS_TYPE_DIR) {
                // Это виртуальная папка (например /hw)
                // Открываем её для чтения списка файлов
                f->type = VFS_TYPE_DIR;
                f->dir.node = res.node; // Запоминаем узел
                
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

        // === ЧТЕНИЕ ФАЙЛА ===
        case VFS_CMD_READ: {
            int fd = req->param2;
            uint64_t size = req->param3;
            
            vfs_file_t* f = vfs_get_file(fd, req->sender_tid);
            if (!f) { resp.param1 = VFS_ERR_PERM; break; }

            // Если это папка, читать её как файл нельзя
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
            
            syscall_ipc_send(req->sender_tid, &resp);
            free(buf);
            return;
        }
        
        // === ЧТЕНИЕ ДИРЕКТОРИИ (ls) ===
        case VFS_CMD_LIST: {
            // param2 = FD, param3 = index (0, 1, 2...)
            int fd = req->param2;
            int index = req->param3;
            
            vfs_file_t* f = vfs_get_file(fd, req->sender_tid);
            if (!f) { resp.param1 = VFS_ERR_PERM; break; }

            vfs_dirent_t dirent; // Структура ответа (определи её в protocol.h)
            memset(&dirent, 0, sizeof(vfs_dirent_t));
            int res = 0;

            if (f->type == VFS_TYPE_DIR) {
                // Виртуальная папка VFS
                // Итерируемся по списку детей: f->dir.node->children
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
                // Папка внутри FAT32
                // Драйвер должен поддерживать readdir
                if (f->mounted_file.driver->readdir) {
                    fs_dirent_t fs_ent;
					memset(&fs_ent, 0, sizeof(fs_dirent_t));
                    res = f->mounted_file.driver->readdir(
                        f->mounted_file.fs, f->mounted_file.handle, index, &fs_ent
                    );
                    if (res) {
                        strlcpy(dirent.name, fs_ent.name, sizeof(dirent.name));
                        dirent.size = fs_ent.size;
                        dirent.type = fs_ent.is_dir ? VFS_FILE_TYPE_DIR : VFS_FILE_TYPE_REGULAR;
                    }
                }
            }

            if (res) {
                resp.param1 = VFS_ERR_OK;
                resp.payload_ptr = (uint8_t*)&dirent; // Отправляем структуру
                resp.payload_size = sizeof(vfs_dirent_t);
            } else {
                resp.param1 = VFS_ERR_UNKNOWN; // EOF
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
    
    syscall_ipc_send(req->sender_tid, &resp);
}

int driver_main(void* reserved1, void* reserved2) {
	
    vfs_init_tree();
    printf("VFS: Tree initialized.\n");
	
	syscall_register_driver(DT_VFS, 0);
	
	message_t msg;
    while (1) {
        syscall_ipc_recv_filtered(0, MSG_TYPE_VFS, MSG_SUBTYPE_NONE, &msg);
		handle_vfs_request(&msg);
		if (msg.payload_ptr != (void*)0 && msg.payload_size > 0) {
            free(msg.payload_ptr);
            msg.payload_ptr = (void*)0;
            msg.payload_size = 0;
        }
    }
}