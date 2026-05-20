#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include "../include/aoslib.h"

static char *empty_mem_list[] = { NULL };

static int getgrpex_r(auth_grpex_t* grp, struct group *dest, char *buffer, size_t buflen, struct group **result) {
    *result = NULL;

    size_t len_name = strnlen(grp->name, 64) + 1;
    size_t len_pass = strnlen(grp->pass, 64) + 1;
    size_t total_needed = len_name + len_pass;

    if (total_needed > buflen) {
        return SYS_RES_RANGE; 
    }

    char *ptr = buffer;
    
    dest->gr_name = ptr;
    memcpy(ptr, grp->name, len_name);
    ptr += len_name;

    dest->gr_passwd = ptr;
    memcpy(ptr, grp->pass, len_pass);
    ptr += len_pass;

    dest->gr_gid = grp->id.user.gid;
    
    dest->gr_mem = empty_mem_list;

    *result = dest;
    return 0;
}

int getgrnam_r(const char *name, struct group *grp, char *buffer, size_t buflen, struct group **result) {
    auth_grpex_t grp_data;
    
    if (auth_get_group_by_name(name, &grp_data) != AUTH_ERR_OK) {
        return 0;
    }

    return getgrpex_r(&grp_data, grp, buffer, buflen, result);
}

int getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t buflen, struct group **result) {
    auth_grpex_t grp_data;
    auth_id_t id;
    id.user.gid = gid;
    id.user.uid = 0;

    if (auth_get_group(id, &grp_data) != AUTH_ERR_OK) {
        return 0;
    }

    return getgrpex_r(&grp_data, grp, buffer, buflen, result);
}

struct group *getgrnam(const char *name) {
    static struct group static_grp;
    static char *static_buf = NULL;
    static size_t static_buf_size = 512;
    struct group *res;

    if (static_buf == NULL) {
        static_buf = malloc(static_buf_size);
        if (!static_buf) return NULL;
    }

    int err = getgrnam_r(name, &static_grp, static_buf, static_buf_size, &res);

    if (err == SYS_RES_RANGE) {
        static_buf_size *= 2;
        static_buf = realloc(static_buf, static_buf_size);
        if (!static_buf) return NULL;
        
        if (getgrnam_r(name, &static_grp, static_buf, static_buf_size, &res) != 0) {
            return NULL;
        }
    } else if (err != 0) {
        return NULL;
    }

    return res;
}

struct group *getgrgid(gid_t gid) {
    static struct group static_grp;
    static char *static_buf = NULL;
    static size_t static_buf_size = 512;
    struct group *res;

    if (static_buf == NULL) {
        static_buf = malloc(static_buf_size);
        if (!static_buf) return NULL;
    }

    int err = getgrgid_r(gid, &static_grp, static_buf, static_buf_size, &res);

    if (err == SYS_RES_RANGE) {
        static_buf_size *= 2;
        static_buf = realloc(static_buf, static_buf_size);
        if (!static_buf) return NULL;
        
        if (getgrgid_r(gid, &static_grp, static_buf, static_buf_size, &res) != 0) {
            return NULL;
        }
    } else if (err != 0) {
        return NULL;
    }

    return res;
}