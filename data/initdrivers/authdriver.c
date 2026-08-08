#include <stdint.h>
#include <aoslib.h>

#define AUTHBASE_VERSION 1
#define AUTHBASE_KEY     "A5U3T9H3B2A0S3E6"

AOS_DECLARE_DRIVER(DT_AUTH, 0, 0);

typedef struct {
	uint8_t magic[4]; // AHBS
	uint32_t version;
	uint8_t key[16]; // AUTHBASE_KEY
	uint64_t reserved; // flags
	uint64_t users_offset;
	uint64_t groups_offset;
} authbase_header_t;

typedef struct authbase_idex_node_t {
	auth_idex_t data;
	uint64_t next;
} authbase_idex_node_t;

typedef struct authbase_members_node_t {
	auth_members_t data;
	uint64_t next;
} authbase_members_node_t;

typedef struct authbase_grpex_node_t {
	auth_grpex_t grp;
	uint64_t members;
	uint64_t next;
} authbase_grpex_node_t;

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
	idlist->data.perms = APERM_SUPER;
	idlist->data.flags = AFLAG_LOCAL;
	strlcpy(idlist->data.name, "kernel", sizeof(idlist->data.name));
	strlcpy(idlist->data.pass, "x", sizeof(idlist->data.pass));

	grplist = (auth_grpex_node_t*)malloc(sizeof(auth_grpex_node_t));
	if (!grplist) {
		free(ufreelist);
		free(gfreelist);
		return -1;
	}
	memset(grplist, 0, sizeof(auth_grpex_node_t));
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
	grplist->grp.flags = AFLAG_LOCAL;
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

