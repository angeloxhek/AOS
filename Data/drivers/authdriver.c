#include <stdint.h>
#include "../include/aoslib.h"

typedef struct auth_idex_node_t {
	auth_idex_t data;
	struct auth_idex_node_t* next;
} auth_idex_node_t;

auth_idex_node_t* idlist;

typedef struct auth_members_node_t {
	auth_members_t data;
	struct auth_members_node_t* next;
} auth_members_node_t;

typedef struct auth_grpex_node_t {
	auth_grpex_t grp;
	auth_members_node_t* members;
	struct auth_grpex_node_t* next;
} auth_grpex_node_t;

auth_grpex_node_t* grplist;

typedef struct free_range_t {
    uint32_t start;
    uint32_t end;
    struct free_range_t *next;
} free_range_t;

free_range_t *ufreelist;
free_range_t *gfreelist;

int get_id(free_range_t** list, uint32_t* out) {
    if (!list) return -1;
    uint32_t id = (*list)->start;
    if ((*list)->start == (*list)->end) {
        free_range_t *tmp = *list;
        *list = (*list)->next;
        free(tmp);
    } else {
        (*list)->start++;
    }
	*out = id;
    return 0;
}

int get_uid(uint32_t* out) {
    return get_id(&ufreelist, out);
}

int get_gid(uint32_t* out) {
    return get_id(&gfreelist, out);
}

void del_id(free_range_t** list, uint32_t id) {
    free_range_t* prev = NULL;
    free_range_t* curr = *list;

    while (curr != NULL && curr->start < id) {
        prev = curr;
        curr = curr->next;
    }

    int can_merge_prev = (prev != NULL && prev->end + 1 == id);
    int can_merge_next = (curr != NULL && id + 1 == curr->start);

    if (can_merge_prev && can_merge_next) {
        prev->end = curr->end;
        prev->next = curr->next;
        free(curr);
    } 
    else if (can_merge_prev) {
        prev->end++;
    } 
    else if (can_merge_next) {
        curr->start--;
    } 
    else {
        free_range_t* new_node = (free_range_t*)malloc(sizeof(free_range_t));
        if (!new_node) return;
        new_node->start = id;
        new_node->end = id;
        new_node->next = curr;
        
        if (prev) prev->next = new_node;
        else *list = new_node;
    }
}

void del_uid(uint32_t uid) {
	del_id(&ufreelist, uid);
}

void del_gid(uint32_t gid) {
	del_id(&gfreelist, gid);
}

int test_id(free_range_t** list, uint32_t id) {
    free_range_t* curr = *list;
    while (curr != NULL) {
        if (id >= curr->start && id <= curr->end) {
            return 1;
        }
        if (curr->start > id) {
            break;
        }
        curr = curr->next;
    }
    return 0;
}

int test_uid(uint32_t uid) {
	return test_id(&ufreelist, uid);
}

int test_gid(uint32_t gid) {
	return test_id(&gfreelist, gid);
}

int init_auth() {
	ufreelist = (free_range_t*)malloc(sizeof(free_range_t));
	if (!ufreelist) return -1;
	memset(ufreelist, 0, sizeof(free_range_t));
	ufreelist->start = 1;
	ufreelist->end = UINT32_MAX;
	
	gfreelist = (free_range_t*)malloc(sizeof(free_range_t));
	if (!gfreelist) return -1;
	memset(gfreelist, 0, sizeof(free_range_t));
	gfreelist->start = 0;
	gfreelist->end = UINT32_MAX;
	
	idlist = (auth_idex_node_t*)malloc(sizeof(auth_idex_node_t));
	if (!idlist) {
		free(ufreelist);
		free(gfreelist);
		return -1;
	}
	memset(idlist, 0, sizeof(auth_idex_node_t));
	uint32_t uid = 0;
	if (get_uid(&uid)) {
		free(ufreelist);
		free(gfreelist);
		free(idlist);
		return -1;
	}
	idlist->data.id.user.uid = uid;
	idlist->data.pgroup = PGROUP_SUPER;
	idlist->data.auth_type = ATYPE_SUPER;
	idlist->data.perms = (uint32_t)-1;
	strlcpy(idlist->data.name, "kernel", sizeof(idlist->data.name));
	strlcpy(idlist->data.pass, "x", sizeof(idlist->data.pass));
	
	grplist = (auth_grpex_node_t*)malloc(sizeof(auth_grpex_node_t));
	if (!grplist) {
		free(ufreelist);
		free(gfreelist);
		return -1;
	}
	memset(idlist, 0, sizeof(auth_idex_node_t));
	uint32_t gid = 0;
	if (get_gid(&gid)) {
		free(ufreelist);
		free(gfreelist);
		free(idlist);
		free(grplist);
		return -1;
	}
	grplist->grp.id.user.gid = gid;
	grplist->grp.deny_perms = 0;
	grplist->grp.allow_perms = 0;
	grplist->grp.auth_type = ATYPE_DEFAULT;
	strlcpy(grplist->grp.name, "default", sizeof(grplist->grp.name));
	strlcpy(grplist->grp.pass, "x", sizeof(grplist->grp.pass));
	
	return 0;
}

