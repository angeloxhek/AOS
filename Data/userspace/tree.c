#include "../include/aoslib.h"

static inline void print_indent(int level) {
    for (int i = 0; i < level; i++) {
        printf("|   ");
    }
    printf("|-- ");
}

void scan_directory(const char* current_path, int level) {
    int fd = vfs_open(current_path);
    if (fd < 0) {
        printf(" [Error: failed to open %s]\n", current_path);
        return;
    }
    vfs_dirent_t entry;
    int index = 0;
    while (vfs_readdir(fd, index, &entry) == 1) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
            index++;
            continue;
        }
        print_indent(level);
        if (entry.type == VFS_FILE_TYPE_DIR) {
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
                scan_directory(new_path, level + 1);
                free(new_path);
            } else {
                printf(" [Error: Out of memory]\n");
            }
        } 
        else if (entry.type == VFS_FILE_TYPE_SYMLINK) {
            printf("%s [symlink]\n", entry.name);
        }
        else if (entry.type == VFS_FILE_TYPE_DEVICE) {
            printf("%s [device]\n", entry.name);
        }
        else {
            printf("%s [size: %d]\n", entry.name, (int)entry.size);
        }
        index++;
    }
    vfs_close(fd);
}

int main(int argc, char** argv) {
    printf("System Tree Scan Root (/):\n");
    printf(".\n");
    scan_directory("/", 0);

    printf("\nScan complete.\n");
}