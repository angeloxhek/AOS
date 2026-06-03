#include "../include/aoslib.h"

static apid_t input_driver_pid = 0;

#define ensure_input_init() { if (input_driver_pid == 0) input_driver_pid = get_driver_pid(DT_INPUT); }

void input_init() {
    ensure_input_init();
}

static int input_rpc_call(message_t* req, message_t* resp_out) {
    ensure_input_init();

    req->type = MSG_TYPE_INPUT;

    ipc_send(input_driver_pid, req);

    ipc_recv_ex(
        input_driver_pid,
        MSG_TYPE_INPUT,
        MSG_SUBTYPE_NONE,
        resp_out
    );

    if (resp_out->param1 == VIDEO_ERR_OK) {
        return 0;
    }
    
    return -1;
}

int input_subscribe() {
    ensure_input_init();

    message_t req;
    message_t resp;
    
    memset(&req, 0, sizeof(message_t));
    memset(&resp, 0, sizeof(message_t));

    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = INPUT_CMD_SUBSCRIBE;

    return input_rpc_call(&req, &resp);
}