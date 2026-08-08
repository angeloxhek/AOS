#include "../include/aoslib.h"

static uint64_t auth_driver_pid = 0;

#define ensure_auth_init() { if (auth_driver_pid == 0) auth_driver_pid = get_driver_pid(DT_AUTH); }

void auth_init() {
    ensure_auth_init();
}

static int auth_rpc_call(message_t* req, message_t* resp_out) {
    ensure_auth_init();
    req->type = MSG_TYPE_AUTH;
    
    ipc_send(auth_driver_pid, req);
    
    ipc_recv_ex(auth_driver_pid, MSG_TYPE_AUTH, MSG_SUBTYPE_NONE, resp_out);
    
    return (int)resp_out->param1;
}

int auth_get_user(auth_id_t in, auth_idex_t* out) {
	ensure_auth_init();
    if (!out) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_idex_t), &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_USER;
	req.param2 = in.raw;
	
	*(uint64_t*)(req.data) = shm_id;

	int res = auth_rpc_call(&req, &resp);
    if (res == 0) {
		memcpy(out, shm_vaddr, sizeof(auth_idex_t));
    }
    shm_free(shm_id);
    return res;
}

int auth_get_user_by_name(const char* in, auth_idex_t* out) {
	ensure_auth_init();
    if (!out || !in) return -1;	
    message_t req, resp;
	
    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_idex_t), &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, auth_driver_pid);

    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_USER_BY_NAME;
    req.param2 = shm_id;
	
    strlcpy((char*)req.data, in, 64);

    int res = auth_rpc_call(&req, &resp);
    if (res == AUTH_ERR_OK) {
        memcpy(out, shm_vaddr, sizeof(auth_idex_t));
    }
    
    shm_free(shm_id);
    return res;
}

int auth_add_user(auth_idex_t* inout) {
	ensure_auth_init();
    if (!inout) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_idex_t), &shm_vaddr);
    if (!shm_id) return -1;
	
	shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_ADD_USER;
	memcpy(shm_vaddr, inout, sizeof(auth_idex_t));
	*(uint64_t*)(req.data) = shm_id;

	int res = auth_rpc_call(&req, &resp);
    if (res == 0) {
		memcpy(inout, shm_vaddr, sizeof(auth_idex_t));
    }
    shm_free(shm_id);
    return res;
}

int auth_del_user(auth_id_t in) {
	ensure_auth_init();
    message_t req;
    message_t resp;

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_DEL_USER;
	req.param2 = in.raw;
    
    return auth_rpc_call(&req, &resp);
}

int auth_get_group(auth_id_t in, auth_grpex_t* out) {
	ensure_auth_init();
	if (!out) return -1;
	message_t req;
	message_t resp;

	void* shm_vaddr = 0;
	uint64_t shm_id = shm_alloc(sizeof(auth_grpex_t), &shm_vaddr);
	if (!shm_id) return -1;

	shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
	req.param1 = AUTH_CMD_GET_GROUP;
	req.param2 = in.raw;

	*(uint64_t*)(req.data) = shm_id;

	int res = auth_rpc_call(&req, &resp);
    if (res == 0) {
		memcpy(out, shm_vaddr, sizeof(auth_grpex_t));
    }
    shm_free(shm_id);
    return res;
}

int auth_get_group_by_name(const char* in, auth_grpex_t* out) {
	ensure_auth_init();
    if (!out || !in) return -1;	
    message_t req, resp;
	
    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_grpex_t), &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, auth_driver_pid);

    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_GROUP_BY_NAME;
    req.param2 = shm_id;
	
    strlcpy((char*)req.data, in, 64);

    int res = auth_rpc_call(&req, &resp);
    if (res == AUTH_ERR_OK) {
        memcpy(out, shm_vaddr, sizeof(auth_grpex_t));
    }
    
    shm_free(shm_id);
    return res;
}

int auth_add_group(auth_grpex_t* inout) {
	ensure_auth_init();
    if (!inout) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_grpex_t), &shm_vaddr);
    if (!shm_id) return -1;
	
	shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_ADD_GROUP;
	memcpy(shm_vaddr, inout, sizeof(auth_grpex_t));
	*(uint64_t*)(req.data) = shm_id;

	int res = auth_rpc_call(&req, &resp);
    if (res == 0) {
		memcpy(inout, shm_vaddr, sizeof(auth_grpex_t));
    }
    shm_free(shm_id);
    return res;
}

int auth_del_group(auth_id_t in) {
	ensure_auth_init();
    message_t req;
    message_t resp;

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_DEL_GROUP;
	req.param2 = in.raw;
    
    return auth_rpc_call(&req, &resp);
}

int auth_get_members(auth_id_t in, uint32_t index, auth_members_t* out) {
	ensure_auth_init();
    if (!out) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_members_t), &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_MEMBERS;
	req.param2 = in.raw;
	req.param3 = index;
	
	*(uint64_t*)(req.data) = shm_id;

    int res = auth_rpc_call(&req, &resp);
    if (res == 0) {
		memcpy(out, shm_vaddr, sizeof(auth_members_t));
    }
    shm_free(shm_id);
    return res;
}

int authbase_load(uint8_t* buf, uint64_t len) {
	ensure_auth_init();
    if (!buf || !len) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(len, &shm_vaddr);
    if (!shm_id) return -1;
	
	shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_LOAD;
	memcpy(shm_vaddr, buf, len);
	*(uint64_t*)(req.data) = shm_id;

    int res = auth_rpc_call(&req, &resp);
    shm_free(shm_id);
    return res;
}

int authbase_save(uint8_t* buf, uint64_t len) {
	ensure_auth_init();
    if (!buf || !len) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(len, &shm_vaddr);
    if (!shm_id) return -1;
	
	shm_allow(shm_id, auth_driver_pid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_SAVE;
	*(uint64_t*)(req.data) = shm_id;

    int res = auth_rpc_call(&req, &resp);
    if (res == 0) {
		memcpy(buf, shm_vaddr, len);
    }
    shm_free(shm_id);
    return res;
}