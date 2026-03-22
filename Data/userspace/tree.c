#include "../include/aoslib.h"

// Вспомогательная функция для отрисовки отступов
void print_indent(int level) {
    for (int i = 0; i < level; i++) {
        printf("|   ");
    }
    printf("|-- ");
}

// Рекурсивная функция обхода
void scan_directory(const char* current_path, int level) {
    // 1. Открываем директорию
    int fd = vfs_open(current_path);
    if (fd < 0) {
        // Если не удалось открыть (например, нет прав или это не папка), просто выходим
        // Можно раскомментировать для отладки:
        printf(" [Error: failed to open %s]\n", current_path);
        return;
    }

    vfs_dirent_t entry;
    int index = 0;

    // 2. Читаем записи по индексу (0, 1, 2...)
    while (vfs_readdir(fd, index, &entry) == 1) {
        // Пропускаем "." и "..", чтобы избежать бесконечной рекурсии,
        // если драйвер ФС их возвращает.
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
            index++;
            continue;
        }
		
		printf(" (DEBUG: VFS sent '%s' inside '%s') ", entry.name, current_path);

        // Рисуем отступ
        print_indent(level);

        if (entry.type == VFS_FILE_TYPE_DIR) {
            // === ЭТО ДИРЕКТОРИЯ ===
            printf("%s/\n", entry.name);

            uint64_t path_len = strlen(current_path);
            int needs_slash = 0;
            if (path_len > 0) {
                needs_slash = (current_path[path_len - 1] != '/');
            }
            
            uint64_t new_len = path_len + needs_slash + strlen(entry.name) + 1;
            char* new_path = (char*)malloc(new_len);
            
            if (new_path) {
                memset(new_path, 0, new_len);
                strcpy(new_path, current_path);
                if (needs_slash) {
                    strcat(new_path, "/");
                }
                strcat(new_path, entry.name);

                // Рекурсивный вызов
                scan_directory(new_path, level + 1);

                free(new_path);
            } else {
                printf(" [Error: Out of memory]\n");
            }
        } 
        else if (entry.type == VFS_FILE_TYPE_SYMLINK) {
            // === ЭТО СИМЛИНК ===
            printf("%s [symlink]\n", entry.name);
        }
        else if (entry.type == VFS_FILE_TYPE_DEVICE) {
            // === ЭТО УСТРОЙСТВО ===
            printf("%s [device]\n", entry.name);
        }
        else {
            // === ЭТО ОБЫЧНЫЙ ФАЙЛ ===
            printf("%s [size: %d]\n", entry.name, (int)entry.size);
        }

        index++;
    }

    // 3. Закрываем директорию
    vfs_close(fd);
}

int main(int argc, char** argv) {
    // Инициализация соединения с VFS (обязательно)
    vfs_init();

    printf("System Tree Scan Root (/):\n");
    printf(".\n");

    // Запускаем сканирование с корня
    scan_directory("/", 0);

    printf("\nScan complete.\n");
    
    // В микроядре обычно main не возвращает управление в никуда,
    // поэтому либо sys_exit, либо бесконечный цикл.
    // syscall(SYS_EXIT, 0...);
    while(1) {} 
}