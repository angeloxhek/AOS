#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <aos/types.h>
#include <aos/driver.h>
#include <aos/ipc.h>
#include <aos/input.h>
#include <aos/videodriver.h>
#include <aos/window.h>
#include <aos/syscalls.h>

#include <agfx.h>
#include <agfx_ui.h>

AOS_DECLARE_DRIVER(DT_WND, DRV_PERM_GET_SPEC_INFO, 0);

sys_video_t vinfo;
uint32_t* backbuffer = 0;
static agfx_surface_t screen_surface;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct syswindow {
    int win_id;              // Уникальный ID окна
    apid_t owner_pid;        // PID программы
    int x, y;                // Позиция
    int w, h;                // Размеры
    uint64_t shm_id;         // ID разделяемой памяти
    uint32_t* pixels;        // Указатель на пиксели клиента
    uint32_t flags;          // 1 = WND_FLAG_BACKGROUND
    agfx_surface_t surface;  // Поверхность окна в формате AGFX
    
    struct syswindow* next;  // Окно ВЫШЕ
    struct syswindow* prev;  // Окно НИЖЕ
} syswindow_t;

syswindow_t* bottom_window = 0;
syswindow_t* top_window = 0;    
int global_win_id = 1;

syswindow_t* dragged_window = 0;
int drag_offset_x = 0;
int drag_offset_y = 0;

syswindow_t* create_window(apid_t owner_pid, int x, int y, int w, int h, uint64_t shm_id, uint32_t flags) {
    syswindow_t* win = malloc(sizeof(syswindow_t));
    if (!win) return 0;
    
    win->win_id = global_win_id++;
    win->owner_pid = owner_pid;
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->flags = flags;
    win->shm_id = shm_id;
    win->pixels = (uint32_t*)shm_map(shm_id);

    agfx_init(&win->surface, win->pixels, w, h, w * 4);
    
    win->next = 0;
    win->prev = top_window;
    
    if (top_window) top_window->next = win;
    else bottom_window = win;
    top_window = win;
    return win;
}

void bring_to_front(syswindow_t* win) {
    if (win == top_window) return;
    if (win->prev) win->prev->next = win->next;
    else bottom_window = win->next;
    if (win->next) win->next->prev = win->prev;

    win->prev = top_window;
    win->next = 0;
    top_window->next = win;
    top_window = win;
}

void draw_all_windows(int clip_x, int clip_y, int clip_w, int clip_h) {
    agfx_set_clip(&screen_surface, clip_x, clip_y, clip_w, clip_h);

    // Заливаем базовый фон черным цветом
    agfx_fill_rect(&screen_surface, clip_x, clip_y, clip_w, clip_h, 0xFF000000);

    syswindow_t* current = bottom_window;
    while (current) {
        if (clip_x < current->x + current->w && clip_x + clip_w > current->x &&
            clip_y < current->y + current->h && clip_y + clip_h > current->y) {
            
            if (!(current->flags & WND_FLAG_BACKGROUND)) {
                agfx_ui_draw_shadow(&screen_surface, current->x, current->y, current->w, current->h, 10);
            }

            agfx_blit(&screen_surface, current->x, current->y, current->w, current->h, &current->surface);
        }
        current = current->next;
    }
}

static inline void draw_mouse_cursor(int x, int y) {
    agfx_set_clip(&screen_surface, 0, 0, vinfo.width, vinfo.height);

    agfx_fill_triangle(&screen_surface, x, y, x, y + 17, x + 12, y + 12, 0xFFFFFFFF);
    agfx_draw_triangle(&screen_surface, x, y, x, y + 17, x + 12, y + 12, 1, 0xFF000000);
}

void update_mouse_cursor(int old_x, int old_y, int new_x, int new_y, int w, int h) {
    int min_x = MIN(old_x, new_x);
    int min_y = MIN(old_y, new_y);
    int max_x = MAX(old_x, new_x) + w;
    int max_y = MAX(old_y, new_y) + h;

    int dw = max_x - min_x;
    int dh = max_y - min_y;

    draw_all_windows(min_x, min_y, dw, dh);

    draw_mouse_cursor(new_x, new_y);

    video_rect_t damage = { (uint32_t)min_x, (uint32_t)min_y, (uint32_t)dw, (uint32_t)dh };
    video_flush_rects(&damage, 1);
}

