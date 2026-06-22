#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_WND, DRV_PERM_GET_SPEC_INFO, 0);

sys_video_t vinfo;
uint32_t* backbuffer = 0;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct syswindow {
    int win_id;          // Уникальный ID окна
    apid_t owner_pid;    // PID программы
    int x, y;            // Позиция
    int w, h;            // Размеры
    uint64_t shm_id;     // ID разделяемой памяти
    uint32_t* pixels;    // Указатель на пиксели клиента
    uint32_t flags;      // 1 = ROOT_WINDOW (Фон)
    
    struct syswindow* next; // Окно ВЫШЕ
    struct syswindow* prev; // Окно НИЖЕ
} syswindow_t;

syswindow_t* bottom_window = 0;
syswindow_t* top_window = 0;    
int global_win_id = 1;

syswindow_t* dragged_window = 0;
int drag_offset_x = 0;
int drag_offset_y = 0;

uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << vinfo.red_mask_shift) |
           ((uint32_t)g << vinfo.green_mask_shift) |
           ((uint32_t)b << vinfo.blue_mask_shift);
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x >= vinfo.width || y >= vinfo.height) return;
    if (x + w > vinfo.width) w = vinfo.width - x;
    if (y + h > vinfo.height) h = vinfo.height - y;

    for (uint32_t cy = y; cy < y + h; cy++) {
        uint32_t* row = (uint32_t*)((uint8_t*)backbuffer + (cy * vinfo.pitch));
        for (uint32_t cx = x; cx < x + w; cx++) {
            row[cx] = color;
        }
    }
}

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
    draw_rect(clip_x, clip_y, clip_w, clip_h, make_color(0, 0, 0));

    syswindow_t* current = bottom_window;
    while (current) {
        if (clip_x < current->x + current->w && clip_x + clip_w > current->x &&
            clip_y < current->y + current->h && clip_y + clip_h > current->y) {
            
            int draw_start_x = MAX(clip_x, current->x);
            int draw_start_y = MAX(clip_y, current->y);
            int draw_end_x = MIN(clip_x + clip_w, current->x + current->w);
            int draw_end_y = MIN(clip_y + clip_h, current->y + current->h);

            for (int cy = draw_start_y; cy < draw_end_y; cy++) {
                int win_py = cy - current->y;
                int start_px = draw_start_x - current->x;
                
                uint32_t* dest_row = (uint32_t*)((uint8_t*)backbuffer + (cy * vinfo.pitch)) + draw_start_x;
                
                uint32_t* src_row = current->pixels + (win_py * current->w) + start_px;
                
                int copy_bytes = (draw_end_x - draw_start_x) * 4;
                
                hal_memcpy_toio(dest_row, src_row, copy_bytes);
            }
        }
        current = current->next;
    }
}

void update_mouse_cursor(int old_x, int old_y, int new_x, int new_y, int w, int h, uint32_t color) {
    draw_rect(old_x, old_y, w, h, make_color(0, 0, 0));
    draw_all_windows(old_x, old_y, w, h);
    draw_rect(new_x, new_y, w, h, color);

    int min_x = old_x < new_x ? old_x : new_x;
    int min_y = old_y < new_y ? old_y : new_y;
    int max_x = (old_x > new_x ? old_x : new_x) + w;
    int max_y = (old_y > new_y ? old_y : new_y) + h;

    video_rect_t damage[1];
    damage[0].x = min_x;
    damage[0].y = min_y;
    damage[0].w = max_x - min_x;
    damage[0].h = max_y - min_y;

    video_flush_rects(damage, 1);
}

int driver_main(void* reserved1, void* reserved2) {
    printf("WindowManager: Starting...\n");

    video_init();

    if (sysget_spec_info(SPEC_INFO_VIDEO, &vinfo) != SYS_RES_OK) {
        printf("WindowManager: Failed to get video info!\n");
        return -1;
    }

    uint64_t size_bytes = vinfo.height * vinfo.pitch;
    void* shm_vaddr = 0;
    uint64_t shm_id = shm_alloc(size_bytes, &shm_vaddr);
    
    if (!shm_id) return -1;
    backbuffer = (uint32_t*)shm_vaddr;

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
    int mouse_w = 12;
    int mouse_h = 18;
    uint32_t cursor_color = make_color(255, 100, 0);

    draw_rect(mouse_x, mouse_y, mouse_w, mouse_h, cursor_color);
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
                    current_buttons = msg.param3;
                    mouse_needs_redraw = 1;
                }
                else if (msg.param1 == INPUT_EVENT_KEY) {
                    printf("WindowManager: Key: 0x%X\n", msg.param2);
                }
            }
            else if (msg.type == MSG_TYPE_WND) {
                if (msg.subtype == MSG_SUBTYPE_QUERY && msg.param1 == WND_CMD_CREATE) {
                    int w = msg.param2 >> 16;
                    int h = msg.param2 & 0xFFFF;
                    uint32_t flags = msg.param3;
                    uint64_t shm_id = *(uint64_t*)(msg.data);

                    syswindow_t* new_win = create_window(msg.sender_pid, 100, 100, w, h, shm_id, flags);
                    
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
                    // Программа обновила пиксели. В идеале тут нужен Damage Tracking окна,
                    // но пока просто помечаем весь UI на перерисовку.
                    ui_needs_full_redraw = 1; 
                }
				else if (msg.subtype == MSG_SUBTYPE_QUERY && msg.param1 == WND_CMD_GET_SCREEN_INFO) {
                    message_t resp;
                    memset(&resp, 0, sizeof(message_t));
                    resp.type = MSG_TYPE_WND;
                    resp.subtype = MSG_SUBTYPE_RESPONSE;
                    resp.param1 = 0;
                    resp.param2 = (vinfo.width << 16) | (vinfo.height & 0xFFFF);
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
            if (mouse_x > vinfo.width - mouse_w) mouse_x = vinfo.width - mouse_w;
            if (mouse_y > vinfo.height - mouse_h) mouse_y = vinfo.height - mouse_h;

            if ((current_buttons & 1) && !(old_buttons & 1)) {
                syswindow_t* cur = top_window;
                while (cur) {
                    if (cur->flags != 1 && 
                        mouse_x >= cur->x && mouse_x <= cur->x + cur->w &&
                        mouse_y >= cur->y && mouse_y <= cur->y + cur->h) {
                        
                        bring_to_front(cur);
                        ui_needs_full_redraw = 1;

                        if (mouse_y <= cur->y + 30) {
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
                update_mouse_cursor(old_x, old_y, mouse_x, mouse_y, mouse_w, mouse_h, cursor_color);
            }
        }

        if (ui_needs_full_redraw) {
            ui_needs_full_redraw = 0;
            draw_all_windows(0, 0, vinfo.width, vinfo.height);
            draw_rect(mouse_x, mouse_y, mouse_w, mouse_h, cursor_color);
            video_rect_t full_screen = {0, 0, vinfo.width, vinfo.height};
            video_flush_rects(&full_screen, 1);
        } 
    }

    return 0;
}