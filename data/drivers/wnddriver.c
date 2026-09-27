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
agfx_ui_theme_t wm_theme;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, min_v, max_v) (MAX((min_v), MIN((v), (max_v))))

typedef struct syswindow {
    int win_id;              // Уникальный ID окна
    apid_t owner_pid;        // PID программы
    int x, y;                // Позиция
    int w, h;                // Размеры
    uint64_t shm_id;         // ID разделяемой памяти
    uint32_t* pixels;        // Указатель на пиксели клиента
    uint32_t flags;          // WND_FLAG_*
    agfx_surface_t surface;  // Поверхность AGFX
    
    struct syswindow* next;  // Окно ВЫШЕ
    struct syswindow* prev;  // Окно НИЖЕ
} syswindow_t;

syswindow_t* bottom_window = 0;
syswindow_t* top_window = 0;    
int global_win_id = 1;

syswindow_t* dragged_window = 0;
int drag_offset_x = 0;
int drag_offset_y = 0;

syswindow_t* mouse_target_window = 0;

#define TITLE_H  30
#define BORDER_W 1

static inline void get_window_total_bounds(syswindow_t* win, int* out_w, int* out_h) {
    if (win->flags & (WND_FLAG_BACKGROUND | WND_FLAG_NO_TITLEBAR)) {
        *out_w = win->w;
        *out_h = win->h;
    } else {
        *out_w = win->w + BORDER_W * 2;
        *out_h = win->h + TITLE_H + BORDER_W;
    }
}

syswindow_t* find_window_by_id(int win_id) {
    syswindow_t* cur = bottom_window;
    while (cur) {
        if (cur->win_id == win_id) return cur;
        cur = cur->next;
    }
    return NULL;
}

syswindow_t* find_window_at(int x, int y) {
    syswindow_t* cur = top_window;
    while (cur) {
        if (x >= cur->x && x < cur->x + cur->w &&
            y >= cur->y && y < cur->y + cur->h) {
            return cur;
        }
        cur = cur->prev;
    }
    return NULL;
}

syswindow_t* create_window(apid_t owner_pid, int x, int y, int w, int h, uint64_t shm_id, uint32_t flags) {
    syswindow_t* win = (syswindow_t*)malloc(sizeof(syswindow_t));
    if (!win) return 0;
    
    win->win_id = global_win_id++;
    win->owner_pid = owner_pid;
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->flags = flags;
    win->shm_id = shm_id;
    
    uint8_t* shm_ptr = (uint8_t*)shm_map(shm_id);
    
    uint64_t frame_size = (uint64_t)w * h * 4;
    win->pixels = (uint32_t*)(shm_ptr + frame_size);

    agfx_init(&win->surface, win->pixels, w, h, w * 4);
    
    win->next = 0;
    win->prev = top_window;
    
    if (top_window) top_window->next = win;
    else bottom_window = win;
    top_window = win;
    return win;
}

void destroy_window(syswindow_t* win) {
    if (!win) return;
    
    if (dragged_window == win) dragged_window = NULL;
    if (mouse_target_window == win) mouse_target_window = NULL;

    if (win->prev) win->prev->next = win->next;
    else bottom_window = win->next;
    
    if (win->next) win->next->prev = win->prev;
    else top_window = win->prev;
    
    shm_free(win->shm_id);
    free(win);
}

void bring_to_front(syswindow_t* win) {
    if (!win || win == top_window || (win->flags & WND_FLAG_BACKGROUND)) return;
    if (!(win->flags & WND_FLAG_TOPMOST) && (top_window->flags & WND_FLAG_TOPMOST)) {
        syswindow_t* insert_before = top_window;
        while (insert_before->prev && (insert_before->prev->flags & WND_FLAG_TOPMOST)) {
            insert_before = insert_before->prev;
        }
        return;
    }

    if (win->prev) win->prev->next = win->next;
    else bottom_window = win->next;
    if (win->next) win->next->prev = win->prev;

    win->prev = top_window;
    win->next = 0;
    if (top_window) top_window->next = win;
    top_window = win;
}