int driver_main(void* reserved1, void* reserved2) {
    (void)reserved1;
    (void)reserved2;
    printf("WindowManager: Starting with AGFX...\n");

    agfx_set_allocators(malloc, free);

    video_init();

    if (sysget_spec_info(SPEC_INFO_VIDEO, &vinfo) != SYS_RES_OK) {
        printf("WindowManager: Failed to get video info!\n");
        return -1;
    }

    uint64_t size_bytes = (uint64_t)vinfo.height * vinfo.pitch;
    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(size_bytes, &shm_vaddr);
    
    if (!shm_id) return -1;
    backbuffer = (uint32_t*)shm_vaddr;

    agfx_init(&screen_surface, backbuffer, vinfo.width, vinfo.height, vinfo.pitch);

    if (video_set_backbuffer(shm_id) != 0) {
        printf("WindowManager: Failed to set backbuffer in driver!\n");
        return -1;
    }
    
    video_rect_t full_screen = {0, 0, vinfo.width, vinfo.height};
    video_flush_rects(&full_screen, 1);
    
    apid_t input_pid = get_driver_pid(DT_INPUT);
    if (input_pid == 0) {
        printf("WindowManager: ERROR - InputDriver not found!\n");
        return -1;
    }

    message_t sub_msg;
    memset(&sub_msg, 0, sizeof(message_t));
    sub_msg.type = MSG_TYPE_INPUT;
    sub_msg.subtype = MSG_SUBTYPE_QUERY;
    sub_msg.param1 = INPUT_CMD_SUBSCRIBE;
    ipc_send(input_pid, &sub_msg);
    
    printf("WindowManager: Subscribed to Input events.\n");

    int mouse_x = vinfo.width / 2;
    int mouse_y = vinfo.height / 2;
    int mouse_w = 14;
    int mouse_h = 19;

    draw_mouse_cursor(mouse_x, mouse_y);
    video_flush_rects(&full_screen, 1);

    int total_dx = 0;
    int total_dy = 0;
    uint8_t current_buttons = 0;
    uint8_t old_buttons = 0;
    int mouse_needs_redraw = 0;
    int ui_needs_full_redraw = 0;

    message_t msg;

    while (1) {
        ipc_recv(&msg);

        do {
            if (msg.type == MSG_TYPE_INPUT && msg.subtype == MSG_SUBTYPE_SEND) {
                if (msg.param1 == INPUT_EVENT_MOUSE) {
                    total_dx += (int16_t)(msg.param2 & 0xFFFF);
                    total_dy += (int16_t)((msg.param2 >> 16) & 0xFFFF);
                    current_buttons = (uint8_t)msg.param3;
                    mouse_needs_redraw = 1;
                }
                else if (msg.param1 == INPUT_EVENT_KEY) {
                    printf("WindowManager: Key: 0x%X\n", (uint32_t)msg.param2);
                }
            }
            else if (msg.type == MSG_TYPE_WND) {
                if (msg.subtype == MSG_SUBTYPE_QUERY && msg.param1 == WND_CMD_CREATE) {
                    wnd_create_req_t* req = (wnd_create_req_t*)msg.data;

                    syswindow_t* new_win = create_window(
                        msg.sender_pid, 
                        req->x, 
                        req->y, 
                        req->width, 
                        req->height, 
                        req->shm_id, 
                        req->flags
                    );
                    
                    message_t resp;
                    memset(&resp, 0, sizeof(message_t));
                    resp.type = MSG_TYPE_WND;
                    resp.subtype = MSG_SUBTYPE_RESPONSE;
                    
                    if (new_win) {
                        resp.param1 = 0;
                        resp.param2 = new_win->win_id;
                        ui_needs_full_redraw = 1;
                    } else {
                        resp.param1 = -1;
                    }
                    ipc_send(msg.sender_pid, &resp);
                }
                else if (msg.subtype == MSG_SUBTYPE_SEND && msg.param1 == WND_CMD_FLUSH) {
                    ui_needs_full_redraw = 1; 
                }
                else if (msg.subtype == MSG_SUBTYPE_QUERY && msg.param1 == WND_CMD_GET_SCREEN_INFO) {
                    message_t resp;
                    memset(&resp, 0, sizeof(message_t));
                    resp.type = MSG_TYPE_WND;
                    resp.subtype = MSG_SUBTYPE_RESPONSE;
                    resp.param1 = 0;
                    resp.param2 = ((uint64_t)vinfo.width << 16) | (vinfo.height & 0xFFFF);
                    ipc_send(msg.sender_pid, &resp);
                }
            }
        } while (ipc_tryrecv(&msg) == SYS_RES_OK); 

        if (mouse_needs_redraw) {
            mouse_needs_redraw = 0;
            
            int old_x = mouse_x;
            int old_y = mouse_y;

            mouse_x += total_dx;
            mouse_y += total_dy;
            total_dx = 0; total_dy = 0;

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > (int)vinfo.width - mouse_w) mouse_x = (int)vinfo.width - mouse_w;
            if (mouse_y > (int)vinfo.height - mouse_h) mouse_y = (int)vinfo.height - mouse_h;

            if ((current_buttons & 1) && !(old_buttons & 1)) {
                syswindow_t* cur = top_window;
                while (cur) {
                    if (!(cur->flags & WND_FLAG_BACKGROUND) && 
                        mouse_x >= cur->x && mouse_x <= cur->x + cur->w &&
                        mouse_y >= cur->y && mouse_y <= cur->y + cur->h) {
                        
                        bring_to_front(cur);
                        ui_needs_full_redraw = 1;

                        if (mouse_y <= cur->y + 32) {
                            dragged_window = cur;
                            drag_offset_x = mouse_x - cur->x;
                            drag_offset_y = mouse_y - cur->y;
                        }
                        break;
                    }
                    cur = cur->prev;
                }
            }

            if (!(current_buttons & 1)) dragged_window = 0;

            if (dragged_window && (old_x != mouse_x || old_y != mouse_y)) {
                dragged_window->x = mouse_x - drag_offset_x;
                dragged_window->y = mouse_y - drag_offset_y;
                ui_needs_full_redraw = 1;
            }
            
            old_buttons = current_buttons;

            if (!ui_needs_full_redraw && (old_x != mouse_x || old_y != mouse_y)) {
                update_mouse_cursor(old_x, old_y, mouse_x, mouse_y, mouse_w, mouse_h);
            }
        }

        if (ui_needs_full_redraw) {
            ui_needs_full_redraw = 0;
            draw_all_windows(0, 0, vinfo.width, vinfo.height);
            draw_mouse_cursor(mouse_x, mouse_y);
            video_flush_rects(&full_screen, 1);
        } 
    }

    return 0;
}