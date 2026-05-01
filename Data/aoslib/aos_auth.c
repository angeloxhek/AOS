#include "../include/aoslib.h"

static uint64_t auth_driver_tid = 0;

#define ensure_auth_init() { if (auth_driver_tid == 0) auth_driver_tid = get_driver_tid(DT_AUTH); }

void auth_init() {
    ensure_auth_init();
}

static int auth_rpc_call(message_t* req, message_t* resp_out) {
    ensure_auth_init();

    req->type = MSG_TYPE_AUTH;

    ipc_send(auth_driver_tid, req);

    ipc_recv_ex(
        auth_driver_tid,
        MSG_TYPE_AUTH,
        MSG_SUBTYPE_NONE,
        resp_out
    );

    if (resp_out->param1 == AUTH_ERR_OK) {
        return 0;
    }
    
    return -1;
}

int auth_get_user(auth_id_t in, auth_idex_t* out) {
    if (!out) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_idex_t), &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, auth_driver_tid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_USER;
	req.param2 = in.raw;
	
	*(uint64_t*)(req.data) = shm_id;

    if (auth_rpc_call(&req, &resp) == 0) {
		memcpy(out, shm_vaddr, sizeof(auth_idex_t));
		shm_free(shm_id);
        return (int)resp.param1;
    }
    shm_free(shm_id);
    return -1;
}

int auth_get_user_by_name(const char* in, auth_idex_t* out) {
    if (!out || !in) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(64, &shm_vaddr);
    if (!shm_id) return -1;

    shm_allow(shm_id, auth_driver_tid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_USER;
	req.param2 = shm_id;
	
	strlcpy((char*)req.data, in, 64);

    if (auth_rpc_call(&req, &resp) == 0) {
		memcpy(out, shm_vaddr, sizeof(auth_idex_t));
		shm_free(shm_id);
        return (int)resp.param1;
    }
    shm_free(shm_id);
    return -1;
}

int auth_add_user(auth_idex_t* inout) {
    if (!inout) return -1;	
    message_t req;
    message_t resp;
	
	void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(sizeof(auth_idex_t), &shm_vaddr);
    if (!shm_id) return -1;
	
	shm_allow(shm_id, auth_driver_tid);

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_ADD_USER;
	memcpy(shm_vaddr, inout, sizeof(auth_idex_t));
	*(uint64_t*)(req.data) = shm_id;

    if (auth_rpc_call(&req, &resp) == 0) {
		memcpy(inout, shm_vaddr, sizeof(auth_idex_t));
		shm_free(shm_id);
        return (int)resp.param1;
    }
    shm_free(shm_id);
    return -1;
}

int auth_del_user(auth_id_t in) {
    message_t req;
    message_t resp;

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_DEL_USER;
	req.param2 = in.raw;

    if (auth_rpc_call(&req, &resp) == 0) {
        return (int)resp.param1;
    }
    
    return -1;
}