void draw_all_windows(int clip_x, int clip_y, int clip_w, int clip_h) {
    agfx_set_clip(&screen_surface, clip_x, clip_y, clip_w, clip_h);
    agfx_fill_rect(&screen_surface, clip_x, clip_y, clip_w, clip_h, 0xFF000000);

    syswindow_t* current = bottom_window;
    while (current) {
        int tot_w, tot_h;
        get_window_total_bounds(current, &tot_w, &tot_h);

        if (clip_x < current->x + tot_w && clip_x + clip_w > current->x &&
            clip_y < current->y + tot_h && clip_y + clip_h > current->y) {
            
            if (current->flags & WND_FLAG_BACKGROUND) {
                agfx_blit(&screen_surface, current->x, current->y, current->w, current->h, &current->surface);
            }
            else if (current->flags & WND_FLAG_NO_TITLEBAR) {
                agfx_ui_draw_shadow(&screen_surface, current->x, current->y, current->w, current->h, 10);
                agfx_blit(&screen_surface, current->x, current->y, current->w, current->h, &current->surface);
            }
            else {
                agfx_ui_draw_shadow(&screen_surface, current->x, current->y, tot_w, tot_h, 12);

                int is_active = (current == top_window);
                uint32_t title_bg = is_active ? wm_theme.titlebar_active : wm_theme.titlebar_inactive;
                uint32_t border_col = is_active ? wm_theme.border_active : wm_theme.border_inactive;

                agfx_fill_rect(&screen_surface, current->x, current->y, tot_w, TITLE_H, title_bg);

                int btn_w = 36;
                int btn_x = current->x + tot_w - btn_w;
                agfx_draw_line(&screen_surface, btn_x + 13, current->y + 10, btn_x + 23, current->y + 20, 1, wm_theme.close_btn_icon);
                agfx_draw_line(&screen_surface, btn_x + 13, current->y + 20, btn_x + 23, current->y + 10, 1, wm_theme.close_btn_icon);

                agfx_blit(&screen_surface, current->x + BORDER_W, current->y + TITLE_H, 
                          current->w, current->h, &current->surface);

                agfx_draw_rect(&screen_surface, current->x, current->y, tot_w, tot_h, BORDER_W, border_col);
            }
        }
        current = current->next;
    }
}

static inline void draw_mouse_cursor(int x, int y) {
    agfx_set_clip(&screen_surface, 0, 0, vinfo.width, vinfo.height);
    agfx_fill_triangle(&screen_surface, x, y, x, y + 17, x + 12, y + 12, wm_theme.cursor_fill);
    agfx_draw_triangle(&screen_surface, x, y, x, y + 17, x + 12, y + 12, 1, wm_theme.cursor_outline);
}

