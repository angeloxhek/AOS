#include "../include/aoslib.h"

// PS/2 Set 1 scancode -> ASCII (lowercase)
static const char sc_lower[128] = {
    0,  0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};
// Shifted
static const char sc_upper[128] = {
    0,  0, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

static int shift_held = 0;

char scancode_to_char(uint8_t sc) {
    // shift press/release
    if (sc == 42 || sc == 54) { shift_held = 1; return 0; }
    if (sc == (42|0x80) || sc == (54|0x80)) { shift_held = 0; return 0; }
    if (sc & 0x80) return 0; // key release
    if (sc >= 58) return 0;  // unmapped
    return shift_held ? sc_upper[sc] : sc_lower[sc];
}

// ── Shell state ─────────────────────────────────────────────────
#define CMD_MAX 256
#define HISTORY_SIZE 8
static char cmdbuf[CMD_MAX];
static int cmdlen = 0;
static char history[HISTORY_SIZE][CMD_MAX];
static int hist_count = 0;
static int hist_pos = -1;
static char cwd[CMD_MAX] = "/";

void hist_add(const char* cmd) {
    if (cmd[0] == 0) return;
    if (hist_count > 0 && strcmp(history[(hist_count-1) % HISTORY_SIZE], cmd) == 0) return;
    strlcpy(history[hist_count % HISTORY_SIZE], cmd, CMD_MAX);
    hist_count++;
}

void prompt(void) {
    printf("%s$ ", cwd);
}

// ── Path helpers ────────────────────────────────────────────────
void resolve_path(const char* input, char* out, int outsize) {
    if (!input || !*input || strcmp(input, ".") == 0) {
        strlcpy(out, cwd, outsize);
        return;
    }
    if (input[0] == '/') {
        strlcpy(out, input, outsize);
    } else {
        strlcpy(out, cwd, outsize);
        if (cwd[strlen(cwd)-1] != '/') strlcat(out, "/", outsize);
        strlcat(out, input, outsize);
    }
}

// ── Commands ────────────────────────────────────────────────────
void cmd_help(void) {
    printf("Commands:\n");
    printf("  ls [path]        list directory\n");
    printf("  cd <path>        change directory\n");
    printf("  cat <file>       read file contents\n");
    printf("  tree [path]      filesystem tree\n");
    printf("  info             system info\n");
    printf("  pwd              print working directory\n");
    printf("  clear            clear screen\n");
    printf("  echo <text>      print text\n");
    printf("  help             this message\n");
}

void cmd_ls(const char* arg) {
    char path[CMD_MAX];
    resolve_path(*arg ? arg : ".", path, CMD_MAX);
    if (strcmp(path, ".") == 0) strlcpy(path, cwd, CMD_MAX);

    int fd = vfs_open(path);
    if (fd < 0) { printf("ls: no such directory: %s\n", path); return; }

    vfs_dirent_t e[16]; int n;
    while ((n = vfs_readdir(fd, e, 16)) > 0) {
        for (int i = 0; i < n; i++) {
            if (e[i].type == VFS_FILE_TYPE_DIR)
                printf("  %s/\n", e[i].name);
            else if (e[i].type == VFS_FILE_TYPE_SYMLINK)
                printf("  %s -> [link]\n", e[i].name);
            else if (e[i].type == VFS_FILE_TYPE_DEVICE)
                printf("  %s [dev]\n", e[i].name);
            else
                printf("  %s (%d bytes)\n", e[i].name, (int)e[i].size);
        }
    }
    vfs_close(fd);
}

void cmd_cd(const char* arg) {
    if (!*arg) { strlcpy(cwd, "/", CMD_MAX); return; }
    char path[CMD_MAX];
    resolve_path(arg, path, CMD_MAX);
    int fd = vfs_open(path);
    if (fd < 0) { printf("cd: not found: %s\n", path); return; }
    vfs_close(fd);
    strlcpy(cwd, path, CMD_MAX);
    // remove trailing slash (except root)
    int len = strlen(cwd);
    if (len > 1 && cwd[len-1] == '/') cwd[len-1] = 0;
}

void cmd_cat(const char* arg) {
    if (!*arg) { printf("cat: need a path\n"); return; }
    char path[CMD_MAX];
    resolve_path(arg, path, CMD_MAX);
    int fd = vfs_open(path);
    if (fd < 0) { printf("cat: not found: %s\n", path); return; }
    char buf[512];
    int total = 0;
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        printf("%s", buf);
        total += n;
        if (total > 4096) break;
    }
    if (total == 0) printf("(empty)");
    printf("\n");
    vfs_close(fd);
}

