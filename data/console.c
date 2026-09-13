#include <kernel/internal.h>

// -------------------------
//     Print Functions
// -------------------------

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
	uint64_t vaddr = video->framebuffer_addr_virt;
    if (x >= video->width || y >= video->height) return;
    uint64_t offset = (y * video->pitch) + (x * (video->bpp / 8));
    uint32_t* pixel = (uint32_t*)(vaddr + offset);
    *pixel = color;
}

void _kclear() {
	uint64_t vaddr = video->framebuffer_addr_virt;
    for (uint32_t y = 0; y < video->height; y++) {
        uint32_t* row = (uint32_t*)(vaddr + y * video->pitch);
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
	uint64_t vaddr = video->framebuffer_addr_virt;
    if (!(state.system_flags & CAN_PRINT)) return;
    uint32_t font_h = 16;
    uint64_t bytes_to_move = (uint64_t)video->pitch * (video->height - font_h);
    uint8_t* fb = (uint8_t*)vaddr;
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
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            _kprint_char(cursor_x * font_w, cursor_y * font_h, ' ', bg_color, bg_color);
        }
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3;
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
	serial_print(str);
    if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x0000DDDD);
    }
}

void kprint(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) {
		serial_print(str);
		return;
	}
    uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kprint(str);
    spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void _kprint_error(const char* str) {
	serial_print(str);
    if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x00DD0000);
    }
}

void kprint_error(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) {
		serial_print(str);
		return;
	}
    uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kprint_error(str);
    spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}


// ------------------------
//      uint to text
// ------------------------

static void uint_to_hex(uint64_t value, char* out_buffer, uint8_t base, uint8_t full_str) {
    const char *hex_digits = "0123456789ABCDEF";
    
    int num_digits = base / 4; 
    int buffer_idx = 0;
    int started = 0;

    for (int i = num_digits - 1; i >= 0; i--) {
        uint8_t digit_val = (value >> (i * 4)) & 0x0F;
        if (!full_str && !started && digit_val == 0) {
            if (i > 0) continue; 
        }
        started = 1;
        out_buffer[buffer_idx++] = hex_digits[digit_val];
    }
    out_buffer[buffer_idx] = '\0';
}

void uint8_to_hex(uint32_t value, char* out_buffer) { // buff size 3
    uint_to_hex(value, out_buffer, 8, 1);
}

void uint32_to_hex(uint32_t value, char* out_buffer) { // buff size 9
    uint_to_hex(value, out_buffer, 32, 0);
}

void uint64_to_hex(uint64_t value, char* out_buffer) { // buff size 17
    uint_to_hex(value, out_buffer, 64, 0);
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

uint64_t octal_to_int(const char* str) {
    uint64_t size = 0;
    while (*str >= '0' && *str <= '7') {
        size = size * 8 + (*str - '0');
        str++;
    }
    return size;
}


// -------------------------
//          Debug
// -------------------------

__attribute__((noreturn)) void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    hal_disable_interrupts();
	
	state.system_flags |= CAN_PRINT;
    bg_color = 0x00000088;
    _kclear();
    cursor_x = 0; cursor_y = 0;
    
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
	_kprint_error("\n");
#ifdef DEBUG_MODE
	debug_backtrace();
#else
    _kprint_error("The system has been halted!\n");
#endif
    
    hal_halt();
    __builtin_unreachable();
}

__attribute__((noreturn)) void __stack_chk_fail(void) {
    kernel_error(0x1, (uint64_t)__builtin_return_address(0), 0, 0, 0);
    __builtin_unreachable();
}

__attribute__((noreturn)) void breakpoint(){
    kprint("Breakpoint :-)");
    hal_halt();
}

void pausepoint(){
    kprint("Pausepoint. Press any key to continue :3\n");
    hal_debug_pause();
}

#ifdef DEBUG_MODE

typedef struct {
    uint64_t addr;
    const char* name;
    const char* file;
    uint32_t line;
} ksymbol_t;

__attribute__((weak)) const ksymbol_t kernel_symbols[] = { {0, "", "", 0} };
__attribute__((weak)) const uint32_t kernel_symbols_count = 0;

