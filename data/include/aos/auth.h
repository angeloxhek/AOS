#ifndef AOS_AUTH_H
#define AOS_AUTH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUTH_ERR_OK                DRV_ERR_OK
#define AUTH_ERR_USER                -1
#define AUTH_ERR_DENIED              -2
#define AUTH_ERR_FOUND             DRV_ERR_FOUND
#define AUTH_ERR_NOCOMM            DRV_ERR_NOCOMM
#define AUTH_ERR_NOTFOUND          DRV_ERR_NOTFOUND
#define AUTH_ERR_UNKNOWN           DRV_ERR_UNKNOWN

typedef enum {
    AUTH_CMD_GET_USER = 1,
    AUTH_CMD_ADD_USER,
    AUTH_CMD_DEL_USER,
    AUTH_CMD_GET_USER_BY_NAME,
    AUTH_CMD_GET_GROUP,
    AUTH_CMD_ADD_GROUP,
    AUTH_CMD_DEL_GROUP,
    AUTH_CMD_GET_GROUP_BY_NAME,
    AUTH_CMD_GET_MEMBERS,
} auth_cmd_t;

typedef enum : uint8_t {
    PGROUP_SUPER = 0,
    PGROUP_ROOT,
    PGROUP_ADMIN,
    PGROUP_USER,
    PGROUP_TEMP
} auth_pgroup_t;

#define ATYPE_NONE         (0 << 0)
#define ATYPE_CHILD        (1 << 0)
#define ATYPE_TOKEN        (1 << 1)
#define ATYPE_PASSWORD     (1 << 2)
#define ATYPE_CHANGE       (1 << 3)

#define ATYPE_SUPER        (ATYPE_CHILD | ATYPE_CHANGE)
#define ATYPE_ROOT         (ATYPE_CHILD | ATYPE_PASSWORD | ATYPE_CHANGE)
#define ATYPE_ADMIN        (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD | ATYPE_CHANGE)
#define ATYPE_USER         (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD | ATYPE_CHANGE)
#define ATYPE_TEMP         (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD)

#define ATYPE_DEFAULT      (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD | ATYPE_CHANGE)

#define APERM_NONE         (0 << 0)
#define APERM_MANAGE_USER  (1 << 0)
#define APERM_MANAGE_GROUP (1 << 1)
#define APERM_USE_GROUP    (1 << 2)

typedef union {
    struct {
        uint32_t gid; // Dynamic group
        uint32_t uid;
    } user;
    uint64_t raw;
} auth_id_t;

typedef struct {
    auth_id_t id;
    auth_pgroup_t pgroup; // Static group
    uint8_t auth_type; // ATYPE_*
    uint64_t perms; // APERM_*
    char name[64];
    char pass[64];
} auth_idex_t;

typedef struct {
    auth_id_t id;
    uint8_t auth_type; // ATYPE_*
    uint64_t deny_perms;
    uint64_t allow_perms;
    char name[64];
    char pass[64];
} auth_grpex_t; // user perms = user.perms | group.allow_perms & ~group.deny_perms

typedef struct {
    uint32_t freemask;
    uint32_t count;
    auth_id_t data[32];
} auth_members_t;

#define AUTH_GET_FULL_PERMS(user, group) ((user)->perms | ((group)->allow_perms & ~(group)->deny_perms))

#ifdef AOSLIB_AUTH

void auth_init();
int auth_get_user(auth_id_t in, auth_idex_t* out);
int auth_get_user_by_name(const char* in, auth_idex_t* out);
int auth_add_user(auth_idex_t* inout);
int auth_del_user(auth_id_t in);
int auth_get_group(auth_id_t in, auth_grpex_t* out);
int auth_get_group_by_name(const char* in, auth_grpex_t* out);
int auth_add_group(auth_grpex_t* inout);
int auth_del_group(auth_id_t in);
int auth_get_members(auth_id_t in, uint32_t index, auth_members_t* out);

#endif

#ifdef __cplusplus
}
#endif

#endif // AOS_AUTH_H