int get_process_user(uint32_t pid, auth_idex_t* out) {
	if (!out) return -1;
	proc_info_user_t* info = (proc_info_user_t*)malloc(sizeof(proc_info_user_t));
	if (get_proc_info(pid, info) != SYS_RES_OK) {
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

int get_process_group(uint32_t pid, auth_grpex_t* out) {
	if (!out) return -1;
	proc_info_user_t* info = (proc_info_user_t*)malloc(sizeof(proc_info_user_t));
	if (get_proc_info(pid, info) != SYS_RES_OK) {
		return -1;
	}
	return get_group(info->user, out);
}

auth_grpex_node_t* find_group_node(auth_id_t group) {
    auth_grpex_node_t* curr = grplist;
    while (curr) {
        if (curr->grp.id.user.gid == group.user.gid) return curr;
        curr = curr->next;
    }
    return NULL;
}

int get_group_members(auth_id_t group, uint32_t chunk_index, auth_members_t* out) {
    if (!out) return -1;

    auth_grpex_node_t* grp = find_group_node(group);

    auth_members_node_t* curr_chunk = grp->members;
    for (uint32_t i = 0; i < chunk_index; i++) {
        if (!curr_chunk) return -1;
        curr_chunk = curr_chunk->next;
    }

    if (!curr_chunk) return -1;

    memcpy(out, &curr_chunk->data, sizeof(auth_members_t));
    return 0;
}

int add_member(auth_id_t group, auth_id_t user) {
    auth_grpex_node_t* grp = find_group_node(group);
    if (!grp) return -1;

    auth_members_node_t* curr = grp->members;
    while (curr) {
        for (int i = 0; i < 32; i++) {
            if (!(curr->data.freemask & (1U << i))) {
                if (curr->data.data[i].user.uid == user.user.uid) {
                    return -1;
                }
            }
        }
        curr = curr->next;
    }

    curr = grp->members;
    auth_members_node_t* last = NULL;

    while (curr) {
        if (curr->data.freemask != 0) {
            int idx = __builtin_ctz(curr->data.freemask);

            curr->data.data[idx].user.uid = user.user.uid;
            curr->data.freemask &= ~(1U << idx);
            curr->data.count++;
            return 0;
        }
        last = curr;
        curr = curr->next;
    }

    auth_members_node_t* new_node = (auth_members_node_t*)malloc(sizeof(auth_members_node_t));
    if (!new_node) return -1;

    memset(new_node, 0, sizeof(auth_members_node_t));

    new_node->data.freemask = 0xFFFFFFFF;

    new_node->data.data[0].user.uid = user.user.uid;
    new_node->data.freemask &= ~(1U << 0);
    new_node->data.count = 1;
    new_node->next = NULL;

    if (last) {
        last->next = new_node;
    } else {
        grp->members = new_node;
    }

    return 0;
}

int del_member(auth_id_t group, auth_id_t user) {
    auth_grpex_node_t* grp = find_group_node(group);
    if (!grp) return -1;

    auth_members_node_t* curr = grp->members;
    auth_members_node_t* prev = NULL;

    while (curr) {
        for (int i = 0; i < 32; i++) {
            if (!(curr->data.freemask & (1U << i))) {
                if (curr->data.data[i].user.uid == user.user.uid) {
                    curr->data.freemask |= (1U << i);
                    curr->data.count--;
                    curr->data.data[i].raw = 0;
                    if (curr->data.count == 0) {
                        if (grp->members != curr) {
                            if (prev) prev->next = curr->next;
                            else grp->members = curr->next;
                            free(curr);
                        }
                    }
                    return 0;
                }
            }
        }
        prev = curr;
        curr = curr->next;
    }

    return -1;
}

int authbase_save(uint8_t* buf, uint64_t len) {
    if (!buf || len < sizeof(authbase_header_t)) return -1;
    memset(buf, 0, len);
    
    authbase_header_t* header = (authbase_header_t*)buf;
    memcpy(header->magic, "AHBS", 4);
    header->version = AUTHBASE_VERSION;
    memcpy(header->key, AUTHBASE_KEY, 16);
    
    header->users_offset = 0;
    header->groups_offset = 0;
    
    uint64_t offset = sizeof(authbase_header_t);
    
    uint64_t prev_user_offset = 0;
    auth_idex_node_t* curid = idlist;
    
    while (curid != NULL) {
        if (curid->data.flags & AFLAG_LOCAL) { 
            curid = curid->next; 
            continue; 
        }
        
        if (header->users_offset == 0) header->users_offset = offset;
        
        if (offset + sizeof(authbase_idex_node_t) > len) return -1;
        authbase_idex_node_t* idex = (authbase_idex_node_t*)(buf + offset);
        
        if (prev_user_offset != 0) {
            authbase_idex_node_t* prev_idex = (authbase_idex_node_t*)(buf + prev_user_offset);
            prev_idex->next = offset;
        }
        
        memcpy(&idex->data, &curid->data, sizeof(auth_idex_t));
        
        prev_user_offset = offset;
        offset += sizeof(authbase_idex_node_t);
        curid = curid->next;
    }
    
    uint64_t prev_group_offset = 0;
    auth_grpex_node_t* curgrp = grplist;
    
    while (curgrp != NULL) {
        if (curgrp->grp.flags & AFLAG_LOCAL) { 
            curgrp = curgrp->next; 
            continue; 
        }
        
        uint64_t members_head_offset = 0;
        uint64_t prev_mem_offset = 0;
        auth_members_node_t* curmembers = curgrp->members;
        
        while (curmembers != NULL) {
            if (offset + sizeof(authbase_members_node_t) > len) return -1;
            authbase_members_node_t* memex = (authbase_members_node_t*)(buf + offset);
            
            if (members_head_offset == 0) members_head_offset = offset;
            
            if (prev_mem_offset != 0) {
                authbase_members_node_t* p_mem = (authbase_members_node_t*)(buf + prev_mem_offset);
                p_mem->next = offset;
            }
            
            memcpy(&memex->data, &curmembers->data, sizeof(auth_members_t));
            
            prev_mem_offset = offset;
            offset += sizeof(authbase_members_node_t);
            curmembers = curmembers->next;
        }
        
        if (offset + sizeof(authbase_grpex_node_t) > len) return -1;
        authbase_grpex_node_t* grpex = (authbase_grpex_node_t*)(buf + offset);
        
        if (header->groups_offset == 0) header->groups_offset = offset;
        
        if (prev_group_offset != 0) {
            authbase_grpex_node_t* p_grp = (authbase_grpex_node_t*)(buf + prev_group_offset);
            p_grp->next = offset;
        }
        
        grpex->members = members_head_offset;
        memcpy(&grpex->grp, &curgrp->grp, sizeof(auth_grpex_t));
        
        prev_group_offset = offset;
        offset += sizeof(authbase_grpex_node_t);
        curgrp = curgrp->next;
    }
    
    return 0;
}

int authbase_load(uint8_t* buf, uint64_t len) {
    if (!buf || len < sizeof(authbase_header_t)) return -1;
    
    authbase_header_t* header = (authbase_header_t*)buf;
    
    if (memcmp(header->magic, "AHBS", 4) != 0) return -1;
    if (header->version != AUTHBASE_VERSION) return -1;
    if (memcmp(header->key, AUTHBASE_KEY, 16) != 0) return -1;

	int has_root = -1;

    uint64_t offset = header->users_offset;
    while (offset != 0 && offset < len) {
        authbase_idex_node_t* idex = (authbase_idex_node_t*)(buf + offset);
        
        if (!(idex->data.flags & AFLAG_LOCAL)) {
            add_user(&idex->data, 1);
            del_uid(idex->data.id.user.uid);
			
			if (idex->data.pgroup == PGROUP_ROOT) has_root = 0;
        }
        
        offset = idex->next;
    }

    offset = header->groups_offset;
    while (offset != 0 && offset < len) {
        authbase_grpex_node_t* grpex = (authbase_grpex_node_t*)(buf + offset);
        
		if (grpex->grp.flags & AFLAG_LOCAL) { offset = grpex->next; continue; }
		
		add_group(&grpex->grp, 1);
		del_gid(grpex->grp.id.user.gid);
		
		auth_grpex_node_t* ram_group = grplist; 
		
		uint64_t mem_offset = grpex->members;
		auth_members_node_t* last_ram_mem = NULL;
		
		while (mem_offset != 0 && mem_offset < len) {
			authbase_members_node_t* memex = (authbase_members_node_t*)(buf + mem_offset);
			
			auth_members_node_t* ram_mem = (auth_members_node_t*)malloc(sizeof(auth_members_node_t));
			if (!ram_mem) return -1; 
			
			memcpy(&ram_mem->data, &memex->data, sizeof(auth_members_t));
			ram_mem->next = NULL;
			
			if (last_ram_mem) {
				last_ram_mem->next = ram_mem;
			} else {
				ram_group->members = ram_mem;
			}
			last_ram_mem = ram_mem;
			
			mem_offset = memex->next;
		}
        
        offset = grpex->next;
    }
    
    return has_root;
}

int can_create_user(auth_idex_t* parent, auth_grpex_t* group, auth_idex_t* child) {
	uint64_t pperms = AUTH_GET_FULL_PERMS(parent, group);
	if (parent->pgroup > child->pgroup) return 0;
	if ((pperms | child->perms) != pperms) return 0;
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
	if ((pperms | (child->allow_perms & ~child->deny_perms)) != pperms) return 0;
	if (!(pperms & APERM_MANAGE_GROUP)) return 0;
	if ((parent->auth_type | child->auth_type) != parent->auth_type) return 0;
	if ((child->auth_type & ATYPE_CHANGE) && !(parent->auth_type & ATYPE_CHANGE)) return 0;
	return 1;
}

int can_delete_group(auth_idex_t* parent, auth_grpex_t* group, auth_grpex_t* child) {
	uint64_t pperms = AUTH_GET_FULL_PERMS(parent, group);
	if ((pperms | (child->allow_perms & ~child->deny_perms)) != pperms) return 0;
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

			auth_idex_t curr_user;
			auth_grpex_t curr_group;

			if (get_process_user(in->sender_pid, &curr_user) || get_process_group(in->sender_pid, &curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				shm_free(*(uint64_t*)(in->data));
				break;
			}
			if (!can_create_user(&curr_user, &curr_group, new_user)) {
				out->param1 = AUTH_ERR_DENIED;
				shm_free(*(uint64_t*)(in->data));
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
			auth_idex_t curr_user;
			auth_grpex_t curr_group;
			auth_idex_t u;
			if (get_process_user(in->sender_pid, &curr_user) || get_process_group(in->sender_pid, &curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				break;
			}
			if (get_user(user, &u)) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (!can_delete_user(&curr_user, &curr_group, &u)) {
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
			auth_idex_t curr_user;
			auth_grpex_t curr_group;
			if (get_process_user(in->sender_pid, &curr_user) || get_process_group(in->sender_pid, &curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				shm_free(*(uint64_t*)(in->data));
				break;
			}
			if (!can_create_group(&curr_user, &curr_group, new_group)) {
				out->param1 = AUTH_ERR_DENIED;
				shm_free(*(uint64_t*)(in->data));
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
			auth_idex_t curr_user;
			auth_grpex_t curr_group;
			auth_grpex_t g;
			if (get_process_user(in->sender_pid, &curr_user) || get_process_group(in->sender_pid, &curr_group)) {
				out->param1 = AUTH_ERR_NOTFOUND;
				break;
			}
			if (get_group(group, &g)) {
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			if (!can_delete_group(&curr_user, &curr_group, &g)) {
				out->param1 = AUTH_ERR_DENIED;
				break;
			}
			int res = del_group(group);
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		case AUTH_CMD_GET_MEMBERS: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);

			auth_id_t group;
			group.raw = in->param2;
			uint32_t chunk_idx = (uint32_t)in->param3;

			auth_members_t* buf = (auth_members_t*)shm_map(*(uint64_t*)(in->data));
			if (!buf) { out->param1 = AUTH_ERR_UNKNOWN; break; }

			memset(buf, 0, sizeof(auth_members_t));
			int res = get_group_members(group, chunk_idx, buf);

			shm_free(*(uint64_t*)(in->data));
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
			break;
		}
		case AUTH_CMD_SAVE: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			uint8_t* buf = (uint8_t*)shm_map(*(uint64_t*)(in->data));
			uint64_t len = shm_get_size(*(uint64_t*)(in->data));
			if (!buf || !len) {
				shm_free(*(uint64_t*)(in->data));
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
			int res = authbase_save(buf, len);
			shm_free(*(uint64_t*)(in->data));
			out->param1 = res ? AUTH_ERR_UNKNOWN : AUTH_ERR_OK;
			break;
		}
		case AUTH_CMD_LOAD: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			
			if (in->sender_pid != get_driver_pid(DT_INIT)) {
				out->param1 = AUTH_ERR_DENIED;
				break;
			}
			
			uint8_t* buf = (uint8_t*)shm_map(*(uint64_t*)(in->data));
			uint64_t len = shm_get_size(*(uint64_t*)(in->data));
            
			if (!buf || !len) {
				if (*(uint64_t*)(in->data)) shm_free(*(uint64_t*)(in->data));
				out->param1 = AUTH_ERR_UNKNOWN;
				break;
			}
            
			int res = authbase_load(buf, len);
            
			shm_free(*(uint64_t*)(in->data));
			out->param1 = res ? AUTH_ERR_UNKNOWN : AUTH_ERR_OK;
			break;
		}
		default: {
			out->param1 = AUTH_ERR_NOCOMM;
			break;
		}
	}
	ipc_send(in->sender_pid, out);
	free(out);
}

int driver_main(void* reserved1, void* reserved2) {
	if (init_auth() != 0) return -1;

	auth_idex_t* temp_root = (auth_idex_t*)malloc(sizeof(auth_idex_t));
	memset(temp_root, 0, sizeof(auth_idex_t));
	temp_root->pgroup = PGROUP_ROOT;
	temp_root->auth_type = ATYPE_ROOT;
	temp_root->perms = APERM_ROOT;
	temp_root->flags = AFLAG_LOCAL;
	strlcpy(temp_root->name, "localroot", sizeof(temp_root->name));
	strlcpy(temp_root->pass, "aoslocal", sizeof(temp_root->pass));
	
	int auth_res = add_user(temp_root, 0); 
	
	if (auth_res != 0) {
        printf("AUTHDRIVER: Failed to create localroot! Error code: %d\n", auth_res);
    } else {
        printf("AUTHDRIVER: localroot created successfully. Raw auth_id_t=%llx (uid=%x; gid=%x)\n", temp_root->id.raw, temp_root->id.user.uid, temp_root->id.user.gid);
    }
	
    message_t msg;
    while(1) {
        ipc_recv_ex(0, MSG_TYPE_AUTH, MSG_SUBTYPE_NONE, &msg);

        handle_message(&msg);
    }
}