static const ksymbol_t* ksymbol_lookup(uint64_t rip, uint64_t* offset_out) {
    if (kernel_symbols_count == 0 || rip < kernel_symbols[0].addr) return 0;

    int low = 0;
    int high = kernel_symbols_count - 1;
    int best = -1;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (kernel_symbols[mid].addr <= rip) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if (best != -1) {
        if (offset_out) *offset_out = rip - kernel_symbols[best].addr;
        return &kernel_symbols[best];
    }
    return 0;
}

void debug_print_thread(thread_t* th) {
	if (!th) return;
	char buf[32];
	_kprint_error("TID: ");
	uint64_to_dec(th->tid, buf);
	_kprint_error(buf);
	_kprint_error("; state: 0x");
	uint8_to_hex(th->state, buf);
	_kprint_error(buf);
	if (th->owner) {
		_kprint_error("; process name: \"");
		kernel_strncpy(buf, th->owner->name, 32);
		_kprint_error(buf);
		_kprint_error("\"; PID: ");
		uint32_to_hex(th->owner->id, buf);
		_kprint_error(buf);
		_kprint_error("; auth id: ");
		uint64_to_dec(th->owner->user.user.gid, buf);
		_kprint_error(buf);
		_kprint_error("/");
		uint64_to_dec(th->owner->user.user.uid, buf);
		_kprint_error(buf);
		_kprint_error("; IPC limit: ");
		uint64_to_dec(th->owner->ipc_queue_limit, buf);
		_kprint_error(buf);
		if (th->owner->main_thread && th->tid == th->owner->main_thread->tid) _kprint_error("; MAIN");
	}
	if (th->waiting_for_msg) _kprint_error("; IPC WAIT");
	if (th->wake_up_time > 0) _kprint_error("; TIME WAIT");
	_kprint_error("\n");
}

void debug_print_stack_trace(uint64_t max_frames) {
    _kprint_error("Call trace:\n");

    hal_stack_frame_t frame;
    hal_stack_trace_init(&frame);

    char buf[32];
    uint64_t frame_idx = 0;
    const char* stop_reason = "Completed";

    while (frame_idx < max_frames && hal_stack_trace_next(&frame, &stop_reason)) {
        uint64_t offset = 0;
        const ksymbol_t* sym = ksymbol_lookup(frame.ip - 1, &offset);

        if (sym && (kernel_strcmp(sym->name, "debug_print_stack_trace") == 0 ||
                    kernel_strcmp(sym->name, "debug_backtrace") == 0 ||
                    kernel_strcmp(sym->name, "kernel_error") == 0)) {
            continue;
        }

        _kprint_error("  #");
        uint64_to_dec(frame_idx++, buf);
        _kprint_error(buf);
        _kprint_error(" 0x");
        uint64_to_hex(frame.ip, buf);
        _kprint_error(buf);

        if (sym) {
            _kprint_error(" in ");
            _kprint_error(sym->name);
            _kprint_error(" (");
            _kprint_error(sym->file);
            _kprint_error(":");
            uint64_to_dec(sym->line, buf);
            _kprint_error(buf);
            _kprint_error(") +0x");
            uint64_to_hex(offset, buf);
            _kprint_error(buf);
        }
        _kprint_error("\n");
    }

    _kprint_error("  -> Trace stopped: ");
    _kprint_error(stop_reason ? stop_reason : "Limit reached");
    _kprint_error("\n");
}

void debug_backtrace() {
	_kprint_error("===Backtrace===\n");
	debug_print_stack_trace(16);
	_kprint_error("Current thread:\n");
	if (current_thread) debug_print_thread(current_thread);
	else _kprint_error("NULL");
	if (ready_queue) {
        thread_t* t = ready_queue;
        do {
			_kprint_error("============\n");
            debug_print_thread(t);
            t = t->next;
        } while (t != ready_queue);
    }
}

__attribute__((noreturn)) void debug_stop_kernel() {
	kernel_error(0x0, 0x0, 0x0, 0x0, 0x0);
	__builtin_unreachable();
}

#endif