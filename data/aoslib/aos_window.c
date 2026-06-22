#include "../include/aoslib.h"

static apid_t wnd_driver_pid = 0;

#define ensure_wnd_init() { if (wnd_driver_pid == 0) wnd_driver_pid = get_driver_pid(DT_WND); }

window_t* window_create(int w, int h, uint32_t flags) {
    ensure_wnd_init();
    if (wnd_driver_pid == 0) return 0;

    window_t* win = malloc(sizeof(window_t));
    win->w = w;
    win->h = h;

    win->shm_id = shm_alloc(w * h * 4, (void**)&win->buffer);
    if (!win->shm_id) { free(win); return 0; }

    shm_allow(win->shm_id, wnd_driver_pid);

    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    memset(&resp, 0, sizeof(message_t));

    req.type = MSG_TYPE_WND;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = WND_CMD_CREATE;
    req.param2 = (w << 16) | (h & 0xFFFF);
    req.param3 = flags;
    *(uint64_t*)(req.data) = win->shm_id;

    ipc_send(wnd_driver_pid, &req);
    ipc_recv_ex(wnd_driver_pid, MSG_TYPE_WND, MSG_SUBTYPE_RESPONSE, &resp);

    if (resp.param1 == 0) {
        win->win_id = resp.param2;
        return win;
    }

    shm_free(win->shm_id);
    free(win);
    return 0;
}

void window_flush(window_t* win) {
    ensure_wnd_init();
    message_t req;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_WND;
    req.subtype = MSG_SUBTYPE_SEND;
    req.param1 = WND_CMD_FLUSH;
    req.param2 = win->win_id;
    ipc_send(wnd_driver_pid, &req);
}

int get_screen_info(screen_info_t* info) {
	if (!info) return -1;
    ensure_wnd_init();
    if (wnd_driver_pid == 0) return -1;

    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_TYPE_WND;
    req.subtype = MSG_SUBTYPE_QUERY;
    req.param1 = WND_CMD_GET_SCREEN_INFO;

    ipc_send(wnd_driver_pid, &req);
    ipc_recv_ex(wnd_driver_pid, MSG_TYPE_WND, MSG_SUBTYPE_RESPONSE, &resp);

    if (resp.param1 == 0) { // OK
        info->width = (uint16_t)(resp.param2 >> 16);
        info->height = (uint16_t)(resp.param2 & 0xFFFF);
        return 0;
    }
    return -1;
}