void dispatch_mouse_event(syswindow_t* target, int mx, int my, uint8_t buttons) {
    if (!target) return;
    
    message_t client_msg;
    memset(&client_msg, 0, sizeof(message_t));
    client_msg.type = MSG_TYPE_INPUT;
    client_msg.subtype = MSG_SUBTYPE_SEND;
    client_msg.param1 = INPUT_EVENT_MOUSE;

    int lx = mx - target->x;
    int ly = my - target->y;
    client_msg.param2 = (lx & 0xFFFF) | ((ly & 0xFFFF) << 16);
    client_msg.param3 = buttons;

    ipc_send(target->owner_pid, &client_msg);
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

void update_dragged_window(int old_wx, int old_wy, syswindow_t* win, int mx, int my) {
    int margin = 12;
    int min_x = CLAMP(MIN(old_wx, win->x) - margin, 0, (int)vinfo.width);
    int min_y = CLAMP(MIN(old_wy, win->y) - margin, 0, (int)vinfo.height);
    int max_x = CLAMP(MAX(old_wx, win->x) + win->w + margin, 0, (int)vinfo.width);
    int max_y = CLAMP(MAX(old_wy, win->y) + win->h + margin, 0, (int)vinfo.height);

    int dw = max_x - min_x;
    int dh = max_y - min_y;

    draw_all_windows(min_x, min_y, dw, dh);
    draw_mouse_cursor(mx, my);

    video_rect_t damage = { (uint32_t)min_x, (uint32_t)min_y, (uint32_t)dw, (uint32_t)dh };
    video_flush_rects(&damage, 1);
}

int driver_main(void* reserved1, void* reserved2) {
    (void)reserved1;
    (void)reserved2;
    printf("WindowManager: Starting with AGFX...\n");

    agfx_set_allocators(malloc, free);
    wm_theme = agfx_ui_theme_win10_dark();

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
	
	ipc_set_limit(0, 1024);

    int total_dx = 0;
    int total_dy = 0;
    uint8_t current_buttons = 0;
    uint8_t old_buttons = 0;
    int mouse_needs_redraw = 0;
    int ui_needs_full_redraw = 0;

	set_thread_priority(0, THREAD_PRIO_REALTIME);

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
                    if (top_window && !(top_window->flags & WND_FLAG_BACKGROUND)) {
                        ipc_send(top_window->owner_pid, &msg);
                    }
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
                    } else {
                        resp.param1 = -1;
                    }
                    ipc_send(msg.sender_pid, &resp);
                }
                else if (msg.subtype == MSG_SUBTYPE_QUERY && msg.param1 == WND_CMD_DESTROY) {
                    int win_id = (int)msg.param2;
                    syswindow_t* win = find_window_by_id(win_id);
                    if (win && win->owner_pid == msg.sender_pid) {
                        int wx = win->x, wy = win->y, ww = win->w, wh = win->h;
                        destroy_window(win);

                        draw_all_windows(wx - 10, wy - 10, ww + 20, wh + 20);
                        draw_mouse_cursor(mouse_x, mouse_y);
                        video_rect_t dmg = { (uint32_t)MAX(0, wx - 10), (uint32_t)MAX(0, wy - 10), 
                                             (uint32_t)(ww + 20), (uint32_t)(wh + 20) };
                        video_flush_rects(&dmg, 1);
                    }
                    message_t resp;
                    memset(&resp, 0, sizeof(message_t));
                    resp.type = MSG_TYPE_WND;
                    resp.subtype = MSG_SUBTYPE_RESPONSE;
                    resp.param1 = 0;
                    ipc_send(msg.sender_pid, &resp);
                }
                else if (msg.subtype == MSG_SUBTYPE_SEND && msg.param1 == WND_CMD_FLUSH) {
					int win_id = (int)msg.param2;
                    syswindow_t* win = find_window_by_id(win_id);
                    if (win) {
                        int tot_w, tot_h;
                        get_window_total_bounds(win, &tot_w, &tot_h);

                        int dmg_x = MAX(0, win->x - 16);
                        int dmg_y = MAX(0, win->y - 16);
                        int dmg_w = tot_w + 32;
                        int dmg_h = tot_h + 32;

                        draw_all_windows(dmg_x, dmg_y, dmg_w, dmg_h);
                        
                        if (mouse_x + mouse_w >= dmg_x && mouse_x <= dmg_x + dmg_w &&
                            mouse_y + mouse_h >= dmg_y && mouse_y <= dmg_y + dmg_h) {
                            draw_mouse_cursor(mouse_x, mouse_y);
                        }
                        
                        video_rect_t dmg = { (uint32_t)dmg_x, (uint32_t)dmg_y, (uint32_t)dmg_w, (uint32_t)dmg_h };
                        video_flush_rects(&dmg, 1);
                    } else {
                        ui_needs_full_redraw = 1;
                    }
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
				else if (msg.subtype == MSG_SUBTYPE_QUERY && msg.param1 == WND_CMD_GET_THEME) {
                    uint64_t shm_id = *(uint64_t*)(msg.data);
                    void* buf = shm_map(shm_id);
                    
                    message_t resp;
                    memset(&resp, 0, sizeof(message_t));
                    resp.type = MSG_TYPE_WND;
                    resp.subtype = MSG_SUBTYPE_RESPONSE;
                    
                    if (buf) {
                        memcpy(buf, &wm_theme, sizeof(agfx_ui_theme_t));
                        resp.param1 = 0;
                    } else {
                        resp.param1 = -1;
                    }
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

            mouse_x = CLAMP(mouse_x, 0, (int)vinfo.width - mouse_w);
            mouse_y = CLAMP(mouse_y, 0, (int)vinfo.height - mouse_h);

            if ((current_buttons & 1) && !(old_buttons & 1)) {
                syswindow_t* clicked_win = find_window_at(mouse_x, mouse_y);
                mouse_target_window = clicked_win;
								
				if (clicked_win) {
					int tot_w, tot_h;
					get_window_total_bounds(clicked_win, &tot_w, &tot_h);

					if (!(clicked_win->flags & (WND_FLAG_BACKGROUND | WND_FLAG_NO_TITLEBAR))) {
						bring_to_front(clicked_win);
						ui_needs_full_redraw = 1;

						if (mouse_y >= clicked_win->y && mouse_y < clicked_win->y + TITLE_H) {
							int btn_w = 36;
							int btn_x = clicked_win->x + tot_w - btn_w;

							if (mouse_x >= btn_x) {
								message_t close_msg;
								memset(&close_msg, 0, sizeof(message_t));
								close_msg.type = MSG_TYPE_WND;
								close_msg.subtype = MSG_SUBTYPE_SEND;
								close_msg.param1 = WND_CMD_DESTROY;
								ipc_send(clicked_win->owner_pid, &close_msg);
								
								destroy_window(clicked_win);
								ui_needs_full_redraw = 1;
								
								continue; 
							}

							dragged_window = clicked_win;
							drag_offset_x = mouse_x - clicked_win->x;
							drag_offset_y = mouse_y - clicked_win->y;
							continue;
						}
					}

					int offset_y = (clicked_win->flags & (WND_FLAG_BACKGROUND | WND_FLAG_NO_TITLEBAR)) ? 0 : TITLE_H;
					int offset_x = (clicked_win->flags & (WND_FLAG_BACKGROUND | WND_FLAG_NO_TITLEBAR)) ? 0 : BORDER_W;

					dispatch_mouse_event(clicked_win, mouse_x - offset_x, mouse_y - offset_y, current_buttons);
				}
			}

            if (!(current_buttons & 1) && (old_buttons & 1)) {
                dragged_window = NULL;
            }

            syswindow_t* event_target = dragged_window ? dragged_window : 
                                       (mouse_target_window ? mouse_target_window : find_window_at(mouse_x, mouse_y));
            
            if (event_target) {
				int off_y = (event_target->flags & (WND_FLAG_BACKGROUND | WND_FLAG_NO_TITLEBAR)) ? 0 : TITLE_H;
				int off_x = (event_target->flags & (WND_FLAG_BACKGROUND | WND_FLAG_NO_TITLEBAR)) ? 0 : BORDER_W;
				dispatch_mouse_event(event_target, mouse_x - off_x, mouse_y - off_y, current_buttons);
			}

            if (!(current_buttons & 1)) {
                mouse_target_window = NULL;
            }

            if (dragged_window && (old_x != mouse_x || old_y != mouse_y)) {
                int old_wx = dragged_window->x;
                int old_wy = dragged_window->y;

                dragged_window->x = mouse_x - drag_offset_x;
                dragged_window->y = mouse_y - drag_offset_y;

                update_dragged_window(old_wx, old_wy, dragged_window, mouse_x, mouse_y);
            } 
            else if (!ui_needs_full_redraw && (old_x != mouse_x || old_y != mouse_y)) {
                update_mouse_cursor(old_x, old_y, mouse_x, mouse_y, mouse_w, mouse_h);
            }
            
            old_buttons = current_buttons;
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