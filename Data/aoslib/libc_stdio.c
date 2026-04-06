#include <stddef.h>
#include "../include/aoslib.h"

#define va_list            __builtin_va_list
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

#define PRINTF_BUF_SIZE 256

typedef struct {
    char*    dest_buf;
    size_t capacity;
    size_t idx;
    int  total_chars;
    int  is_syscall;
} PrintContext;

static void putc_ctx(PrintContext* ctx, char c) {
    ctx->total_chars++;
    if (ctx->is_syscall) {
        ctx->dest_buf[ctx->idx++] = c;
        if (ctx->idx >= ctx->capacity - 1) {
            ctx->dest_buf[ctx->idx] = '\0';
            sysprint(ctx->dest_buf);
            ctx->idx = 0;
        }
    } else {
        if (ctx->capacity > 0 && ctx->idx < ctx->capacity - 1) {
            ctx->dest_buf[ctx->idx++] = c;
        }
    }
}

static void print_number(PrintContext* ctx, uint64_t val, int base, int is_signed, int upper, int width, char pad_char) {
    char temp[65]; // Буфер для ulltoa (до 64 бит в base=2 + '\0')
    int is_neg = 0;

    if (is_signed && base == 10) {
        if ((int64_t)val < 0) {
            is_neg = 1;
            val = ~(uint64_t)val + 1;
        }
    }

    ulltoa(val, temp, base);

    int32_t len = 0;
    while (temp[len] != '\0') {
        if (upper && temp[len] >= 'a' && temp[len] <= 'z') {
            temp[len] = temp[len] - 'a' + 'A'; 
        }
        len++;
    }

    int32_t pad_len = width - len - is_neg;

    if (pad_char == '0' && is_neg) { 
        putc_ctx(ctx, '-'); 
        is_neg = 0;
    }

    while (pad_len-- > 0) {
        putc_ctx(ctx, pad_char);
    }

    if (is_neg) {
        putc_ctx(ctx, '-');
    }

    for (int32_t i = 0; i < len; i++) {
        putc_ctx(ctx, temp[i]);
    }
}

static void format_core(PrintContext* ctx, const char* format, va_list* args) {
    while (*format) {
        if (*format != '%') {
            putc_ctx(ctx, *format++);
            continue;
        }
        format++;
        if (*format == '\0') break;
        char pad_char = ' ';
        if (*format == '0') { pad_char = '0'; format++; }
        int32_t width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        int32_t is_long = 0;
        if (*format == 'l') {
            is_long = 1; format++;
            if (*format == 'l') { is_long = 2; format++; }
        }
        switch (*format) {
            case 'c': putc_ctx(ctx, (char)va_arg(*args, int32_t)); break;
            case 's': {
                const char* s = va_arg(*args, const char*);
                if (s == (void*)0) s = "(null)";
                while (*s) putc_ctx(ctx, *s++);
                break;
            }
            case 'd':
            case 'i': {
                int64_t num = (is_long >= 1) ? va_arg(*args, int64_t) : va_arg(*args, int32_t);
                print_number(ctx, (uint64_t)num, 10, 1, 0, width, pad_char); break;
            }
            case 'u': {
                uint64_t num = (is_long >= 1) ? va_arg(*args, uint64_t) : va_arg(*args, uint32_t);
                print_number(ctx, num, 10, 0, 0, width, pad_char); break;
            }
            case 'x': 
            case 'X': {
                uint64_t num = (is_long >= 1) ? va_arg(*args, uint64_t) : va_arg(*args, uint32_t);
                print_number(ctx, num, 16, 0, (*format == 'X'), width, pad_char); break;
            }
            case 'p': {
                putc_ctx(ctx, '0'); putc_ctx(ctx, 'x');
                print_number(ctx, (uint64_t)va_arg(*args, void*), 16, 0, 0, 16, '0'); break;
            }
            case '%': putc_ctx(ctx, '%'); break;
            default:  putc_ctx(ctx, '%'); putc_ctx(ctx, *format); break;
        }
        format++;
    }
}

int printf(const char* format, ...) {
    char local_buf[PRINTF_BUF_SIZE];
    PrintContext ctx = { local_buf, PRINTF_BUF_SIZE, 0, 0, 1 };
    va_list args;
    va_start(args, format);
    format_core(&ctx, format, &args);
    va_end(args);
    if (ctx.idx > 0) {
        ctx.dest_buf[ctx.idx] = '\0';
        sysprint(ctx.dest_buf);
    }
    return ctx.total_chars;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    PrintContext ctx = { str, size, 0, 0, 0 };
    va_list args;
    va_start(args, format);
    format_core(&ctx, format, &args);
    va_end(args);
    if (size > 0) {
        ctx.dest_buf[ctx.idx] = '\0';
    }
    return ctx.total_chars;
}

int sprintf(char* str, const char* format, ...) {
    uint64_t infinite_size = (uint64_t)-1;
    PrintContext ctx = { str, infinite_size, 0, 0, 0 };
    
    va_list args;
    va_start(args, format);
    format_core(&ctx, format, &args);
    va_end(args);

    ctx.dest_buf[ctx.idx] = '\0';
    return ctx.total_chars;
}

#undef va_list
#undef va_start
#undef va_arg
#undef va_end