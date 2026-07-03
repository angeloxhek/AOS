#include <stdint.h>
#include <aoslib.h>

int main(int argc, char** argv) {
    printf("Desktop: Starting Shell...\n");

    screen_info_t s_info;
    if (get_screen_info(&s_info) != 0) {
        printf("Desktop: Failed to get screen resolution!\n");
        return -1;
    }

    window_t* win = window_create(s_info.width, s_info.height, WND_FLAG_BACKGROUND);
    if (!win) {
        printf("Desktop: Failed to create window!\n");
        return -1;
    }

    ui_context_t ctx;
    ui_init_from_window(&ctx, win);

    ui_draw_gradient_v(&ctx, 0, 0, s_info.width, s_info.height, COLOR_DARK_BLUE, COLOR_LIGHT_BLUE);

    int taskbar_h = 40;
    int taskbar_y = s_info.height - taskbar_h;
    ui_fill_rect(&ctx, 0, taskbar_y, s_info.width, taskbar_h, COLOR_DARK_GRAY);

    ui_draw_button(&ctx, 5, taskbar_y + 4, 80, 32, COLOR_GRAY, 0);

    window_flush(win);

    printf("Desktop: Render complete!\n");

    message_t msg;
    while(1) {
        ipc_recv(&msg);
    }

    return 0;
}