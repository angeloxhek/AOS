#include <stdint.h>
#include "../include/aoslib.h"

typedef struct auth_idex_node_t {
	auth_idex_t data;
	struct auth_idex_node_t* next;
} auth_idex_node_t;

auth_idex_node_t* idlist;

typedef struct free_range_t {
    uint32_t start;
    uint32_t end;
    struct free_range_t *next;
} free_range_t;

free_range_t *freelist;

int get_uid(uint32_t* out) {
    if (!freelist) return -1;
    uint32_t uid = freelist->start;
    if (freelist->start == freelist->end) {
        free_range_t *tmp = freelist;
        freelist = freelist->next;
        free(tmp);
    } else {
        freelist->start++;
    }
	*out = uid;
    return 0;
}

void del_uid(uint32_t uid) {
    free_range_t* prev = NULL;
    free_range_t* curr = freelist;
    while (curr != NULL && curr->start < uid) {
        prev = curr;
        curr = curr->next;
    }
    
    int merged_prev = 0;
    if (prev != NULL && prev->end + 1 == uid) {
        prev->end++;
        merged_prev = 1;
    }

    int merged_next = 0;
    if (curr != NULL && uid + 1 == curr->start) {
        curr->start--;
        merged_next = 1;
    }

    if (merged_prev && merged_next) {
        prev->end = curr->end;
        prev->next = curr->next;
        free(curr);
    } 
    else if (!merged_prev && !merged_next) {
        free_range_t* new_node = (free_range_t*)malloc(sizeof(free_range_t));
		if (!new_node) return;
        new_node->start = uid;
        new_node->end = uid;
        new_node->next = curr;
        if (prev) prev->next = new_node;
        else freelist = new_node;
    }
}

int test_uid(uint32_t uid) {
    free_range_t* curr = freelist;
    while (curr != NULL) {
        if (uid >= curr->start && uid <= curr->end) {
            return 1;
        }
        if (curr->start > uid) {
            break;
        }
        curr = curr->next;
    }
    return 0;
}

int init_auth() {
	freelist = (free_range_t*)malloc(sizeof(free_range_t));
	if (!freelist) return -1;
	memset(freelist, 0, sizeof(free_range_t));
	freelist->end = UINT32_MAX;
	
	idlist = (auth_idex_node_t*)malloc(sizeof(auth_idex_node_t));
	if (!idlist) {
		free(freelist);
		return -1;
	}
	memset(idlist, 0, sizeof(auth_idex_node_t));
	uint32_t uid = 0;
	if (get_uid(&uid)) {
		free(freelist);
		free(idlist);
		return -1;
	}
	idlist->data.id.user.uid = uid;
	idlist->data.pgroup = PGROUP_SUPER;
	idlist->data.auth_type = ATYPE_SUPER;
	idlist->data.perms = (uint32_t)-1;
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
	}
	user->next = idlist;
	idlist = user;
	return 0;
}

int del_user(auth_id_t user) {
    auth_idex_node_t* curr = idlist;
    auth_idex_node_t* prev = NULL;
    while (curr != NULL) {
        if (curr->data.id.raw == user.raw) {
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
	auth_idex_node_t* curr = idlist;
	while (curr) {
		if (curr->data.id.raw == user.raw) {
			memcpy(out, &curr->data, sizeof(auth_idex_t));
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

void handle_message(message_t* in) {
	message_t* out = (message_t*)malloc(sizeof(message_t));
	if(!out) return;
	memset(out, 0, sizeof(message_t));
	out->type = MSG_TYPE_AUTH;
	out->subtype = MSG_SUBTYPE_RESPONSE;
	switch (in->param1) {
		case AUTH_CMD_GET_USER: {
			AOS_HANDLE_SUBTYPE_CHECK(MSG_SUBTYPE_QUERY);
			int res = get_user((auth_id_t)in->param2, (auth_idex_t*)&out->data);
			out->param1 = res ? AUTH_ERR_NOTFOUND : AUTH_ERR_OK;
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