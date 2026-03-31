#include "../include/aoslib.h"

static const char sc2a[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

void do_ls(const char* path) {
    int fd = vfs_open(path);
    if (fd < 0) { printf("err\n"); return; }
    vfs_dirent_t e[16]; int n;
    while ((n = vfs_readdir(fd, e, 16)) > 0)
        for (int i = 0; i < n; i++) printf("  %s\n", e[i].name);
    vfs_close(fd);
}

int main(int argc, char** argv) {
    printf("AOS Shell\nType: ls, info, help\naos> ");
    char cmd[64]; int pos = 0;
    while (1) {
        uint8_t sc = get_scancode();
        if (sc == 0 || sc >= 58) continue;
        char c = sc2a[sc];
        if (c == 0) continue;
        if (c == '\n') {
            printf("\n"); cmd[pos] = 0;
            if (strcmp(cmd,"ls")==0) do_ls("/");
            else if (strcmp(cmd,"info")==0) {
                system_info_t si; get_sysinfo(&si);
                printf("uptime: %d\n",(int)si.uptime);
            } else if (strcmp(cmd,"help")==0) {
                printf("ls info help\n");
            } else if (pos>0) printf("? %s\n",cmd);
            pos=0; printf("aos> ");
        } else if (c=='\b') {
            if (pos>0) { pos--; printf("\b \b"); }
        } else if (pos<62) {
            cmd[pos++]=c; char s[2]={c,0}; printf("%s",s);
        }
    }
}
