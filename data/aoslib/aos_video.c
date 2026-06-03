#include "../include/aoslib.h"

static apid_t video_driver_pid = 0;

#define ensure_video_init() { if (video_driver_pid == 0) video_driver_pid = get_driver_pid(DT_VIDEO); }

void video_init() {
    ensure_video_init();
}

static int video_rpc_call(message_t* req, message_t* resp_out) {
    ensure_video_init();

    req->type = MSG_TYPE_VIDEO;

    ipc_send(video_driver_pid, req);

    ipc_recv_ex(
        video_driver_pid,
        MSG_TYPE_VIDEO,
        MSG_SUBTYPE_NONE,
        resp_out
    );

    if (resp_out->param1 == VIDEO_ERR_OK) {
        return 0;
    }
    
    return -1;
}

int video_set_backbuffer(uint64_t shm_id) {
    if (!shm_id) return -1;
    
    ensure_video_init();

    shm_allow(shm_id, video_driver_pid);

    message_t req;
    message_t resp;
    
    memset(&req, 0, sizeof(message_t));
    memset(&resp, 0, sizeof(message_t));

    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VIDEO_CMD_SET_BACKBUFFER;
    
    *(uint64_t*)(req.data) = shm_id;

    return video_rpc_call(&req, &resp);
}

int video_flush_rects(video_rect_t* rects, uint32_t count) {
    if (!rects || count == 0) return 0;

    ensure_video_init();

    void* shm_vaddr = 0;
    uint64_t size = count * sizeof(video_rect_t);
    
    uint64_t shm_id = shm_alloc(size, &shm_vaddr);
    if (!shm_id) return -1;

    memcpy(shm_vaddr, rects, size);
    shm_allow(shm_id, video_driver_pid);

    message_t req;
    message_t resp;
    
    memset(&req, 0, sizeof(message_t));
    memset(&resp, 0, sizeof(message_t));

    req.subtype = MSG_SUBTYPE_PING;
    req.param1 = VIDEO_CMD_FLUSH_RECTS;
    req.param2 = count;
    
    *(uint64_t*)(req.data) = shm_id;

    int result = video_rpc_call(&req, &resp);

    shm_free(shm_id);

    return result;
}

int video_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    ensure_video_init();

    message_t req;
    message_t resp;
    
    memset(&req, 0, sizeof(message_t));
    memset(&resp, 0, sizeof(message_t));

    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = VIDEO_CMD_FILL_RECT;
    
    req.param2 = (x << 16) | (y & 0xFFFF);
    req.param3 = (w << 16) | (h & 0xFFFF);
    
    *(uint32_t*)(req.data) = color;

    return video_rpc_call(&req, &resp);
}