int add_user(auth_idex_t* inout, uint8_t isload) {
	auth_idex_node_t* user = (auth_idex_node_t*)malloc(sizeof(auth_idex_node_t));
	if (!user) return -1;
	memcpy(&user->data, inout, sizeof(auth_idex_t));
	if (!isload) {
		uint32_t uid = 0;
		if (get_uid(&uid)) {
			free(user);
			return -1;
		}
		user->data.id.user.uid = uid;
		inout->id.user.uid = uid;
		user->data.id.user.gid = 0;
		inout->id.user.gid = 0;
	}
	user->next = idlist;
	idlist = user;
	return 0;
}

int del_user(auth_id_t user) {
    auth_idex_node_t* curr = idlist;
    auth_idex_node_t* prev = NULL;
    while (curr != NULL) {
        if (curr->data.id.user.uid == user.user.uid) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                idlist = curr->next;
            }
            del_uid(curr->data.id.user.uid);
            free(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

int get_user(auth_id_t user, auth_idex_t* out) {
	if (!out) return -1;
	auth_idex_node_t* curr = idlist;
	while (curr) {
		if (curr->data.id.user.uid == user.user.uid) {
			memcpy(out, &curr->data, sizeof(auth_idex_t));
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

int get_user_by_name(const char* user, auth_idex_t* out) {
	if (!out) return -1;
	auth_idex_node_t* curr = idlist;
	while (curr) {
		if (strcmp(curr->data.name, user) == 0) {
			memcpy(out, &curr->data, sizeof(auth_idex_t));
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

int get_thread_user(uint64_t tid, auth_idex_t* out) {
	if (!out) return -1;
	thread_info_user_t* info = (thread_info_user_t*)malloc(sizeof(thread_info_user_t));
	if (get_thread_info(tid, info) != SYS_RES_OK) {
		free(info);
		return -1;
	}
	return get_user(info->user, out);
}

int add_group(auth_grpex_t* inout, uint8_t isload) {
	auth_grpex_node_t* group = (auth_grpex_node_t*)malloc(sizeof(auth_grpex_node_t));
	if (!group) return -1;
	memcpy(&group->grp, inout, sizeof(auth_grpex_t));
	if (!isload) {
		uint32_t gid = 0;
		if (get_gid(&gid)) {
			free(group);
			return -1;
		}
		group->grp.id.user.uid = 0;
		inout->id.user.uid = 0;
		group->grp.id.user.gid = gid;
		inout->id.user.gid = gid;
	}
	group->next = grplist;
	grplist = group;
	return 0;
}

int del_group(auth_id_t group) {
    auth_grpex_node_t* curr = grplist;
    auth_grpex_node_t* prev = NULL;
    while (curr != NULL) {
        if (curr->grp.id.user.gid == group.user.gid) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                grplist = curr->next;
            }
            del_gid(curr->grp.id.user.uid);
            free(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

int get_group(auth_id_t group, auth_grpex_t* out) {
	if (!out) return -1;
	auth_grpex_node_t* curr = grplist;
	while (curr) {
		if (curr->grp.id.user.gid == group.user.gid) {
			memcpy(out, &curr->grp, sizeof(auth_grpex_t));
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

int get_group_by_name(const char* group, auth_grpex_t* out) {
	if (!out) return -1;
	auth_grpex_node_t* curr = grplist;
	while (curr) {
		if (strcmp(curr->grp.name, group) == 0) {
			memcpy(out, &curr->grp, sizeof(auth_grpex_t));
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

int get_thread_group(uint64_t tid, auth_grpex_t* out) {
	if (!out) return -1;
	thread_info_user_t* info = (thread_info_user_t*)malloc(sizeof(thread_info_user_t));
	if (get_thread_info(tid, info) != SYS_RES_OK) {
		free(info);
		return -1;
	}
	return get_group(info->user, out);
}

int can_create_user(auth_idex_t* parent, auth_grpex_t* group, auth_idex_t* child) {
	uint64_t pperms = AUTH_GET_FULL_PERMS(parent, group);
	if (parent->pgroup > child->pgroup) return 0;
	if (parent->pgroup == child->pgroup && (pperms | child->perms) != pperms) return 0;
	if (!(pperms & APERM_MANAGE_USER)) return 0;
	if ((parent->auth_type | child->auth_type) != parent->auth_type) return 0;
	if ((child->auth_type & ATYPE_CHANGE) && !(parent->auth_type & ATYPE_CHANGE)) return 0;
	return 1;
}

int can_delete_user(auth_idex_t* parent, auth_grpex_t* group, auth_idex_t* child) {
	uint64_t pperms = AUTH_GET_FULL_PERMS(parent, group);
	if (parent->pgroup > child->pgroup) return 0;
	if (parent->pgroup == child->pgroup && (pperms | child->perms) != pperms) return 0;
	if (!(pperms & APERM_MANAGE_USER)) return 0;
	return 1;
}

int can_create_group(auth_idex_t* parent, auth_grpex_t* group, auth_grpex_t* child) {
	uint64_t pperms = AUTH_GET_FULL_PERMS(parent, group);
	if ((pperms | child->allow_perms & ~child->deny_perms) != pperms) return 0;
	if (!(pperms & APERM_MANAGE_GROUP)) return 0;
	if ((parent->auth_type | child->auth_type) != parent->auth_type) return 0;
	if ((child->auth_type & ATYPE_CHANGE) && !(parent->auth_type & ATYPE_CHANGE)) return 0;
	return 1;
}

int can_delete_group(auth_idex_t* parent, auth_grpex_t* group, auth_grpex_t* child) {
	uint64_t pperms = AUTH_GET_FULL_PERMS(parent, group);
	if ((pperms | child->allow_perms & ~child->deny_perms) != pperms) return 0;
	if (!(pperms & APERM_MANAGE_GROUP)) return 0;
	return 1;
}

void handle_message(message_t* in) {
	message_t* out = (message_t*)malloc(sizeof(message_t));
	if (!out) return;
	memset(out, 0, sizeof(message_t));
	out->type = MSG_TYPE_AUTH;
	out->subtype = MSG_SUBTYPE_RESPONSE;
	switch (in->param1) {
		case AUTH_CMD_GET_USER: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			auth_id_t user;
			user.raw = in->param2;
			void* buf = shm_map(*(uint64_t*)(in->data));
			if (!buf) { out->param1 = AUTH_ERR_UNKNOWN; break; }
			memset(buf, 0, sizeof(auth_idex_t));
			int res = get_user(user, (auth_idex_t*)buf);
			strlcpy(((auth_idex_t*)buf)->pass, "x", sizeof(((auth_idex_t*)buf)->pass));
			shm_free(*(uint64_t*)(in->data));
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		case AUTH_CMD_GET_USER_BY_NAME: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			void* buf = shm_map(in->param2);
			if (!buf) { out->param1 = AUTH_ERR_UNKNOWN; break; }
			memset(buf, 0, sizeof(auth_idex_t));
			int res = get_user_by_name((const char*)in->data, (auth_idex_t*)buf);
			strlcpy(((auth_idex_t*)buf)->pass, "x", sizeof(((auth_idex_t*)buf)->pass));
			shm_free(in->param2);
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		
		case AUTH_CMD_ADD_USER: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
            auth_idex_t* new_user = (auth_idex_t*)shm_map(*(uint64_t*)(in->data));
			if (!new_user) { out->param1 = AUTH_ERR_UNKNOWN; break; }
			auth_idex_t* curr_user = (auth_idex_t*)malloc(sizeof(auth_idex_t));
			auth_grpex_t* curr_group = (auth_grpex_t*)malloc(sizeof(auth_grpex_t));
			if (!curr_user || !curr_group) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (get_thread_user(in->sender_tid, curr_user) || get_thread_group(in->sender_tid, curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				break;
			}
			if (!can_create_user(curr_user, curr_group, new_user)) {
				out->param1 = AUTH_ERR_DENIED;
				break;
			}
            int res = add_user(new_user, 0);
			shm_free(*(uint64_t*)(in->data));
            out->param1 = (!res) ? AUTH_ERR_OK : AUTH_ERR_USER;
            break;
        }
		case AUTH_CMD_DEL_USER: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			auth_id_t user;
			user.raw = in->param2;
			auth_idex_t* curr_user = (auth_idex_t*)malloc(sizeof(auth_idex_t));
			auth_grpex_t* curr_group = (auth_grpex_t*)malloc(sizeof(auth_grpex_t));
			auth_idex_t* u = (auth_idex_t*)malloc(sizeof(auth_idex_t));
			if (!curr_user || !curr_group || !u) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (get_thread_user(in->sender_tid, curr_user) || get_thread_group(in->sender_tid, curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				break;
			}
			if (get_user(user, u)) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (!can_delete_user(curr_user, curr_group, u)) {
				out->param1 = AUTH_ERR_DENIED;
				break;
			}
			int res = del_user(user);
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		case AUTH_CMD_GET_GROUP: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			auth_id_t group;
			group.raw = in->param2;
			void* buf = shm_map(*(uint64_t*)(in->data));
			if (!buf) { out->param1 = AUTH_ERR_UNKNOWN; break; }
			memset(buf, 0, sizeof(auth_grpex_t));
			int res = get_group(group, (auth_grpex_t*)buf);
			strlcpy(((auth_grpex_t*)buf)->pass, "x", sizeof(((auth_grpex_t*)buf)->pass));
			shm_free(*(uint64_t*)(in->data));
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		case AUTH_CMD_GET_GROUP_BY_NAME: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			void* buf = shm_map(in->param2);
			if (!buf) { out->param1 = AUTH_ERR_UNKNOWN; break; }
			memset(buf, 0, sizeof(auth_idex_t));
			int res = get_group_by_name((const char*)in->data, (auth_grpex_t*)buf);
			strlcpy(((auth_grpex_t*)buf)->pass, "x", sizeof(((auth_grpex_t*)buf)->pass));
			shm_free(in->param2);
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		
		case AUTH_CMD_ADD_GROUP: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
            auth_grpex_t* new_group = (auth_grpex_t*)shm_map(*(uint64_t*)(in->data));
			if (!new_group) { out->param1 = AUTH_ERR_UNKNOWN; break; }
			auth_idex_t* curr_user = (auth_idex_t*)malloc(sizeof(auth_idex_t));
			auth_grpex_t* curr_group = (auth_grpex_t*)malloc(sizeof(auth_grpex_t));
			if (!curr_user || !curr_group) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (get_thread_user(in->sender_tid, curr_user) || get_thread_group(in->sender_tid, curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				break;
			}
			if (!can_create_group(curr_user, curr_group, new_group)) {
				out->param1 = AUTH_ERR_DENIED;
				break;
			}
            int res = add_group(new_group, 0);
			shm_free(*(uint64_t*)(in->data));
            out->param1 = (!res) ? AUTH_ERR_OK : AUTH_ERR_USER;
            break;
        }
		case AUTH_CMD_DEL_GROUP: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			auth_id_t group;
			group.raw = in->param2;
			auth_idex_t* curr_user = (auth_idex_t*)malloc(sizeof(auth_idex_t));
			auth_grpex_t* curr_group = (auth_grpex_t*)malloc(sizeof(auth_grpex_t));
			auth_grpex_t* g = (auth_grpex_t*)malloc(sizeof(auth_grpex_t));
			if (!curr_user || !curr_group || !g) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (get_thread_user(in->sender_tid, curr_user) || get_thread_group(in->sender_tid, curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				break;
			}
			if (get_group(group, g)) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (!can_delete_group(curr_user, curr_group, g)) {
				out->param1 = AUTH_ERR_DENIED;
				break;
			}
			int res = del_group(group);
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		default: {
			out->param1 = AUTH_ERR_NOCOMM;
			break;
		}
	}
	ipc_send(in->sender_tid, out);
	free(out);
}

int driver_main(void* reserved1, void* reserved2) {
	if (init_auth() != 0) return -1;
    register_driver(DT_AUTH, 0);
    
    message_t msg;
    while(1) {
        ipc_recv_ex(0, MSG_TYPE_AUTH, MSG_SUBTYPE_NONE, &msg);
        
        handle_message(&msg);
    }
}