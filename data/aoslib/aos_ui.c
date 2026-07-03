#include "../include/aoslib.h"

#define UI_CLIP(ctx, x, y, w, h) \
    if (x >= ctx->width || y >= ctx->height) return; \
    if (x < 0) { w += x; x = 0; } \
    if (y < 0) { h += y; y = 0; } \
    if (x + w > ctx->width) w = ctx->width - x; \
    if (y + h > ctx->height) h = ctx->height - y; \
    if (w <= 0 || h <= 0) return;

void ui_init_from_window(ui_context_t* ctx, window_t* win) {
    if (!ctx || !win) return;
    ctx->buffer = win->buffer;
    ctx->width = win->w;
    ctx->height = win->h;
}

void ui_fill_rect(ui_context_t* ctx, int x, int y, int w, int h, uint32_t color) {
    UI_CLIP(ctx, x, y, w, h);
    
    for (int cy = y; cy < y + h; cy++) {
        uint32_t* row = ctx->buffer + (cy * ctx->width);
        for (int cx = x; cx < x + w; cx++) {
            row[cx] = color;
        }
    }
}

void ui_draw_gradient_v(ui_context_t* ctx, int x, int y, int w, int h, uint32_t color_top, uint32_t color_bottom) {
    UI_CLIP(ctx, x, y, w, h);

    uint8_t a1 = (color_top >> 24) & 0xFF, r1 = (color_top >> 16) & 0xFF, g1 = (color_top >> 8) & 0xFF, b1 = color_top & 0xFF;
    uint8_t a2 = (color_bottom >> 24) & 0xFF, r2 = (color_bottom >> 16) & 0xFF, g2 = (color_bottom >> 8) & 0xFF, b2 = color_bottom & 0xFF;

    for (int cy = y; cy < y + h; cy++) {
        // Вычисляем процент от 0 до 255 для текущей строки
        int percent = ((cy - y) * 255) / h;
        int inv = 255 - percent;

        // Линейная интерполяция (Lerp) для каждого канала
        uint8_t a = (a1 * inv + a2 * percent) / 255;
        uint8_t r = (r1 * inv + r2 * percent) / 255;
        uint8_t g = (g1 * inv + g2 * percent) / 255;
        uint8_t b = (b1 * inv + b2 * percent) / 255;

        uint32_t final_color = (a << 24) | (r << 16) | (g << 8) | b;

        uint32_t* row = ctx->buffer + (cy * ctx->width);
        for (int cx = x; cx < x + w; cx++) {
            row[cx] = final_color;
        }
    }
}

void ui_draw_button(ui_context_t* ctx, int x, int y, int w, int h, uint32_t color, int is_pressed) {
    UI_CLIP(ctx, x, y, w, h);

    ui_fill_rect(ctx, x, y, w, h, color);

    uint32_t highlight = 0xFFFFFFFF;
    uint32_t shadow    = 0xFF111111;

    uint32_t top_color = is_pressed ? shadow : highlight;
    uint32_t bot_color = is_pressed ? highlight : shadow;

    ui_fill_rect(ctx, x, y, w, 2, top_color);
    ui_fill_rect(ctx, x, y, 2, h, top_color);
    ui_fill_rect(ctx, x, y + h - 2, w, 2, bot_color);
    ui_fill_rect(ctx, x + w - 2, y, 2, h, bot_color);
}