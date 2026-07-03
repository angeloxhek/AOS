#ifndef AOS_UI_H
#define AOS_UI_H

#include <stdint.h>
#include "types.h"
#include "window.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COLOR_BLACK       0xFF000000
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_RED         0xFFFF0000
#define COLOR_GREEN       0xFF00FF00
#define COLOR_BLUE        0xFF0000FF
#define COLOR_DARK_BLUE   0xFF002244
#define COLOR_LIGHT_BLUE  0xFF0088FF
#define COLOR_GRAY        0xFF888888
#define COLOR_DARK_GRAY   0xFF333333

typedef struct {
    uint32_t* buffer;
    int width;
    int height;
} ui_context_t;

#ifdef AOSLIB_UI

void ui_init_from_window(ui_context_t* ctx, window_t* win);

void ui_fill_rect(ui_context_t* ctx, int x, int y, int w, int h, uint32_t color);
void ui_draw_gradient_v(ui_context_t* ctx, int x, int y, int w, int h, uint32_t color_top, uint32_t color_bottom);
void ui_draw_button(ui_context_t* ctx, int x, int y, int w, int h, uint32_t color, int is_pressed);
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_UI_H