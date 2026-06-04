#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_WND, DRV_PERM_GET_SPEC_INFO, 0);

sys_video_t vinfo;
uint32_t* backbuffer = 0;

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

void draw_desktop_ui(uint32_t clip_x, uint32_t clip_y, uint32_t clip_w, uint32_t clip_h) {
    // 1. Фон (Темно-синий)
    draw_rect(clip_x, clip_y, clip_w, clip_h, make_color(0, 64, 128));
    
    uint32_t win_x = 100, win_y = 100, win_w = 500, win_h = 350;
    
    if (clip_x < win_x + win_w && clip_x + clip_w > win_x &&
        clip_y < win_y + win_h && clip_y + clip_h > win_y) {
        
        draw_rect(win_x, win_y, win_w, win_h, make_color(200, 200, 200));
        draw_rect(win_x, win_y, win_w, 30,  make_color(255, 255, 255));
        draw_rect(win_x + win_w - 30, win_y + 5, 20, 20, make_color(220, 50, 50));
    }
}

void update_mouse_cursor(int old_x, int old_y, int new_x, int new_y, int w, int h, uint32_t color) {
    draw_rect(old_x, old_y, w, h, make_color(0, 64, 128));
    draw_desktop_ui(old_x, old_y, w, h);
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

    draw_rect(0, 0, vinfo.width, vinfo.height, make_color(0, 64, 128)); // Фон
    draw_desktop_ui(0, 0, vinfo.width, vinfo.height); // Окна
    
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
    int mouse_needs_redraw = 0;

    message_t msg;

    while (1) {
        ipc_recv(&msg);

        do {
            if (msg.type == MSG_TYPE_INPUT) {
                
                if (msg.param1 == INPUT_EVENT_MOUSE) {
                    total_dx += (int16_t)(msg.param2 & 0xFFFF);
                    total_dy += (int16_t)((msg.param2 >> 16) & 0xFFFF);
                    mouse_needs_redraw = 1;
                }
                else if (msg.param1 == INPUT_EVENT_KEY) {
                    uint8_t scancode = msg.param2;
                    printf("WindowManager: Key pressed: 0x%X\n", scancode);
                    // Здесь в будущем будем обрабатывать клавиатуру (Alt+Tab, ввод текста)
                }
            }
            
        } while (ipc_tryrecv(&msg) == SYS_RES_OK); 


        if (mouse_needs_redraw) {
            mouse_needs_redraw = 0;

            if (total_dx != 0 || total_dy != 0) {
                int old_x = mouse_x;
                int old_y = mouse_y;

                mouse_x += total_dx;
                mouse_y += total_dy;

                total_dx = 0;
                total_dy = 0;

                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x > vinfo.width - mouse_w) mouse_x = vinfo.width - mouse_w;
                if (mouse_y > vinfo.height - mouse_h) mouse_y = vinfo.height - mouse_h;

                update_mouse_cursor(old_x, old_y, mouse_x, mouse_y, mouse_w, mouse_h, cursor_color);
            }
        }
    }

    return 0;
}