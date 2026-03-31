#include "include/kernel_internal.h"

// -------------------------
//     Print Functions
// -------------------------

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= video->width || y >= video->height) return;
    uint64_t offset = (y * video->pitch) + (x * (video->bpp / 8));
    uint32_t* pixel = (uint32_t*)(video->framebuffer_addr + offset);
    *pixel = color;
}

void _kclear() {
    uint32_t* fb = (uint32_t*)video->framebuffer_addr;
    for (uint32_t y = 0; y < video->height; y++) {
        uint32_t* row = (uint32_t*)(video->framebuffer_addr + y * video->pitch);
        for (uint32_t x = 0; x < video->width; x++) {
             row[x] = bg_color;
        }
    }

    if (!(state.system_flags & CAN_PRINT)) return;
    cursor_x = 0;
    cursor_y = 0;
}

void kclear() {
    if (!(state.system_flags & CAN_PRINT)) return;
    uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kclear();
    spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void kprint_scroll() {
    if (!(state.system_flags & CAN_PRINT)) return;
    uint32_t font_h = 16;
    uint64_t bytes_to_move = (uint64_t)video->pitch * (video->height - font_h);
    uint8_t* fb = (uint8_t*)video->framebuffer_addr;
    kernel_memcpy(fb, fb + (font_h * video->pitch), bytes_to_move);
    uint8_t* bottom_part = fb + bytes_to_move;
    uint64_t bottom_size = (uint64_t)font_h * video->pitch;
    kernel_memset64(bottom_part, ((uint64_t)bg_color << 32) | bg_color, bottom_size);
}

void _kprint_char(int x_pos, int y_pos, char c, uint32_t fg, uint32_t bg) {
    const uint8_t* glyph = (*font)[(unsigned char)c];
    for (int y = 0; y < 16; y++) {
        const uint8_t line = glyph[y];
        for (int x = 0; x < 8; x++) {
            if ((line >> (7 - x)) & 1) {
                put_pixel(x_pos + x, y_pos + y, fg);
            } else {
                put_pixel(x_pos + x, y_pos + y, bg);
            }
        }
    }
}

void kprint_char(char c, uint32_t color) {
    if (!(state.system_flags & CAN_PRINT)) return;
    const int font_w = 8;
    const int font_h = 16;
    int max_cols = video->width / font_w;
    int max_rows = video->height / font_h;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        _kprint_char(cursor_x * font_w, cursor_y * font_h, c, color, bg_color);
        cursor_x++;
    }
    if (cursor_x >= max_cols) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= max_rows) {
        kprint_scroll();
        cursor_y = max_rows - 1;
    }
}

void _kprint(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x0000FFFF);
    }
}

void kprint(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
    uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kprint(str);
    spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void _kprint_error(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x00FF0000);
    }
}

void kprint_error(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
    uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kprint_error(str);
    spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void _kprint_error_vga(const char* str) {
    volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;
    for(int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x1F00;
    }
    int i = 0;
    while(str[i]) {
        vga_buffer[i] = (uint16_t)str[i] | 0x4F00;
        i++;
    }
}


// ------------------------
//      uint to text
// ------------------------

void uint32_to_hex(uint32_t value, char* out_buffer) { // buff size 9
    const char *hex_digits = "0123456789ABCDEF";
    out_buffer[0] = hex_digits[(value >> 28) & 0x0F];
    out_buffer[1] = hex_digits[(value >> 24) & 0x0F];
    out_buffer[2] = hex_digits[(value >> 20) & 0x0F];
    out_buffer[3] = hex_digits[(value >> 16) & 0x0F];
    out_buffer[4] = hex_digits[(value >> 12) & 0x0F];
    out_buffer[5] = hex_digits[(value >> 8) & 0x0F];
    out_buffer[6] = hex_digits[(value >> 4) & 0x0F];
    out_buffer[7] = hex_digits[value & 0x0F];
    out_buffer[8] = 0;
}

void uint64_to_hex(uint64_t value, char* out_buffer) { // buff size 17
    const char *hex_digits = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        out_buffer[i] = hex_digits[value & 0x0F];
        value >>= 4;
    }
    out_buffer[16] = '\0';
}

void uint32_to_dec(uint32_t value, char* out_buffer) { // buff size 11
    char temp[11];
    int i = 0;
    if (value == 0) {
        out_buffer[0] = '0';
        out_buffer[1] = '\0';
        return;
    }
    while (value > 0) {
        temp[i++] = (value % 10) + '0';
        value /= 10;
    }
    int j = 0;
    while (i > 0) {
        out_buffer[j++] = temp[--i];
    }
    out_buffer[j] = '\0';
}

void uint64_to_dec(uint64_t value, char* out_buffer) { // buff size 21
    char temp[21];
    int i = 0;
    if (value == 0) {
        out_buffer[0] = '0';
        out_buffer[1] = '\0';
        return;
    }
    while (value > 0) {
        temp[i++] = (value % 10) + '0';
        value /= 10;
    }
    int j = 0;
    while (i > 0) {
        out_buffer[j++] = temp[--i];
    }
    out_buffer[j] = '\0';
}


// -------------------------
//          Debug
// -------------------------

__attribute__((noreturn)) void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    asm volatile("cli");
    _kprint_error("KERNEL STOP: 0x");
    char buff[17];
    uint64_to_hex(code, buff);
    _kprint_error(buff);
    _kprint_error(" (");
    if (code < KERNEL_MESSAGES_COUNT)
        _kprint_error(kernel_messages[code]);
    else
        _kprint_error("UNKNOWN");
    _kprint_error(")\nARGS: 0x");
    uint64_to_hex(arg1, buff);
    _kprint_error(buff);
    _kprint_error("; 0x");
    uint64_to_hex(arg2, buff);
    _kprint_error(buff);
    _kprint_error("; 0x");
    uint64_to_hex(arg3, buff);
    _kprint_error(buff);
    _kprint_error("; 0x");
    uint64_to_hex(arg4, buff);
    _kprint_error(buff);
    _kprint_error("\nThe system has been halted!");
    while (1) {
        asm volatile("hlt");
    }
    __builtin_unreachable();
}

__attribute__((noreturn)) void __stack_chk_fail(void) {
    kernel_error(0x1, (uint64_t)__builtin_return_address(0), 0, 0, 0);
    __builtin_unreachable();
}

__attribute__((noreturn)) void breakpoint(){
    kprint("Breakpoint :-)");
    while (1) {
        asm volatile("cli; hlt");
    }
    __builtin_unreachable();
}

void pausepoint(){
    kprint("Pausepoint. Press any key to continue :3\n");
    while ((inb(0x64) & 1) == 0);
    inb(0x60);
}