void do_tree(int dir_fd, int depth) {
    vfs_dirent_t e[16]; int n;
    while ((n = vfs_readdir(dir_fd, e, 16)) > 0) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < depth; j++) printf("  ");
            if (e[i].type == VFS_FILE_TYPE_DIR) {
                printf("%s/\n", e[i].name);
                int c = vfs_openat(dir_fd, e[i].name);
                if (c >= 0) { do_tree(c, depth + 1); vfs_close(c); }
            } else if (e[i].type == VFS_FILE_TYPE_SYMLINK) {
                printf("%s -> [link]\n", e[i].name);
            } else {
                printf("%s\n", e[i].name);
            }
        }
    }
}

void cmd_tree(const char* arg) {
    char path[CMD_MAX];
    resolve_path(*arg ? arg : ".", path, CMD_MAX);
    if (strcmp(path, ".") == 0) strlcpy(path, cwd, CMD_MAX);
    printf("%s\n", path);
    int fd = vfs_open(path);
    if (fd < 0) { printf("tree: not found: %s\n", path); return; }
    do_tree(fd, 1);
    vfs_close(fd);
}

void cmd_info(void) {
    system_info_t si;
    get_sysinfo(&si);
    printf("Uptime:     %d ticks\n", (int)si.uptime);
    printf("Flags:      0x%x\n", (int)si.flags);
    printf("CPU flags:  0x%x\n", (int)si.cpu_flags);
}

void cmd_echo(const char* arg) {
    printf("%s\n", arg);
}

// ── Execute ─────────────────────────────────────────────────────
void execute(const char* line) {
    char buf[CMD_MAX];
    strlcpy(buf, line, CMD_MAX);

    // trim leading spaces
    char* cmd = buf;
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) return;

    // split cmd + arg
    char* arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg = 0; arg++; }
    while (*arg == ' ') arg++;

    if      (strcmp(cmd, "help") == 0)  cmd_help();
    else if (strcmp(cmd, "ls") == 0)    cmd_ls(arg);
    else if (strcmp(cmd, "cd") == 0)    cmd_cd(arg);
    else if (strcmp(cmd, "cat") == 0)   cmd_cat(arg);
    else if (strcmp(cmd, "tree") == 0)  cmd_tree(arg);
    else if (strcmp(cmd, "info") == 0)  cmd_info();
    else if (strcmp(cmd, "pwd") == 0)   printf("%s\n", cwd);
    else if (strcmp(cmd, "echo") == 0)  cmd_echo(arg);
    else if (strcmp(cmd, "clear") == 0) { /* kernel doesn't expose kclear to userspace */ printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"); }
    else printf("unknown command: '%s' (type 'help')\n", cmd);
}

// ── Main loop ───────────────────────────────────────────────────
int main(int argc, char** argv) {
    printf("=== AOS Shell ===\nType 'help' for commands\n\n");
    prompt();

    while (1) {
        uint8_t sc = get_scancode();
        char c = scancode_to_char(sc);
        if (c == 0) continue;

        if (c == '\n') {
            printf("\n");
            cmdbuf[cmdlen] = 0;
            hist_add(cmdbuf);
            hist_pos = -1;
            execute(cmdbuf);
            cmdlen = 0;
            prompt();
        } else if (c == '\b') {
            if (cmdlen > 0) {
                cmdlen--;
                printf("\b \b");
            }
        } else if (c == '\t') {
            // simple tab = 4 spaces visual
        } else if (cmdlen < CMD_MAX - 1) {
            cmdbuf[cmdlen++] = c;
            char s[2] = {c, 0};
            printf("%s", s);
        }
    }
}
