#include "../include/aoslib.h"

static inline void print_indent(int level) {
    for (int i = 0; i < level; i++) {
        printf("|   ");
    }
    printf("|-- ");
}

void scan_directory(int dir_fd, int level) {
    #define BATCH_SIZE 32
    vfs_dirent_t* entries = (vfs_dirent_t*)calloc(BATCH_SIZE, sizeof(vfs_dirent_t));
    if (!entries) {
        printf(" [Error: Out of memory for entries]\n");
        return;
    }

    int entries_read = 0;

    while ((entries_read = vfs_readdir(dir_fd, entries, BATCH_SIZE)) > 0) {
        
        for (int i = 0; i < entries_read; i++) {
            vfs_dirent_t* entry = &entries[i];

            if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
                continue;
            }
            
            print_indent(level);
            
            if (entry->type == VFS_FILE_TYPE_DIR) {
                printf("%s/\n", entry->name);
                
                int child_fd = vfs_openat(dir_fd, entry->name, 0);
                
                if (child_fd >= 0) {
                    scan_directory(child_fd, level + 1);
                    vfs_close(child_fd);
                } else {
                    printf(" [Error: failed to open %s]\n", entry->name);
                }
            } 
            else if (entry->type == VFS_FILE_TYPE_SYMLINK) {
                printf("%s [symlink]\n", entry->name);
            }
            else if (entry->type == VFS_FILE_TYPE_DEVICE) {
                printf("%s [device]\n", entry->name);
            }
            else {
                printf("%s [size: %d]\n", entry->name, (int)entry->size);
            }
        }
    }
    
    free(entries);
}

int main(int argc, char** argv) {
    printf("System Tree Scan Root (/):\n");
    printf(".\n");
    
    int root_fd = vfs_open("/", 0);
    
    if (root_fd >= 0) {
        scan_directory(root_fd, 0);
        vfs_close(root_fd);
    } else {
        printf("Error: Could not open root directory!\n");
    }

    printf("\nScan complete.\n");
    return 0;
}