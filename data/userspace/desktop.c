#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <aos/types.h>
#include <aos/window.h>
#include <aos/ipc.h>

#include <agfx.h>
#include <agfx_ui.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Desktop: Starting with AGFX Engine...\n");

    agfx_set_allocators(malloc, free);

    screen_info_t s_info;
    get_screen_info(&s_info);

    window_t* win = window_create(0, 0, s_info.width, s_info.height, WND_FLAG_BACKGROUND);

    agfx_surface_t screen;
    agfx_init(&screen, win->buffer, s_info.width, s_info.height, s_info.width * 4);

    agfx_fill_rect(&screen, 0, 0, s_info.width, s_info.height, 0xFF1E1E2E);

    agfx_ui_context_t ui;
    agfx_ui_init(&ui, &screen, NULL);
    agfx_ui_theme_t theme = agfx_ui_theme_win10_dark();
    agfx_ui_set_theme(&ui, &theme);

    int taskbar_h = 40;
    int taskbar_y = s_info.height - taskbar_h;
    agfx_fill_rect(&screen, 0, taskbar_y, s_info.width, taskbar_h, theme.bg);
    agfx_draw_line(&screen, 0, taskbar_y, s_info.width, taskbar_y, 1, theme.border);

    agfx_ui_begin(&ui, 8, taskbar_y + 6);
    agfx_ui_button(&ui, "Start", 80, 28);

    window_flush(win);

    message_t msg;
    while(1) {
        ipc_recv(&msg);
    }
    return 0;
}