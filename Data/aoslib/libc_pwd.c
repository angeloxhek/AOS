#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include "../include/aoslib.h"

static struct passwd static_passwd;
static char static_name[64];
static char static_pass[64];
static char static_dir[64];
static char static_shell[64];

static int getpwidex_r(auth_idex_t* user, struct passwd *pwd, char *buffer, size_t buflen, struct passwd **result) {
	char dir[256] = "/";
    char shell[256] = "/bin/sh";

    size_t len_name = strnlen(user->name, 64) + 1;
    size_t len_pass = strnlen(user->pass, 64) + 1;
    size_t len_dir = strlen(dir) + 1;
    size_t len_shell = strlen(shell) + 1;
    size_t total_needed = len_name + len_pass + len_dir + len_shell;

    if (total_needed > buflen) {
        return SYS_RES_RANGE; 
    }

    char *ptr = buffer;
    
    pwd->pw_name = ptr;
    memcpy(ptr, user->name, len_name);
    ptr += len_name;

    pwd->pw_passwd = ptr;
    memcpy(ptr, user->pass, len_pass);
    ptr += len_pass;

    pwd->pw_dir = ptr;
    memcpy(ptr, dir, len_dir);
    ptr += len_dir;

    pwd->pw_shell = ptr;
    memcpy(ptr, shell, len_shell);
    ptr += len_shell;

    pwd->pw_uid = user->id.user.uid;
    pwd->pw_gid = user->id.user.gid;
    pwd->pw_gecos = "";

    *result = pwd;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buffer, size_t buflen, struct passwd **result) {
    auth_idex_t user;
    *result = NULL;

    if (auth_get_user_by_name(name, &user) != AUTH_ERR_OK) {
        return 0;
    }

    return getpwidex_r(&user, pwd, buffer, buflen, result);
}

struct passwd *getpwnam(const char *name) {
    static struct passwd static_pwd;
    static char *static_buf = NULL;
    static size_t static_buf_size = 512;
    struct passwd *res;

    if (static_buf == NULL) {
        static_buf = malloc(static_buf_size);
        if (!static_buf) return NULL;
    }

    int err = getpwnam_r(name, &static_pwd, static_buf, static_buf_size, &res);

    if (err == SYS_RES_RANGE) {
        static_buf_size *= 2;
        static_buf = realloc(static_buf, static_buf_size);
        if (!static_buf) return NULL;
        
        if (getpwnam_r(name, &static_pwd, static_buf, static_buf_size, &res) != 0) {
            return NULL;
        }
    } else if (err != 0) {
        return NULL;
    }

    return res;
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t buflen, struct passwd **result) {
    auth_idex_t user;
    *result = NULL;
	
	auth_id_t u;
	u.user.uid = uid;

    if (auth_get_user(u, &user) != AUTH_ERR_OK) {
        return 0;
    }

    return getpwidex_r(&user, pwd, buffer, buflen, result);
}

struct passwd *getpwuid(uid_t uid) {
    static struct passwd static_pwd;
    static char *static_buf = NULL;
    static size_t static_buf_size = 512;
    struct passwd *res;

    if (static_buf == NULL) {
        static_buf = malloc(static_buf_size);
        if (!static_buf) return NULL;
    }

    int err = getpwuid_r(uid, &static_pwd, static_buf, static_buf_size, &res);

    if (err == SYS_RES_RANGE) {
        static_buf_size *= 2;
        static_buf = realloc(static_buf, static_buf_size);
        if (!static_buf) return NULL;
        
        if (getpwuid_r(uid, &static_pwd, static_buf, static_buf_size, &res) != 0) {
            return NULL;
        }
    } else if (err != 0) {
        return NULL;
    }

    return res;
}