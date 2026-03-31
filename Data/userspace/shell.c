#include "../include/aoslib.h"

static const char sc2a[128] = {
    0,  0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

void do_ls(const char* path) {
    int fd = vfs_open(path);
    if (fd < 0) { printf("error: cannot open %s\n", path); return; }
    vfs_dirent_t e[16]; int n;
    while ((n = vfs_readdir(fd, e, 16)) > 0)
        for (int i = 0; i < n; i++) {
            if (e[i].type == VFS_FILE_TYPE_DIR) printf("  %s/\n", e[i].name);
            else if (e[i].type == VFS_FILE_TYPE_SYMLINK) printf("  %s -> [link]\n", e[i].name);
            else if (e[i].type == VFS_FILE_TYPE_DEVICE) printf("  %s [dev]\n", e[i].name);
            else printf("  %s (%d)\n", e[i].name, (int)e[i].size);
        }
    vfs_close(fd);
}

void do_cat(const char* path) {
    int fd = vfs_open(path);
    if (fd < 0) { printf("error: cannot open %s\n", path); return; }
    char buf[512];
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; printf("%s\n", buf); }
    else printf("(empty)\n");
    vfs_close(fd);
}

void do_tree(int dir_fd, int depth) {
    vfs_dirent_t e[16]; int n;
    while ((n = vfs_readdir(dir_fd, e, 16)) > 0)
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < depth; j++) printf("  ");
            printf("%s", e[i].name);
            if (e[i].type == VFS_FILE_TYPE_DIR) {
                printf("/\n");
                int c = vfs_openat(dir_fd, e[i].name);
                if (c >= 0) { do_tree(c, depth + 1); vfs_close(c); }
            } else printf("\n");
        }
}

int main(int argc, char** argv) {
    printf("=== AOS Shell ===\nType 'help' for commands\n\naos> ");

    char cmd[128]; int pos = 0;
    while (1) {
        uint8_t sc = get_scancode();
        if (sc & 0x80) continue;       // skip key release
        if (sc == 0 || sc >= 58) continue;  // skip unmapped
        char c = sc2a[sc];
        if (c == 0) continue;

        if (c == '\n') {
            printf("\n"); cmd[pos] = 0;
            // parse command + arg
            char* arg = cmd;
            while (*arg && *arg != ' ') arg++;
            if (*arg == ' ') { *arg = 0; arg++; }
            while (*arg == ' ') arg++;

            if (strcmp(cmd, "help") == 0) {
                printf("  help          - this message\n");
                printf("  ls [path]     - list directory\n");
                printf("  cat <path>    - read file\n");
                printf("  tree          - filesystem tree\n");
                printf("  info          - system info\n");
            } else if (strcmp(cmd, "ls") == 0) {
                do_ls(*arg ? arg : "/");
            } else if (strcmp(cmd, "cat") == 0) {
                do_cat(arg);
            } else if (strcmp(cmd, "tree") == 0) {
                printf("/\n");
                int fd = vfs_open("/");
                if (fd >= 0) { do_tree(fd, 1); vfs_close(fd); }
            } else if (strcmp(cmd, "info") == 0) {
                system_info_t si; get_sysinfo(&si);
                printf("Uptime: %d ticks\nFlags:  0x%x\n", (int)si.uptime, (int)si.flags);
            } else if (pos > 0) {
                printf("unknown: '%s'\n", cmd);
            }
            pos = 0; printf("aos> ");
        } else if (c == '\b') {
            if (pos > 0) { pos--; printf("\b \b"); }
        } else if (pos < 126) {
            cmd[pos++] = c;
            char s[2] = {c, 0};
            printf("%s", s);
        }
    }
}
