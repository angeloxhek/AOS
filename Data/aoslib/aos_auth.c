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

	req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = AUTH_CMD_GET_USER;
	req.param2 = in;

    if (auth_rpc_call(&req, &resp) == 0) {
		kernel_memcpy(outh, &resp.data, sizeof(auth_idex_t));
        return (int)resp.param2;
    }
    
    return -1;
}